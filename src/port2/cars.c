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
static uint8_t rd(uint16_t a) { return tg_bus_read8(a); }
static uint16_t rd16(uint16_t a) { return (uint16_t) (rd(a) | rd((uint16_t) (a + 1)) << 8); }
static void m7a(uint16_t v) {
  tg_bus_write8(0x211B, (uint8_t) v);
  tg_bus_write8(0x211B, (uint8_t) (v >> 8));
}
static void m7b(uint8_t v) { tg_bus_write8(0x211C, v); }
static uint16_t f(uint16_t y, uint16_t field) { return w((uint16_t) (0xC400 + y + field)); }
static void fs(uint16_t y, uint16_t field, uint16_t v) { ws((uint16_t) (0xC400 + y + field), v); }


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

// --- Passing ($E890) and dead heats ($E808) ---

static bool two_player(void) { return neg(w(0x0102)); }
static uint16_t absw(uint16_t v) { return neg(v) ? (uint16_t) (0 - v) : v; }

// Jumps into original code that ends with the caller's RTS (a player's car in a bump).
static void jump(TgRegs* r, uint16_t target, uint16_t site, uint16_t x, uint16_t y) {
  r->x = x;
  r->y = y;
  jsr(r, target, site);
}

// A player catching car y: the engine-note sample for the overtake ($BB07 with table $F293)
// when the player is clearly faster.
static void overtake_sound(TgRegs* r, uint16_t speed, uint16_t y, uint16_t out, uint16_t site, uint16_t x) {
  m7a(w(speed));
  m7b(0x70);
  uint16_t a = (uint16_t) ((uint16_t) ((uint16_t) (rd16(0x2135) + w(speed)) << 2) - f(y, 0x1F));
  if(neg(a)) return;
  tg_push16(x);
  tg_push16(y);
  r->a = 0x001E;
  r->x = 0xF293;
  r->y = y;
  r->c = a >= 0x0100;
  jsr(r, 0xBB07, site);
  tg_pull16();
  tg_pull16();
  ws(out, r->a);
}

// Car x stays behind car y ($E93E): it's put just behind it and they swap speeds, or a
// player's car gets the bump.
static void stay_behind(TgRegs* r, uint16_t x, uint16_t y, uint16_t site) {
  uint16_t lo = f(y, 0x18);
  fs(x, 0x18, (uint16_t) (lo - 0x8C00));
  fs(x, 0x1A, (uint16_t) (f(y, 0x1A) - (lo < 0x8C00)));
  if(y == 0) return jump(r, 0xE984, site, x, y);
  if(y == 0x40 && two_player()) return jump(r, 0xEA14, site, x, y);
  for(uint16_t k = 0x1E; k <= 0x20; k = (uint16_t) (k + 2)) {
    uint16_t t = f(x, k);
    fs(x, k, f(y, k));
    fs(y, k, t);
  }
  r->x = x;
  r->y = y;
  r->c = true;
}

// Car x has caught car y: it passes unless they're side by side. Carry set: it didn't.
static void try_pass(TgRegs* r, uint16_t x, uint16_t y, uint16_t site) {
  if(absw((uint16_t) (f(x, 0x14) - f(y, 0x14))) < 0x0026) {
    if(x == 0) return jump(r, 0xEAA4, site, x, y);
    if(x == 0x40 && two_player()) return jump(r, 0xED4D, site, x, y);
    return stay_behind(r, x, y, site);
  }
  if(x == 0) overtake_sound(r, 0x0610, y, 0x0A1E, 0xE8D3, x);
  if(x == 0x40) overtake_sound(r, 0x0612, y, 0x0A3A, 0xE909, x);
  // Level on laps: they swap grid places too.
  if(f(x, 0x22) == f(y, 0x22)) {
    uint16_t t = f(y, 0x26);
    fs(y, 0x26, f(x, 0x26));
    fs(x, 0x26, t);
  }
  r->x = x;
  r->y = y;
  r->c = false;
}

// Cars x and y level on the lap: if they touch, they move apart sideways, or the one that
// was behind stays there.
static void dead_heat(TgRegs* r, uint16_t x, uint16_t y, uint16_t site) {
  r->x = x;
  r->y = y;
  r->c = false;
  if((uint16_t) (f(y, 0x18) - f(x, 0x18)) >= 0x8C00) return;
  if(absw((uint16_t) (f(x, 0x14) - f(y, 0x14))) >= 0x0026) return;
  if(x == 0) return jump(r, 0xEAA4, site, x, y);
  if(x == 0x40 && two_player()) return jump(r, 0xED4D, site, x, y);
  uint16_t ylo = f(y, 0x2C), xlo = f(x, 0x2C);
  ws(0x06E0, (uint16_t) (ylo - xlo));
  uint16_t hi = (uint16_t) (f(y, 0x2E) - f(x, 0x2E) - (ylo < xlo));
  if(hi != 0 || (uint16_t) (ylo - xlo) >= 0x8C00) return stay_behind(r, x, y, site);
  uint16_t d = (uint16_t) (f(x, 0x14) - f(y, 0x14));
  uint16_t mid = (uint16_t) ((neg(d) ? (d >> 1 | 0x8000) : d >> 1) + f(y, 0x14));
  fs(neg(d) ? y : x, 0x14, (uint16_t) (mid + 0x13));
  fs(neg(d) ? x : y, 0x14, (uint16_t) (mid - 0x13));
  r->c = false;
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
      dead_heat(r, x, y, (uint16_t) (base + 0x20));
    } else if(neg(d)) {
      try_pass(r, x, y, (uint16_t) (base + 0x13));
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

// --- Each car's update ($80:E417 by state) ---

// Whether the car is within $60 ahead of where a view starts drawing cars ($06EC, and
// $06EA in two-player games). far: its distance is $60 or more.
static bool near_view(uint16_t a) {
  uint16_t len = w(0x010A);
  if(a < 0x0060) {
    uint16_t d = (uint16_t) (a - w(0x06EC));
    if(d < 0x0060 || (uint16_t) (d + len) < 0x0060) return true;
    if(!neg(w(0x0102))) return false;
    d = (uint16_t) (a - w(0x06EA));
    return d < 0x0060 || (uint16_t) (d + len) < 0x0060;
  }
  if((uint16_t) (a - w(0x06EC)) < 0x0060) return true;
  if(!neg(w(0x0102))) return false;
  return (uint16_t) (a - w(0x06EA)) < 0x0060;
}

// The sideways move: $C428 (speed across) times frames into $C42A:$C414, kept between
// the road's edges at this distance ($7E:2000 left, $7E:3300 right). Starting cars bounce
// off an edge (the speed turns round); racing ones bounce off the edge they cross the
// wrong way.
static void sideways(uint16_t y, bool racing) {
  uint16_t x = (uint16_t) (f(y, 0x1A) << 1);
  m7a(f(y, 0x28));
  m7b(g_ram[0x00F8]);
  unsigned lo = rd(0x2134) + g_ram[(uint16_t) (0xC42A + y)];
  g_ram[(uint16_t) (0xC42A + y)] = (uint8_t) lo;
  uint16_t a = rd16(0x2135);
  uint16_t left = w((uint16_t) (0x2000 + x)), right = w((uint16_t) (0x3300 + x));
  bool back = neg(a);
  a = (uint16_t) (a + f(y, 0x14) + (lo >> 8));
  fs(y, 0x14, a);
  bool turn = false;
  if(!racing) {
    if(back) {
      if(neg((uint16_t) (a - left))) {
        fs(y, 0x14, left);
        turn = true;
      }
    } else if(!neg((uint16_t) (a - right))) {
      fs(y, 0x14, right);
      turn = true;
    }
  } else if(back) {
    if(neg((uint16_t) (a - left))) {
      fs(y, 0x14, left);
      turn = true;
    } else if(!neg((uint16_t) (a - right))) {
      fs(y, 0x14, right);
    }
  } else if(!neg((uint16_t) (a - right))) {
    fs(y, 0x14, right);
    turn = true;
  } else if(neg((uint16_t) (a - left))) {
    fs(y, 0x14, left);
  }
  if(turn) fs(y, 0x28, (uint16_t) (0 - f(y, 0x28)));
}

// Adds rate * frames to the speed $C41E:$C41F (the low byte rounds).
static void accelerate(uint16_t y, uint16_t rate, bool doubled) {
  m7a(rate);
  m7b((uint8_t) (doubled ? g_ram[0x00F8] << 1 : g_ram[0x00F8]));
  unsigned lo = rd(0x2134) + g_ram[(uint16_t) (0xC41E + y)];
  g_ram[(uint16_t) (0xC41E + y)] = (uint8_t) lo;
  fs(y, 0x1F, (uint16_t) (rd16(0x2135) + f(y, 0x1F) + (lo >> 8)));
}

// The car moves on by speed a times frames * 2 / 256.
static void advance(uint16_t y, uint16_t a) {
  m7a(a);
  m7b((uint8_t) (g_ram[0x00F8] << 1));
  g_ram[0x06E3] = 0;
  uint8_t hi = rd(0x2136);
  if(hi & 0x80) g_ram[0x06E3] = 0xFF;
  g_ram[0x06E2] = hi;
  ws(0x06E0, rd16(0x2134));
  uint32_t s = (uint32_t) f(y, 0x18) + w(0x06E0);
  fs(y, 0x18, (uint16_t) s);
  fs(y, 0x1A, (uint16_t) (f(y, 0x1A) + w(0x06E2) + (s >> 16)));
}

// State 8 ($E424): off the line, up to racing speed ($C432), then state 2.
static bool starting(uint16_t y) {
  uint16_t a = f(y, 0x1A);
  // The original's check for far cars here jumps into the middle of an instruction; run it.
  if(a >= 0x0060 && (uint16_t) (a - w(0x06EC)) >= 0x0060) return false;
  if(near_view(a)) {
    fs(y, 0x2B, f(y, 0x2B) | 1);
    sideways(y, false);
  } else {
    fs(y, 0x2B, f(y, 0x2B) & 0xFFFE);
  }
  accelerate(y, f(y, 0x30), false);
  a = f(y, 0x1F);
  if(!neg((uint16_t) (a - f(y, 0x32)))) fs(y, 0x1C, 0x0002);
  advance(y, f(y, 0x1F));
  return true;
}

// State 2 ($E541): racing: speed towards the target $C424 by the curves at $80:F093
// (speeding up) and $80:F193 (slowing down).
static void racing(uint16_t y) {
  if(near_view(f(y, 0x1A))) {
    fs(y, 0x2B, f(y, 0x2B) | 1);
    sideways(y, true);
  } else {
    fs(y, 0x2B, f(y, 0x2B) & 0xFFFE);
  }
  uint16_t d = (uint16_t) (f(y, 0x24) - f(y, 0x1F));
  if(d != 0) {
    if(!neg(d)) {
      accelerate(y, rom16(0x80F093 + ((d >> 8) & 0xFE)), true);
      if(neg((uint16_t) (f(y, 0x24) - f(y, 0x1F)))) fs(y, 0x1F, f(y, 0x24));
    } else {
      d = (uint16_t) (0 - d);
      accelerate(y, rom16(0x80F193 + ((d >> 8) & 0xFE)), true);
      if(!neg((uint16_t) (f(y, 0x24) - f(y, 0x1F)))) fs(y, 0x1F, f(y, 0x24));
    }
  }
  advance(y, f(y, 0x1F));
}

// States 4 and 6 ($E6C4/$E6DE): a player's car record follows that player.
static void follow(uint16_t y, uint16_t moved, uint16_t across) {
  uint32_t s = (uint32_t) f(y, 0x18) + w(moved);
  fs(y, 0x18, (uint16_t) s);
  fs(y, 0x1A, (uint16_t) (f(y, 0x1A) + w((uint16_t) (moved + 2)) + (s >> 16)));
  fs(y, 0x14, w(across));
}

// Runs car y's update in C; false for the ones left to the original (state $0A, the
// finished cars' slow-down, isn't in the recordings).
static bool update_car(uint16_t y, uint16_t state) {
  switch(state) {
    case 0x0: return true;
    case 0x2: racing(y); return true;
    case 0x4: follow(y, 0x0212, 0x0205); return true;
    case 0x6: follow(y, 0x024E, 0x0241); return true;
    case 0x8: return starting(y);
    default: return false;
  }
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
    if(!update_car(y, r->x)) jsr(r, rom16(0x80E417 + r->x), (uint16_t) (0xD84B + i * 0x15));
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
