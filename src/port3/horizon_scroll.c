// Top Gear 3000: the background's scroll across, following the bends (bank $03).
//
// $03:8989 adds the road's sideways push this frame ($01CC) into the 24-bit scroll at
// $00D4..$00D6 (unless bit 4 of $0026 holds it), then sets the background's scroll for
// this player (the table slot in $E2) at $1536 and $1556 from its middle word, offset by
// $3C. With $1F32 negative the first of those scrolls a little slower for a parallax.

#include <stddef.h>

#include "../port/port.h"

static uint16_t w(uint16_t a) { return ram16(a); }
static void ws(uint16_t a, uint16_t v) { ram16_set(a, v); }

void Tg3HorizonScroll(TgRegs* r) {
  uint8_t db = r->db;
  if(!(db < 0x40 || db == 0x7E || (db >= 0x80 && db < 0xC0))) {
    tg_port_decline();
    return;
  }
  r->x = ram16((uint16_t) (r->d + 0xE2));
  if(!(w(0x0026) & 0x10)) {
    uint16_t push = w(0x01CC);
    r->y = (push & 0x8000) ? 0xFFFF : 0;
    uint32_t s = (uint32_t) push + w(0x00D4);
    ws(0x00D4, (uint16_t) s);
    g_ram[0x00D6] = (uint8_t) (r->y + g_ram[0x00D6] + (s >> 16));
  }
  uint16_t d5 = w(0x00D5);
  uint16_t a = (uint16_t) (d5 + 0x3C);
  r->c = a < d5;
  r->v = (~(d5 ^ 0x3C) & (d5 ^ a) & 0x8000) != 0;
  ws((uint16_t) (0x1536 + r->x), a);
  ws((uint16_t) (0x1556 + r->x), a);
  a = w(0x1F32);
  if(a & 0x8000) {
    uint16_t v = (uint16_t) ~(d5 >> 3);
    uint32_t s = (uint32_t) d5 + v + 1;
    a = (uint16_t) s;
    r->c = (s >> 16) != 0;
    r->v = (~(d5 ^ v) & (d5 ^ a) & 0x8000) != 0;
    ws((uint16_t) (0x1536 + r->x), a);
  }
  r->a = a;
  r->n = (a & 0x8000) != 0;
  r->z = a == 0;
}
