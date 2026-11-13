// Top Gear 3000: building the road for a view with the DSP-4 (bank $05).
//
// $05:B828 sets the chip up with the camera (the view's place on the track: $0224-$0226,
// its height $0242, the track's tables at $7E:3AEC-3EEE) and then feeds it the track
// ahead section by section ($14 runs from the camera's place to $0E00 in steps of $80).
// For each section the chip returns how many screen lines the road takes there and, for
// each line, its scroll and shape words (stored at $0E56/$0E58 + the line's offset). The
// roadside scenery strip for each section (8 bytes) is copied below $18 in bank $7E.

#include "../port/port.h"

static uint16_t z(const TgRegs* r, uint8_t o) { return ram16((uint16_t) (r->d + o)); }
static void zs(const TgRegs* r, uint8_t o, uint16_t v) { ram16_set((uint16_t) (r->d + o), v); }
static uint16_t w(uint16_t a) { return ram16(a); }
static void ws(uint16_t a, uint16_t v) { ram16_set(a, v); }
// Bank $7E by long address.
static uint16_t l7e(uint32_t a) { return tg_read16(a); }
static void l7e_set(uint32_t a, uint16_t v) { ram16_set((a - 0x7E0000) & 0x1FFFF, v); }

static void dput(uint16_t v) { tg_bus_write16(0x308000, v); }
static uint16_t dget(void) { return (uint16_t) (tg_bus_read8(0x308000) | tg_bus_read8(0x308001) << 8); }
static void dwait(void) {
  while(!(tg_bus_read8(0x30C001) & 0x80)) {}
}
static uint16_t swap(uint16_t v) { return (uint16_t) (v << 8 | v >> 8); }

// A 32-bit product (low word, then high) shifted left twice: its high word.
static uint16_t product_x4(TgRegs* r, uint8_t keep) {
  uint32_t p = dget();
  p |= (uint32_t) dget() << 16;
  p <<= 2;
  zs(r, keep, (uint16_t) p);
  return (uint16_t) (p >> 16);
}

void Tg3BuildRoad(TgRegs* r) {
  uint16_t y = 0x23, x = 0x292B;
  zs(r, 0x1A, 0x035C);
  zs(r, 0x1C, 0x0004);
  bool top = w(0x00B4) != 0;  // which view (1 player: the only one)
  if(!top) {
    y = 0x45;
    x = 0x2A2B;
    zs(r, 0x1A, 0x050C);
    zs(r, 0x1C, 0x01A4);
  }
  zs(r, 0x18, x);
  zs(r, 0x14, (uint16_t) (0x80 - ((w(0x0225) >> 1) & 0x7E)));
  zs(r, 0x16, (uint16_t) ((w(0x0226) << 1) & 0x3E));
  x = z(r, 0x16);
  l7e_set(0x7E376C, l7e(0x7E3E6C + x));
  l7e_set(0x7E356C, l7e(0x7E3DEC + x));
  // The camera's height over the road ahead: command 0 (multiply).
  dput(0);
  dput((uint16_t) ((l7e(0x7E3E6E + x) - l7e(0x7E3E6C + x)) << 1));
  dput((uint16_t) ((w(0x0224) >> 1) & 0xFF00));
  dget();
  ws(0x1EF2, (uint16_t) (w(0x0242) + dget() + l7e(0x7E3E6C + x)));
  dput(0);
  dput(l7e(0x7E3AEC + x));
  dput(0xFC00);
  uint16_t lo = dget();
  ws(0x017C, (uint16_t) (dget() + (lo >= 0x8000)));  // rounded
  // The camera's side position and the road's turn, from the section's two ends.
  dput(0);
  dput(l7e(0x7E3B6E + x));
  dput(swap((uint16_t) ((0x80 - z(r, 0x14)) >> 1)));
  uint16_t a = (uint16_t) -product_x4(r, 0x1E);
  zs(r, 0x1E, (uint16_t) (w(0x1EDA) + a));
  dput(0);
  dput(l7e(0x7E3AEE + x));
  dput(swap((uint16_t) ((0x80 - z(r, 0x14)) >> 1)));
  zs(r, 0x26, (uint16_t) -product_x4(r, 0x26));
  dput(0);
  dput(z(r, 0x26));
  dput(swap((uint16_t) (z(r, 0x14) >> 1)));
  zs(r, 0x2A, product_x4(r, 0x2A));
  // The road's start: command 7 (or $10 with the scenery mode in $E4).
  x = y;
  l7e_set(0x7E4D49 + x, 0);
  uint16_t mode = z(r, 0xE4);
  if(mode & 0x8000) {
    dput(0x0007);
  } else {
    dput(0x0010);
    dput(mode);
  }
  dput(0);
  uint16_t h = w(0x1EF2);
  bool below = (h & 0x8000) != 0;  // the camera below the road's level here
  dput((uint16_t) (h + 0x33));
  l7e_set(0x7E4D49 + x, below ? (uint16_t) (h + 0xDC) : 0x00DC);
  x = (uint16_t) (x - 1);
  dput(below ? (uint16_t) (h + 0x33) : 0x0033);
  dput(0xFF5B);
  dput(0x00BF);
  dput(below ? (uint16_t) (h + 0xDC) : 0x00DC);
  dput(w(0x1EF4));
  dput(w(0x1EF6));
  dput(0x0180);
  dput(below ? (uint16_t) ((h << 2) + z(r, 0x1A)) : z(r, 0x1A));
  dput(0x00C0);
  zs(r, 0x22, x);
  zs(r, 0x24, 0x0002);

  // Section by section.
  uint16_t s14 = z(r, 0x14);
  for(;;) {
    uint16_t s16 = z(r, 0x16), s24 = z(r, 0x24);
    uint16_t far = tg_read16(0x83ACEC + s14);
    dput(far);
    zs(r, 0x30, far);
    dput(l7e(0x7E37EC + s24));
    dput(l7e(0x7E3E6E + s16));
    dput(l7e(0x7E35EC + s24));
    dput(l7e(0x7E3DEE + s16));
    dput(l7e(0x7E3BEC + s16));
    s16 = (uint16_t) (s16 + 2);
    zs(r, 0x16, s16);
    l7e_set(0x7E376C + s24, (uint16_t) (l7e(0x7E3E6C + s16) + l7e(0x7E36EC + s24)));
    l7e_set(0x7E356C + s24, (uint16_t) (l7e(0x7E3DEC + s16) + l7e(0x7E34EC + s24)));
    l7e_set(0x7E366C + s24, dget());
    l7e_set(0x7E386C + s24, dget());
    l7e_set(0x7E396C + s24, far);
    l7e_set(0x7E3A6C + s24, s14);
    zs(r, 0x24, (uint16_t) (s24 + 2));
    dwait();
    uint16_t lines = dget();
    if(lines != 0) {
      // The lines this section takes, off the count left.
      uint16_t s22 = z(r, 0x22);
      uint32_t at = (0x7E4D49 + s22 - 0x7E0000) & 0x1FFFF;
      g_ram[at] = (uint8_t) (g_ram[(at + 1) & 0x1FFFF] - (uint8_t) lines);
      // The section's scenery strip: 8 bytes from its entry, below $18.
      uint16_t src = l7e(0x7E3FEC + z(r, 0x16));
      uint16_t dst = z(r, 0x18);
      if(z(r, 0xE4) & 0x8000) {
        uint32_t bank = (src & 0x8000) ? 0x900000 : 0x7E0000;
        for(int i = 0; i < 8; i++)
          g_ram[(uint16_t) (dst - i)] = tg_read8(bank | (uint16_t) (src - i));
        zs(r, 0x18, (uint16_t) (dst - 8));
      } else {
        // Each of its four words turned by the section's shading ($7E:6C33).
        dput(tg_read16(0x83ACEC + s14));
        uint16_t from = (uint16_t) (l7e(0x7E3FEC + z(r, 0x16)) - 7);
        uint32_t bank = (from & 0x8000) ? 0x900000 : 0x7E0000;
        dput(tg_read16(bank | from));
        dst = (uint16_t) (z(r, 0x18) - 8);
        zs(r, 0x18, dst);
        l7e_set(0x7E0001 + dst, dget());
        for(int k = 2; k <= 6; k += 2) {
          dput(l7e(0x7E6C33 + s14));
          dput(tg_read16(bank | (uint16_t) (from + k)));
          l7e_set(0x7E0001 + dst + k, dget());
        }
      }
      // Then the lines: offset, then the two words for it.
      dwait();
      for(uint16_t n = lines; n != 0; n--) {
        uint16_t off = dget();
        uint16_t v = dget();
        tg_bus_write16(0x300E58 + off, v);
        v = dget();
        tg_bus_write16(0x300E56 + off, v);
      }
      zs(r, 0x22, (uint16_t) (s22 - 1));
    }
    s14 = (uint16_t) (s14 + 0x80);
    zs(r, 0x14, s14);
    if(s14 >= 0x0E00) break;
  }
  dput(0x8000);
  zs(r, 0x94, z(r, 0x24));
  x = top ? 0x23 : 0x45;
  uint16_t count = 0;
  h = w(0x1EF2);
  if(h & 0x8000) {
    count = (uint16_t) (h + 0xDC);
    r->v = false;
    r->c = count >= 4;  // CMP #4
    if((uint16_t) (count - 4) & 0x8000) count = 4;
  } else {
    r->c = true;
    r->v = false;
  }
  l7e_set(0x7E4D49 + x, count);
  ws(0x0168, z(r, 0x22));
  uint16_t yy = (uint16_t) (z(r, 0x18) + 1);
  ws(0x0160, yy);
  ws(0x1E40, z(r, 0x1A));
  r->a = z(r, 0x1A);
  r->x = x;
  r->y = yy;
  r->db = 0x83;
  r->n = true;  // PLB of $83
  r->z = false;
}

// $05:B3D7: the other half of the view's road, the same way as BuildRoad but from the
// car's own place ($0200-$020D) rather than the camera's: for each section the chip's four
// answers go to the section tables at $7E:34EC-37EC (its far distance and place at
// $38EC/$39EC), each line's two words to $0796/$0798 + its offset, and some sections
// (bit 15 of their entry at $7E:3D6A) pass the road's banking on to the chip.
// Bank $90: the ROM above $8000, low RAM below.
static uint32_t bank90(uint16_t a) { return (a & 0x8000) ? 0x900000u | a : 0x7E0000u | a; }
static uint16_t sext8(uint16_t v) { v &= 0xFF; return (v & 0x80) ? (uint16_t) (v | 0xFF00) : v; }

void Tg3BuildRoadCar(TgRegs* r) {
  bool top = w(0x00B4) != 0;
  uint16_t y = top ? 0x23 : 0x45;
  zs(r, 0x1A, top ? 0x06BC : 0x0D7C);
  zs(r, 0x1C, top ? 0x0004 : 0x01A4);
  zs(r, 0x18, top ? 0x272B : 0x282B);
  zs(r, 0x14, (uint16_t) (0x80 - ((w(0x0225) >> 1) & 0x7E)));
  zs(r, 0x16, (uint16_t) ((w(0x0226) << 1) & 0x3E));
  uint16_t x = z(r, 0x16);
  dput(0);
  dput(l7e(0x7E3AEC + x));
  dput(0xFC00);
  uint16_t lo = dget();
  ws(0x0174, (uint16_t) (dget() + (lo >= 0x8000)));  // rounded
  // The car's side position against the road's turn, from the section's two ends.
  dput(0);
  dput(l7e(0x7E3B6E + x));
  dput(swap((uint16_t) ((0x80 - z(r, 0x14)) >> 1)));
  zs(r, 0x1E, (uint16_t) (w(0x0208) - product_x4(r, 0x1E)));
  dput(0);
  dput(w(0x0208));
  dput(swap((uint16_t) (z(r, 0x14) >> 1)));
  zs(r, 0x20, product_x4(r, 0x20));
  // The car's place along the track (24 bits at $020B) less the turn so far: $26-$28.
  dput(0);
  dput(l7e(0x7E3AEE + x));
  dput(swap((uint16_t) ((0x80 - z(r, 0x14)) >> 1)));
  uint32_t p = dget();
  p |= (uint32_t) dget() << 16;
  r->c = (p >> 30) & 1;  // the last bit ROL shifted out
  uint16_t turn = (uint16_t) -(uint16_t) (p >> 14);
  zs(r, 0x26, turn);
  uint16_t hi = (turn & 0x8000) ? 0xFFFF : 0;
  zs(r, 0x26, adc16(r, w(0x020B), turn));
  zs(r, 0x28, adc16(r, sext8(w(0x020D)), hi));
  dput(0);
  dput(z(r, 0x26));
  dput(swap((uint16_t) (z(r, 0x14) >> 1)));
  dget();
  uint16_t a = dget();
  zs(r, 0x2A, a);
  zs(r, 0x2C, (a & 0x8000) ? 0xFFFF : 0);
  dput(0);
  dput((uint16_t) (z(r, 0x28) + ((z(r, 0x26) & 0x8000) ? 1 : 0)));
  dput(swap((uint16_t) (z(r, 0x14) >> 1)));
  r->c = false;
  uint16_t l2 = adc16(r, dget(), z(r, 0x2A));
  uint16_t h2 = adc16(r, dget(), z(r, 0x2C));
  uint32_t q = ((uint32_t) h2 << 16 | l2) << 2;
  zs(r, 0x2A, (uint16_t) q);
  zs(r, 0x2C, (uint16_t) (q >> 16));
  // The road's start: command 1 (or $0F with the scenery mode in $E4).
  x = y;
  l7e_set(0x7E4CFD + x, 0);
  uint16_t mode = z(r, 0xE4);
  if(mode & 0x8000) {
    dput(0x0001);
  } else {
    dput(0x000F);
    dput(mode);
  }
  dput(0);
  uint16_t h = w(0x0242);
  bool below = (h & 0x8000) != 0;  // the car below the road's level here
  dput((uint16_t) (h + 0x33));
  l7e_set(0x7E4CFD + x, below ? (uint16_t) (h + 0xDC) : 0x00DC);
  x = (uint16_t) (x - 1);
  dput(below ? (uint16_t) (h + 0x33) : 0x0033);
  dput(0xFF5B);
  dput(0x00BF);
  dput(below ? (uint16_t) (h + 0xDC) : 0x00DC);
  dput(w(0x0200));
  a = w(0x0202);  // one step towards 0
  if(a != 0) a = (uint16_t) (((uint16_t) (a - 1) & 0x8000) ? a + 1 : a - 1);
  dput(a);
  dput(0x0180);
  dput(below ? (uint16_t) ((h << 2) + z(r, 0x1A)) : z(r, 0x1A));
  dput(0x00C0);
  zs(r, 0x22, x);
  dput(z(r, 0x1D) & 0xFF00);
  dput(sext8(z(r, 0x1F)));
  dput((uint16_t) ((z(r, 0x25) & 0xFF00) | (w(0x020A) & 0xFF)));
  dput(z(r, 0x27));
  zs(r, 0x24, 0x0002);

  // Section by section.
  uint16_t s14 = z(r, 0x14);
  for(bool first = true;; first = false) {
    uint16_t far = tg_read16(0x83ACEC + s14);
    dput(far);
    zs(r, 0x30, far);
    if(first) {
      dput(z(r, 0x20));
      dput(z(r, 0x29) & 0xFF00);
      dput(z(r, 0x2B));
    }
    uint16_t s16 = z(r, 0x16);
    dput(l7e(0x7E3B6E + s16));
    dput(l7e(0x7E3AEE + s16));
    dput(l7e(0x7E3BEC + s16));
    s16 = (uint16_t) (s16 + 2);
    zs(r, 0x16, s16);
    uint16_t s24 = z(r, 0x24);
    l7e_set(0x7E34EC + s24, dget());
    l7e_set(0x7E35EC + s24, dget());
    l7e_set(0x7E36EC + s24, dget());
    l7e_set(0x7E37EC + s24, dget());
    l7e_set(0x7E38EC + s24, z(r, 0x30));
    l7e_set(0x7E39EC + s24, s14);
    s24 = (uint16_t) (s24 + 2);
    zs(r, 0x24, s24);
    dwait();
    uint16_t lines = dget();
    if(lines != 0) {
      uint16_t s22 = z(r, 0x22);
      uint32_t at = (0x7E4CFD + s22 - 0x7E0000) & 0x1FFFF;
      g_ram[at] = (uint8_t) (g_ram[(at + 1) & 0x1FFFF] - (uint8_t) lines);
      // The section's scenery strip: 8 bytes from its entry in bank $90, below $18.
      uint16_t src = l7e(0x7E3CEA + s16);
      if(z(r, 0xE4) & 0x8000) {
        uint16_t dst = z(r, 0x18);
        for(int i = 0; i < 8; i++) g_ram[(uint16_t) (dst - i)] = tg_read8(bank90((uint16_t) (src - i)));
        zs(r, 0x18, (uint16_t) (dst - 8));
      } else {
        // Each of its four words turned by the section's shading ($7E:6C33).
        dput(tg_read16(0x83ACEC + s14));
        uint16_t from = (uint16_t) (src - 7);
        dput(tg_read16(bank90(from)));
        uint16_t dst = (uint16_t) (z(r, 0x18) - 8);
        zs(r, 0x18, dst);
        l7e_set(0x7E0001 + dst, dget());
        for(int k = 2; k <= 6; k += 2) {
          dput(l7e(0x7E6C33 + s14));
          dput(tg_read16(bank90((uint16_t) (from + k))));
          l7e_set(0x7E0001 + dst + k, dget());
        }
      }
      dwait();
      for(uint16_t n = lines; n != 0; n--) {
        uint16_t off = dget();
        uint16_t v = dget();
        tg_bus_write16(0x300798 + off, v);
        v = dget();
        tg_bus_write16(0x300796 + off, v);
      }
      zs(r, 0x22, (uint16_t) (s22 - 1));
    }
    // Banked sections: command $8001, then the bank across the section.
    if(l7e(0x7E3D6A + s16) & 0x8000) {
      uint16_t flags = l7e(0x7E3D6A + s16);
      dput(0x8001);
      dput(l7e(0x7E38EA + s24));
      if(flags & 2) {
        uint16_t b = sext8(l7e(0x7E3AED + s16));
        zs(r, 0x20, b);
        uint16_t d = (uint16_t) (l7e(0x7E3DEE + s16) - l7e(0x7E3DF0 + s16));
        dput((uint16_t) -(uint16_t) (d + l7e(0x7E3DEE + s16)));
        dput((uint16_t) (d - b));
      } else {
        uint16_t b = sext8(l7e(0x7E3AEB + s16));
        zs(r, 0x20, b);
        dput(l7e(0x7E3DEC + s16));
        dput((uint16_t) (l7e(0x7E3DEC + s16) - l7e(0x7E3DEA + s16) - b));
      }
    }
    s14 = (uint16_t) (s14 + 0x80);
    zs(r, 0x14, s14);
    if(s14 >= 0x0E00) break;
  }
  dput(0x8000);
  zs(r, 0x90, z(r, 0x24));
  x = top ? 0x23 : 0x45;
  uint16_t count = 0;
  h = w(0x0242);
  r->v = false;
  if(h & 0x8000) {
    count = (uint16_t) (h + 0xDC);
    r->c = count >= 4;  // CMP #4
    if((uint16_t) (count - 4) & 0x8000) count = 4;
  } else {
    r->c = true;
  }
  l7e_set(0x7E4CFD + x, count);
  ws(0x0164, z(r, 0x22));
  uint16_t yy = (uint16_t) (z(r, 0x18) + 1);
  ws(0x015C, yy);
  ws(0x1E3E, z(r, 0x1A));
  r->a = z(r, 0x1A);
  r->x = x;
  r->y = yy;
  r->db = 0x83;
  r->n = true;  // PLB of $83
  r->z = false;
}
