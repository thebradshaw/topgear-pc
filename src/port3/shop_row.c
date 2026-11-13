// Top Gear 3000: one row of a scrolling text list (bank $00).
//
// $00:B2F2 draws string X/2 of the list at $80:B371 centred at row $22 (only while the
// row is on screen, $1000-$57FF), moves the row down 8 lines and steps X. It stops the
// list (X = $1E, count in $F0) at the first entry whose byte at $036E is clear.

#include <stddef.h>

#include "../port/port.h"

static uint16_t z(const TgRegs* r, uint8_t o) { return ram16((uint16_t) (r->d + o)); }
static void zs(const TgRegs* r, uint8_t o, uint16_t v) { ram16_set((uint16_t) (r->d + o), v); }

void Tg3ListRow(TgRegs* r) {
  uint16_t row = z(r, 0x22);
  if(!((uint16_t) (row - 0x1000) & 0x8000) && ((uint16_t) (row - 0x5800) & 0x8000)) {
    uint16_t x = r->x;
    r->x = (uint16_t) (tg_bus_read8(0x80B371u + x) | tg_bus_read8(0x80B372u + x) << 8);
    r->y = (uint16_t) (row + z(r, 0xE7));
    r->a = 0x0080;
    const TgPortFunc* fn = tg_port_find(0x0084BE);
    bool done = false;
    if(fn != NULL) {
      fn->fn(r);
      done = !tg_port_take_declined();
    }
    if(!done) tg_call_jsr(r, 0x84BE, 0xB310, TG_MX_16);
    r->x = x;
  }
  row = z(r, 0x22);
  uint16_t next = (uint16_t) (row + 0x800);
  r->v = (~(row ^ 0x800) & (row ^ next) & 0x8000) != 0;
  zs(r, 0x22, next);
  uint16_t x = (uint16_t) (r->x + 2);
  r->x = x;
  if(x == 0x20) {
    r->a = next;
    r->n = false;
    r->z = true;
    r->c = true;
    return;
  }
  uint16_t half = (uint16_t) (x >> 1);
  r->y = half;
  uint32_t p = ((uint32_t) r->db << 16) | (uint16_t) (0x036E + half);
  uint16_t a = tg_bus_read8(p);
  r->c = (x & 1) != 0;
  r->n = false;
  if(a != 0) {
    r->a = a;
    r->z = false;
    return;
  }
  r->a = half;
  zs(r, 0xF0, half);
  r->x = 0x1E;
  r->z = false;
}
