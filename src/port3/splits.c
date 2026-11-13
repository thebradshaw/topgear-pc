// Top Gear 3000: the screen's raster splits (bank $05).
//
// The road builders leave two lists of line counts: the far road's bands at
// $7E:4CFE + $0164 and the near road's at $7E:4D4A + $0168, each ending with 0. $05:B1FF
// merges them into the table the scanline interrupt walks: each split's height at $0486
// and the DMA channels to start there at $0596 (bit 2/3: the near road's, bit 1: the far
// road's, bit 7 on the first of each), starting at $00C0 (2, or $34 for the lower view).

#include "../port/port.h"

static uint16_t z(const TgRegs* r, uint8_t o) { return ram16((uint16_t) (r->d + o)); }
static void zs(const TgRegs* r, uint8_t o, uint16_t v) { ram16_set((uint16_t) (r->d + o), v); }
static uint8_t zb(const TgRegs* r, uint8_t o) { return g_ram[(uint16_t) (r->d + o)]; }
static void zbs(const TgRegs* r, uint8_t o, uint8_t v) { g_ram[(uint16_t) (r->d + o)] = v; }
// INC dp with an 8-bit accumulator: the low byte only.
static void zinc8(const TgRegs* r, uint8_t o) { zbs(r, o, (uint8_t) (zb(r, o) + 1)); }

// $05:B2B7 does the same for the second player's half of the two-player screen, from its
// lists ($7E:4D96 + $0166, $7E:4DE2 + $016A) into its own table ($050E/$061E, from $00C6).
typedef struct { uint16_t far_i, near_i, which, start; uint32_t far, near; uint16_t mode, height, dma; } Splits;

static void merge_splits(TgRegs* r, const Splits* p) {
  uint32_t bank = (uint32_t) r->db << 16;
  zs(r, 0x14, ram16(p->far_i));
  zs(r, 0x16, ram16(p->near_i));
  zs(r, 0x18, 0x8080);
  bool top = ram16(p->which) != 0;
  uint16_t y = top ? 2 : 0x34;
  tg_bus_write16(bank | p->start, y);
  zs(r, 0x1A, top ? 2 : 0);
  uint16_t s = z(r, 0x1A);
#define FAR() g_ram[(p->far + z(r, 0x14)) & 0x1FFFF]
#define NEAR() g_ram[(p->near + z(r, 0x16)) & 0x1FFFF]
  uint8_t a = FAR(), b = NEAR();
  // The first split's colour mode, by which list starts with the taller band.
  if(a == b) {
    tg_bus_write8(bank | (uint16_t) (p->mode + s), 0x39);
  } else {
    tg_bus_write8(bank | (uint16_t) (p->mode + s), a > b ? 0x29 : 0x19);
    tg_bus_write8(bank | (uint16_t) (p->mode + 1 + s), 0x39);
  }
  for(;;) {
    a = FAR();
    b = NEAR();
    if(a == b) {
      tg_bus_write8(bank | (uint16_t) (p->height + y), a);
      if(a == 0) break;
      tg_bus_write8(bank | (uint16_t) (p->dma + y), (uint8_t) (0x0E | zb(r, 0x18) | zb(r, 0x19)));
      zbs(r, 0x18, 0);
      zbs(r, 0x19, 0);
      y++;
      zinc8(r, 0x14);
      zinc8(r, 0x16);
      continue;
    }
    // The shorter band first (a zero count means that list has ended).
    bool near = a > b ? b != 0 : a == 0;
    if(near) {
      tg_bus_write8(bank | (uint16_t) (p->height + y), b);
      tg_bus_write8(bank | (uint16_t) (p->dma + y), (uint8_t) (0x0C | zb(r, 0x18)));
      zbs(r, 0x18, 0);
      y++;
      zinc8(r, 0x16);
    } else {
      tg_bus_write8(bank | (uint16_t) (p->height + y), a);
      tg_bus_write8(bank | (uint16_t) (p->dma + y), (uint8_t) (0x02 | zb(r, 0x19)));
      zbs(r, 0x19, 0);
      y++;
      zinc8(r, 0x14);
    }
  }
#undef FAR
#undef NEAR
  r->a = ram16(p->which) & 0xFF00;  // B from LDA $00B4, A from the final 0
  r->x = z(r, 0x16);
  r->y = y;
  r->n = false;
  r->z = true;
  r->c = true;
}

void Tg3MergeSplits(TgRegs* r) {
  static const Splits k = { 0x0164, 0x0168, 0x00B4, 0x00C0, 0x4CFE, 0x4D4A, 0x047E, 0x0486, 0x0596 };
  merge_splits(r, &k);
}
void Tg3MergeSplitsBottom(TgRegs* r) {
  static const Splits k = { 0x0166, 0x016A, 0x00B6, 0x00C6, 0x4D96, 0x4DE2, 0x0482, 0x050E, 0x061E };
  merge_splits(r, &k);
}
