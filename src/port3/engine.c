// Top Gear 3000: a player's car each frame, engine to steering (bank $03).
//
// $03:EAC6 runs once a frame for each car driven by a player, with D pointing at the car's
// block ($0200, $0262, $02B3 or $0315). The DSP-4 (bank $30) does the multiplying: command 0
// takes two words and gives back a 32-bit product, low word first.
//
// Paused (or waiting at the start, $1ECE set) it only lets the engine revs ($12/$14) rise
// with the accelerator or fall back. Otherwise, for each frame of the step ($1E9E):
// - the items and damage flags in $1E9C (nitro, a hit, a jump) start their sounds and timers;
// - the revs drive the speed ($16/$18, with $17 its whole part) through the gear's torque
//   table, less the drag, and the revs follow the speed once the clutch is in ($44 bit 4);
// - the gear ($1F) goes up or down at the gear's limits ($1EAA/$1EAC), which loads its
//   settings (03:EA9B) and works the revs out again from the speed (a 40-step division);
// - slowing down comes from the grass, the road's edge, damage and braking ($013C);
// - steering ($21) turns the car across the road ($04/$06), with the road's own bend
//   ($28/$2A) pushing it outwards, kept within $0280 of the road's middle;
// - a spin or a crash ($1A) runs its animation; the car's sprite ($3A/$3B) follows the
//   steering; boost pads and other road features ($53) under the car take effect;
// - and the distance travelled feeds the lap position ($1E).

#include <stddef.h>

#include "../port/port.h"

static uint16_t g_d;
static uint16_t z(uint8_t o) { return ram16((uint16_t) (g_d + o)); }
static void zs(uint8_t o, uint16_t v) { ram16_set((uint16_t) (g_d + o), v); }
static uint16_t za(uint8_t o) { return (uint16_t) (g_d + o); }
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
static void mul(uint16_t a, uint16_t b) { dput(0); dput(a); dput(b); }

static bool neg(uint16_t v) { return (v & 0x8000) != 0; }
static uint16_t absw(uint16_t v) { return neg(v) ? (uint16_t) -v : v; }
static uint16_t swap(uint16_t v) { return (uint16_t) (v << 8 | v >> 8); }
static uint16_t frames(void) { return w(0x1E9E); }
// $1F31 bit 15: the extras below (grass, road features, the road's edge) are off.
static bool plain(void) { return neg(w(0x1F31)); }

// Adds the chip's 32-bit result to the pair at lo/hi; returns the new high word.
static uint16_t add_result(uint16_t lo, uint16_t hi) {
  uint32_t s = (uint32_t) w(lo) + dget();
  ws(lo, (uint16_t) s);
  uint16_t h = (uint16_t) (w(hi) + dget() + (s >> 16));
  ws(hi, h);
  return h;
}
static uint16_t sub_result(uint16_t lo, uint16_t hi) {
  uint16_t a = w(lo), b = dget();
  ws(lo, (uint16_t) (a - b));
  uint16_t h = (uint16_t) (w(hi) - dget() - (a < b));
  ws(hi, h);
  return h;
}
// Adds the low word to $013C, then reads (and drops) the high word.
static void slow_down(uint16_t a, uint16_t b) {
  mul(a, b);
  ws(0x013C, (uint16_t) (w(0x013C) + dget()));
  dget();
}

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
// $03:FE9A: asks for a sound (A) for this car.
static void sound(TgRegs* r, uint16_t a, uint16_t y, uint16_t site) {
  r->a = a;
  r->x = w(0x1EBE);
  r->y = y;
  call(r, 0xFE9A, site);
}

static void leave(TgRegs* r) {
  r->db = 0x83;  // PLB
  r->n = true;
  r->z = false;
}

// $03:EC33: an item in use ($81: which) wears down with the frames.
static void item_wear(TgRegs* r) {
  uint8_t sub = (uint8_t) ~(uint8_t) (frames() << 3);
  unsigned s = g_ram[za(0x80)] + sub + 1u;
  g_ram[za(0x80)] = (uint8_t) s;
  g_ram[za(0x50)] = (uint8_t) (g_ram[za(0x50)] - (s > 0xFF ? 0 : 1));
  r->x = z(0x81);
  if(r->x != 0 && !((tg_read16(0x8103DC) & 0xF) != 0 && r->x == 0x35)) {
    uint16_t x = r->x;
    ws(0x011C, l7e(0x7EFAC9u + x));
    r->a = l7e(0x7EFACBu + x);
    ws(0x011E, r->a);
    call(r, 0xF6E1, 0xEC76);
    x = r->x;
    uint16_t a = l7e(0x7EFAC9u + x), b = w(0x0115);
    l7e_set(0x7EFAC9u + x, (uint16_t) (a - b));
    uint16_t h = (uint16_t) (l7e(0x7EFACBu + x) - w(0x0117) - (a < b));
    l7e_set(0x7EFACBu + x, h);
    if(neg(h)) {
      l7e_set(0x7EFAC9u + x, 0);
      l7e_set(0x7EFACBu + x, 0);
    }
    ws(0x013C, (uint16_t) (w(0x013C) - w(0x0116)));
    return;
  }
  if(r->x != 0) r->x = 0xB3;
  uint16_t x = r->x;
  ws(0x011C, w((uint16_t) (0x020E + x)));
  r->a = w((uint16_t) (0x0210 + x));
  ws(0x011E, r->a);
  call(r, 0xF6E1, 0xECB6);
  x = r->x;
  mul(w(0x0116), w((uint16_t) (0x0295 + x)));
  uint16_t a = w((uint16_t) (0x0216 + x)), b = dget();
  ws((uint16_t) (0x0216 + x), (uint16_t) (a - b));
  uint16_t h = (uint16_t) (w((uint16_t) (0x0218 + x)) - dget() - (a < b));
  if(neg(h)) {
    ws(0x0216, 0);
    h = 0;
  }
  ws((uint16_t) (0x0218 + x), h);
  ws(0x013C, (uint16_t) (w(0x013C) - w(0x0117)));
}

// $03:EF1F: a gear change. Loads the gear's settings and works the revs out from the speed.
static void change_gear(TgRegs* r, int step) {
  zs(0x1F, (uint16_t) (z(0x1F) + step));
  r->x = (uint16_t) ((w(0x1EB2) & 0x7F) << 1);
  r->x = (uint16_t) ((z(0x1F) << 1) + tg_read16(0x86DAD9u + r->x));
  r->a = r->x;
  call(r, 0xEA9B, 0xEF30);
  zs(0x2B, r->a);
  if(!plain()) zs(0x95, w(0x1EA2));
  uint16_t q0 = z(0x0E), q1 = z(0x10);
  uint16_t lo = (uint16_t) (q0 << 1);
  uint16_t hi = (uint16_t) (q1 << 1 | q0 >> 15);
  uint16_t a = (uint16_t) (q1 >> 15);
  for(int i = 0; i < 40; i++) {
    uint16_t d = w(0x1EA0);
    unsigned c = 0;
    if(a >= d) {
      a = (uint16_t) (a - d);
      c = 1;
    }
    unsigned c1 = lo >> 15, c2 = hi >> 15;
    lo = (uint16_t) (lo << 1 | c);
    hi = (uint16_t) (hi << 1 | c1);
    a = (uint16_t) (a << 1 | c2);
  }
  ws(0x0114, lo);
  ws(0x0116, hi);
  zs(0x16, lo);
  zs(0x18, hi);
  r->x = 0;
}

// $03:F139/$03:F1A1: turns the steering by k a frame, scaled by the speed.
static uint16_t steer(TgRegs* r, uint16_t k) {
  mul(frames(), k);
  uint16_t a = dget();
  dget();
  dput(0);
  r->x = z(0x10) >= 0x80 ? 0x7FFF : z(0x0F);
  dput(r->x);
  dput(a);
  dget();
  return (uint16_t) (z(0x21) + dget());
}

// $03:F590 and $03:F5EA: the road feature's sound, every 16 frames.
static void feature_sound(TgRegs* r, uint16_t s, uint16_t site) {
  uint16_t a = (uint16_t) (z(0x4C) - frames());
  zs(0x4C, a);
  if(!neg(a)) return;
  zs(0x4C, 0);
  if(w(0x0463) & 0xFF) return;
  zs(0x4C, 0x10);
  sound(r, s, 0, site);
}

static void crash(TgRegs* r, uint16_t site) {
  zs(0x44, (uint16_t) (z(0x44) | 0x20));
  zs(0x1A, 5);
  r->x = w(0x1EBA);
  l7e_set(0x7EFADEu + r->x, l7e(0x7EFADEu + r->x) | 8);
  sound(r, 0xFF29, 0, site);
}

void Tg3PlayerEngine(TgRegs* r) {
  g_d = r->d;
  uint16_t p = w(0x1ECE);
  if(p == 0) {
    // The two jump tables below only have these entries.
    if(!plain()) {
      uint16_t t = tg_read16(0x03F3B9u + (z(0x60) & 0xFF));
      if(t != 0xF423 && t != 0xF3C5) {
        tg_port_decline();
        return;
      }
    }
    if((z(0x53) & 0xFF) > 4) {
      tg_port_decline();
      return;
    }
  }
  r->db = 0x30;  // PEA $8330; PLB

  if(p != 0) {
    if(neg(p)) {
      r->a = p;
      call(r, 0xF73E, 0xEAD1);
    }
    if(w(0x1E9C) & 1) {
      mul(0x60, swap(frames()));
      if(add_result(za(0x12), za(0x14)) >= 9) {
        zs(0x14, 9);
        zs(0x12, 0xFFFF);
      }
    }
    mul(0xFFE0, swap(frames()));
    if(neg(add_result(za(0x12), za(0x14)))) {
      zs(0x12, 0);
      zs(0x14, 0);
    }
    leave(r);
    return;
  }

  ws(0x013A, 0);
  ws(0x013C, 0);
  if((w(0x1E9C) & 0x10) && z(0x31) == 0) {
    if(z(0x4E) & 0xFF) {
      zs(0x4E, (uint16_t) (z(0x4E) - 1));
      zs(0x31, w(0x1EB6));
      sound(r, 0xFF1B, 0x3C, 0xEB5A);
    } else {
      sound(r, 0xFF1C, 0, 0xEB68);
    }
  }
  {
    uint16_t a = (uint16_t) (z(0x31) - frames());
    zs(0x31, neg(a) ? 0 : a);
  }
  uint16_t t = w(0x1E9C);
  ws(0x1E9C, t & ~0x80);
  if((t & 0x80) && z(0x1A) == 0 && (z(0x51) & 0xFF)) {
    zs(0x51, (uint16_t) (z(0x51) - 1));
    if(!(z(0x44) & 0x20)) {
      zs(0x44, (uint16_t) (z(0x44) | 0x120));
      zs(0x1A, 5);
      sound(r, 0xFF29, 0, 0xEBA7);
      r->x = w(0x1EBA);
      l7e_set(0x7EFADEu + r->x, l7e(0x7EFADEu + r->x) | 8);
      zs(0x5E, 0x20);
      zs(0x70, z(0x10));
      zs(0x72, z(0x10));
    }
  }
  t = w(0x1E9C);
  ws(0x1E9C, t & ~0x20);
  if((t & 0x20) && z(0x1A) == 0) {
    zs(0x7E, 0);
    zs(0x1A, 1);
    sound(r, 0xFF1D, 0, 0xEBEA);
  }
  if(!plain() && !neg(z(0x7E))) {
    uint16_t old = z(0x7E);
    r->x = old;
    uint16_t a = (uint16_t) (old + frames());
    zs(0x7E, a);
    if((old < 0x54 && a >= 0x54) || (old < 0x58 && a >= 0x58)) sound(r, 0x7F21, 0, 0xEC1A);
  }
  if((w(0x1E9C) & 0x40) && (z(0x50) & 0x7F) && !neg(z(0x81))) item_wear(r);

  // $03:ECF6: the distance this step.
  mul(z(0x16), frames());
  ws(0x0136, dget());
  ws(0x0138, dget());
  dput(0);
  dput((uint16_t) (z(0x18) + (neg(z(0x16)) ? 1 : 0)));
  dput(frames());
  ws(0x0138, (uint16_t) (w(0x0138) + dget()));
  dget();
  if(!plain() && z(0x58) != 0) {
    mul(z(0x17), 0x3000);
    dget();
    r->x = dget();
    mul(frames(), r->x);
    uint16_t a = z(0x56), b = dget();
    uint16_t lo = (uint16_t) (a - b);
    dput(lo);
    zs(0x56, lo);
    uint16_t h = (uint16_t) (z(0x58) - (a < b));
    zs(0x58, neg(h) ? 0 : h);
  }

  // $03:ED8A: a frame at a time.
  ws(0x0134, frames());
  do {
    if(w(0x1E9C) & 1) {
      uint16_t a = 0;
      r->x = z(0x48);
      if(r->x) a = 0x2400;
      r->x = z(0x31);
      if(r->x) a = (uint16_t) (a + w(0x1EB4));
      dput(0);
      a = (uint16_t) (a + w(0x1EA8));
      r->x = a;
      if(!(z(0x44) & 0x10)) r->x = a >> 1;
      dput(r->x);
      dput(w(0x1EA6));
      uint16_t lo = (uint16_t) (dget() >> 8);
      ws(0x0114, lo);
      uint16_t hi = dget();
      if(hi & 0xFF80) hi = 0x7F;
      mul((uint16_t) ((hi & 0xFF) << 8 | lo), 1);
      add_result(za(0x16), za(0x18));
    }
    // $03:EDF9: drag, by the square of the speed.
    mul(z(0x17), z(0x17));
    uint16_t a = dget();
    r->x = dget();
    if(neg(a)) r->x++;
    mul(a, (uint16_t) (w(0x1EA4) - w(0x1EC0)));
    r->y = 0;
    ws(0x0114, dget());
    uint16_t h = dget();
    ws(0x0116, h);
    if(neg(h)) r->y = 0xFFFF;
    ws(0x0118, r->y);
    mul(r->x, (uint16_t) (w(0x1EA4) - w(0x1EC0)));
    add_result(0x0116, 0x0118);
    ws(0x011A, 0);
    {
      uint16_t s = z(0x16), b = w(0x0117);
      zs(0x16, (uint16_t) (s - b));
      zs(0x18, (uint16_t) (z(0x18) - w(0x0119) - (s < b)));
    }
    mul((uint16_t) (w(0x1EA4) - w(0x1EC0)), 1);
    if(neg(sub_result(za(0x16), za(0x18)))) {
      zs(0x16, 0);
      zs(0x18, 0);
    }
    ws(0x0134, (uint16_t) (w(0x0134) - 1));
  } while(w(0x0134) != 0);

  // $03:EEA0: the revs.
  bool follow = (z(0x44) & 0x10) != 0;
  if(!follow) {
    r->x = (w(0x1E9C) & 1) ? 0xFFF0 : 0xFFC0;
    mul(r->x, swap(frames()));
    uint16_t a = add_result(za(0x12), za(0x14));
    if(neg(a)) {
      zs(0x14, 0);
      zs(0x12, 0);
      a = 0;
    }
    if(a <= z(0x18)) {
      zs(0x44, (uint16_t) (z(0x44) | 0x10));
      follow = true;
    }
  }
  if(follow) {
    zs(0x12, z(0x16));
    zs(0x14, z(0x18));
  }
  mul(z(0x17), w(0x1EA0) >> 1);
  {
    uint16_t lo = dget();
    zs(0x0E, (uint16_t) (lo << 1));
    zs(0x10, (uint16_t) (dget() << 1 | lo >> 15));
  }
  if(z(0x17) >= w(0x1EAA)) change_gear(r, 1);
  else if(z(0x17) < w(0x1EAC)) change_gear(r, -1);

  // $03:EF6E: the engine's push.
  mul(z(0x10), z(0x10));
  {
    uint16_t lo = (uint16_t) (dget() >> 8);
    ws(0x0114, lo);
    r->x = 0x7FFF;
    uint16_t a = dget();
    if(a < 0x100) r->x = (uint16_t) ((a & 0xFF) << 8 | lo);
  }
  mul(0, r->x);
  add_result(0x013A, 0x013C);
  if(z(0x10) >= 0x10) slow_down(z(0x37), frames());
  if(!plain() && z(0x58) == 0 && z(0x10) >= 0x10) slow_down(frames(), 0x80);

  // $03:F004: the road's edges here.
  if(!plain()) {
    r->x = w(0x1EBC);
    uint16_t x = r->x, a = 0;
    if(!(l7e(0x7E3D6Eu + x) & 8)) {
      dput(0);
      dput((uint16_t) ((l7e(0x7E3DEEu + x) - l7e(0x7E3DECu + x)) << 1));
      dput((uint16_t) ((w(0x0224) & 0xFF00) >> 1));
      uint16_t b = l7e(0x7E3DECu + x);
      dput(b);
      a = (uint16_t) -(uint16_t) (b + dget());
    }
    zs(0x9E, a);
    zs(0x9C, 0);
    if(w(0x1F2C) & 0x8001) zs(0x9C, z(0x9E));
  }
  r->x = w(0x1EBC);
  r->y = l7e(0x7E3BECu + r->x) ? 2 : 0;
  if(absw(z(0x06)) >= w((uint16_t) (0x1F28 + r->y))) {
    bool off = true;
    if(!plain()) {
      r->x = w(0x1EBC);
      r->y = l7e(0x7E3BECu + r->x) ? 2 : 0;
      if(absw((uint16_t) (z(0x06) - z(0x9E))) >= w((uint16_t) (0x1F28 + r->y))) zs(0x7C, z(0x7C) & ~0x10);
      else off = false;
    }
    if(off && z(0x10) >= 0x10) slow_down(frames(), 0x80);
  }
  if(!plain() && z(0x10) >= 0x10) slow_down(frames(), (uint16_t) ((z(0x5B) & 0xFF) >> 2));
  if(w(0x1E9C) & 2) slow_down(frames(), 0x100);

  // $03:F115
  mul(w(0x013C), w(0x1EA2));
  {
    uint16_t a = z(0x16), b = dget();
    zs(0x16, (uint16_t) (a - b));
    uint16_t h = (uint16_t) (z(0x18) - dget() - (a < b));
    if(neg(h)) {
      zs(0x16, 0);
      h = 0;
    }
    zs(0x18, h);
  }

  // $03:F139: the steering eases back to the middle.
  r->x = 0x000C;
  if(z(0x21) != 0) {
    ws(0x0114, z(0x21));
    if(!neg(z(0x21))) r->x = 0xFFF4;
    uint16_t a = steer(r, r->x);
    zs(0x21, a);
    if(neg(a ^ w(0x0114))) zs(0x21, 0);
  }
  ws(0x013C, 0);
  uint16_t k = 0;
  if(w(0x1E9C) & 8) k = 0x18;
  else if(w(0x1E9C) & 4) k = 0xFFE8;
  if(k) {
    r->x = frames();
    uint16_t a = steer(r, k);
    if(!neg((uint16_t) (a - 0x80))) {
      a = 0x80;
      ws(0x013C, 0x80);
    }
    if(neg((uint16_t) (a - 0xFF80))) {
      a = 0xFF80;
      ws(0x013C, 0xFF80);
    }
    zs(0x21, a);
  }

  // $03:F1EC: thrown sideways.
  if(z(0x1C) != 0) {
    mul(z(0x1C), swap(frames()));
    add_result(za(0x04), za(0x06));
    mul(0xA0, frames());
    uint16_t a = z(0x1C);
    if(!neg(a)) {
      a = (uint16_t) (a - dget());
      zs(0x1C, a);
      dput(a);
      if(neg(a)) zs(0x1C, 0);
    } else {
      a = (uint16_t) (a + dget());
      zs(0x1C, a);
      dput(a);
      if(!neg(a)) zs(0x1C, 0);
    }
  }

  // $03:F250: across the road.
  if(!(z(0x44) & 0x20) && !(z(0x44) & 1)) {
    mul(z(0x0E), w(0x1EB8));
    r->x = 0;
    ws(0x0114, dget());
    uint16_t h = dget();
    ws(0x0116, h);
    if(neg(h)) r->x = 0xFFFF;
    ws(0x0118, r->x);
    dput(0);
    dput((uint16_t) (z(0x10) + (neg(z(0x0E)) ? 1 : 0)));
    dput(w(0x1EB8));
    add_result(0x0116, 0x0118);
    uint16_t a = z(0x21);
    if(!neg(a)) {
      a = (uint16_t) (a - w(0x0117));
      if(neg(a)) a = 0;
    } else {
      a = (uint16_t) (a + w(0x0117));
      if(!neg(a)) a = 0;
    }
    mul(a, frames());
    r->x = dget();
    dget();
    mul((uint16_t) (z(0x0F) >> 2), r->x);
    ws(0x0114, dget());
    ws(0x0116, dget());
    {
      uint32_t s = (uint32_t) z(0x04) + w(0x0114);
      zs(0x04, (uint16_t) s);
      zs(0x06, (uint16_t) (z(0x06) + w(0x0116) + (s >> 16)));
    }
    if(z(0x1C) == 0) {
      // The bend pushes the car out.
      uint16_t b = z(0x28) & 0xFF00;
      ws(0x0118, b);
      uint32_t s = (uint32_t) b + z(0x04);
      zs(0x04, (uint16_t) s);
      uint16_t u = z(0x2A) & 0xFF;
      if(u & 0x80) u |= 0xFF00;
      ws(0x011A, u);
      zs(0x06, (uint16_t) (u + z(0x06) + (s >> 16)));
    }
    bool skid = true;
    if(z(0x44) & 0x10) {
      if(z(0x29) == 0 || !neg(w(0x013C) ^ z(0x29)) || neg((uint16_t) (w(0x0115) + w(0x0119)))) skid = false;
    }
    if(skid) {
      uint16_t s = (uint16_t) ((z(0x1A) & 0x7FFF) - 1);
      if(s == 0 || s == 5) skid = false;
    }
    if(skid) {
      // The tyres' squeal.
      uint16_t c = (uint16_t) (z(0x46) - frames());
      zs(0x46, c);
      if(neg(c)) {
        zs(0x46, (uint16_t) (c + 4));
        sound(r, 0x4F21, 0, 0xF355);
      }
    } else {
      zs(0x46, 0);
    }
  }

  // $03:F35C: kept on the road.
  if(!plain()) {
    if(absw(z(0x06)) >= 0x280 && absw((uint16_t) (z(0x06) - z(0x9E))) >= 0x280) {
      uint16_t a;
      if(!(w(0x1F2C) & 0x8001)) a = neg(z(0x06)) ? 0xFD80 : 0x0280;
      else a = (uint16_t) (z(0x9E) + (neg((uint16_t) (z(0x06) - z(0x9E))) ? 0xFD80 : 0x0280));
      zs(0x06, a);
    }
    r->x = z(0x60) & 0xFF;
    if(tg_read16(0x03F3B9u + r->x) == 0xF3C5) {
      uint16_t a = z(0x26) & 0x3F;
      if(a < 0x38 && a >= 0x18) {
        uint16_t e = z(0x9E);
        a = (uint16_t) (e >> 1 | (e & 0x8000));
        bool below = neg((uint16_t) (a - z(0x06)));
        bool store = neg(w(0x1F2C)) ? (neg(e) ? below : !below) : (neg(e) ? !below : below);
        if(store) zs(0x06, a);
      }
    }
  } else if(absw(z(0x06)) >= 0x280) {
    zs(0x06, neg(z(0x06)) ? 0xFD80 : 0x0280);
  }

  // $03:F423: the car's sprite: a spin or crash animation, or by the steering.
  bool by_steering = false;
  if((tg_read16(0x8103DC) & 3) == 2 || z(0x1A) == 0) {
    by_steering = true;
  } else if(!neg(z(0x1A))) {
    uint16_t a = z(0x1A) | 0x8000;
    zs(0x1A, a);
    r->x = (uint16_t) (a << 1);
    zs(0x44, z(0x44) & ~0x40);
    zs(0x3A, tg_read16(0x86DCF7u + r->x));
  } else {
    r->x = (uint16_t) (z(0x1A) << 1);
    if(!(z(0x44) & 0x40)) {
      mul(frames(), tg_read16(0x86DD1Bu + r->x));
      uint16_t a = dget();
      dget();
      a = (uint16_t) (a + z(0x3A));
      zs(0x3A, a);
      if(a >= tg_read16(0x86DD03u + r->x)) {
        if(tg_read16(0x86DD0Fu + r->x) & 1) {
          zs(0x44, z(0x44) | 0x40);
          zs(0x3A, tg_read16(0x86DD03u + r->x));
        } else {
          zs(0x1A, 0);
          by_steering = true;
        }
      }
    }
  }
  if(by_steering) {
    static const uint16_t kTurn[6] = { 0xFFA0, 0xFFC0, 0xFFE8, 0x0018, 0x0040, 0x0060 };
    uint16_t a = z(0x21);
    r->x = 6;
    for(int i = 0; i < 6 && !neg((uint16_t) (a - kTurn[i])); i++) r->x--;
    zs(0x3A, (uint16_t) (r->x << 8));
  }
  r->x = (uint16_t) ((z(0x3B) & 0xFF) << 1);
  {
    uint16_t a = tg_read16(0x86CB54u + r->x);
    r->x = w(0x1EBA);
    l7e_set(0x7EFABFu + r->x, a);
  }
  mul(frames(), 0x1E);
  {
    uint16_t a = (uint16_t) (z(0x48) - dget());
    dput(a);
    zs(0x48, neg(a) ? 0 : a);
  }

  // $03:F4F6: a road feature under the car.
  bool none = plain();
  if(!none) {
    uint16_t s = (uint16_t) ((z(0x1A) & 0x7FFF) - 1);
    none = s == 0 || s == 5 || (z(0x53) & 0xFF) == 0;
  }
  if(!none) {
    uint16_t c = (uint16_t) ((z(0xB1) & 0xFF) << 4);
    if(c & 0x800) c |= 0xF000;
    ws(0x0114, c);
    ws(0x0116, (uint16_t) ((z(0xB2) & 0xFF) + 0x20));
    none = !neg((uint16_t) (absw((uint16_t) (z(0x06) - c)) - w(0x0116)));
  }
  if(none) {
    zs(0x4C, 0);
  } else {
    r->x = (uint16_t) ((z(0x53) & 0xFF) << 1);
    switch(r->x) {
    case 2:  // a boost pad
      r->a = 0x3A;
      call(r, 0xFEBD, 0xF558);
      mul(0x2000, frames());
      if(add_result(za(0x56), za(0x58)) >= 0x28) {
        zs(0x58, 0x28);
        zs(0x56, 0xFFFF);
      }
      feature_sound(r, 0xFF20, 0xF5B2);
      break;
    case 4:
      r->a = 0x3B;
      call(r, 0xFEBD, 0xF5BB);
      if(!plain()) {
        mul(0x200, frames());
        uint16_t a = z(0x5A), b = dget();
        zs(0x5A, a >= b ? (uint16_t) (a - b) : 0);
        dget();
      }
      feature_sound(r, 0xFF14, 0xF60C);
      break;
    case 6:
      zs(0x48, 0x258);
      break;
    case 8:
      if(!(z(0x44) & 0x20)) crash(r, 0xF640);
      break;
    }
  }

  // $03:F647: the boost's sound.
  if(!plain()) {
    mul(frames(), 0x800);
    uint32_t s = (uint32_t) z(0x97) + dget();
    zs(0x97, (uint16_t) s);
    dget();
    if((s >> 16) && z(0x58) != 0 && z(0x58) < 9 && !(z(0x44) & 4)) sound(r, 0xFF26, 0, 0xF68A);
  }

  // $03:F68D: along the track.
  mul(z(0x10), 0x100);
  ws(0x0114, dget());
  ws(0x0116, dget());
  mul(w(0x0115), frames());
  uint16_t a = dget();
  if(a >= 0x80) a = 0x80;
  g_ram[za(0x1E)] = (uint8_t) (g_ram[za(0x1E)] + a);
  r->a = dget();
  leave(r);
}
