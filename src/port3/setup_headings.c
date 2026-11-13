// Top Gear 3000: the player setup screen's sliding headings (bank $00, data in bank $81).
//
// $00:C35E draws the two cursor bars (sprites, by $00:C475) and the four headings NAME,
// CONTROL, SPEED and RACE, with the one picked ($48) lit, then moves the scroll ($4A
// high byte) a step toward the picked heading's place in the table at $BE0A. Between
// two places the bars' width ($14) is worked out with the CPU's divider and the PPU's
// multiplier, as the original does.

#include <stddef.h>

#include "../port/port.h"

static uint16_t z(const TgRegs* r, uint8_t o) { return ram16((uint16_t) (r->d + o)); }
static void zs(const TgRegs* r, uint8_t o, uint16_t v) { ram16_set((uint16_t) (r->d + o), v); }

static uint32_t at(const TgRegs* r, uint16_t a) { return ((uint32_t) r->db << 16) | a; }
static uint8_t rd8(const TgRegs* r, uint16_t a) { return tg_bus_read8(at(r, a)); }
static uint16_t rd16(const TgRegs* r, uint16_t a) {
  return (uint16_t) (rd8(r, a) | rd8(r, (uint16_t) (a + 1)) << 8);
}
static void wr16(const TgRegs* r, uint16_t a, uint16_t v) { tg_bus_write16(at(r, a), v); }

// $00:C475: one cursor bar, eight sprites at slot X, centred on $18, tiles from $1A.
static void bar(TgRegs* r) {
  uint16_t x = r->x, w = z(r, 0x14), c = z(r, 0x18);
  uint16_t p = (uint16_t) (c - (w >> 1));
  static const uint8_t step[8] = { 0, 8, 8, 8, 0, 8, 6, 8 };
  for(int i = 0; i < 8; i++) {
    if(i == 4) p = (uint16_t) ((w >> 1) + c + (w & 1) - 0x20);
    else p = (uint16_t) (p + step[i]);
    wr16(r, (uint16_t) (0x0900 + x + 4 * i), p);
  }
  uint16_t t = (uint16_t) (z(r, 0x1A) + 0x150);
  wr16(r, (uint16_t) (0x0902 + x), t);
  for(int i = 1; i < 7; i++) wr16(r, (uint16_t) (0x0902 + x + 4 * i), (uint16_t) (t + 1));
  wr16(r, (uint16_t) (0x091E + x), (uint16_t) (t | 0x4000));
  for(int i = 0; i < 8; i++) wr16(r, (uint16_t) (0x0B01 + x + 4 * i), 0);
  uint32_t s = (uint32_t) x + 0x20;
  uint16_t res = (uint16_t) s;
  r->c = s > 0xFFFF;
  r->v = (~(x ^ 0x20) & (x ^ res) & 0x8000) != 0;
  r->n = (res & 0x8000) != 0;
  r->z = res == 0;
  r->a = res;
  r->x = res;
}

void Tg3SetupBar(TgRegs* r) { bar(r); }

static void centred(TgRegs* r, uint16_t site) {
  const TgPortFunc* fn = tg_port_find(0x0084BE);
  if(fn != NULL) {
    fn->fn(r);
    if(!tg_port_take_declined()) return;
  }
  tg_call_jsr(r, 0x84BE, (uint16_t) (site + 3), TG_MX_16);
}

void Tg3SetupHeadings(TgRegs* r) {
  uint16_t h = z(r, 0x4A) & 0xFF00;
  uint16_t x = 0;
  for(;;) {
    uint16_t m = rd16(r, (uint16_t) (0xBE0A + x));
    uint16_t diff = (uint16_t) (h - m);
    if(diff & 0x8000) {
      tg_bus_write16(0x004204, (uint16_t) (m - h));
      tg_bus_write8(0x004206, (uint8_t) (rd8(r, (uint16_t) (0xBE0B + x)) - rd8(r, (uint16_t) (0xBE09 + x))));
      tg_bus_write8(0x00211B, tg_bus_read8(0x004214));
      tg_bus_write8(0x00211B, tg_bus_read8(0x004215));
      tg_bus_write8(0x00211C, (uint8_t) (rd8(r, (uint16_t) (0xBE12 + x)) - rd8(r, (uint16_t) (0xBE10 + x))));
      uint16_t prod = (uint16_t) (tg_bus_read8(0x002135) | tg_bus_read8(0x002136) << 8);
      zs(r, 0x14, (uint16_t) (rd16(r, (uint16_t) (0xBE12 + x)) - prod));
      break;
    }
    if(diff == 0) {
      zs(r, 0x14, rd16(r, (uint16_t) (0xBE12 + x)));
      break;
    }
    x = (uint16_t) (x + 2);
  }
  r->x = z(r, 0x74);
  zs(r, 0x18, (uint16_t) (z(r, 0x4A) + 0x1D80 + z(r, 0xE7)));
  zs(r, 0x1A, 0);
  bar(r);
  zs(r, 0x18, (uint16_t) (z(r, 0x18) + 0x600));
  zs(r, 0x1A, 0x8000);
  bar(r);
  zs(r, 0x74, r->x);
  zs(r, 0x18, (uint16_t) (0x2080 + z(r, 0xE7)));
  zs(r, 0x1A, (uint16_t) (z(r, 0x48) + 1));
  static const uint16_t text[4] = { 0xBDE0, 0xBDE5, 0xBDED, 0xBDF3 };
  static const uint16_t row[4] = { 0, 0x0B00, 0x1600, 0x2100 };
  static const uint16_t site[4] = { 0xC401, 0xC41F, 0xC43D, 0xC45B };
  for(int i = 0; i < 4; i++) {
    uint16_t n = (uint16_t) (z(r, 0x1A) - 1);
    zs(r, 0x1A, n);
    zs(r, 0x14, n == 0 ? 0x0300 : 0x0100);
    r->y = (uint16_t) (z(r, 0x18) + row[i]);
    r->x = text[i];
    r->a = 0x0081;
    centred(r, site[i]);
  }
  // Move the scroll a step toward the picked heading.
  r->x = (uint16_t) (z(r, 0x48) << 1);
  h = z(r, 0x4A) & 0xFF00;
  r->a = h;
  uint16_t m = rd16(r, (uint16_t) (0xBE0A + r->x));
  uint16_t diff = (uint16_t) (h - m);
  r->c = h >= m;
  r->v = ((h ^ m) & (h ^ diff) & 0x8000) != 0;
  if(diff == 0) {
    r->n = false;
    r->z = true;
    return;
  }
  uint16_t s = (uint16_t) (z(r, 0x4B) + ((diff & 0x8000) ? 1 : -1));
  zs(r, 0x4B, s);
  r->n = (s & 0x8000) != 0;
  r->z = s == 0;
}
