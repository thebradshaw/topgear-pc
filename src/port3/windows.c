// Top Gear 3000: the view's window table, from the DSP-4 (bank $01).
//
// $01:8564 clears the table at $7E:2E2C (or $318C for the lower view): 27 rows of eight
// 4-byte entries, each "off" ($00FF, $FF00). It then starts the chip's command 8 with the
// view's place and fixed screen limits, and passes it every road section built this frame
// (the tables at $7E:34EC-39EC); for each, the chip answers with lists of (address, word)
// pairs, written straight into bank $7E. Finally it picks the window settings ($B6-$BB)
// and, when the chip says so, a shadow band in the table under the nearest section.

#include "../port/port.h"

static uint16_t z(const TgRegs* r, uint8_t o) { return ram16((uint16_t) (r->d + o)); }
static void zs(const TgRegs* r, uint8_t o, uint16_t v) { ram16_set((uint16_t) (r->d + o), v); }
static void zb(const TgRegs* r, uint8_t o, uint8_t v) { g_ram[(uint16_t) (r->d + o)] = v; }
static uint16_t w(uint16_t a) { return ram16(a); }
static void ws(uint16_t a, uint16_t v) { ram16_set(a, v); }
static uint16_t l7e(uint32_t a) { return ram16((a - 0x7E0000) & 0x1FFFF); }
static void l7e_set(uint32_t a, uint16_t v) { ram16_set((a - 0x7E0000) & 0x1FFFF, v); }

static void dput(uint16_t v) { tg_bus_write16(0x308000, v); }
static uint16_t dget(void) { return (uint16_t) (tg_bus_read8(0x308000) | tg_bus_read8(0x308001) << 8); }
// LDA $C000 until bit 15: what A holds after the wait.
static uint16_t dwait(void) {
  for(;;) {
    uint16_t s = (uint16_t) (tg_bus_read8(0x30C000) | tg_bus_read8(0x30C001) << 8);
    if(s & 0x8000) return s;
  }
}
static void dwait_bit(void) {
  while(!(tg_bus_read8(0x30C001) & 0x80)) {}  // BIT $C000
}
static uint16_t swap(uint16_t v) { return (uint16_t) (v << 8 | v >> 8); }
// N of a - b: the game's "less" test (CMP, then BMI/BPL).
static bool minus(uint16_t a, uint16_t b) { return ((uint16_t) (a - b) & 0x8000) != 0; }

// $01:89F5: two lists from the chip, each a count and then (address, word) pairs for
// bank $7E.
static void take_lists(TgRegs* r) {
  for(int list = 0; list < 2; list++) {
    r->a = dwait();
    uint16_t n = dget();
    r->y = n;
    flags_nz16(r, n);
    for(; n != 0; n--) {
      uint16_t x = dget();
      uint16_t v = dget();
      l7e_set(0x7E0000 + x, v);
      r->x = x;
      r->a = v;
      flags_nz16(r, v);
      r->y = (uint16_t) (n - 1);
    }
  }
}

static void send_section(uint16_t s) {
  dput(l7e(0x7E38EC + s));
  dput(l7e(0x7E35EC + s));
  dput(l7e(0x7E37EC + s));
  dput(l7e(0x7E366C + s));
  dput(l7e(0x7E386C + s));
}

static void send_ends(uint16_t x, bool near) {
  uint16_t o = near ? 0 : 2;
  dput(l7e(0x7E3C6C + o + x));
  dput(l7e(0x7E3F6C + o + x));
  dput(l7e(0x7E3C6C + o + x));
  dput(l7e(0x7E3F6C + o + x));
}

// The chip's answer for one side of the shadow, as a byte (0-$FF).
static uint16_t shadow_edge(uint16_t a, uint16_t dist) {
  dput(0);
  dput((uint16_t) (a << 1));
  dput(dist);
  dget();
  a = (uint16_t) (dget() + 0x80);
  if(a & 0x8000) a = 0;
  if(a & 0xFF00) a = 0xFF;
  return a;
}

void Tg3RoadWindows(TgRegs* r) {
  uint16_t x = w(0x00B4) != 0 ? 0x2E2C : 0x318C;
  zs(r, 0x16, x);
  ws(0x1E4E, x);
  zs(r, 0x1A, (uint16_t) ((w(0x0226) << 1) & 0x3E));
  for(int row = 0; row < 0x1B; row++, x = (uint16_t) (x + 0x20)) {
    for(int i = 0; i < 0x20; i += 4) {
      l7e_set(0x7E0000 + (uint16_t) (x + i), 0x00FF);
      l7e_set(0x7E0000 + (uint16_t) (x + i + 2), 0xFF00);
    }
  }
  x = z(r, 0x1A);
  // The road's two ends where the view is, between this section's points.
  dput(0);
  dput((uint16_t) ((l7e(0x7E3C6E + x) - l7e(0x7E3C6C + x)) << 1));
  uint16_t frac = swap((uint16_t) ((w(0x0225) >> 1) & 0x7F));
  dput(frac);
  zs(r, 0x1E, frac);
  dget();
  zs(r, 0x1C, (uint16_t) (dget() + l7e(0x7E3C6C + x)));
  dput(0);
  dput((uint16_t) ((l7e(0x7E3F6E + x) - l7e(0x7E3F6C + x)) << 1));
  dput(z(r, 0x1E));
  dget();
  zs(r, 0x1E, (uint16_t) (dget() + l7e(0x7E3F6C + x)));
  // Command 8 and the screen's limits.
  static const uint16_t kFixed[] = { 0x0008, 0x00FF, 0x00FF, 0x00FF, 0x00FF, 0, 0, 0, 0, 0x00A6, 0x00A6,
    0x00A6, 0x00A6, 0x00A5, 0x00A5, 0x00A7, 0x00A7, 0x0081, 0x007F, 0x0081, 0x007F };
  for(unsigned i = 0; i < sizeof(kFixed) / sizeof(kFixed[0]); i++) dput(kFixed[i]);
  uint16_t base = z(r, 0x16);
  uint16_t h = w(0x0242), h2 = w(0x1EF2);
  uint16_t a = (uint16_t) (base + 0x035C);
  if(h & 0x8000) {
    a = (uint16_t) ((uint16_t) (h << 2) + base + 0x035C);
    if(a < base) a = base;
  }
  dput(a);
  dput(a);
  zs(r, 0x18, a);
  a = (uint16_t) (base + 0x035E);
  if(h2 & 0x8000) {
    a = (uint16_t) ((uint16_t) (h2 << 2) + base + 0x035E);
    if(a < base) a = (uint16_t) (base + 2);
  }
  dput(a);
  dput(a);
  a = (h & 0x8000) ? (uint16_t) (h + 0x33) : 0x0033;
  dput(a);
  dput(a);
  a = (h2 & 0x8000) ? (uint16_t) (h2 + 0x33) : 0x0033;
  dput(a);
  dput(a);
  for(int i = 0; i < 4; i++) dput(0xFF5B);
  dput(0x2FC8);
  dput(0x0034);
  dput(0xFF5C);
  dput(0x0035);
  dput(0x7FFF);
  dput(w(0x0202));
  dput((uint16_t) (w(0x0242) + 0x33));
  dput(w(0x1EF6));
  dput((uint16_t) (w(0x1EF2) + 0x33));
  x = z(r, 0x1A);
  if(l7e(0x7E3D6E + x) & 1) {
    zs(r, 0x1C, l7e(0x7E3C6C + x));
    zs(r, 0x1E, l7e(0x7E3F6C + x));
  }
  dput(z(r, 0x1C));
  dput(z(r, 0x1E));
  dput(z(r, 0x1C));
  dput(z(r, 0x1E));
  // With the view below the road, the chip first sends the rows it covers (at most $D8).
  x = z(r, 0x18);
  a = (uint16_t) -w(0x0242);
  if(!(a & 0x8000) && a != 0) {
    if(a >= 0xD8) a = 0xD8;
    for(uint16_t n = a; n != 0; n--) {
      dwait_bit();
      uint16_t v = dget();
      x = (uint16_t) (x + 4);
      l7e_set(0x7E0000 + x, v);
    }
  } else {
    dwait_bit();
    dget();
  }
  dwait_bit();
  // Then each section.
  uint16_t s = 2;
  zs(r, 0x14, s);
  do {
    send_section(s);
    x = z(r, 0x1A);
    bool split = l7e(0x7E3D6E + x) & 1;
    send_ends(x, split);
    take_lists(r);
    x = z(r, 0x1A);
    if(l7e(0x7E3D6E + x) & 1) {
      send_section(z(r, 0x14));
      send_ends(z(r, 0x1A), false);
      take_lists(r);
    }
    s = (uint16_t) (z(r, 0x14) + 2);
    zs(r, 0x1A, (uint16_t) (z(r, 0x1A) + 2));
    zs(r, 0x14, s);
  } while(s != w(0x0190));
  x = s;
  dput(0x8000);
  bool shadow = dget() != 0;
  // The window settings, and the nearest section (the least far distance and height).
  zs(r, 0xC8, shadow ? 1 : 0);
  static const uint8_t kPlain[3] = { 0xC2, 0xB0, 0xA1 }, kShadow[6] = { 0xC8, 0xB0, 0x21, 0x20, 0x42, 0x82 };
  for(int i = 0; i < 3; i++) zb(r, (uint8_t) (0xB6 + i), shadow ? kShadow[i] : kPlain[i]);
  for(int i = 0; i < 3; i++)
    zb(r, (uint8_t) (0xB9 + i), shadow ? kShadow[3 + i] : g_ram[0x4E55 + 2 * i]);
  zs(r, 0x14, 0x3FFF);
  zs(r, 0x16, shadow ? 0xC001 : 0x3FFF);
  x = 2;
  do {
    a = l7e(0x7E386C + x);
    if(a == z(r, 0x14) || minus(a, z(r, 0x14))) {
      zs(r, 0x14, a);
      zs(r, 0x18, x);
    }
    a = l7e(0x7E37EC + x);
    if(shadow ? !minus(a, z(r, 0x16)) : (a == z(r, 0x16) || minus(a, z(r, 0x16)))) {
      zs(r, 0x16, a);
      zs(r, 0x1A, x);
    }
    x = (uint16_t) (x + 2);
  } while(x != w(0x0194));
  if(minus(z(r, 0x16), z(r, 0x14))) {
    // The shadow band: between the chip's two edges, over the rows between them.
    x = z(r, 0x1A);
    zs(r, 0x18, l7e(0x7E34EC + x));
    zs(r, 0x1C, l7e(0x7E38EC + x));
    x = (uint16_t) (((w(0x0226) << 1) & 0x3E) + z(r, 0x1A));
    zs(r, 0x1A, (uint16_t) -(uint16_t) (l7e(0x7E3F6C + x) + z(r, 0x18)));
    zs(r, 0x18, (uint16_t) -(uint16_t) (l7e(0x7E3C6C + x) + z(r, 0x18)));
    uint16_t left = shadow_edge(z(r, 0x18), z(r, 0x1C));
    zs(r, 0x18, left);
    uint16_t right = shadow_edge(z(r, 0x1A), z(r, 0x1C));
    zs(r, 0x18, (uint16_t) (right << 8 | left));
    zs(r, 0x14, (uint16_t) (((uint16_t) (z(r, 0x14) + 0xA5) << 2) + w(0x1E4E)));
    x = (uint16_t) (((uint16_t) (z(r, 0x16) + 0xA5) << 2) + w(0x1E4E));
    do {
      l7e_set(0x7E0002 + x, z(r, 0x18));
      x = (uint16_t) (x + 4);
    } while(x != z(r, 0x14));
  }
  // A counter stepping by $11 through $A411-$A421.
  r->c = false;
  a = adc16(r, w(0x00EA), 0x0011);
  r->c = a >= 0xA422;
  if(a == 0xA422) a = 0xA411;
  ws(0x00EA, a);
  r->a = a;
  r->x = x;
  r->y = 0;
  r->db = 0x83;
  r->n = true;  // PLB of $83
  r->z = false;
}
