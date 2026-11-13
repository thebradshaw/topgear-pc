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

// The views a road is built for: one player's, and each of the two players' half screens
// (the same code at $03:D1C1/DA0A and $03:CD70/D612, with the second player's tables
// further on in RAM and a shorter view).
typedef struct View {
  uint16_t which;                 // the view-selected flag ($B4, second player $B6)
  uint16_t road_top, road_bottom; // $1A for BuildRoad
  uint16_t car_top, car_bottom;   // $1A for BuildRoadCar
  uint16_t dc, ff5b;              // the view's bottom line and its pair
  uint16_t clamp_cmp, clamp_set;  // the lines kept below the camera
  uint16_t rec;                   // the player's record ($0200 or $02B3)
  uint16_t trk, sec, line, out, strip, cam, s2, s4;  // table shifts
  uint32_t shade;
  uint8_t mode_dp, end_dp;
} View;

static void build_road(TgRegs* r, const View* v) {
  uint16_t y = 0x23, x = (uint16_t) (0x292B + v->strip);
  zs(r, 0x1A, v->road_top);
  zs(r, 0x1C, 0x0004);
  bool top = w(v->which) != 0;  // which view (1 player: the only one)
  if(!top) {
    y = 0x45;
    x = (uint16_t) (0x2A2B + v->strip);
    zs(r, 0x1A, v->road_bottom);
    zs(r, 0x1C, 0x01A4);
  }
  zs(r, 0x18, x);
  zs(r, 0x14, (uint16_t) (0x80 - ((w(0x0225 + v->rec) >> 1) & 0x7E)));
  zs(r, 0x16, (uint16_t) ((w(0x0226 + v->rec) << 1) & 0x3E));
  x = z(r, 0x16);
  l7e_set(0x7E376C + v->sec, l7e(0x7E3E6C + v->trk + x));
  l7e_set(0x7E356C + v->sec, l7e(0x7E3DEC + v->trk + x));
  // The camera's height over the road ahead: command 0 (multiply).
  dput(0);
  dput((uint16_t) ((l7e(0x7E3E6E + v->trk + x) - l7e(0x7E3E6C + v->trk + x)) << 1));
  dput((uint16_t) ((w(0x0224 + v->rec) >> 1) & 0xFF00));
  dget();
  ws(0x1EF2 + v->cam, (uint16_t) (w(0x0242 + v->rec) + dget() + l7e(0x7E3E6C + v->trk + x)));
  dput(0);
  dput(l7e(0x7E3AEC + v->trk + x));
  dput(0xFC00);
  uint16_t lo = dget();
  ws(0x017C + v->s4, (uint16_t) (dget() + (lo >= 0x8000)));  // rounded
  // The camera's side position and the road's turn, from the section's two ends.
  dput(0);
  dput(l7e(0x7E3B6E + v->trk + x));
  dput(swap((uint16_t) ((0x80 - z(r, 0x14)) >> 1)));
  uint16_t a = (uint16_t) -product_x4(r, 0x1E);
  zs(r, 0x1E, (uint16_t) (w(0x1EDA + v->s2) + a));
  dput(0);
  dput(l7e(0x7E3AEE + v->trk + x));
  dput(swap((uint16_t) ((0x80 - z(r, 0x14)) >> 1)));
  zs(r, 0x26, (uint16_t) -product_x4(r, 0x26));
  dput(0);
  dput(z(r, 0x26));
  dput(swap((uint16_t) (z(r, 0x14) >> 1)));
  zs(r, 0x2A, product_x4(r, 0x2A));
  // The road's start: command 7 (or $10 with the scenery mode in $E4).
  x = y;
  l7e_set(0x7E4D49 + v->line + x, 0);
  uint16_t mode = z(r, v->mode_dp);
  if(mode & 0x8000) {
    dput(0x0007);
  } else {
    dput(0x0010);
    dput(mode);
  }
  dput(0);
  uint16_t h = w(0x1EF2 + v->cam);
  bool below = (h & 0x8000) != 0;  // the camera below the road's level here
  dput((uint16_t) (h + 0x33));
  l7e_set(0x7E4D49 + v->line + x, below ? (uint16_t) (h + v->dc) : v->dc);
  x = (uint16_t) (x - 1);
  dput(below ? (uint16_t) (h + 0x33) : 0x0033);
  dput(v->ff5b);
  dput(0x00BF);
  dput(below ? (uint16_t) (h + v->dc) : v->dc);
  dput(w(0x1EF4 + v->cam));
  dput(w(0x1EF6 + v->cam));
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
    dput(l7e(0x7E37EC + v->sec + s24));
    dput(l7e(0x7E3E6E + v->trk + s16));
    dput(l7e(0x7E35EC + v->sec + s24));
    dput(l7e(0x7E3DEE + v->trk + s16));
    dput(l7e(0x7E3BEC + v->trk + s16));
    s16 = (uint16_t) (s16 + 2);
    zs(r, 0x16, s16);
    l7e_set(0x7E376C + v->sec + s24, (uint16_t) (l7e(0x7E3E6C + v->trk + s16) + l7e(0x7E36EC + v->sec + s24)));
    l7e_set(0x7E356C + v->sec + s24, (uint16_t) (l7e(0x7E3DEC + v->trk + s16) + l7e(0x7E34EC + v->sec + s24)));
    l7e_set(0x7E366C + v->sec + s24, dget());
    l7e_set(0x7E386C + v->sec + s24, dget());
    l7e_set(0x7E396C + v->sec + s24, far);
    l7e_set(0x7E3A6C + v->sec + s24, s14);
    zs(r, 0x24, (uint16_t) (s24 + 2));
    dwait();
    uint16_t lines = dget();
    if(lines != 0) {
      // The lines this section takes, off the count left.
      uint16_t s22 = z(r, 0x22);
      uint32_t at = (0x7E4D49 + v->line + s22 - 0x7E0000) & 0x1FFFF;
      g_ram[at] = (uint8_t) (g_ram[(at + 1) & 0x1FFFF] - (uint8_t) lines);
      // The section's scenery strip: 8 bytes from its entry, below $18.
      uint16_t src = l7e(0x7E3FEC + v->trk + z(r, 0x16));
      uint16_t dst = z(r, 0x18);
      if(z(r, v->mode_dp) & 0x8000) {
        uint32_t bank = (src & 0x8000) ? 0x900000 : 0x7E0000;
        for(int i = 0; i < 8; i++)
          g_ram[(uint16_t) (dst - i)] = tg_read8(bank | (uint16_t) (src - i));
        zs(r, 0x18, (uint16_t) (dst - 8));
      } else {
        // Each of its four words turned by the section's shading ($7E:6C33).
        dput(tg_read16(0x83ACEC + s14));
        uint16_t from = (uint16_t) (l7e(0x7E3FEC + v->trk + z(r, 0x16)) - 7);
        uint32_t bank = (from & 0x8000) ? 0x900000 : 0x7E0000;
        dput(tg_read16(bank | from));
        dst = (uint16_t) (z(r, 0x18) - 8);
        zs(r, 0x18, dst);
        l7e_set(0x7E0001 + dst, dget());
        for(int k = 2; k <= 6; k += 2) {
          dput(l7e(0x7E6C33 + v->shade + s14));
          dput(tg_read16(bank | (uint16_t) (from + k)));
          l7e_set(0x7E0001 + dst + k, dget());
        }
      }
      // Then the lines: offset, then the two words for it.
      dwait();
      for(uint16_t n = lines; n != 0; n--) {
        uint16_t off = dget();
        uint16_t val = dget();
        tg_bus_write16(0x300E58 + v->out + off, val);
        val = dget();
        tg_bus_write16(0x300E56 + v->out + off, val);
      }
      zs(r, 0x22, (uint16_t) (s22 - 1));
    }
    s14 = (uint16_t) (s14 + 0x80);
    zs(r, 0x14, s14);
    if(s14 >= 0x0E00) break;
  }
  dput(0x8000);
  zs(r, v->end_dp, z(r, 0x24));
  x = top ? 0x23 : 0x45;
  uint16_t count = 0;
  h = w(0x1EF2 + v->cam);
  if(h & 0x8000) {
    count = (uint16_t) (h + v->dc);
    r->v = false;
    r->c = count >= v->clamp_cmp;  // CMP
    if((uint16_t) (count - v->clamp_cmp) & 0x8000) count = v->clamp_set;
  } else {
    r->c = true;
    r->v = false;
  }
  l7e_set(0x7E4D49 + v->line + x, count);
  ws(0x0168 + v->s2, z(r, 0x22));
  uint16_t yy = (uint16_t) (z(r, 0x18) + 1);
  ws(0x0160 + v->s2, yy);
  ws(0x1E40 + v->s4, z(r, 0x1A));
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

static void build_road_car(TgRegs* r, const View* v) {
  bool top = w(v->which) != 0;
  uint16_t y = top ? 0x23 : 0x45;
  zs(r, 0x1A, top ? v->car_top : v->car_bottom);
  zs(r, 0x1C, top ? 0x0004 : 0x01A4);
  zs(r, 0x18, (uint16_t) ((top ? 0x272B : 0x282B) + v->strip));
  zs(r, 0x14, (uint16_t) (0x80 - ((w(0x0225 + v->rec) >> 1) & 0x7E)));
  zs(r, 0x16, (uint16_t) ((w(0x0226 + v->rec) << 1) & 0x3E));
  uint16_t x = z(r, 0x16);
  dput(0);
  dput(l7e(0x7E3AEC + v->trk + x));
  dput(0xFC00);
  uint16_t lo = dget();
  ws(0x0174 + v->s4, (uint16_t) (dget() + (lo >= 0x8000)));  // rounded
  // The car's side position against the road's turn, from the section's two ends.
  dput(0);
  dput(l7e(0x7E3B6E + v->trk + x));
  dput(swap((uint16_t) ((0x80 - z(r, 0x14)) >> 1)));
  zs(r, 0x1E, (uint16_t) (w(0x0208 + v->rec) - product_x4(r, 0x1E)));
  dput(0);
  dput(w(0x0208 + v->rec));
  dput(swap((uint16_t) (z(r, 0x14) >> 1)));
  zs(r, 0x20, product_x4(r, 0x20));
  // The car's place along the track (24 bits at $020B) less the turn so far: $26-$28.
  dput(0);
  dput(l7e(0x7E3AEE + v->trk + x));
  dput(swap((uint16_t) ((0x80 - z(r, 0x14)) >> 1)));
  uint32_t p = dget();
  p |= (uint32_t) dget() << 16;
  r->c = (p >> 30) & 1;  // the last bit ROL shifted out
  uint16_t turn = (uint16_t) -(uint16_t) (p >> 14);
  zs(r, 0x26, turn);
  uint16_t hi = (turn & 0x8000) ? 0xFFFF : 0;
  zs(r, 0x26, adc16(r, w(0x020B + v->rec), turn));
  zs(r, 0x28, adc16(r, sext8(w(0x020D + v->rec)), hi));
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
  l7e_set(0x7E4CFD + v->line + x, 0);
  uint16_t mode = z(r, v->mode_dp);
  if(mode & 0x8000) {
    dput(0x0001);
  } else {
    dput(0x000F);
    dput(mode);
  }
  dput(0);
  uint16_t h = w(0x0242 + v->rec);
  bool below = (h & 0x8000) != 0;  // the car below the road's level here
  dput((uint16_t) (h + 0x33));
  l7e_set(0x7E4CFD + v->line + x, below ? (uint16_t) (h + v->dc) : v->dc);
  x = (uint16_t) (x - 1);
  dput(below ? (uint16_t) (h + 0x33) : 0x0033);
  dput(v->ff5b);
  dput(0x00BF);
  dput(below ? (uint16_t) (h + v->dc) : v->dc);
  dput(w(0x0200 + v->rec));
  a = w(0x0202 + v->rec);  // one step towards 0
  if(a != 0) a = (uint16_t) (((uint16_t) (a - 1) & 0x8000) ? a + 1 : a - 1);
  dput(a);
  dput(0x0180);
  dput(below ? (uint16_t) ((h << 2) + z(r, 0x1A)) : z(r, 0x1A));
  dput(0x00C0);
  zs(r, 0x22, x);
  dput(z(r, 0x1D) & 0xFF00);
  dput(sext8(z(r, 0x1F)));
  dput((uint16_t) ((z(r, 0x25) & 0xFF00) | (w(0x020A + v->rec) & 0xFF)));
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
    dput(l7e(0x7E3B6E + v->trk + s16));
    dput(l7e(0x7E3AEE + v->trk + s16));
    dput(l7e(0x7E3BEC + v->trk + s16));
    s16 = (uint16_t) (s16 + 2);
    zs(r, 0x16, s16);
    uint16_t s24 = z(r, 0x24);
    l7e_set(0x7E34EC + v->sec + s24, dget());
    l7e_set(0x7E35EC + v->sec + s24, dget());
    l7e_set(0x7E36EC + v->sec + s24, dget());
    l7e_set(0x7E37EC + v->sec + s24, dget());
    l7e_set(0x7E38EC + v->sec + s24, z(r, 0x30));
    l7e_set(0x7E39EC + v->sec + s24, s14);
    s24 = (uint16_t) (s24 + 2);
    zs(r, 0x24, s24);
    dwait();
    uint16_t lines = dget();
    if(lines != 0) {
      uint16_t s22 = z(r, 0x22);
      uint32_t at = (0x7E4CFD + v->line + s22 - 0x7E0000) & 0x1FFFF;
      g_ram[at] = (uint8_t) (g_ram[(at + 1) & 0x1FFFF] - (uint8_t) lines);
      // The section's scenery strip: 8 bytes from its entry in bank $90, below $18.
      uint16_t src = l7e(0x7E3CEA + v->trk + s16);
      if(z(r, v->mode_dp) & 0x8000) {
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
          dput(l7e(0x7E6C33 + v->shade + s14));
          dput(tg_read16(bank90((uint16_t) (from + k))));
          l7e_set(0x7E0001 + dst + k, dget());
        }
      }
      dwait();
      for(uint16_t n = lines; n != 0; n--) {
        uint16_t off = dget();
        uint16_t val = dget();
        tg_bus_write16(0x300798 + v->out + off, val);
        val = dget();
        tg_bus_write16(0x300796 + v->out + off, val);
      }
      zs(r, 0x22, (uint16_t) (s22 - 1));
    }
    // Banked sections: command $8001, then the bank across the section.
    if(l7e(0x7E3D6A + v->trk + s16) & 0x8000) {
      uint16_t flags = l7e(0x7E3D6A + v->trk + s16);
      dput(0x8001);
      dput(l7e(0x7E38EA + v->sec + s24));
      if(flags & 2) {
        uint16_t b = sext8(l7e(0x7E3AED + v->trk + s16));
        zs(r, 0x20, b);
        uint16_t d = (uint16_t) (l7e(0x7E3DEE + v->trk + s16) - l7e(0x7E3DF0 + v->trk + s16));
        dput((uint16_t) -(uint16_t) (d + l7e(0x7E3DEE + v->trk + s16)));
        dput((uint16_t) (d - b));
      } else {
        uint16_t b = sext8(l7e(0x7E3AEB + v->trk + s16));
        zs(r, 0x20, b);
        dput(l7e(0x7E3DEC + v->trk + s16));
        dput((uint16_t) (l7e(0x7E3DEC + v->trk + s16) - l7e(0x7E3DEA + v->trk + s16) - b));
      }
    }
    s14 = (uint16_t) (s14 + 0x80);
    zs(r, 0x14, s14);
    if(s14 >= 0x0E00) break;
  }
  dput(0x8000);
  zs(r, v->end_dp, z(r, 0x24));
  x = top ? 0x23 : 0x45;
  uint16_t count = 0;
  h = w(0x0242 + v->rec);
  r->v = false;
  if(h & 0x8000) {
    count = (uint16_t) (h + v->dc);
    r->c = count >= v->clamp_cmp;  // CMP
    if((uint16_t) (count - v->clamp_cmp) & 0x8000) count = v->clamp_set;
  } else {
    r->c = true;
  }
  l7e_set(0x7E4CFD + v->line + x, count);
  ws(0x0164 + v->s2, z(r, 0x22));
  uint16_t yy = (uint16_t) (z(r, 0x18) + 1);
  ws(0x015C + v->s2, yy);
  ws(0x1E3E + v->s4, z(r, 0x1A));
  r->a = z(r, 0x1A);
  r->x = x;
  r->y = yy;
  r->db = 0x83;
  r->n = true;  // PLB of $83
  r->z = false;
}

static const View kOne = { 0x00B4, 0x035C, 0x050C, 0x06BC, 0x0D7C, 0x00DC, 0xFF5B, 4, 4,
                           0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xE4, 0 };
static const View kTop = { 0x00B4, 0x019C, 0x033C, 0x019C, 0x033C, 0x006C, 0xFFCB, 4, 4,
                           0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xE4, 0 };
// The second player's: still the full view's $DC, but the shorter view's $FFCB.
static const View kBottom = { 0x00B6, 0x019C, 0x033C, 0x019C, 0x033C, 0x00DC, 0xFFCB, 0, 0x74,
                              0xB3, 0x580, 0x40, 0x98, 0x360, 0x400, 0x0A, 2, 4, 0x1040, 0xE6, 0 };

// Widescreen: this frame is a race frame, which the PPU redraws wider.
// The sprite lists' entries start as HUD pieces; the DSP's object pieces get a ground line
// (dsp_objects.c).
extern int tg3_obj_order;
extern uint8_t tg3_race_flags;
static void mark_race_frame(bool two) {
  (void) two;  // 2 players side by side: not yet (the PPU's layout for it is Top Gear 1's)
  bool frame = tg_port_wide_margin || tg_port_interp;
  if(!frame && !tg_port_ghost) return;
  if(frame) {
    tg3_race_flags = 1;  // PPU_RACE_FRAME
    tg_port_race_frame = (uint8_t) (tg3_race_flags | 32);  // and PPU_NEW_PICTURE
  }
  tg3_obj_order = 0x7000;
  for(uint16_t a = 0x1576; a < 0x1576 + 0x640; a++) {
    tg_port_obj_grounds[a] = -32768;  // PPU_NO_GROUND
    tg_port_obj_idents[a] = 0;
    tg_port_obj_flags[a] = 0;
  }
}

void Tg3BuildRoad(TgRegs* r) { mark_race_frame(false); View v = kOne; v.end_dp = 0x94; build_road(r, &v); }
void Tg3BuildRoadTop(TgRegs* r) { mark_race_frame(true); View v = kTop; v.end_dp = 0x94; build_road(r, &v); }
void Tg3BuildRoadBottom(TgRegs* r) { View v = kBottom; v.end_dp = 0x96; v.clamp_cmp = 4; build_road(r, &v); }
void Tg3BuildRoadCar(TgRegs* r) { View v = kOne; v.end_dp = 0x90; build_road_car(r, &v); }
void Tg3BuildRoadCarTop(TgRegs* r) { View v = kTop; v.end_dp = 0x90; build_road_car(r, &v); }
void Tg3BuildRoadCarBottom(TgRegs* r) { View v = kBottom; v.end_dp = 0x92; v.clamp_cmp = 0x74; build_road_car(r, &v); }
