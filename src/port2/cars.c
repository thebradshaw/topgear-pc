// Top Gear 2: the race's cars each frame ($00:D726): the countdown lights, each car's
// update (by its state), the running order, the cars that cross the line (laps, finishing
// times) and the players' race positions.
//
// The 20 cars' records are $40 bytes each at $7E:C400 + n * $40: $18:$1A distance round
// the lap, $1C state, $22 laps, $26 grid place, $10 driver (0 and 1: the players), $2B
// flags. The running order is a list of record offsets at $7E:C3E8, last car first.

#include "../port/port.h"

#define ORDER 0xC3E8u  // 20 record offsets, last place first
#define NCARS 20

static uint16_t w(uint16_t a) { return ram16(a); }
static void ws(uint16_t a, uint16_t v) { ram16_set(a, v); }
static uint16_t car(int i) { return ram16((uint16_t) (ORDER + 2 * i)); }
static void set_car(int i, uint16_t v) { ram16_set((uint16_t) (ORDER + 2 * i), v); }
static bool neg(uint16_t v) { return (v & 0x8000) != 0; }

// Original code in bank $00 with D = $2100, DB = $7E, as the routine runs it.
static void jsr(TgRegs* r, uint16_t target, uint16_t site) {
  r->d = 0x2100;
  r->db = 0x7E;
  tg_call_jsr(r, target, (uint16_t) (site + 3), TG_MX_16);
}

// The countdown and the start ($D72D-$D7FC). Returns false when the routine stops here.
static bool countdown(void) {
  uint16_t a = w(0x06F4);
  if(a == 0) return true;
  if(a < 0x003C) {
    a = (uint16_t) (a - w(0x00F8));
    ws(0x06F4, a);
    if(a == 0 || neg(a)) {
      ws(0x06F4, 0);
      if(w(0xFF1A) == 0) {
        ws(0x0A12, 0);
        ws(0x0A2E, 0);
      }
    }
    return true;
  }
  a = w(0x0634) | w(0x0636);
  if(!(a & 0x0010)) {
    a = (uint16_t) (w(0x06F4) - w(0x00F8));
    ws(0x06F4, a);
    if(a < 0x003C) {
      // Green light.
      ws(0x0602, w(0x001A));
      ws(0x0634, w(0x0634) & 0x7FFF);
      ws(0x0636, w(0x0636) & 0x7FFF);
      ws(0x05A3, 0x003F);
      ws(0x05A1, 0xFF1A);
      if(w(0xFF1A) == 0) {
        ws(0x0A12, 1);
        ws(0x0A2E, 1);
      }
      return false;
    }
  }
  // The lights: 2, 3 and 4 as the count goes down, 0 before them.
  uint16_t x;
  if(a < 0x0078) x = 2;
  else if(a < 0x00B4) x = 3;
  else if(a < 0x00F0) x = 4;
  else {
    x = 0;
    if(a >= 0x012C) {
      if(w(0xFF1A) != 0) {
        ws(0x0A16, 0x0029);
        ws(0x0A32, 0x0029);
      } else {
        ws(0x0A12, 5);
        ws(0x0A2E, 6);
      }
      return false;
    }
    goto lights;
  }
  if(x != w(0x0676)) {
    ws(0x05A3, 0x003F);
    ws(0x05A1, 0xFF19);
  }
lights:
  ws(0x0676, x);
  if(w(0xFF1A) == 0) {
    ws(0x0A12, x);
    ws(0x0A2E, x);
  }
  return false;
}

// A pair in the running order ($D9E3 and the 18 below it): x is behind y in the list.
// The 32-bit compare leaves the high word's difference and its carry, which $E890 reads.
static uint16_t compare(TgRegs* r, uint16_t x, uint16_t y) {
  bool c = w((uint16_t) (0xC418 + y)) >= w((uint16_t) (0xC418 + x));
  uint16_t hy = w((uint16_t) (0xC41A + y)), hx = w((uint16_t) (0xC41A + x));
  r->a = (uint16_t) (hy - hx - !c);
  r->c = (uint32_t) hx + !c <= hy;
  r->x = x;
  r->y = y;
  return r->a;
}

// Sorts the running order: one bubble pass from the front, each car moved forward carried
// on up while it keeps passing ($E890 says whether it may), $E808 for dead heats. $06DE
// counts the steps for them.
static void sort(TgRegs* r) {
  ws(0x06DE, 0);
  int j = NCARS - 2;
  uint16_t x = car(j);
  for(;;) {
    uint16_t y = car(j + 1);
    uint16_t d = compare(r, x, y);
    // The call sites: level j's code is $29 bytes long, the top one at $D9E3.
    uint16_t base = (uint16_t) (0xD9E3 + (NCARS - 2 - j) * 0x29);
    if(d == 0) {
      jsr(r, 0xE808, (uint16_t) (base + 0x20));
    } else if(neg(d)) {
      jsr(r, 0xE890, (uint16_t) (base + 0x13));
      if(!r->c) {
        x = r->x;  // as $E890 left them
        set_car(j, r->y);
        set_car(j + 1, x);
        if(j < NCARS - 2) {
          j++;
          continue;
        }
      }
    }
    if(j == 0) break;
    ws(0x06DE, (uint16_t) (w(0x06DE) + 1));
    j--;
    x = car(j);
  }
}

// A car crossed the line ($DE23): it goes to the back of the order (the front car is the
// one at the end), with a lap more; on the last lap it gets its finishing time.
static void crossed_line(TgRegs* r) {
  uint16_t x = car(NCARS - 1);
  for(int i = NCARS - 1; i > 0; i--) set_car(i, car(i - 1));
  set_car(0, x);
  uint16_t laps = (uint16_t) (w((uint16_t) (0xC422 + x)) + 1);
  ws((uint16_t) (0xC422 + x), laps);
  if((uint16_t) (laps - 1) != w(0x019E)) return;
  uint16_t flags = w((uint16_t) (0xC42B + x));
  if(flags & 0x0002) return;
  ws((uint16_t) (0xC42B + x), flags | 0x0002);
  uint16_t who = w((uint16_t) (0xC410 + x));
  if(who == 0 || (who == 1 && neg(w(0x0102)))) {
    // A player finished: $E6F8 (player 1) or $E726 (player 2).
    r->x = x;
    jsr(r, who == 0 ? 0xE6F8 : 0xE726, who == 0 ? 0xDEBD : 0xDEE2);
    ws(0x06F0, (uint16_t) (w(0x06F0) + 1));
    uint16_t bit = who == 0 ? 0x0634 : 0x0636;
    ws(bit, w(bit) | 0x8000);
    uint16_t n = w(0x06EE);
    if(n < 0x0014) ws(who == 0 ? 0x0A16 : 0x0A32, (uint16_t) ((n >> 1) + 1));
  }
  // The results table: driver and time in finishing order; when all ten are in, every car
  // from tenth place down gets its place with no time.
  uint16_t n = w(0x06EE);
  if(n >= 0x0014) return;
  ws((uint16_t) (0xFE9E + n), w((uint16_t) (0xC410 + x)));
  ws((uint16_t) (0xFEC6 + n), (uint16_t) (w(0x001A) - w(0x0602)));
  n = (uint16_t) (n + 2);
  ws(0x06EE, n);
  if(n != 0x0014) return;
  for(uint16_t c = 0; c != 0x0500; c = (uint16_t) (c + 0x40)) {
    uint16_t place = w((uint16_t) (0xC426 + c));
    if(place < 0x000A) continue;
    uint16_t y = (uint16_t) (place << 1);
    ws((uint16_t) (0xFE9E + y), w((uint16_t) (0xC410 + c)));
    ws((uint16_t) (0xFEC6 + y), 0);
  }
}

// A car went back over the line ($E006): it goes to the front of the order a lap down.
static void backed_over_line(void) {
  uint16_t x = car(0);
  for(int i = 0; i < NCARS - 1; i++) set_car(i, car(i + 1));
  set_car(NCARS - 1, x);
  ws((uint16_t) (0xC41A + x), (uint16_t) (w((uint16_t) (0xC41A + x)) + w(0x010A)));
  ws((uint16_t) (0xC422 + x), (uint16_t) (w((uint16_t) (0xC422 + x)) - 1));
}

// Whether car k in the order is level with or ahead of a player at pos ($0614 or $0618).
static bool ahead(int k, uint16_t pos) {
  uint16_t x = car(k);
  bool c = w((uint16_t) (0xC418 + x)) >= w(pos);
  return !neg((uint16_t) (w((uint16_t) (0xC41A + x)) - w((uint16_t) (pos + 2)) - !c));
}

// A player's place in the order, times 2, by a search down the list ($E0B8). The original
// asks car 10 where it means car 5 in one branch; kept.
static uint16_t place(uint16_t pos) {
  uint16_t y;
  if(!ahead(9, pos)) {
    if(!ahead(14, pos)) {
      if(!ahead(17, pos)) {
        y = 0x24;
        if(!ahead(18, pos)) y += 2;
      } else if(!ahead(16, pos)) {
        y = 0x22;
      } else {
        y = 0x1E;
        if(!ahead(15, pos)) y += 2;
      }
    } else if(!ahead(12, pos)) {
      y = 0x1A;
      if(!ahead(13, pos)) y += 2;
    } else if(!ahead(11, pos)) {
      y = 0x18;
    } else {
      y = 0x14;
      if(!ahead(10, pos)) y += 2;
    }
  } else if(!ahead(4, pos)) {
    if(!ahead(7, pos)) {
      y = 0x10;
      if(!ahead(8, pos)) y += 2;
    } else if(!ahead(6, pos)) {
      y = 0x0E;
    } else {
      y = 0x0A;
      if(!ahead(10, pos)) y += 2;
    }
  } else if(!ahead(2, pos)) {
    y = 0x06;
    if(!ahead(3, pos)) y += 2;
  } else if(!ahead(1, pos)) {
    y = 0x04;
  } else {
    y = 0x00;
    if(!ahead(0, pos)) y += 2;
  }
  return y;
}

static uint16_t clamp_lap(uint16_t a) {
  if(neg((uint16_t) (a - 1))) a = 1;
  if(neg((uint16_t) (a - w(0x019E)))) return a;
  return w(0x019E);
}

void Tg2Cars(TgRegs* r) {
  tg_push8(r->db);  // phb
  tg_push16(r->d);  // phd
  if(!countdown()) goto done;

  // Where the views start drawing cars: 32 behind each player.
  {
    uint16_t len = w(0x010A);
    uint16_t a = w(0x020E);
    bool c = a >= 0x0020;
    a = (uint16_t) (a - 0x0020);
    if(neg(a)) {
      c = (uint32_t) a + len > 0xFFFF;
      a = (uint16_t) (a + len);
    }
    ws(0x06EC, a);
    a = w(0x024A);
    c = a >= 0x0020;
    a = (uint16_t) (a - 0x0020);
    if(neg(a)) {
      c = (uint32_t) a + len > 0xFFFF;
      a = (uint16_t) (a + len);
    }
    ws(0x06EA, a);
    r->c = c;
  }
  tg_bus_write8(0x211B, g_ram[0x061D]);
  tg_bus_write8(0x211B, g_ram[0x061E]);
  tg_bus_write8(0x211C, g_ram[0x06D4]);
  ws(0x0610, (uint16_t) (tg_bus_read8(0x2135) | tg_bus_read8(0x2136) << 8));
  // Each car's update by its state ($80:E417).
  for(int i = 0; i < NCARS; i++) {
    uint16_t y = (uint16_t) (i * 0x40);
    ws((uint16_t) (0xC42C + y), w((uint16_t) (0xC418 + y)));
    r->a = w((uint16_t) (0xC41A + y));
    ws((uint16_t) (0xC42E + y), r->a);
    r->x = w((uint16_t) (0xC41C + y));
    r->y = y;
    jsr(r, rom16(0x80E417 + r->x), (uint16_t) (0xD84B + i * 0x15));
  }

  for(;;) {
    sort(r);
    // Cars that crossed the line, counted from the front.
    int n = 0;
    while(n < NCARS) {
      uint16_t x = car(NCARS - 1 - n);
      uint16_t a = (uint16_t) (w((uint16_t) (0xC41A + x)) - w(0x010A));
      if(neg(a)) break;
      ws((uint16_t) (0xC41A + x), a);
      n++;
    }
    if(n) {
      while(n--) crossed_line(r);
      continue;
    }
    // Cars that went back over it, counted from the back.
    while(n < NCARS && neg(w((uint16_t) (0xC41A + car(n))))) n++;
    if(!n) break;
    while(n--) backed_over_line();
  }

  // Who's where: player 1 ($0136), player 2 ($0138), the grid's first car ($013A).
  for(uint16_t y = 0; y != 0x0028; y = (uint16_t) (y + 2)) {
    uint16_t a = w((uint16_t) (ORDER + y));
    if(a == 0) ws(0x0136, y);
    if(a == 0x0040) ws(0x0138, y);
    if(w((uint16_t) (0xC426 + a)) == 0) ws(0x013A, y);
  }
  ws(0x060C, place(0x0614));
  ws(0x060E, place(0x0618));
  ws(0x01A0, w(0xC426));
  ws(0x01BA, w(0xC466));
  ws(0x01A2, clamp_lap(w(0xC422)));
  ws(0x01BC, clamp_lap(w(0xC462)));
done:
  r->d = tg_pull16();
  r->db = tg_pull8();
}
