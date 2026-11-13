// Top Gear 3000: the road's edges and bend at the camera (bank $02).
//
// $02:EC36 runs after 02:DFAD. From the edge table at $7E:3DEC (by the step the camera is
// on, $0226) and how far into the step it is ($0224), the DSP-4 gives where the road's
// edges are ($1EF4, $1EF6). The step's two road-feature handlers then run ($0261 from the
// table at $02:ED23, $0260 from $02:ED17; the second sees the two edges on the stack and
// may change them before they land in $1EC2/$1EC4). Finally the bend this frame ($01CE,
// from 02:DFAD) is shared out over the frames of the step ($1E9E) with the hardware
// divider, for the road's sideways push.

#include <stddef.h>

#include "../port/port.h"

static uint16_t w(uint16_t a) { return ram16(a); }
static void ws(uint16_t a, uint16_t v) { ram16_set(a, v); }
static uint16_t m(uint32_t a) { return ram16(a & 0x1FFFF); }

static void dput(uint16_t v) { tg_bus_write16(0x308000, v); }
static uint16_t dget(void) {
  uint8_t lo = tg_bus_read8(0x308000);
  uint8_t hi = tg_bus_read8(0x308001);
  return (uint16_t) (lo | hi << 8);
}

static void call(TgRegs* r, uint16_t target, uint16_t site) {
  r->db = 0x30;
  const TgPortFunc* fn = tg_port_find(0x020000u | target);
  if(fn != NULL && fn->call == TG_CALL_JSR) {
    fn->fn(r);
    if(!tg_port_take_declined()) return;
  }
  tg_call_jsr(r, target, (uint16_t) (site + 3), TG_MX_16);
  r->db = 0x30;
}

void Tg3RoadEdges(TgRegs* r) {
  r->db = 0x30;  // PEA $8330; PLB
  uint16_t d = r->d;
  ram16_set((uint16_t) (d + 0x3C), 0);
  uint16_t x = (uint16_t) ((w(0x0226) << 1) & 0x3E);
  ram16_set((uint16_t) (d + 0x16), x);
  dput(0);
  dput((uint16_t) ((m(0x3DEEu + x) - m(0x3DECu + x)) << 1));
  uint16_t f = (uint16_t) ((w(0x0224) >> 1) & 0xFF80);
  dput(f);
  ram16_set((uint16_t) (d + 0x14), f);
  ws(0x1EF4, dget());
  uint16_t a = (uint16_t) (dget() + m(0x3DECu + x));
  if(a != 0 && !(a & 0x8000)) a--;
  ws(0x1EF6, a);

  r->x = w(0x0261) & 0xFF;
  r->a = r->x;
  call(r, tg_read16(0x02ED23u + r->x), 0xEC7F);
  tg_push16(w(0x1EF4));
  tg_push16(w(0x1EF6));
  r->x = w(0x0260) & 0xFF;
  r->a = r->x;
  call(r, tg_read16(0x02ED17u + r->x), 0xEC91);
  ws(0x1EC4, tg_pull16());
  ws(0x1EC2, tg_pull16());

  a = w(0x01CE);
  ws(0x0229, a);
  uint16_t k = 0xFFFF;
  if(a & 0x8000) {
    a = (uint16_t) -a;
    k = 1;
  }
  tg_bus_write16(0x304204, a);
  tg_bus_write8(0x304206, (uint8_t) w(0x1E9E));
  dput(0);
  dput(k);
  dput((uint16_t) (tg_bus_read8(0x304214) | tg_bus_read8(0x304215) << 8));
  ws(0x01CE, dget());
  r->a = dget();
  r->db = 0x83;
  r->n = true;
  r->z = false;
}
