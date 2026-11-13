// Top Gear 3000: the engine's pitch and the lead car's distance for the sound (bank $02).
//
// $02:AC52 has the DSP-4 scale the speed ($0213) into a pitch, less $0B00 and capped at
// $3000, for $7E:4E2D. When bit 0 of $0244 is set it also works out how far ahead the car
// at $7E:FAB4 is (in steps, against the camera's $0224/$0226) as a volume for $7E:4E35,
// and with bit 2 of $0244 it blends the pitch by that distance with the hardware divider.

#include <stddef.h>

#include "../port/port.h"

static uint16_t w(uint16_t a) { return ram16(a); }
static uint16_t m(uint32_t a) { return ram16(a & 0x1FFFF); }
static void ms(uint32_t a, uint16_t v) { ram16_set(a & 0x1FFFF, v); }

static void dput(uint16_t v) { tg_bus_write16(0x308000, v); }
static uint16_t dget(void) {
  uint8_t lo = tg_bus_read8(0x308000);
  uint8_t hi = tg_bus_read8(0x308001);
  return (uint16_t) (lo | hi << 8);
}
static bool neg(uint16_t v) { return (v & 0x8000) != 0; }

static uint16_t pitch(uint16_t a) {
  a = (uint16_t) (a - 0x0B00);
  if(!neg((uint16_t) (a - 0x3000))) a = 0x3000;
  return a;
}

void Tg3EngineSound(TgRegs* r) {
  uint8_t db = r->db;
  if(!(db < 0x40 || (db >= 0x80 && db < 0xC0))) {  // $4204 must be the divider
    tg_port_decline();
    return;
  }
  dput(0);
  dput(w(0x0213));
  dput(0x0300);
  uint16_t hi = dget() >> 8;
  ram16_set((uint16_t) (r->d + 0x14), hi);
  uint16_t a = (uint16_t) ((dget() << 8) | hi);
  ms(0x7E4E2D, pitch(a));
  r->a = 0;
  if(!(w(0x0244) & 1)) return;
  uint16_t c = m(0x7EFAB4) >= w(0x0224);
  a = (uint16_t) (m(0x7EFAB6) - w(0x0226) - (1 - c));
  if(neg(a)) a = 0;
  ram16_set((uint16_t) (r->d + 0x14), (uint16_t) (a << 1));
  a = (uint16_t) (0xB0 - (uint16_t) (a << 1));
  if(neg(a)) a = 0;
  ms(0x7E4E35, a & 0xF8);
  if(!(w(0x0244) & 4)) return;
  a = (uint16_t) (m(0x7E4E2D) + 0x0B00);
  if(neg(a)) a = 0;
  tg_bus_write16(0x304204, a);
  a = (uint16_t) (m(0x7EFAB6) - w(0x0226));
  if(neg(a)) a = 0;
  a = (uint16_t) (a + 0x10);
  if(a >= 0xD0) a = 0xD0;
  tg_bus_write8(0x304206, (uint8_t) a);
  a = (uint16_t) (tg_bus_read8(0x304214) | tg_bus_read8(0x304215) << 8);
  ms(0x7E4E2D, pitch((uint16_t) (a << 4)));
}
