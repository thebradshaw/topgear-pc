// Top Gear 3000: the per-frame HDMA tables for the two road layers (bank $05).
//
// $05:BC20 swaps the double-buffered table pointers, then for each view half writes an
// HDMA table backwards from its end: the sky above the horizon, the road's lines (from
// the line words the road builder left at $0796 or $0E56), and the bands down to the car
// ($0237: the dashboard's top). Each entry is a line count (bit 7: one word per line)
// and a source address.

#include "../port/port.h"

static uint16_t z(const TgRegs* r, uint8_t o) { return ram16((uint16_t) (r->d + o)); }
static void zs(const TgRegs* r, uint8_t o, uint16_t v) { ram16_set((uint16_t) (r->d + o), v); }
static uint16_t w(uint16_t a) { return ram16(a); }
static void ws(uint16_t a, uint16_t v) { ram16_set(a, v); }

typedef struct {
  uint16_t height, countIdx;
  uint32_t counts;
  uint16_t lineBase, lines, ptr, band, last;
} Half;

static uint32_t g_bank;
// One entry: count, then the address over its second and third bytes.
static void entry(uint16_t* y, uint16_t count, uint16_t adr) {
  tg_bus_write16(g_bank | *y, count);
  tg_bus_write16(g_bank | (uint16_t) (*y + 1), adr);
  *y = (uint16_t) (*y - 3);
}

static void half(TgRegs* r, const Half* h) {
  uint16_t y = w(h->ptr);
  tg_bus_write16(g_bank | y, 0);
  y = (uint16_t) (y - 3);
  zs(r, 0x14, 0xD8);
  uint16_t a = w(h->height);
  if(a & 0x8000) {
    // The view below the road: the sky starts lower.
    a = (uint16_t) -a;
    if(!((uint16_t) (a - 0xD8) & 0x8000)) a = 0xD8;
    if(a >= 0x80) {
      a = (uint16_t) (a - 0x7F);
      zs(r, 0x14, (uint16_t) (z(r, 0x14) - a));
      entry(&y, a, 0xA5D8);
      a = 0x7F;
    }
    zs(r, 0x14, (uint16_t) (z(r, 0x14) - a));
    entry(&y, a, 0xA5D8);
  }
  zs(r, 0x16, z(r, 0x14));
  a = g_ram[(h->counts + w(h->countIdx)) & 0x1FFFF];
  if(a == 0) a = 0xDC;
  a = (uint16_t) (a - 4);
  if(a & 0x8000) a = 0;
  if((uint16_t) (a - z(r, 0x14)) & 0x8000) {
    zs(r, 0x14, a);
    a = (uint16_t) (z(r, 0x16) - a);
    if(a != 0) {
      uint16_t t = (uint16_t) ((0xD7 - z(r, 0x14)) << 2);
      entry(&y, a | 0x80, (uint16_t) (w(h->lineBase) - t + h->lines));
    }
  }
  a = (uint16_t) (w(0x0237) + 0xB0);
  if(a & 0x8000) a = 0;
  zs(r, 0x16, a);
  uint16_t d = (uint16_t) (a - z(r, 0x14));
  if(d & 0x8000) {
    zs(r, 0x14, a);
    entry(&y, (uint16_t) (-d | 0x80), (uint16_t) ((a << 2) + 0xA968));
  }
  a = (uint16_t) (w(0x0237) + 0x70);
  if(a & 0x8000) a = 0;
  zs(r, 0x16, a);
  d = (uint16_t) (a - z(r, 0x14));
  if(d & 0x8000) {
    entry(&y, (uint16_t) -d, (uint16_t) (w(0x00E0) + h->band));
    zs(r, 0x14, a);
  }
  a = z(r, 0x14);
  if(a != 0) {
    if(a >= 0x80) {
      entry(&y, (uint16_t) ((a - 0x7F) | 0x80), 0xA7D8);
      a = 0x7F;
    }
    entry(&y, a | 0x80, 0xA5DC);
  }
  tg_bus_write16(g_bank | y, 4);
  ws(h->ptr, y);
  r->x = w(h->countIdx);
  r->y = w(0x00E0);
  r->c = true;
  a = sbc16(r, 0xBB, w(0x0237));
  r->c = true;
  a = sbc16(r, a, 0x70);
  tg_bus_write16(g_bank | (uint16_t) (r->y + h->last), a);
  r->a = a;
}

void Tg3RoadHdmaTables(TgRegs* r) {
  g_bank = (uint32_t) r->db << 16;
  uint16_t t = w(0x00E0);
  ws(0x00E0, w(0x00E4));
  ws(0x00E4, t);
  t = w(0x1E36);
  ws(0x1E36, w(0x1E32));
  ws(0x00D0, w(0x1E32));
  ws(0x1E32, w(0x1E2E));
  ws(0x1E2E, t);
  t = w(0x1E38);
  ws(0x1E38, w(0x1E34));
  ws(0x00D2, w(0x1E34));
  ws(0x1E34, w(0x1E30));
  ws(0x1E30, t);
  static const Half kFar = { 0x0242, 0x0164, 0x4CFE, 0x1E3E, 0x0796, 0x00D0, 0x1536, 0x1538 };
  static const Half kNear = { 0x1EF2, 0x0168, 0x4D4A, 0x1E40, 0x0E56, 0x00D2, 0x1556, 0x1558 };
  half(r, &kFar);
  half(r, &kNear);
}

// $03:DE02: the same for the two-player screen. Four tables are rotated instead of two, and
// each of the two layers' tables holds both players' halves: the second player's ($02EA:
// its dashboard top) and then the first's, each a shorter view ($68 lines) than the one
// player's. Every half ends with the next half's first count (8), the last with 4.
typedef struct {
  uint16_t ptr, first, height, countIdx;
  uint32_t counts;
  uint16_t empty, keep, lineBase, lines, band, high, low, sky, end, last, top;
} Half2;

static void half2(TgRegs* r, const Half2* h) {
  uint16_t y = w(h->ptr);
  tg_bus_write16(g_bank | y, h->first);
  y = (uint16_t) (y - 3);
  zs(r, 0x14, 0x68);
  uint16_t a = w(h->height);
  if(a & 0x8000) {
    a = (uint16_t) -a;
    if(!((uint16_t) (a - 0x68) & 0x8000)) a = 0x68;
    zs(r, 0x14, (uint16_t) (z(r, 0x14) - a));
    entry(&y, a, 0xA5D8);
  }
  zs(r, 0x16, z(r, 0x14));
  a = g_ram[(h->counts + w(h->countIdx)) & 0x1FFFF];
  if(a == 0) a = h->empty;
  a = (uint16_t) (a - h->keep);
  if(a & 0x8000) a = 0;
  if((uint16_t) (a - z(r, 0x14)) & 0x8000) {
    zs(r, 0x14, a);
    a = (uint16_t) (z(r, 0x16) - a);
    if(a != 0) {
      uint16_t t = (uint16_t) ((0x67 - z(r, 0x14)) << 2);
      entry(&y, a | 0x80, (uint16_t) (w(h->lineBase) - t + h->lines));
    }
  }
  uint16_t band = w(h->band);
  a = (uint16_t) (band + 0x40);
  if(a & 0x8000) a = 0;
  zs(r, 0x16, a);
  uint16_t d = (uint16_t) (a - z(r, 0x14));
  if(d & 0x8000) {
    zs(r, 0x14, a);
    entry(&y, (uint16_t) (-d | 0x80), (uint16_t) ((a << 2) + h->high));
  }
  a = band;
  if(a & 0x8000) a = 0;
  zs(r, 0x16, a);
  d = (uint16_t) (a - z(r, 0x14));
  if(d & 0x8000) {
    entry(&y, (uint16_t) -d, (uint16_t) (w(0x00E0) + h->low));
    zs(r, 0x14, a);
  }
  a = z(r, 0x14);
  if(a != 0) entry(&y, a | 0x80, h->sky);
  tg_bus_write16(g_bank | y, h->end);
  ws(h->ptr, y);
  r->x = w(h->countIdx);
  r->y = w(0x00E0);
  r->c = true;
  a = sbc16(r, h->top, band);
  tg_bus_write16(g_bank | (uint16_t) (r->y + h->last), a);
  r->a = a;
}

void Tg3RoadHdmaTables2p(TgRegs* r) {
  g_bank = (uint32_t) r->db << 16;
  uint16_t t = w(0x00E8);
  ws(0x00E8, w(0x00E6));
  ws(0x00E6, w(0x00E4));
  ws(0x00E4, w(0x00E0));
  ws(0x00E0, t);
  t = w(0x1E3A);
  ws(0x1E3A, w(0x1E36));
  ws(0x1E36, w(0x1E32));
  ws(0x1E32, w(0x1E2E));
  ws(0x1E2E, t);
  ws(0x00D0, t);
  t = w(0x1E3C);
  ws(0x1E3C, w(0x1E38));
  ws(0x1E38, w(0x1E34));
  ws(0x1E34, w(0x1E30));
  ws(0x1E30, t);
  ws(0x00D2, t);
  static const Half2 kHalves[4] = {
    { 0x00D0, 0, 0x02F5, 0x0166, 0x4D96, 0xDC, 0x74, 0x1E42, 0x0AF6, 0x02EA, 0xAB28, 0x1546, 0xA79C, 0, 0x1548, 0x4B },
    { 0x00D0, 8, 0x0242, 0x0164, 0x4CFE, 0x6C, 0x04, 0x1E3E, 0x0796, 0x0237, 0xA968, 0x1536, 0xA5DC, 4, 0x1538, 0xBB },
    { 0x00D2, 8, 0x1EFC, 0x016A, 0x4DE2, 0xDC, 0x74, 0x1E44, 0x11B6, 0x02EA, 0xAB28, 0x1566, 0xA79C, 4, 0x1568, 0x4B },
    { 0x00D2, 8, 0x1EF2, 0x0168, 0x4D4A, 0x6C, 0x04, 0x1E40, 0x0E56, 0x0237, 0xA968, 0x1556, 0xA5DC, 4, 0x1558, 0xBB },
  };
  for(int i = 0; i < 4; i++) half2(r, &kHalves[i]);
}
