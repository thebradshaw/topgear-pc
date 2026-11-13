// Top Gear 3000: where the camera looks, following the player's car (bank $02).
//
// $02:A9B7 takes the car being followed ($0281: its record at $7E:FAB4; $0283: its place
// in the road-edge tables at $7E:34EC and $7E:36EC) and has the DSP-4 work out where it is
// on the screen across ($16) and up ($18), scaled by the zoom ($0293). With no car
// ($0281 negative) the camera looks straight ahead. The view ($028B across, $028D up)
// then moves towards that by at most 4 a frame.

#include <stddef.h>

#include "../port/port.h"

static uint16_t g_d;
static uint16_t z(uint8_t o) { return ram16((uint16_t) (g_d + o)); }
static void zs(uint8_t o, uint16_t v) { ram16_set((uint16_t) (g_d + o), v); }
static uint16_t m(uint32_t a) { return ram16(a & 0x1FFFF); }
static uint16_t w(uint16_t a) { return ram16(a); }
static void ws(uint16_t a, uint16_t v) { ram16_set(a, v); }

static void dput(uint16_t v) { tg_bus_write16(0x308000, v); }
static uint16_t dget(void) {
  uint8_t lo = tg_bus_read8(0x308000);
  uint8_t hi = tg_bus_read8(0x308001);
  return (uint16_t) (lo | hi << 8);
}
static bool neg(uint16_t v) { return (v & 0x8000) != 0; }

// $02:AB3F: the car's place across ($16) and up ($18) at its distance into the step.
static void car_place(uint16_t x, uint16_t y) {
  dput(0);
  uint16_t t = (uint16_t) ((m(0xFAB4u + x) & 0xFF00) >> 1);
  zs(0x14, t);
  dput(t);
  dput((uint16_t) ((m(0x34EEu + y) - m(0x34ECu + y)) << 1));
  uint16_t b = m(0x34ECu + y);
  dput(b);
  zs(0x16, (uint16_t) (b + dget() - m(0xFABBu + x)));
  dput(0);
  dput(z(0x14));
  dput((uint16_t) ((m(0x36EEu + y) - m(0x36ECu + y)) << 1));
  b = m(0x36ECu + y);
  dput(b);
  zs(0x18, (uint16_t) (b + dget() - 0x0C));
}

// Moves the view at addr towards target by at most 4 a frame.
static void follow(uint16_t addr, uint16_t target) {
  uint16_t step = (uint16_t) (w(0x1E9E) << 2);
  uint16_t v = w(addr), a;
  if(!neg((uint16_t) (target - v))) {
    a = (uint16_t) (step + v);
    if(!neg((uint16_t) (a - target))) a = target;
  } else {
    a = (uint16_t) (v - step);
    if(neg((uint16_t) (a - target))) a = target;
  }
  ws(addr, a);
}

void Tg3CameraFollow(TgRegs* r) {
  g_d = r->d;
  r->db = 0x7E;  // PEA $837E; PLB
  if(neg(w(0x0281))) {
    zs(0x18, 0xFFD0);
    zs(0x16, 0);
  } else {
    r->y = w(0x0283);
    r->x = w(0x0281);
    car_place(r->x, r->y);
    dput(0);
    dput((uint16_t) (z(0x16) << 1));
    dput(w(0x0293));
    dget();
    zs(0x16, (uint16_t) -dget());
    dput(0);
    dput((uint16_t) (z(0x18) << 1));
    dput(w(0x0293));
    dput(0x33);
    zs(0x18, (uint16_t) -(uint16_t) (0x33 - dget()));
  }
  follow(0x028B, z(0x16));
  follow(0x028D, z(0x18));
  r->a = w(0x028D);
  r->db = 0x83;
  r->n = true;
  r->z = false;
}
