// Top Gear 2: the track stream ($08:8BCA for player 1, $08:AB9F for player 2). As a car
// moves, rows of the track come into view ahead and leave behind: each row is four
// words a column at $7E:9000 (a ring of $40 rows, $10 bytes each, copied at $9200) that
// the road and object code read. A row's first three words come from the track's
// segment tables in banks $89/$8A; the next four are roadside objects decoded from a
// run-length stream of 10-bit fields (three to a 32-bit record, which field depends on the
// detail level at $0120). Objects that can be hit get a slot in a ring of $100 words at
// $9800 and a flag in their state byte at $6400. The routine also walks the track's event
// lists ($5800 and $5D00), which start scenery animations, and works out the road's
// curve and the camera's sideways scroll for the frame.
//
// Player 2's copy is the same code on different variables, so this file runs both
// through an address map, as drive.c does.

#include <stddef.h>

#include "../port/port.h"

#define DPB 0x0300u

// Player 2's addresses for player 1's ones.
static const uint8_t kDp2[][2] = {
  { 0x02, 0x12 }, { 0x04, 0x14 }, { 0x06, 0x16 }, { 0x08, 0x18 }, { 0x0A, 0x1A }, { 0x0C, 0x1C },
  { 0x0E, 0x1E }, { 0x10, 0x20 }, { 0x22, 0x26 }, { 0x24, 0x28 }, { 0x2A, 0x32 }, { 0x2C, 0x34 },
  { 0x2E, 0x36 }, { 0x30, 0x38 }, { 0x5E, 0x60 }, { 0x62, 0x64 }, { 0x66, 0x68 }, { 0x6A, 0x6C },
  { 0x6E, 0x70 },
};
static const uint16_t kAbs2[][2] = {
  { 0x013C, 0x013E }, { 0x0168, 0x016A }, { 0x016C, 0x016E }, { 0x0170, 0x0172 }, { 0x1CF0, 0x1DF8 },
  { 0x9800, 0x9A00 },
};

static bool g_p2;
static uint8_t g_dp[256];

static void set_player(bool p2) {
  g_p2 = p2;
  for(int i = 0; i < 256; i++) g_dp[i] = (uint8_t) i;
  if(p2)
    for(size_t i = 0; i < sizeof kDp2 / sizeof kDp2[0]; i++) g_dp[kDp2[i][0]] = kDp2[i][1];
}
// Absolute addresses: the $02xx block moves by $3C, the row ring by $400, the rest by the map.
static uint16_t ad(uint16_t a) {
  if(!g_p2) return a;
  if(a >= 0x020C && a <= 0x022F) return (uint16_t) (a + 0x3C);
  if(a >= 0x9000 && a < 0x9400) return (uint16_t) (a + 0x400);
  for(size_t i = 0; i < sizeof kAbs2 / sizeof kAbs2[0]; i++)
    if(kAbs2[i][0] == a) return kAbs2[i][1];
  return a;
}
static uint16_t P(uint16_t p1, uint16_t p2) { return g_p2 ? p2 : p1; }

static uint16_t z(uint8_t o) { return ram16(DPB + g_dp[o]); }
static void zs(uint8_t o, uint16_t v) { ram16_set(DPB + g_dp[o], v); }
static void zbs(uint8_t o, uint8_t v) { g_ram[DPB + g_dp[o]] = v; }
static void zinc(uint8_t o) { zs(o, (uint16_t) (z(o) + 1)); }
static void zdec(uint8_t o) { zs(o, (uint16_t) (z(o) - 1)); }
static uint16_t w(uint16_t a) { return ram16(ad(a)); }
static void ws(uint16_t a, uint16_t v) { ram16_set(ad(a), v); }
// The row ring (and its copy at +$200), indexed by the row offset y.
static uint16_t ring(uint16_t a, uint16_t y) { return ram16((uint16_t) (ad(a) + y)); }
static void ring_set(uint16_t o, uint16_t y, uint16_t v) {
  ram16_set((uint16_t) (ad((uint16_t) (0x9000 + o)) + y), v);
  ram16_set((uint16_t) (ad((uint16_t) (0x9200 + o)) + y), v);
}
static uint8_t bus8(uint32_t a) { return tg_bus_read8(a & 0xFFFFFF); }
static uint16_t bus16(uint32_t a) { return (uint16_t) (bus8(a) | bus8(a + 1) << 8); }
static bool neg(uint16_t v) { return (v & 0x8000) != 0; }

// The hardware multiplier: a signed 16-bit value times a signed byte, read at $2134-$2136.
static void m7a(uint16_t v) {
  tg_bus_write8(0x211B, (uint8_t) v);
  tg_bus_write8(0x211B, (uint8_t) (v >> 8));
}
static void m7b(uint8_t v) { tg_bus_write8(0x211C, v); }
static uint8_t mpy8(uint16_t a) { return tg_bus_read8(a); }
static uint16_t mpy16(uint16_t a) { return (uint16_t) (tg_bus_read8(a) | tg_bus_read8((uint16_t) (a + 1)) << 8); }

// The high byte of a word, sign-extended to 16 bits (and #$FF00, ora #$00FF if negative, xba).
static uint16_t sx_hi(uint16_t v) { return (uint16_t) (int16_t) (int8_t) (v >> 8); }

// One field of the object stream's 32-bit record at $89:0000 + x: detail level 0 takes
// bits 0-9, 1 bits 10-19, 2 bits 20-29.
static uint16_t field(uint16_t x, int level) {
  uint32_t a = 0x890000u + x;
  switch(level) {
  case 0: return bus16(a) & 0x03FF;
  case 1: return (uint16_t) ((bus16(a + 1) & 0x0FFC) >> 2);
  default: return (uint16_t) ((bus16(a + 2) & 0x3FF0) >> 4);
  }
}
static int detail(void) {
  uint16_t d = ram16(0x0120);
  return d == 0 ? 0 : d == 1 ? 1 : 2;
}

// The start of the object stream for the segment record at $8A:0000 + x ($8990C0 table).
static uint16_t stream_start(uint16_t seg) {
  uint16_t i = (uint16_t) (((uint16_t) ((seg & 0x0FC0) << 3)) >> 8);
  return bus16(0x8990C0u + i);
}
// The stream's header: a byte count of records to skip, at the record for the detail level.
static uint16_t skip_count(uint16_t start) {
  uint16_t l = ram16(0x0120);
  if(l >= 3) l = 2;
  return (uint16_t) (bus8(0x890000u + (uint16_t) (l + start)) << 2);
}

// The segment record for the row at track position pos.
static uint16_t segment(uint16_t pos) {
  uint16_t x = (uint16_t) (ram16(0x0108) << 3);
  uint16_t a = (uint16_t) (((pos & 0xFFC0) >> 4) + bus16(0x8A8000u + x));
  return bus16(0x8A0000u + a);
}

// A row's road words: three columns from the segment's tables (pos is $04 going forward,
// $02 going back; the 6200 byte for the row goes to dp).
static void road_row(uint16_t pos, uint16_t y, uint8_t dp) {
  zs(0x3C, (uint16_t) ((pos & 0x3C) >> 1));
  uint16_t x = (uint16_t) (ram16(0x0108) << 3);
  uint16_t a = (uint16_t) ((pos & 0xFFC0) >> 4);
  zs(0x3E, x);
  zs(0x40, a);
  zs(dp, g_ram[0x6200 + (uint16_t) (a >> 2)]);
  x = (uint16_t) (z(0x40) + bus16(0x8A8000u + z(0x3E)));
  zs(0x3E, bus16(0x8A0000u + x));
  zs(0x40, bus16(0x8A0002u + x));
  ring_set(0, y, bus16(0x898000u + (uint16_t) (((z(0x40) & 0x3F00) >> 3) + z(0x3C))));
  ring_set(2, y, bus16(0x898800u + (uint16_t) (((z(0x40) & 0x00FC) << 3) + z(0x3C))));
  uint16_t w3f = ram16(DPB + 0x3F);
  ring_set(4, y, bus16(0x898C40u + (uint16_t) (((w3f & 0x03F0) << 1) + z(0x3C))));
}

// An object leaving the view: if it could be hit and wasn't, its state byte marks it
// passed (bit 7, when bit 4 is set and the other player's bit isn't). Going backward the
// object is also kept at dp $30.
static bool leave_object(uint16_t v, bool keep) {
  if(!neg(v) || (v & 0x4000)) return false;
  uint16_t x = v & 0x3FFF;
  if(keep) zs(0x30, x);
  uint8_t b = g_ram[0x6400 + x] & 0xDF;
  if((b & 0x10) && !(b & P(0x20, 0x40))) b |= 0x80;
  g_ram[0x6400 + x] = b;
  return true;
}

// The object stream that runs ahead of the stream being decoded: it only counts runs, so the
// other end knows where to start. cnt counts down a run of empty records, len counts up.
static uint16_t skip_cell(uint16_t x, int level, uint8_t cnt, uint8_t len, int dx) {
  if(z(cnt) != 0) {
    zdec(cnt);
    if(z(cnt) != 0) {
      zinc(len);
      return x;
    }
  } else {
    uint16_t f = field(x, level);
    zs(0x42, f);
    if(!(f & 0x0380) && f != 0) {
      zs(cnt, f);
      zs(len, 1);
      return x;
    }
  }
  zs(len, 0);
  return (uint16_t) (x + dx);
}

// The roadside lookup for an object field: the object table entry, or 0 where the row's
// mask ($6200) hides objects of its kind.
typedef struct Cell { uint16_t x; uint16_t v; } Cell;

// One object column of a row coming into view ahead (going forward: the stream runs
// backwards, records 4 bytes apart). cnt/len are the run counters ($0E/$10), mask the
// row's hide mask at dp $24.
static Cell ahead_cell(uint16_t x, int level) {
  Cell c = { x, 0 };
  if(z(0x0E) != 0) {
    zinc(0x10);
    zdec(0x0E);
    return c;
  }
  zs(0x10, 0);
  x = (uint16_t) (x - 4);
  c.x = x;
  uint16_t f = field(x, level);
  zs(0x44, f);
  if(!(f & 0x0380)) {
    zs(0x0E, z(0x44));
    zs(0x10, 1);
    return c;
  }
  zs(0x46, x);
  uint16_t i = (uint16_t) ((f & 0x0380) >> 6);
  if(bus16(0x88EABBu + i) & z(0x24)) {
    zs(0x0E, 0);
    zs(0x10, 1);
    return c;
  }
  uint16_t e = bus16(0x89B1EBu + (uint16_t) (i + z(0x42)));
  if(neg(e)) {
    if(e & 0x4000) {
      c.v = e;
    } else {
      uint16_t m = e & 0x3FFF;
      uint16_t t = (uint16_t) (z(0x2C) - m);
      if(z(0x2C) < m) t = (uint16_t) (t + z(0x00));
      zs(0x2C, t);
      zs(0x2A, (uint16_t) ((z(0x2A) - 2) & 0x01FF));
      ram16_set((uint16_t) (ad(0x9800) + z(0x2A)), t);
      ram16_set((uint16_t) (0x6400 + t), (uint16_t) (ram16((uint16_t) (0x6400 + t)) | P(0x40, 0x20)));
      c.v = t | 0x8000;
    }
  } else {
    zs(0x48, e & 0xFF);
    c.v = (uint16_t) ((ram16(DPB + 0x43) & 0x7F00) | z(0x48));
  }
  return c;
}

// The same going backward: the stream runs forwards, counters $0C/$0A, mask at dp $22.
static Cell behind_cell(uint16_t x, int level) {
  Cell c = { x, 0 };
  if(z(0x0C) != 0) {
    zinc(0x0A);
    zdec(0x0C);
    return c;
  }
  zs(0x0A, 0);
  x = (uint16_t) (x + 4);
  c.x = x;
  uint16_t f = field(x, level);
  zs(0x44, f);
  if(!(f & 0x0380)) {
    zs(0x0C, z(0x44));
    zs(0x0A, 1);
    return c;
  }
  zs(0x46, x);
  uint16_t i = (uint16_t) ((f & 0x0380) >> 6);
  if(bus16(0x88EABBu + i) & z(0x22)) {
    zs(0x0C, 0);
    zs(0x0A, 1);
    return c;
  }
  uint16_t e = bus16(0x89B1EBu + (uint16_t) (i + z(0x42)));
  if(neg(e)) {
    if(e & 0x4000) {
      c.v = e;
    } else {
      zs(0x48, e);
      uint16_t s = z(0x2E);
      ram16_set((uint16_t) (ad(0x9800) + s), z(0x30));
      zs(0x2E, (uint16_t) ((s + 2) & 0x01FF));
      uint16_t a = (uint16_t) ((z(0x48) & 0x3FFF) + z(0x30));
      if(a == z(0x00)) a = 0;
      uint16_t old = z(0x30);
      zs(0x30, a);
      ram16_set((uint16_t) (0x6400 + old), (uint16_t) (ram16((uint16_t) (0x6400 + old)) | P(0x40, 0x20)));
      c.v = old | 0x8000;
    }
  } else {
    zs(0x48, e & 0xFF);
    c.v = (uint16_t) ((ram16(DPB + 0x43) & 0x7F00) | z(0x48));
  }
  return c;
}

// The scenery flags each row's marker words (at $9016 on) switch on and off as the car
// passes them.
static void marker(uint16_t y, bool ahead) {
  uint16_t v = ring(0x9016, y);
  if(v < 0xC000) return;
  v &= 0x000F;
  if(v < 2) return;
  if(ahead) v ^= 1;
  uint16_t x = (uint16_t) (v << 1);
  ws(0x013C, (uint16_t) ((w(0x013C) | bus16(0x88EA7Fu + x)) & bus16(0x88EA9Bu + x)));
}

// The sideways scroll a row adds: the row's lean ($9002) times 16 and its change from the
// last row times 10, both 16 x 8-bit products, taken from (sign < 0) or added to the 24-bit
// scroll at $021A/$021C.
static void scroll_step(uint16_t mul, uint16_t v, bool add) {
  uint16_t hi = neg(v) ? 0xFFFF : 0;
  m7a(v);
  m7b((uint8_t) mul);
  ram16_set(DPB + 0x48, (uint16_t) ((hi & 0xFF00) | mpy8(0x2136)));
  uint16_t p = mpy16(0x2134);
  uint16_t lo = w(0x021A);
  if(add) {
    uint32_t s = (uint32_t) lo + p;
    ws(0x021A, (uint16_t) s);
    ws(0x021C, (uint16_t) (w(0x021C) + z(0x48) + (s >> 16)));
  } else {
    uint16_t borrow = lo < p;
    ws(0x021A, (uint16_t) (lo - p));
    ws(0x021C, (uint16_t) (w(0x021C) - z(0x48) - borrow));
  }
}

// One scenery event from a track list: hi byte 0 sets the object height to use ($6E),
// otherwise it starts an animation slot at $0800 (16 slots of 16 bytes).
static void event(uint16_t e) {
  zs(0x3C, e);
  uint16_t k = e >> 8;
  if(k == 0) {
    zs(0x6E, (uint16_t) ((e & 0xFF) << 1));
    return;
  }
  uint16_t x = (uint16_t) (k << 2);
  uint8_t bank = bus8(0x8BDBE5u + x);
  tg_push8(0x7E);  // phb
  tg_push8(bank);  // pha
  tg_pull8();      // plb
  uint16_t p = bus16(0x8BDBE3u + x);
  p = bus16((uint32_t) bank << 16 | (uint16_t) (p + 8));
  x = bus16((uint32_t) bank << 16 | (uint16_t) (p + 2));
  tg_pull8();      // plb
  uint16_t h = (uint16_t) ((z(0x3C) & 0xFF) << 1);
  ram16_set((uint16_t) (ad(0x1CF0) + x), h);
  zs(0x3C, (uint16_t) ((h << 4) + P(0x4000, 0x5000)));
  zs(0x3E, ram16((uint16_t) (0x1BF0 + x)));
  uint16_t x8 = (uint16_t) (x << 3);
  uint16_t a = bus16(0x8BF34Du + x8);
  uint16_t d = (uint16_t) ((uint16_t) (bus16(0x8BF34Fu + x8) - a) >> 1);
  if(d == 0) return;
  zs(0x40, d);
  zs(0x3E, (uint16_t) (a + z(0x3E)));
  uint16_t s = ram16(0x00E8) & 0x01F0;
  ram16_set(0x00E8, (uint16_t) ((s + 0x10) & 0x01F0));
  ram16_set((uint16_t) (0x0800 + s), 0x7FC0);
  ram16_set((uint16_t) (0x0802 + s), z(0x3E));
  ram16_set((uint16_t) (0x0804 + s), z(0x3C));
  ram16_set((uint16_t) (0x0806 + s), z(0x40));
  for(int i = 8; i < 16; i += 2) ram16_set((uint16_t) (0x0800 + s + i), 0);
  ram16_set(0x00EC, (uint16_t) (ram16(0x00EC) + 1));
}

// Where the car is on the track for the event lists: $020E, one less if the fraction is
// under $800, plus $4C, wrapped to the lap.
static uint16_t event_pos(void) {
  uint16_t y = w(0x020E);
  if(!(w(0x020C) & 0xF800)) y--;
  uint16_t a = (uint16_t) (y + 0x4C);
  if(a >= ram16(0x010A)) a = (uint16_t) (a - ram16(0x010A));
  zs(0x3A, a);
  return a;
}

// Going forward: the $5D00 list starts events behind (it is walked backwards from the last
// position), the $5800 list only keeps its place.
static void events_ahead(void) {
  uint16_t p = event_pos();
  uint16_t y;
  if(z(0x5C) != 0) {
    y = z(0x6A);
    if(p != z(0x62)) {
      if(p > z(0x62)) {
        // Past the lap's end: every event down to the first, then from the last.
        if(ram16((uint16_t) (0x5D04 + y)) == 0xFFFF) goto check_wrap;
        for(;;) {
          uint16_t e = ram16((uint16_t) (0x5D02 + y));
          y = (uint16_t) (y - 4);
          event(e);
          if(y == 0xFFFC) {
            y = (uint16_t) (z(0x5C) - 4);
          check_wrap:
            if(p >= ram16((uint16_t) (0x5D00 + y))) goto done;
            goto run_down;
          }
        }
      }
      if(ram16((uint16_t) (0x5D04 + y)) == 0xFFFF && p < ram16(0x5D00)) goto done;
      for(;;) {
        if(p >= ram16((uint16_t) (0x5D00 + y))) goto done;
      run_down:
        {
          uint16_t e = ram16((uint16_t) (0x5D02 + y));
          y = (uint16_t) (y - 4);
          event(e);
        }
        if(y == 0xFFFC) {
          y = (uint16_t) (z(0x5C) - 4);
          goto done;
        }
      }
    done:
      zs(0x62, z(0x3A));
      zs(0x6A, y);
    }
  }
  if(z(0x5A) == 0) return;
  y = z(0x66);
  if(p == z(0x5E)) return;
  if(p < z(0x5E)) {
    if(ram16((uint16_t) (0x5804 + y)) == 0xFFFF && p < ram16(0x5800)) goto keep;
  } else {
    y = (uint16_t) (z(0x5A) - 4);
  }
  while(p < ram16((uint16_t) (0x5800 + y))) {
    y = (uint16_t) (y - 4);
    if(y == 0xFFFC) {
      y = (uint16_t) (z(0x5A) - 4);
      break;
    }
  }
keep:
  zs(0x5E, z(0x3A));
  zs(0x66, y);
}

// Going backward: the $5800 list starts events ahead, the $5D00 list only keeps its place.
static void events_behind(void) {
  uint16_t p = event_pos();
  uint16_t y;
  if(z(0x5A) != 0) {
    y = z(0x66);
    if(p != z(0x5E)) {
      uint16_t a = p;
      if(p < z(0x5E)) {
        // Past the lap's end: every event up to the last, then from the first.
        for(;;) {
          if(ram16((uint16_t) (0x5804 + y)) == 0xFFFF) {
            if(p < ram16(0x5800)) {
              a = ram16((uint16_t) (0x5800 + y));
              goto store5e;
            }
            y = 0;
            goto run_up;
          }
          y = (uint16_t) (y + 4);
          event(ram16((uint16_t) (0x5802 + y)));
        }
      }
      for(;;) {
        a = z(0x3A);
        if(a < ram16((uint16_t) (0x5804 + y))) break;
        y = (uint16_t) (y + 4);
      run_up:
        event(ram16((uint16_t) (0x5802 + y)));
      }
      zs(0x66, y);
    store5e:
      zs(0x5E, a);
      zs(0x66, y);
    }
  }
  if(z(0x5C) == 0) return;
  y = z(0x6A);
  if(p == z(0x62)) return;
  uint16_t a = p;
  if(p < z(0x62)) {
    for(;;) {
      if(ram16((uint16_t) (0x5D04 + y)) == 0xFFFF) {
        if(p < ram16(0x5D00)) {
          a = ram16((uint16_t) (0x5D00 + y));
          goto store62;
        }
        y = 0;
        break;
      }
      y = (uint16_t) (y + 4);
    }
  }
  for(;;) {
    if(a < ram16((uint16_t) (0x5D04 + y))) break;
    y = (uint16_t) (y + 4);
    a = z(0x3A);
  }
  a = z(0x3A);
store62:
  zs(0x62, a);
  zs(0x6A, y);
}

// $08:8BDF. Moving backward along the track (speed negative): rows enter at the near end.
static void backward(void) {
  uint16_t x0 = w(0x020E);
  uint32_t f = (uint32_t) w(0x020C) + w(0x0212);
  ws(0x020C, (uint16_t) f);
  uint32_t s = (uint32_t) w(0x020E) + w(0x0214) + (f >> 16);
  uint16_t y = (uint16_t) s;
  uint16_t a = (uint16_t) s;
  if(!(s >> 16)) a = (uint16_t) (a + ram16(0x010A));
  ws(0x020E, a);
  zs(0x3A, y);
  uint16_t d = (uint16_t) (x0 - y);
  if(d != 0) {
    zs(0x3C, x0);
    zs(0x3E, d);
    zs(0x40, x0 & 3);
    uint16_t r = (uint16_t) (((x0 & 3) << 1) + w(0x0226));
    do {
      marker(r, true);
      r = (uint16_t) (r - 2);
      zs(0x40, (uint16_t) ((z(0x40) - 1) & 3));
      if(z(0x40) == 0) r = (uint16_t) (r - 8);
      zdec(0x3E);
    } while(z(0x3E) != 0);
    y = z(0x3A);
    uint16_t x = z(0x3C);
    zs(0x3A, y & 0xFFFC);
    d = (uint16_t) ((x & 0xFFFC) - z(0x3A));
  }
  if(d == 0) {
    events_ahead();
    return;
  }
  zs(0x3A, (uint16_t) (d >> 2));
  ram16_set(0x02C0, ring(0x9002, w(0x0226)));
  int level = detail();
  do {
    uint16_t p = z(0x04);
    if(p == 0) p = ram16(0x010A);
    zs(0x04, (uint16_t) (p - 4));
    p = z(0x02);
    if(p == 0) p = ram16(0x010A);
    zs(0x02, (uint16_t) (p - 4));
    uint16_t q = w(0x0226);
    if(q == 0x0080) q = 0x0280;
    q = (uint16_t) (q - 0x10);
    ws(0x0226, q);
    uint16_t ry = (uint16_t) (q - 0x80);
    road_row(z(0x04), ry, 0x24);
    if(z(0x3C) == 0x001E) {
      zs(0x10, 0);
      zs(0x0E, 0);
      zs(0x08, stream_start(z(0x3E)));
      zs(0x08, (uint16_t) (skip_count(z(0x08)) + 7 + z(0x08)));
    }
    for(uint16_t o = 0x0C; o >= 0x06; o -= 2) {
      if(leave_object(ring((uint16_t) (0x9200 + o), ry), true)) zs(0x2E, (uint16_t) ((z(0x2E) - 2) & 0x01FF));
    }
    zs(0x42, (uint16_t) ((z(0x3E) & 0x003F) << 4));
    uint16_t x = z(0x08);
    for(uint16_t o = 0x0C; o >= 0x06; o -= 2) {
      Cell c = ahead_cell(x, level);
      x = c.x;
      ring_set(o, ry, c.v);
    }
    zs(0x08, x);
    x = z(0x06);
    for(int i = 0; i < 4; i++) x = skip_cell(x, level, 0x0A, 0x0C, -4);
    zs(0x06, x);
    if((z(0x02) & 0x003C) == 0x003C) {
      zs(0x0C, 0);
      zs(0x0A, 0);
      zs(0x06, stream_start(segment(z(0x02))));
      zs(0x06, (uint16_t) (skip_count(z(0x06)) + 3 + z(0x06)));
    }
    ring_set(0x0E, ry, 0);
    ram16_set(0x02C4, ring(0x9002, w(0x0226)));
    tg_push16(0x7E00);  // pea
    tg_pull8();         // plb
    scroll_step(0x10, ram16(0x02C4), false);
    scroll_step(0x0A, (uint16_t) (ram16(0x02C0) - ram16(0x02C4)), false);
    ram16_set(0x02C0, ram16(0x02C4));
    tg_pull8();         // plb
    zdec(0x3A);
  } while(z(0x3A) != 0);
  events_ahead();
}

// $08:9A82. Moving forward (speed zero or more): rows enter at the far end.
static void forward(void) {
  uint16_t x0 = w(0x020E);
  uint32_t f = (uint32_t) w(0x020C) + w(0x0212);
  ws(0x020C, (uint16_t) f);
  uint32_t s = (uint32_t) w(0x020E) + w(0x0214) + (f >> 16);
  uint16_t y = (uint16_t) s;
  uint16_t a = (uint16_t) s;
  if((s >> 16) || a >= ram16(0x010A)) a = (uint16_t) (a - ram16(0x010A));
  ws(0x020E, a);
  zs(0x3A, x0);
  uint16_t d = (uint16_t) (y - x0);
  if(d != 0) {
    zs(0x3C, y);
    zs(0x3E, d);
    zs(0x40, x0 & 3);
    uint16_t r = (uint16_t) (((x0 & 3) << 1) + w(0x0226));
    do {
      r = (uint16_t) (r + 2);
      zs(0x40, (uint16_t) ((z(0x40) + 1) & 3));
      if(z(0x40) == 0) r = (uint16_t) (r + 8);
      marker(r, false);
      zdec(0x3E);
    } while(z(0x3E) != 0);
    uint16_t x = z(0x3A);
    y = z(0x3C);
    zs(0x3A, x & 0xFFFC);
    d = (uint16_t) ((y & 0xFFFC) - z(0x3A));
  }
  if(d == 0) {
    events_behind();
    return;
  }
  zs(0x3A, (uint16_t) (d >> 2));
  ram16_set(0x02C4, ring(0x9002, w(0x0226)));
  int level = detail();
  do {
    uint16_t p = z(0x04);
    if(p == ram16(0x010A)) p = 0;
    zs(0x04, (uint16_t) (p + 4));
    p = z(0x02);
    if(p == ram16(0x010A)) p = 0;
    zs(0x02, (uint16_t) (p + 4));
    uint16_t ry = (uint16_t) (w(0x0226) - 0x80);
    road_row(z(0x02), ry, 0x22);
    if(z(0x3C) == 0) {
      zs(0x0A, 0);
      zs(0x0C, 0);
      zs(0x06, (uint16_t) (stream_start(z(0x3E)) - 1));
    }
    uint16_t x = z(0x08);
    for(int i = 0; i < 4; i++) x = skip_cell(x, level, 0x10, 0x0E, 4);
    zs(0x08, x);
    if((z(0x04) & 0x003C) == 0) {
      zs(0x0E, 0);
      zs(0x10, 0);
      zs(0x08, (uint16_t) (stream_start(segment(z(0x04))) + 3));
    }
    for(uint16_t o = 0x06; o <= 0x0C; o += 2) {
      if(leave_object(ring((uint16_t) (0x9000 + o), ry), false)) {
        zs(0x2A, (uint16_t) ((z(0x2A) + 2) & 0x01FF));
        if(z(0x2A) == z(0x2E)) zs(0x2C, z(0x30));
        else zs(0x2C, ram16((uint16_t) (ad(0x9800) + z(0x2A))) & 0x3FFF);
      }
    }
    zs(0x42, (uint16_t) ((z(0x3E) & 0x003F) << 4));
    x = z(0x06);
    for(uint16_t o = 0x06; o <= 0x0C; o += 2) {
      Cell c = behind_cell(x, level);
      x = c.x;
      ring_set(o, ry, c.v);
    }
    zs(0x06, x);
    ring_set(0x0E, ry, 0);
    uint16_t q = w(0x0226);
    if(q == 0x0270) q = 0x0070;
    q = (uint16_t) (q + 0x10);
    ws(0x0226, q);
    ram16_set(0x02C0, ring(0x9002, q));
    tg_push16(0x7E00);  // pea
    tg_pull8();         // plb
    scroll_step(0x10, ram16(0x02C4), true);
    scroll_step(0x0A, (uint16_t) (ram16(0x02C0) - ram16(0x02C4)), true);
    ram16_set(0x02C4, ram16(0x02C0));
    tg_pull8();         // plb
    zdec(0x3A);
  } while(z(0x3A) != 0);
  events_behind();
}

// $08:A93B. The object under the car (for the shadow and the height it rides at), then the
// road's curve and the sideways scroll for the frame. Returns the last adc's carry and
// overflow for the flags.
static void finish(TgRegs* r) {
  uint16_t y = w(0x0226);
  uint16_t k = w(0x020E) & 3;
  bool hi = (w(0x020C) & 0xF800) != 0;
  static const uint16_t kLo[4] = { 0x9016, 0x9018, 0x901A, 0x901C };
  static const uint16_t kHi[4] = { 0x9018, 0x901A, 0x901C, 0x9026 };
  uint16_t v = ring(hi ? kHi[k] : kLo[k], y);
  uint16_t a = v;
  bool skip = v == 0;
  if(!skip && neg(v)) {
    if(v >= 0xC000) {
      if(v >= 0xC002) skip = true;
      else a = 0x005B;
    } else {
      uint16_t t = (uint16_t) ((ram16((uint16_t) (0x6400 + (v & 0x3FFF))) & 0x000F) << 1);
      a = ram16(bus16(0x88EACBu + t));
    }
  }
  if(!skip) {
    a &= 0x00FF;
    if(a != 0) {
      uint16_t x = (uint16_t) (a << 2);
      uint8_t bank = bus8(0x8BDBE5u + x);
      tg_push8(0x7E);  // phb
      tg_push8(bank);  // pha
      tg_pull8();      // plb
      uint16_t p = bus16(0x8BDBE3u + x);
      p = bus16((uint32_t) bank << 16 | (uint16_t) (p + 8));
      uint16_t o = bus16((uint32_t) bank << 16 | (uint16_t) (p + 2));
      tg_pull8();      // plb
      zs(0x3A, ram16((uint16_t) (0x1BF0 + o)));
      uint16_t t = ram16(ad(0x020D)) & 0x00F8;
      if(t) t = (uint16_t) (~t + 0x0101);
      zs(0x3C, bus16(0x8FCC40u + (uint16_t) (t >> 2)));
      uint16_t o8 = (uint16_t) (o << 3);
      uint16_t c = (o >> 13) & 1;
      x = (uint16_t) (o8 + z(0x3C) + c);
      uint16_t base = bus16(0x8BF345u + x);
      ws(0x0168, (uint16_t) (base + z(0x3A)));
      uint16_t h = z(0x6E);
      ws(0x016C, (uint16_t) ((uint16_t) (h << 4) + P(0x4000, 0x5000) + ((h >> 12) & 1)));
      ws(0x0170, (uint16_t) (bus16(0x8BF347u + x) - base));
    }
  }

  // The road's curve at the car: the lean of the row under it, interpolated.
  uint16_t x = (uint16_t) ((((w(0x020E) & 3) << 2) | w(0x0226)) >> 2);
  x = (uint16_t) ((uint16_t) (x + (w(0x020C) >> 15)) << 2) & 0xFFF0;
  uint16_t lean = ring(0x9002, x);
  ram16_set(0x02C4, 0);
  ram16_set(0x02C6, neg(lean) ? 0xFFFF : 0);
  ram16_set(0x02C5, lean);
  ram16_set(0x02C0, ring(0x9012, x));
  tg_push16(0x7E00);  // pea
  tg_pull8();         // plb
  x = (uint16_t) (((uint16_t) (ram16(ad(0x020D)) + 0x80) & 0x03F8) >> 3);
  m7a((uint16_t) (ram16(0x02C0) - ram16(0x02C5)));
  m7b((uint8_t) x);
  g_ram[ad(0x022E)] = mpy8(0x2136) ^ 0x80;
  g_ram[ad(0x022F)] = 0;
  uint32_t m = (uint32_t) w(0x022E) << 16 | mpy16(0x2134);
  m <<= 3;
  ws(0x022C, (uint16_t) m);
  ws(0x022E, (uint16_t) ((m >> 16) - 0x0400));
  uint32_t l = (uint32_t) ram16(0x02C6) << 16 | ram16(0x02C4);
  l <<= 2;
  ram16_set(0x02C6, (uint16_t) (l >> 16));
  uint32_t sum = (uint32_t) w(0x022C) + (uint16_t) l;
  ws(0x022C, (uint16_t) sum);
  ws(0x022E, (uint16_t) (ram16(0x02C6) + w(0x022E) + (sum >> 16)));
  uint16_t c = (w(0x022B) >> 15) & 1;
  sum = (uint32_t) w(0x022D) + c;
  ws(0x022C, (uint16_t) sum);
  ws(0x022E, (uint16_t) (sx_hi(w(0x022E)) + (sum >> 16)));

  // The sideways scroll at the car: the lean times the fraction through the row.
  ram16_set(0x02C4, ring(0x9002, w(0x0226)));
  ram16_set(0x02C0, ring(0x9012, w(0x0226)));
  x = (uint16_t) ((ram16(ad(0x020D)) & 0x03F8) >> 2);
  uint16_t sgn = neg(ram16(0x02C4)) ? 0xFFFF : 0;
  zs(0x3A, sgn);
  m7a(ram16(0x02C4));
  m7b((uint8_t) ((x & 0xFF) >> 1));
  uint8_t hb = mpy8(0x2136);
  zbs(0x3A, hb);
  if(hb == 0) zbs(0x3B, 0);
  uint32_t q = (uint32_t) z(0x3A) << 16 | mpy16(0x2134);
  q <<= 5;
  zs(0x3A, (uint16_t) (q >> 16));
  ws(0x021E, (uint16_t) q);
  ws(0x0220, z(0x3A));
  m7a((uint16_t) (ram16(0x02C0) - ram16(0x02C4)));
  m7b(bus8(0x88E983u + x));
  sum = (uint32_t) mpy16(0x2134) + w(0x021E);
  ws(0x021E, (uint16_t) sum);
  ws(0x0220, (uint16_t) (sx_hi(mpy16(0x2135)) + w(0x0220) + (sum >> 16)));
  m7b(bus8(0x88E984u + x));
  uint16_t b8 = (uint16_t) (mpy8(0x2134) + g_ram[ad(0x021F)]);
  g_ram[ad(0x021F)] = (uint8_t) b8;
  sum = (uint32_t) mpy16(0x2135) + w(0x0220) + (b8 >> 8);
  ws(0x0220, (uint16_t) sum);
  c = (w(0x021D) >> 15) & 1;
  sum = (uint32_t) w(0x021F) + w(0x021A) + c;
  ws(0x0216, (uint16_t) sum);
  uint16_t s1 = sx_hi(w(0x0220)), s2 = w(0x021C);
  uint32_t res = (uint32_t) s1 + s2 + (sum >> 16);
  ws(0x0218, (uint16_t) res);
  tg_pull8();  // plb
  r->c = (res >> 16) != 0;
  r->v = (~(s1 ^ s2) & (s1 ^ (uint16_t) res) & 0x8000) != 0;
}

static void run(TgRegs* r, bool p2) {
  set_player(p2);
  tg_push8(r->db);   // phb
  tg_push16(r->d);   // phd
  tg_push16(0x0000);  // pea
  tg_pull16();       // plb plb
  if(neg(w(0x0214))) {
    tg_push16(0x7E7E);
    tg_pull16();
    backward();
  } else {
    tg_push16(0x7E7E);
    tg_pull16();
    forward();
  }
  finish(r);
  tg_pull16();  // pld
  tg_pull8();   // plb
  r->n = (r->db & 0x80) != 0;
  r->z = r->db == 0;
}

// $08:8BCA, player 1.
void Tg2Stream1(TgRegs* r) { run(r, false); }
// $08:AB9F, player 2.
void Tg2Stream2(TgRegs* r) { run(r, true); }
