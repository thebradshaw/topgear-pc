// Top Gear 3000: horizontal bars of sprites through the DSP-4 (bank $01), such as the
// gauges on the race screen.
//
// $01:8487 draws a bar A pixels long centred on X at height Y: a left end piece ($20F6),
// middle pieces ($20F7) every 8 pixels, and a right end ($60F6, the left end flipped).
// The chip answers each piece with the sprites to show, collected after $84 in the sprite
// buffer at $1574 up to the limit in $1A.

#include "../port/port.h"

static uint16_t z(const TgRegs* r, uint8_t o) { return ram16((uint16_t) (r->d + o)); }
static void zs(const TgRegs* r, uint8_t o, uint16_t v) { ram16_set((uint16_t) (r->d + o), v); }

static uint32_t g_dr;
static void dput(uint16_t v) { tg_bus_write16(g_dr, v); }
static uint16_t dget(void) { return (uint16_t) (tg_bus_read8(g_dr) | tg_bus_read8(g_dr + 1) << 8); }
static void dwait(void) {
  while(!(tg_bus_read8(g_dr + 0x4001) & 0x80)) {}  // BIT $C000
}

// $01:8527: one piece (X: its tile and flags) at $16, height $32 - 8. True: buffer full.
static bool piece(TgRegs* r, uint16_t tile, uint16_t* y) {
  dput(tile);
  r->c = false;
  dput(adc16(r, 0xFFF8, z(r, 0x32)));
  dput(z(r, 0x16));
  dwait();
  while(dget() != 0) {
    *y = (uint16_t) (*y + 2);
    tg_bus_write16((uint32_t) r->db << 16 | (uint16_t) (0x1574 + *y), dget());
    *y = (uint16_t) (*y + 2);
    tg_bus_write16((uint32_t) r->db << 16 | (uint16_t) (0x1574 + *y), dget());
    if(*y == z(r, 0x1A)) return true;
    dwait();
  }
  return false;
}

void Tg3DrawBar(TgRegs* r) {
  g_dr = (uint32_t) r->db << 16 | 0x8000;
  uint16_t len = r->a;
  if(len == 0) {
    dput(0);
    dput(0);
    r->c = false;
    return;
  }
  zs(r, 0x32, r->y);
  uint16_t keep14 = z(r, 0x14), keep16 = z(r, 0x16);
  len |= 1;
  zs(r, 0x14, len);
  zs(r, 0x16, (uint16_t) (r->x - (len >> 1)));
  uint16_t y = z(r, 0x84);
  dput(0);
  uint16_t tile = 0x20F6;
  bool full = false;
  for(;;) {
    zs(r, 0x14, (uint16_t) (z(r, 0x14) - 8));
    if(z(r, 0x14) & 0x8000) break;
    if((full = piece(r, tile, &y))) break;
    zs(r, 0x16, (uint16_t) (z(r, 0x16) + 8));
    tile = 0x20F7;
  }
  if(!full) {
    zs(r, 0x16, (uint16_t) (z(r, 0x14) + z(r, 0x16)));
    tile = 0x60F6;
    full = piece(r, tile, &y);
  }
  r->x = tile;
  r->y = y;
  if(full) {
    // The rest of the chip's answer is dropped. (The original then returns with $14 and
    // $16 still on the stack, which would crash; it isn't seen in play.)
    for(;;) {
      dwait();
      if(dget() == 0) break;
      dget();
      dget();
    }
    dput(0);
    zs(r, 0x84, y);
    r->a = 0;
    r->n = false;
    r->z = true;
    r->c = true;
    return;
  }
  zs(r, 0x16, keep16);
  zs(r, 0x14, keep14);
  dput(0);
  zs(r, 0x84, y);
  r->a = keep14;
  flags_nz16(r, keep14);
}
