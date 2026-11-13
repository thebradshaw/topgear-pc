// Top Gear 3000: scrolls five animated 8x8 tiles one row up (bank $05).
//
// $05:9147 rotates the tiles at $7E:2000-$213F (five tiles of 8 rows, 8 bytes a row; each
// row is one word repeated four times) up by a row, the top row going round to the bottom.
// It is unrolled in the original.

#include "../port/port.h"

void Tg3TileScroll(TgRegs* r) {
  uint16_t a = 0, x = 0;
  for(int b = 0; b < 5; b++) {
    uint16_t base = (uint16_t) (0x2000 + 0x40 * b);
    x = ram16(base);
    for(int i = 0; i < 8; i++) {
      uint16_t v = x;
      if(i < 7) v = a = ram16((uint16_t) (base + 8 * (i + 1)));
      for(int k = 0; k < 4; k++) ram16_set((uint16_t) (base + 8 * i + 2 * k), v);
    }
  }
  r->a = a;
  r->x = x;
  r->n = (r->db & 0x80) != 0;  // PLB of the caller's bank
  r->z = r->db == 0;
}
