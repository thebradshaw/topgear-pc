// Top Gear 3000: player 1's car in the race order (bank $01).
//
// $01:FA8F is the car handler (from the table at $01:F876, called by 01:E812 with Y on the
// car's record at $7E:FAB4) for player 1. It copies the car's track position, distance
// and place across the road from player 1's block at $0200 into the record, then finds
// the road segment it is on (01:FBB1) and marks it shown ($0010) on the near side or the
// far one ($0020, by $1E66). While the car is in the air or crashing ($0244 bit 0) the
// record is moved on by the car's own speed instead.

#include <stddef.h>

#include "../port/port.h"

static uint16_t m(uint32_t a) { return ram16(a & 0x1FFFF); }
static void ms(uint32_t a, uint16_t v) { ram16_set(a & 0x1FFFF, v); }
static uint16_t w(uint16_t a) { return ram16(a); }

static uint16_t dget(void) {
  uint8_t lo = tg_bus_read8(0x308000);
  uint8_t hi = tg_bus_read8(0x308001);
  return (uint16_t) (lo | hi << 8);
}

static void segment(TgRegs* r, uint16_t site) {
  const TgPortFunc* fn = tg_port_find(0x01FBB1);
  if(fn != NULL && fn->call == TG_CALL_JSR) {
    fn->fn(r);
    if(!tg_port_take_declined()) return;
  }
  tg_call_jsr(r, 0xFBB1, (uint16_t) (site + 3), TG_MX_16);
}

void Tg3PlayerRecord(TgRegs* r) {
  if(r->db != 0x7E) {
    tg_port_decline();
    return;
  }
  uint16_t y = r->y;
  uint32_t rec = 0xFAB4u + y;
  if(!(w(0x0244) & 1)) {
    uint32_t s = (uint32_t) m(rec + 1) + w(0x022D);
    ms(rec + 1, (uint16_t) s);
    ms(rec + 3, (uint16_t) (m(rec + 3) + w(0x022F) + (s >> 16)));
    ms(rec + 0x15, w(0x020E));
    ms(rec + 0x17, w(0x0210));
    ms(rec + 5, w(0x0204));
    ms(rec + 7, w(0x0206));
    ms(rec + 0x13, 0xFFFF);
    segment(r, 0xFAC8);
    uint16_t v = m(rec + 0x2A) & 0xFFDF;
    if(w(0x1E66) & 0x8000) v |= 0x20;
    ms(rec + 0x2A, v | 0x10);
    return;
  }
  tg_bus_write16(0x308000, 0);
  tg_bus_write16(0x308000, w(0x1E9E));
  tg_bus_write16(0x308000, w(0x0210));
  uint32_t s = (uint32_t) m(rec + 1) + dget();
  ms(rec + 1, (uint16_t) s);
  ms(rec + 3, (uint16_t) (m(rec + 3) + dget() + (s >> 16)));
  ms(rec + 0x13, 0xFFFF);
  ms(rec + 0x15, w(0x020E));
  ms(rec + 0x17, w(0x0210));
  segment(r, 0xFB1C);
}
