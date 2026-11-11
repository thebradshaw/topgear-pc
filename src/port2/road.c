// Top Gear 2: the road builder (bank $08). Projects the track ahead of the camera into
// per-scanline tables for the road layer's HDMA, one 8-byte row per road segment in
// $7E:BE80, and the sky/horizon HDMA lists. One player's screen is $08:CB74; two players'
// is $08:D63F, which runs the same builder for player 2's half (on player 2's camera,
// track rows and segment list) and then player 1's, each $68 lines high.

#include <stddef.h>

#include "../port/port.h"

// The routine sets its direct page to $0200.
#define DPB 0x0200u

// What changes between the one-player screen and the two halves of the split screen.
typedef struct Half {
  bool p2;            // player 2's variables (the maps below)
  uint16_t lines;     // $96's start
  uint16_t bottom;    // the last line
  uint16_t far;       // how far ahead the road is drawn ($9A)
  uint16_t flat, lift;  // the horizon line when the camera is level, and its offset
  uint16_t sky0, sky1, sky2;  // the background scroll's constants (sky_scroll)
  uint16_t top;       // where the line table's start goes
} Half;
static const Half kHalf1P = { false, 0x0060, 0x00E0, 0x1000, 0x003C, 0x0044, 0x035F, 0x039B, 0x0090, 0x015E };
static const Half kHalfP1 = { false, 0x03EC, 0x0068, 0x0800, 0x003B, 0x0045, 0x03D3, 0x040E, 0x0104, 0x015E };
static const Half kHalfP2 = { true, 0x005C, 0x0068, 0x0800, 0x003B, 0x0045, 0x0363, 0x039E, 0x0094, 0x0160 };
static const Half* g_h = &kHalf1P;

// Player 2's direct page words for player 1's.
static const uint8_t kDp2[][2] = {
  { 0x04, 0x40 }, { 0x06, 0x42 }, { 0x08, 0x44 }, { 0x09, 0x45 }, { 0x0A, 0x46 }, { 0x0D, 0x49 },
  { 0x0E, 0x4A }, { 0x10, 0x4C }, { 0x16, 0x52 }, { 0x17, 0x53 }, { 0x26, 0x62 }, { 0x28, 0x64 },
  { 0x2A, 0x66 }, { 0x2C, 0x68 }, { 0x2D, 0x69 }, { 0x2E, 0x6A }, { 0x3C, 0x78 }, { 0x3E, 0x7A },
  { 0x7C, 0x7E }, { 0x80, 0x82 }, { 0x81, 0x83 },
};
static uint8_t g_dp[256];
static void set_half(const Half* h) {
  g_h = h;
  for(int i = 0; i < 256; i++) g_dp[i] = (uint8_t) i;
  if(h->p2)
    for(size_t i = 0; i < sizeof kDp2 / sizeof kDp2[0]; i++) g_dp[kDp2[i][0]] = kDp2[i][1];
}

static uint16_t z(uint8_t o) { return ram16(DPB + g_dp[o]); }
static void zs(uint8_t o, uint16_t v) { ram16_set(DPB + g_dp[o], v); }
static uint8_t zb(uint8_t o) { return g_ram[DPB + g_dp[o]]; }
static void zbs(uint8_t o, uint8_t v) { g_ram[DPB + g_dp[o]] = v; }

// Bank $7E words (the data bank for most of the routine). Player 2's track rows are at
// $9400 and its segment list at $BFA0.
static uint16_t ad(uint16_t a) {
  if(!g_h->p2) return a;
  if(a >= 0x9000 && a < 0x9400) return (uint16_t) (a + 0x400);
  if(a >= 0xBE80 && a < 0xBFA0) return (uint16_t) (a + 0x120);
  return a;
}
static uint16_t w(uint16_t adr) { return ram16(ad(adr)); }
static void ws(uint16_t adr, uint16_t v) { ram16_set(ad(adr), v); }

static uint8_t rd8(uint16_t adr) { return tg_bus_read8(adr); }
static uint16_t rd16(uint16_t adr) { return (uint16_t) (rd8(adr) | rd8((uint16_t) (adr + 1)) << 8); }
static void m7a(uint16_t a) {
  tg_bus_write8(0x211B, (uint8_t) a);
  tg_bus_write8(0x211B, (uint8_t) (a >> 8));
}
static void m7b(uint8_t b) { tg_bus_write8(0x211C, b); }

static uint16_t asl(TgRegs* r, uint16_t v) {
  r->c = v >> 15;
  return (uint16_t) (v << 1);
}
static uint16_t rol(TgRegs* r, uint16_t v) {
  bool c = r->c;
  r->c = v >> 15;
  return (uint16_t) (v << 1 | c);
}
static uint16_t ror(TgRegs* r, uint16_t v) {
  bool c = r->c;
  r->c = v & 1;
  return (uint16_t) (v >> 1 | (uint16_t) c << 15);
}
// ASL the dp word o, ROL the bit into hi: twice.
static uint16_t shift2(TgRegs* r, uint8_t o, uint16_t hi) {
  for(int i = 0; i < 2; i++) {
    zs(o, asl(r, z(o)));
    hi = rol(r, hi);
  }
  return hi;
}
// The sign-extended high byte of a word (AND #$FF00, ORA #$00FF if negative, XBA).
static uint16_t sext_hi(uint16_t v) { return (uint16_t) (int16_t) (int8_t) (v >> 8); }

// a x the 16-bit fraction $98:$99, in two 16 x 8 multiplies. The low word goes to dp lo,
// the scratch byte pair t gets the second product's high byte (sign-flipped); returns the
// high word with the carry of the low word added.
static uint16_t scale(TgRegs* r, uint16_t a, uint8_t t, uint8_t lo) {
  m7a(a);
  m7b(zb(0x99));
  uint16_t x = rd16(0x2133);
  uint16_t y = rd16(0x2135);
  m7b(zb(0x98));
  zbs(t, rd8(0x2136) ^ 0x80);
  zbs((uint8_t) (t + 1), 0);
  r->c = false;
  uint16_t l = adc16(r, x & 0xFF00, rd16(0x2134));
  uint16_t h = adc16(r, y, z(t));
  zs(lo, l);
  r->x = lo == t ? l : x;
  r->y = y;
  return h;
}

// A segment's screen x: $9D scaled into dp lo (rounded high word, minus $80).
static uint16_t centre_x(TgRegs* r, uint8_t t, uint8_t lo) {
  uint16_t h = shift2(r, lo, scale(r, z(0x9D), t, lo));
  r->c = true;
  return sbc16(r, h, 0x0080);
}

// A width/height a scaled into t, plus $B2 - $200.
static uint16_t scaled_edge(TgRegs* r, uint16_t a, uint8_t t) {
  uint16_t h = shift2(r, t, scale(r, a, t, t));
  r->c = false;
  h = adc16(r, h, z(0xB2));
  r->c = true;
  return sbc16(r, h, 0x0200);
}

// The 32-bit hi:lo times f (8-bit), shifted, added into the dp pair dlo/dhi. Leaves the
// product in $A8-$AB.
static void mul_add(TgRegs* r, uint16_t hi, uint16_t lo, uint8_t f, uint8_t dlo, uint8_t dhi) {
  m7a(hi);
  m7b(f);
  zs(0xAA, rd16(0x2134));
  m7a(lo);
  m7b(f);
  uint16_t t = sext_hi(rd16(0x2135));
  r->c = false;
  zs(0xAA, adc16(r, t, z(0xAA)));
  uint16_t a = rd16(0x2134);
  a = asl(r, a);
  zs(0xAA, rol(r, z(0xAA)));
  a = asl(r, a);
  zs(0xAA, rol(r, z(0xAA)));
  zs(0xA8, a);
  asl(r, z(0xA7));
  zs(dlo, adc16(r, z(0xA9), z(dlo)));
  zs(dhi, adc16(r, sext_hi(z(0xAA)), z(dhi)));
}

// $BA:$B8 rounded to its high word.
static uint16_t round_ba(void) { return (uint16_t) (z(0xBA) + ((z(0xB8) & 0x8000) ? 1 : 0)); }

// Signed load: dp lo = v, dp hi = its sign.
static void sext_store(uint8_t lo, uint8_t hi, uint16_t v) {
  zs(lo, v);
  zs(hi, (v & 0x8000) ? 0xFFFF : 0);
}

// The slope step from the track table: $C0:$C2 = word - $C4 (sign-extended), $C4 = word.
static void slope_step(TgRegs* r, uint16_t word) {
  r->c = true;
  uint16_t d = sbc16(r, word, z(0xC4));
  zs(0xC4, word);
  sext_store(0xC0, 0xC2, d);
}

// The section's surface pointer: ($8A & $0C) x 2 + $C8 (+ 4 for the far half).
static uint16_t surface(TgRegs* r, bool far) {
  r->c = false;
  uint16_t a = (uint16_t) ((z(0x8A) & 0x000C) << 1);
  if(far) a = adc16(r, a, 0x0004);
  return adc16(r, a, z(0xC8));
}

// The divide of $D4/$D8 steps by the segment's line count y.
static void divide_steps(TgRegs* r, uint16_t y) {
  uint16_t a = z(0xD7);
  if(a != 0) {
    tg_bus_write16(0x4204, a);
    tg_bus_write8(0x4206, (uint8_t) y);
    zs(0xD7, rd16(0x4214));
  }
  a = z(0xD4);
  if(a == 0) return;
  if(!(a & 0x8000)) {
    tg_bus_write16(0x4204, (uint16_t) (a << 8 | a >> 8));
    tg_bus_write8(0x4206, (uint8_t) y);
    zs(0xD3, rd16(0x4214));
  } else {
    a = (uint16_t) -a;
    tg_bus_write16(0x4204, (uint16_t) (a << 8 | a >> 8));
    tg_bus_write8(0x4206, (uint8_t) y);
    a = (uint16_t) -rd16(0x4214);
    zs(0xD3, a);
    if(!(a & 0x8000)) zbs(0xD5, 0);
  }
}

// One line of the edges: $CE:$D0 -= $D6:$D8, $CA:$CC += $D2:$D4. Returns the new $D0.
static uint16_t edge_step(TgRegs* r) {
  r->c = true;
  zs(0xCE, sbc16(r, z(0xCE), z(0xD6)));
  uint16_t d0 = sbc16(r, z(0xD0), z(0xD8));
  zs(0xD0, d0);
  return d0;
}
static uint16_t edge_step2(TgRegs* r) {
  r->c = false;
  zs(0xCA, adc16(r, z(0xCA), z(0xD2)));
  uint16_t cc = adc16(r, z(0xCC), z(0xD4));
  zs(0xCC, cc);
  return cc;
}

// An HDMA list entry going down (x -= 3, or 6 for a run over 127 lines): line count n,
// then the word v (v2 for the second half of a long run).
static uint16_t hdma_entry(uint16_t p, uint16_t n, uint16_t v, uint16_t v2) {
  if(n < 0x80) {
    p = (uint16_t) (p - 3);
    ws(p, n);
    ws((uint16_t) (p + 1), v);
  } else {
    p = (uint16_t) (p - 6);
    ws((uint16_t) (p + 3), (uint16_t) ((n + 1) & 0x7F));
    ws(p, 0x7F);
    ws((uint16_t) (p + 1), v);
    ws((uint16_t) (p + 4), v2);
  }
  return p;
}
// The same in repeat mode (count | $80; long runs as $FF then the rest).
static uint16_t hdma_repeat(uint16_t p, uint16_t n, uint16_t v) {
  if(n < 0x80) {
    p = (uint16_t) (p - 3);
    ws(p, n | 0x80);
    ws((uint16_t) (p + 1), v);
  } else {
    p = (uint16_t) (p - 6);
    ws((uint16_t) (p + 3), (uint16_t) (n + 1));
    ws(p, 0x00FF);
    ws((uint16_t) (p + 1), v);
    ws((uint16_t) (p + 4), (uint16_t) (v + 0x01FC));
  }
  return p;
}

// $3C/$3E: the background scroll for the horizon line $80.
static void sky_scroll(TgRegs* r) {
  uint16_t a = (uint16_t) (z(0x80) + g_h->sky0);
  zs(0x3C, a);
  m7a((uint16_t) (a - g_h->sky1));
  m7b(0x4D);
  r->c = (rd8(0x2134) & 0x80) != 0;
  uint16_t x = rol(r, rd16(0x2135));
  asl(r, asl(r, rd16(0x2133)));
  zs(0x3E, adc16(r, x, g_h->sky2));
}

// An entry in both the HDMA list at *px (going down) and the indirect list at y: the
// count n, then the word v. Returns y past the entry.
static uint16_t split_entry(uint16_t* px, uint16_t y, uint16_t n, uint16_t v, bool repeat) {
  uint16_t x = *px;
  if(n < 0x80) {
    x = (uint16_t) (x - 3);
    uint16_t c = repeat ? (uint16_t) (n | 0x80) : n;
    ws(x, c);
    ws((uint16_t) (y + 4), c);
    ws((uint16_t) (x + 1), v);
    ws((uint16_t) (y + 5), v);
    ws(y, 1);
    ws((uint16_t) (y + 2), x);
    y = (uint16_t) (y + 5);
  } else {
    x = (uint16_t) (x - 6);
    uint16_t c = repeat ? (uint16_t) (n + 1) : (uint16_t) ((n + 1) & 0x7F);
    ws((uint16_t) (x + 3), c);
    ws((uint16_t) (y + 7), c);
    uint16_t f = repeat ? 0x00FF : 0x007F;
    ws(x, f);
    ws((uint16_t) (y + 4), f);
    ws((uint16_t) (x + 1), v);
    ws((uint16_t) (y + 5), v);
    uint16_t v2 = repeat ? (uint16_t) (v + 0x01FC) : v;
    ws((uint16_t) (x + 4), v2);
    ws((uint16_t) (y + 8), v2);
    ws(y, 2);
    ws((uint16_t) (y + 2), x);
    y = (uint16_t) (y + 8);
  }
  *px = x;
  return y;
}

// $08:D2F1: the road ended above the bottom of the screen. Splits the lines below it
// into ground bands ($E0, $DE, $DC, $E6, $E4) and writes their HDMA lists.
static void sky_lists(TgRegs* r) {
  sky_scroll(r);
  uint16_t h = z(0x80), top = z(0x92);
  uint16_t hill = (uint16_t) ((z(0x86) & 0x3F) + 0x20 + h);
  if((int16_t) (h - 0x00E0) >= 0) {
    zs(0xE0, (uint16_t) (0x00E0 - top));
    zs(0xDE, 0);
    zs(0xDC, 0);
    zs(0xE6, 0);
    zs(0xE4, 0);
    zs(0xDA, 0);
  } else if((int16_t) (h - 0x00A0) >= 0) {
    uint16_t a = (uint16_t) (h - top);
    if(!(a & 0x8000)) {
      zs(0xE0, a);
      zs(0xDE, (uint16_t) (0x00E0 - h));
      zs(0xDA, (uint16_t) (0x00E0 - h));
      zs(0xDC, 0);
      zs(0xE6, (uint16_t) (0x00E0 - h));
    } else {
      zs(0xE0, 0);
      zs(0xDE, (uint16_t) (0x00E0 - top));
      zs(0xDC, 0);
      zs(0xE6, (uint16_t) (0x00E0 - top));
    }
    zs(0xE4, 0);
    zs(0xE2, hill);
  } else {
    uint16_t a = (uint16_t) (h - top);
    if(a & 0x8000) {
      zs(0xE0, 0);
      a = (uint16_t) (a + 0x40);
      if(a & 0x8000) {
        zs(0xDE, 0);
        uint16_t dc = (uint16_t) (0x00E0 - top);
        zs(0xDC, dc);
        a = (uint16_t) (((z(0x86) & 0x3F) | 0x0100) + h);
        if((int16_t) (a - 0x00E0) >= 0) {
          zs(0xE2, (uint16_t) (a - 0x00E0));
          zs(0xE6, dc);
          zs(0xE4, 0);
        } else {
          a = (uint16_t) (a - top);
          if(a & 0x8000) {
            zs(0xE4, dc);
            zs(0xE6, 0);
          } else {
            zs(0xE6, a);
            zs(0xE4, (uint16_t) (0x00E0 - a - top));
            zs(0xE2, 0);
          }
        }
      } else {
        zs(0xDE, a);
        zs(0xDC, (uint16_t) (0x00A0 - h));
        if(hill & 0x8000) {
          zs(0xE2, 0);
          zs(0xE4, (uint16_t) -hill);
          zs(0xE6, (uint16_t) (z(0xDC) + z(0xDE) - z(0xE4)));
        } else {
          zs(0xE2, hill);
          zs(0xE4, 0);
          zs(0xE6, (uint16_t) (z(0xDC) + z(0xDE)));
        }
      }
    } else {
      zs(0xE0, a);
      zs(0xDE, 0x0040);
      zs(0xDC, (uint16_t) (0x00A0 - h));
      uint16_t e6 = (uint16_t) (z(0xDC) + 0x40);
      zs(0xE6, e6);
      zs(0xE4, 0);
      zs(0xDA, e6);
      zs(0xE2, (uint16_t) (((z(0x86) & 0x3F) | 0x0100) - e6));
    }
  }

  uint16_t x = z(0x8C), y = z(0x8E), a;
  if((a = z(0x92)) != 0) y = hdma_repeat(y, a, z(0x90));
  if((a = z(0xE0)) != 0) {
    x = hdma_entry(x, a, z(0x88), z(0x88));
    y = hdma_repeat(y, a, (uint16_t) ((z(0xDA) << 2) + 0x9C40));
  }
  if((a = z(0xDE)) != 0) y = hdma_entry(y, a, 0x0238, 0x0238);
  if((a = z(0xDC)) != 0) y = hdma_repeat(y, a, 0x9C40);
  zs(0x8E, y);
  y = z(0x10);
  ws(y, 0);
  if((a = z(0xE6)) != 0) y = split_entry(&x, y, a, (uint16_t) ((z(0xE2) << 2) + 0x8600), true);
  y = (uint16_t) (y + 2);
  ws(y, 0);
  if((a = z(0xE4)) != 0) split_entry(&x, y, a, 0x8600, false);
  zs(0x8C, x);
  zs(0x80, z(0x92));
  zs(0x7C, (uint16_t) (z(0x7C) >> 3));
  zs(0x00, z(0xAC));
  r->a = z(0xAC);
  r->x = x;
  r->y = y;
}

// One screen's (or half's) road, from the camera down to the bottom line or until the track
// runs out. Returns true when it reached the bottom.
static bool build(TgRegs* r) {
  zs(0x9A, 0);
  zs(0x7C, 0);
  zs(0x92, 0);
  zs(0x96, g_h->lines);
  zs(0x98, rom16(0x8F8B40));
  uint16_t a = rom16(0x8FABC0);
  zs(0xCE, 0);
  zs(0xD0, a);
  zs(0xB2, rom16(0x8FBC00));
  ram16_set(g_h->top, z(0x90));
  zs(0x9C, z(0x04));
  zs(0x9E, z(0x06));
  r->c = true;
  a = sbc16(r, 0, z(0x08));
  zs(0xA0, a);
  zs(0xA4, a);
  a = sbc16(r, 0, z(0x0A));
  zs(0xA2, a);
  zs(0xA6, a);
  r->c = false;
  zs(0x8A, adc16(r, (uint16_t) ((z(0x0E) & 3) << 2), z(0x26)));

  // The first segment, at the camera.
  zs(0xCC, centre_x(r, 0xCC, 0xCA));
  if(z(0xA1) == 0) zs(0x94, z(0xB2));
  else zs(0x94, scaled_edge(r, z(0xA1), 0x94));
  uint16_t x = z(0x7C);
  ws((uint16_t) (0xBE80 + x), z(0x9D));
  ws((uint16_t) (0xBE82 + x), z(0xA1));
  ws((uint16_t) (0xBE84 + x), z(0xCC));
  ws((uint16_t) (0xBE86 + x), z(0x94));
  zs(0x7C, (uint16_t) (x + 8));

  // The camera's position within the first track section.
  zs(0x9A, (uint16_t) ((z(0x0D) & 0x00F8) >> 2));
  x = z(0x8A) & 0xFFF0;
  a = w((uint16_t) (0x9002 + x));
  zs(0xC4, a);
  uint16_t y = (a & 0x8000) ? 0xFFFF : 0;
  zs(0xBA, y);
  a = asl(r, a);
  zs(0xBA, rol(r, z(0xBA)));
  a = asl(r, a);
  zs(0xBA, rol(r, z(0xBA)));
  r->c = true;
  zs(0xB8, sbc16(r, a, z(0x2C)));
  zs(0xBA, sbc16(r, z(0xBA), z(0x2E)));
  r->c = false;
  zs(0x8A, adc16(r, z(0x8A), 0x0010));
  zs(0xC8, w((uint16_t) (0x9014 + x)));
  sext_store(0xBC, 0xBE, w((uint16_t) (0x9010 + x)));
  slope_step(r, w((uint16_t) (0x9012 + x)));

  // $B4:$B6 = $BC:$BE x (64 - $9A) / 64, plus $28:$2A.
  m7a(z(0xBC));
  m7b((uint8_t) (0x40 - zb(0x9A)));
  zbs(0xA9, rd8(0x2134));
  a = rd16(0x2135);
  r->c = false;
  zs(0xA8, asl(r, z(0xA8)));
  a = rol(r, a);
  zs(0xA8, asl(r, z(0xA8)));
  a = rol(r, a);
  zs(0xA8, asl(r, z(0xA8)));
  zs(0xB4, adc16(r, a, z(0x28)));
  zs(0xB6, adc16(r, z(0xBE), z(0x2A)));

  // $B8:$BA += $C0:$C2 x (($0E & 3) + 1).
  m7a(z(0xC0));
  m7b((uint8_t) ((zb(0x0E) & 3) + 1));
  y = (uint16_t) (zb(0xC2) << 8 | rd8(0x2136));
  r->c = false;
  zs(0xB8, adc16(r, rd16(0x2134), z(0xB8)));
  zs(0xBA, adc16(r, y, z(0xBA)));

  // The partial step into the section, into $9C:$9E, $A0:$A2 and $A4:$A6.
  r->c = false;
  uint16_t lo = adc16(r, z(0xBC), z(0x28));
  a = adc16(r, z(0xBE), z(0x2A));
  if(lo & 0x8000) a++;
  mul_add(r, a, lo, (uint8_t) (0x40 - zb(0x9A)), 0x9C, 0x9E);
  mul_add(r, round_ba(), z(0xB8), (uint8_t) (0x40 - zb(0x9A)), 0xA0, 0xA2);
  if(z(0x9A) >= 0x0020) {
    zs(0xA4, 0);
    zs(0xA6, 0);
  } else {
    mul_add(r, round_ba(), z(0xB8), (uint8_t) (0x20 - zb(0x9A)), 0xA4, 0xA6);
  }

  // $80: the horizon line.
  r->c = (zb(0x16) + zb(0x08)) > 0xFF;
  a = adc16(r, z(0x17), z(0x09));
  if(a == 0) {
    a = g_h->flat;
  } else {
    m7a(a);
    m7b(0xF8);
    uint16_t px = rd16(0x2133);
    uint16_t py = rd16(0x2135);
    m7b(0x35);
    zbs(0x80, rd8(0x2136) ^ 0x80);
    zbs(0x81, 0);
    r->c = false;
    adc16(r, px & 0xFF00, rd16(0x2134));
    a = adc16(r, py, z(0x80));
    r->c = true;
    a = sbc16(r, a, g_h->lift);
  }
  zs(0x80, a);
  m7a(z(0x2D));
  m7b(0x84);
  y = rd16(0x2135);
  x = asl(r, rd16(0x2133));
  y = rol(r, y);
  x = asl(r, x);
  y = rol(r, y);
  asl(r, x);
  zs(0x80, adc16(r, y, z(0x80)));
  r->c = true;
  a = sbc16(r, 0x0040, z(0x9A));
  zs(0x9A, a);

  // One segment per pass, until the road reaches the bottom of the screen.
  for(bool first = true;; first = false) {
    if(!first) {
      r->c = false;
      x = adc16(r, z(0x8A), 0x0004);
      zs(0x8A, x);
      if((x & 0x000C) == 0) {  // the next track section
        zs(0xC8, w((uint16_t) (0x9004 + x)));
        sext_store(0xBC, 0xBE, w((uint16_t) (0x9000 + x)));
        slope_step(r, w((uint16_t) (0x9002 + x)));
      }
      r->c = false;
      zs(0xB4, adc16(r, z(0xBC), z(0xB4)));
      zs(0xB6, adc16(r, z(0xBE), z(0xB6)));
      r->c = false;
      zs(0xB8, adc16(r, z(0xC0), z(0xB8)));
      zs(0xBA, adc16(r, z(0xC2), z(0xBA)));
      r->c = false;
      zs(0x9C, adc16(r, z(0xB4), z(0x9C)));
      zs(0x9E, adc16(r, z(0xB6), z(0x9E)));
      r->c = z(0xBA) >= 0x8000;
      uint16_t hx = ror(r, z(0xBA));
      uint16_t hl = ror(r, z(0xB8));
      zs(0xA4, adc16(r, hl, z(0xA0)));
      zs(0xA6, adc16(r, hx, z(0xA2)));
      r->c = false;
      zs(0xA0, adc16(r, z(0xB8), z(0xA0)));
      zs(0xA2, adc16(r, z(0xBA), z(0xA2)));
      zs(0xCA, z(0xA8));
      zs(0xCC, z(0xAA));
      zs(0xCE, 0);
      zs(0xD0, z(0xB0));
      zs(0x94, z(0xAC));
      r->c = false;
      a = adc16(r, z(0x9A), 0x0040);
      zs(0x9A, a);
      if(a >= g_h->far) return false;  // ran out of track to draw: sky below
    }
    x = z(0x9A);
    zs(0x98, rom16(0x8F8B40 + x));
    zs(0xB0, rom16(0x8FABC0 + x));
    zs(0xB2, rom16(0x8FBC00 + x));
    zs(0xAA, centre_x(r, 0xAA, 0xA8));
    if(z(0xA1) == 0) zs(0xAC, z(0xB2));
    else zs(0xAC, scaled_edge(r, z(0xA1), 0xAC));
    x = z(0x7C);
    ws((uint16_t) (0xBE80 + x), z(0x9D));
    ws((uint16_t) (0xBE82 + x), z(0xA1));
    ws((uint16_t) (0xBE84 + x), z(0xAA));
    ws((uint16_t) (0xBE86 + x), z(0xAC));
    zs(0x7C, (uint16_t) (x + 8));
    if((int16_t) (z(0x92) - z(0xAC)) >= 0) continue;  // hidden behind a nearer hill

    // The far edge of the segment, half a step back.
    r->c = true;
    a = sbc16(r, z(0x9A), 0x0020);
    if(a & 0x8000) {
      zs(0xAE, 0);
    } else {
      x = a;
      zs(0xB2, rom16(0x8FBC00 + x));
      zs(0x98, rom16(0x8F8B40 + x));
      if(z(0xA5) == 0) {
        zs(0xAE, z(0xB2));
      } else {
        a = scaled_edge(r, z(0xA5), 0xAE);
        if(a & 0x8000) zs(0xAE, 0);
        else zs(0xAE, (int16_t) (a - z(0xAC)) < 0 ? a : z(0xAC));
      }
    }

    // Steps per line between the two edges.
    r->c = true;
    zs(0xD4, sbc16(r, z(0xAA), z(0xCC)));
    zs(0xD2, 0);
    r->c = true;
    zs(0xD8, sbc16(r, z(0xD0), z(0xB0)));
    zs(0xD6, 0);
    r->c = true;
    y = sbc16(r, z(0xAC), z(0x94));
    if(y != 1) divide_steps(r, y);
    uint16_t bottom = g_h->bottom;
    if(z(0xAE) > bottom) zs(0xAE, bottom);
    if(z(0xAC) > bottom) {
      zs(0xAC, bottom);
      y = (uint16_t) (bottom - z(0x94));
    }
    // Skip the lines a nearer hill already covers.
    for(uint16_t n = (uint16_t) (z(0x92) - z(0x94)); n != 0; n--, y--) {
      edge_step(r);
      edge_step2(r);
    }
    if((int16_t) w((uint16_t) (surface(r, false) + 2)) < 0) {  // no road here
      zs(0xCA, 0);
      zs(0xCC, 0x0380);
      zs(0xD2, 0);
      zs(0xD4, 0);
    }
    // The surface list: near and far halves of the segment.
    x = (uint16_t) (z(0x8C) - 3);
    r->c = true;
    a = sbc16(r, z(0x92), z(0xAE));
    if(!(a & 0x8000)) {
      ws(x, y);
      ws((uint16_t) (x + 1), surface(r, true));
    } else {
      ws(x, (uint16_t) -a);
      ws((uint16_t) (x + 1), surface(r, false));
      r->c = true;
      a = sbc16(r, z(0xAC), z(0xAE));
      if(a != 0) {
        x = (uint16_t) (x - 3);
        ws(x, a);
        ws((uint16_t) (x + 1), surface(r, true));
      }
    }
    zs(0x8C, x);
    // The road's per-line position and width.
    x = z(0x90);
    do {
      x = (uint16_t) (x - 4);
      a = edge_step(r);
      r->c = true;
      a = sbc16(r, a, z(0x96));
      zs(0x96, (uint16_t) (z(0x96) - 1));
      ws((uint16_t) (x + 2), a);
      ws(x, edge_step2(r));
      y--;
    } while(y != 0);
    zs(0x90, x);
    a = z(0xAC);
    zs(0x92, a);
    if(a == bottom) return true;  // reached the bottom: no sky below the road
  }
}

// $08:CB74.
void Tg2BuildRoad(TgRegs* r) {
  set_half(&kHalf1P);
  // Two sets of tables (double-buffered by $84's sign).
  if(!(z(0x84) & 0x8000)) {
    zs(0x8E, 0x9FFF);
    zs(0x90, 0xA3C0);
    zs(0x8C, 0xA9DF);
    ws(0x9FFE, 0);
    ws(0xA9DE, 0);
    zs(0x10, 0x0516);
  } else {
    zs(0x8E, 0xA03F);
    zs(0x90, 0xA740);
    zs(0x8C, 0xAC7F);
    ws(0xA03E, 0);
    ws(0xAC7E, 0);
    zs(0x10, 0x0536);
  }
  if(!build(r)) {
    sky_lists(r);
    return;
  }
  sky_scroll(r);
  uint16_t x = (uint16_t) (z(0x8E) - 6);
  ws(x, 0x00F0);
  ws((uint16_t) (x + 3), 0x00F0);
  ws((uint16_t) (x + 1), z(0x90));
  ws((uint16_t) (x + 4), (uint16_t) (z(0x90) + 0x01C0));
  zs(0x8E, x);
  zs(0x80, z(0x92));
  zs(0x7C, (uint16_t) (z(0x7C) >> 3));
  zs(0x00, z(0xAC));
  uint16_t y = z(0x10);
  ws(y, 0);
  ws((uint16_t) (y + 2), 0);
}

// $08:DDE4 (player 2) / $08:E73E (player 1): a half whose road ended above its bottom line.
// The lines below it split into ground bands as sky_lists does, on the half's $68 lines
// (no run is long enough to need two HDMA entries). Returns the HDMA list pointer x.
static uint16_t sky_lists_half(TgRegs* r) {
  sky_scroll(r);
  const uint16_t bottom = 0x0068;
  uint16_t h = z(0x80), top = z(0x92);
  uint16_t hill = (uint16_t) ((z(0x86) & 0x3F) + 0x98 + h);
  if(!((h - bottom) & 0x8000)) {
    zs(0xE0, (uint16_t) (bottom - top));
    zs(0xDE, 0);
    zs(0xDC, 0);
    zs(0xE6, 0);
    zs(0xE4, 0);
    zs(0xDA, 0);
  } else if(!((h - 0x0028) & 0x8000)) {
    uint16_t a = (uint16_t) (h - top);
    if(a & 0x8000) {
      zs(0xE0, 0);
      zs(0xDE, (uint16_t) (bottom - top));
      zs(0xDC, 0);
      zs(0xE6, (uint16_t) (bottom - top));
    } else {
      zs(0xE0, a);
      zs(0xDE, (uint16_t) (bottom - h));
      zs(0xDA, (uint16_t) (bottom - h));
      zs(0xDC, 0);
      zs(0xE6, (uint16_t) (bottom - h));
    }
    zs(0xE4, 0);
    zs(0xE2, hill);
  } else {
    uint16_t a = (uint16_t) (h - top);
    if(a & 0x8000) {
      zs(0xE0, 0);
      a = (uint16_t) (a + 0x40);
      if(a & 0x8000) {
        zs(0xDE, 0);
        uint16_t dc = (uint16_t) (bottom - top);
        zs(0xDC, dc);
        a = (uint16_t) (((z(0x86) & 0x3F) | 0x0100) + h);
        if(!((a - bottom) & 0x8000)) {
          zs(0xE2, (uint16_t) (a - bottom));
          zs(0xE6, dc);
          zs(0xE4, 0);
        } else {
          a = (uint16_t) (a - top);
          if(a & 0x8000) {
            zs(0xE4, dc);
            zs(0xE6, 0);
          } else {
            zs(0xE6, a);
            zs(0xE4, (uint16_t) (bottom - a - top));
            zs(0xE2, 0);
          }
        }
      } else {
        zs(0xDE, a);
        zs(0xDC, (uint16_t) (0x0028 - h));
        if(hill & 0x8000) {
          zs(0xE2, 0);
          zs(0xE4, (uint16_t) -hill);
          zs(0xE6, (uint16_t) (z(0xDC) + z(0xDE) - z(0xE4)));
        } else {
          zs(0xE2, hill);
          zs(0xE4, 0);
          zs(0xE6, (uint16_t) (z(0xDC) + z(0xDE)));
        }
      }
    } else {
      zs(0xE0, a);
      zs(0xDE, 0x0040);
      zs(0xDC, (uint16_t) (0x0028 - h));
      uint16_t e6 = (uint16_t) (z(0xDC) + 0x40);
      zs(0xE6, e6);
      zs(0xE4, 0);
      zs(0xDA, e6);
      zs(0xE2, (uint16_t) (((z(0x86) & 0x3F) | 0x0100) - e6));
    }
  }

  const uint16_t ground = g_h->p2 ? 0x9E10 : 0x9C50;
  uint16_t x = z(0x8C), y = z(0x8E), a;
  if((a = z(0x92)) != 0) {
    y = (uint16_t) (y - 3);
    ws(y, a | 0x80);
    ws((uint16_t) (y + 1), z(0x90));
  }
  if((a = z(0xE0)) != 0) {
    x = (uint16_t) (x - 3);
    y = (uint16_t) (y - 3);
    ws(x, a);
    ws(y, a | 0x80);
    ws((uint16_t) (x + 1), z(0x88));
    ws((uint16_t) (y + 1), (uint16_t) ((z(0xDA) << 2) + ground));
  }
  if((a = z(0xDE)) != 0) {
    y = (uint16_t) (y - 3);
    ws(y, a);
    ws((uint16_t) (y + 1), g_h->p2 ? 0x0274 : 0x0238);
  }
  if((a = z(0xDC)) != 0) {
    y = (uint16_t) (y - 3);
    ws(y, a | 0x80);
    ws((uint16_t) (y + 1), ground);
  }
  zs(0x8E, y);
  y = z(0x10);
  ws(y, 0);
  if((a = z(0xE6)) != 0) {
    x = (uint16_t) (x - 3);
    ws(x, a | 0x80);
    ws((uint16_t) (y + 4), a | 0x80);
    uint16_t v = (uint16_t) ((z(0xE2) << 2) + 0x8600);
    ws((uint16_t) (x + 1), v);
    ws((uint16_t) (y + 5), v);
    ws(y, 1);
    ws((uint16_t) (y + 2), x);
    y = (uint16_t) (y + 5);
  }
  y = (uint16_t) (y + 2);
  ws(y, 0);
  if((a = z(0xE4)) != 0) {
    x = (uint16_t) (x - 3);
    ws(x, a);
    ws((uint16_t) (y + 4), a);
    ws((uint16_t) (x + 1), 0x8600);
    ws((uint16_t) (y + 5), 0x8600);
    ws(y, 1);
    ws((uint16_t) (y + 2), x);
  }
  return x;
}

// $08:DD8D / $08:E6E7: a half whose road reached its bottom line.
static uint16_t bottom_half(TgRegs* r) {
  sky_scroll(r);
  uint16_t x = z(0x8C);
  uint16_t y = (uint16_t) (z(0x8E) - 3);
  ws(y, 0x00E8);
  ws((uint16_t) (y + 1), z(0x90));
  zs(0x8E, y);
  y = z(0x10);
  ws(y, 0);
  ws((uint16_t) (y + 2), 0);
  return x;
}

// $08:DFF4 / $08:E94E: the half's closing HDMA entries, and its horizon and row count.
static void close_half(uint16_t x) {
  bool p2 = g_h->p2;
  uint16_t y = (uint16_t) (z(0x8E) - 3);
  x = (uint16_t) (x - 3);
  ws(x, p2 ? 0x0008 : 0x0004);
  ws(y, p2 ? 0x0088 : 0x0084);
  ws((uint16_t) (x + 1), 0x83FC);
  ws((uint16_t) (y + 1), p2 ? 0x9DF0 : 0x9C40);
  zs(0x8C, x);
  zs(0x8E, y);
  zs(0x80, z(0x92));
  zs(0x7C, (uint16_t) (z(0x7C) >> 3));
  ram16_set(DPB + (p2 ? 0x02 : 0x00), z(0xAC));
}

// $08:D63F. Two players: player 2's half (the top of the screen), then player 1's.
void Tg2BuildRoad2P(TgRegs* r) {
  set_half(&kHalfP1);
  bool alt = (z(0x84) & 0x8000) != 0;
  uint16_t hz = alt ? 0xA03C : 0x9FFC, hs = alt ? 0xAC7C : 0xA9DC;
  zs(0x8E, hz);
  zs(0x90, alt ? 0xA740 : 0xA3C0);
  zs(0x8C, hs);
  ws(hz, 0x0084);
  ws((uint16_t) (hz + 2), 0);
  ws((uint16_t) (hz + 1), 0x9FB0);
  ws(hs, 0x0004);
  ws((uint16_t) (hs + 2), 0);
  ws((uint16_t) (hs + 1), 0x83FC);
  zs(0x10, alt ? 0x0536 : 0x0516);
  zs(0x4C, alt ? 0x0576 : 0x0556);
  for(int i = 0; i < 2; i++) {
    set_half(i == 0 ? &kHalfP2 : &kHalfP1);
    close_half(build(r) ? bottom_half(r) : sky_lists_half(r));
  }
}
