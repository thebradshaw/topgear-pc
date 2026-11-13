// Top Gear 3000: moves a player's car along the track (bank $03).
//
// $03:F7F9 runs after the engine (03:EAC6), with D on the car's block. The distance this
// step (speed $10 times the frames, or a jump's or crash's own movement while $44 bit 5
// is set) is added to the track position $25/$27 (in 1/256 of a road step). Then 03:F9D1
// counts laps, and for every road step crossed it adds up how far the road's edges moved
// across ($011E, from the edge table at $7E:3DEC) and runs the handler of any road
// feature placed on that step (the list at $7E:8CB3, handlers from $03:8000). Finally
// $2D/$2F get the distance moved.

#include <stddef.h>

#include "../port/port.h"

static uint16_t g_d;
static uint16_t z(uint8_t o) { return ram16((uint16_t) (g_d + o)); }
static void zs(uint8_t o, uint16_t v) { ram16_set((uint16_t) (g_d + o), v); }
static uint16_t w(uint16_t a) { return ram16(a); }
static void ws(uint16_t a, uint16_t v) { ram16_set(a, v); }
static uint16_t l7e(uint32_t a) { return ram16((a - 0x7E0000) & 0x1FFFF); }
static void l7e_set(uint32_t a, uint16_t v) { ram16_set((a - 0x7E0000) & 0x1FFFF, v); }

static void dput(uint16_t v) { tg_bus_write16(0x308000, v); }
static uint16_t dget(void) {
  uint8_t lo = tg_bus_read8(0x308000);
  uint8_t hi = tg_bus_read8(0x308001);
  return (uint16_t) (lo | hi << 8);
}
static uint16_t frames(void) { return w(0x1E9E); }

static void call(TgRegs* r, uint16_t target, uint16_t site) {
  r->db = 0x30;
  const TgPortFunc* fn = tg_port_find(0x030000 | target);
  if(fn != NULL && fn->call == TG_CALL_JSR) {
    fn->fn(r);
    if(!tg_port_take_declined()) return;
  }
  tg_call_jsr(r, target, (uint16_t) (site + 3), TG_MX_16);
  r->db = 0x30;
}

// $03:F8AA: the position goes on by the frames times $70.
static void move_by_70(void) {
  dput(0);
  dput(frames());
  dput(z(0x70));
  uint32_t s = (uint32_t) z(0x25) + dget();
  zs(0x25, (uint16_t) s);
  zs(0x27, (uint16_t) (z(0x27) + dget() + (s >> 16)));
}
// $03:F898: $70 grows by 4 a frame up to $100 first.
static void speed_up_70(void) {
  uint16_t a = (uint16_t) (z(0x70) + (frames() << 2));
  zs(0x70, a < 0x100 ? a : 0x100);
  move_by_70();
}
// $03:F8D0: the jump ends in a crash.
static void land_badly(TgRegs* r) {
  zs(0x44, (uint16_t) ((z(0x44) & ~0x120) | 0x80));
  zs(0x1A, 4);
  r->a = 0xFF2A;
  r->x = w(0x1EBE);
  r->y = 0;
  call(r, 0xFE9A, 0xF8E8);
  ws(0x0120, (uint16_t) (w(0x0120) - 1));
}

void Tg3TrackPosition(TgRegs* r) {
  g_d = r->d;
  r->db = 0x30;  // PEA $8330; PLB
  zs(0x2D, z(0x25));
  zs(0x2F, z(0x27));
  {
    uint32_t s = (uint32_t) z(0x25) + 0x100;
    uint16_t hi = (uint16_t) (z(0x27) + (s >> 16));
    ws(0x0114, (uint16_t) (((s >> 8) & 0xFF) | (hi << 8 & 0xFF00)));
  }
  ws(0x0120, 0);
  uint16_t f = z(0x44);
  zs(0x44, f & ~0x80);
  if(f & 0x80) {
    r->x = w(0x1EBA);
    l7e_set(0x7EFADEu + r->x, l7e(0x7EFADEu + r->x) & 0xFFF7);
  }

  if((z(0x44) & 0x20) == 0x20) {
    // In the air or crashing.
    if(z(0x44) & 1) {
    } else if(z(0x44) & 0x100) {
      if(z(0x5E) != 0) {
        uint16_t a = z(0x5E);
        zs(0x5E, a >= frames() ? (uint16_t) (a - frames()) : 0);
        speed_up_70();
      } else {
        uint16_t a = (uint16_t) (z(0x70) - (frames() << 2));
        if((uint16_t) (a - z(0x72)) & 0x8000) {
          land_badly(r);
        } else {
          zs(0x70, a);
          move_by_70();
        }
      }
    } else {
      speed_up_70();
      if(z(0x26) < z(0x5E)) {
        ws(0x0120, (uint16_t) (w(0x0120) - 1));
      } else {
        zs(0x26, z(0x5E));
        land_badly(r);
      }
    }
  } else if(!(z(0x44) & 1)) {
    dput(0);
    dput(frames());
    dput(z(0x10));
    uint32_t s = (uint32_t) z(0x25) + dget();
    zs(0x25, (uint16_t) s);
    zs(0x27, (uint16_t) (z(0x27) + dget() + (s >> 16)));
  }

  call(r, 0xF9D1, 0xF914);  // laps

  uint32_t s = (uint32_t) z(0x25) + 0x100;
  uint16_t hi = (uint16_t) (z(0x27) + (s >> 16));
  uint16_t now = (uint16_t) (((s >> 8) & 0xFF) | (hi << 8 & 0xFF00));
  ws(0x0116, (uint16_t) (now - w(0x0114)));
  if(w(0x0116) != 0) {
    ws(0x0114, (uint16_t) (w(0x0114) << 2));
    ws(0x0118, (uint16_t) (w(0x019A) << 2));
    r->x = w(0x1EBA);
    ws(0x012C, l7e(0x7EFABFu + r->x));
    r->x = (w(0x0114) >> 1) & 0x3E;
    ws(0x011E, (uint16_t) -l7e(0x7E3DECu + r->x));
    do {
      r->x = (w(0x0114) >> 1) & 0x3E;
      uint16_t a = (uint16_t) -(uint16_t) (l7e(0x7E3DEEu + r->x) - l7e(0x7E3DECu + r->x));
      a = (uint16_t) (a - z(0x0C) + w(0x011E));
      ws(0x011E, a);
      a = w(0x0114);
      if(a >= w(0x0118)) {
        a = (uint16_t) (a - w(0x0118));
        ws(0x0114, a);
      }
      r->x = a;
      ws(0x011A, a);
      uint16_t v = l7e(0x7E8CB3u + a);
      if(v != 0) {
        r->x = (uint16_t) ((v >> 8 & 0xFF) << 1);
        r->a = (uint16_t) (r->x << 1);
        ws(0x011C, r->a);
        call(r, tg_read16(0x037FFEu + r->x), 0xF9B0);
      }
      ws(0x0114, (uint16_t) (w(0x0114) + 4));
      ws(0x0116, (uint16_t) (w(0x0116) - 1));
    } while(w(0x0116) != 0);
  }

  uint16_t a = z(0x25), b = z(0x2D);
  zs(0x2D, (uint16_t) (a - b));
  zs(0x2F, (uint16_t) (z(0x27) - z(0x2F) - (a < b)));
  r->db = 0x83;
  r->n = true;
  r->z = false;
}
