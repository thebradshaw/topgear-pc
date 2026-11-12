// Top Gear 2: a player's car each frame (bank $00): gears, engine speed, road speed,
// steering and grip, the roadside objects it touches, the track edges and the engine and
// skid sounds. Player 1's copy is at $00:8A41, player 2's at $00:A2A5; they are the same
// code on different variables (the direct page is $0600 for both), so this file runs both
// through an address map.

#include <stddef.h>

#include "../port/port.h"

#define DPB 0x0600u

// Player 2's addresses for player 1's ones (direct page offsets and absolute words).
static const uint8_t kDp2[][2] = {
  { 0x06, 0x0A }, { 0x10, 0x12 }, { 0x11, 0x13 }, { 0x14, 0x18 }, { 0x16, 0x1A }, { 0x1C, 0x20 },
  { 0x1D, 0x21 }, { 0x1E, 0x22 }, { 0x24, 0x28 }, { 0x25, 0x29 }, { 0x2C, 0x2E }, { 0x30, 0x32 },
  { 0x34, 0x36 }, { 0x38, 0x3C }, { 0x39, 0x3D }, { 0x40, 0x42 }, { 0x46, 0x44 }, { 0x48, 0x4A },
  { 0x4C, 0x4E }, { 0x50, 0x52 }, { 0x54, 0x58 }, { 0x56, 0x5A }, { 0x5C, 0x5E }, { 0x60, 0x62 }, { 0x64, 0x66 },
  { 0x68, 0x6A }, { 0x6C, 0x6E }, { 0x70, 0x72 }, { 0x7A, 0x9C }, { 0x7C, 0x9E }, { 0x7E, 0xA0 },
  { 0x80, 0xA2 }, { 0x82, 0xA4 }, { 0x84, 0xA6 }, { 0x86, 0xA8 }, { 0x88, 0xAA }, { 0x8A, 0xAC },
  { 0x8C, 0xAE }, { 0x8E, 0xB0 }, { 0x90, 0xB2 }, { 0x92, 0xB4 }, { 0x94, 0xB6 }, { 0x96, 0xB8 }, { 0x97, 0xB9 }, { 0xBC, 0xBE },
  { 0xC0, 0xC2 }, { 0xC4, 0xC6 }, { 0xCA, 0xCB }, { 0xCC, 0xCE },
};
static const uint16_t kAbs2[][2] = {
  { 0x0028, 0x002A }, { 0x0030, 0x0032 }, { 0x0178, 0x017A }, { 0x017C, 0x017E }, { 0x0184, 0x0186 },
  { 0x018C, 0x018E }, { 0x0190, 0x0192 }, { 0xFE38, 0xFE61 }, { 0xFE3C, 0xFE65 }, { 0xFE9A, 0xFE9C },
  { 0x019A, 0x019C }, { 0x01A4, 0x01BE }, { 0x01A6, 0x01C0 }, { 0x01A8, 0x01C2 }, { 0x01AA, 0x01C4 },
  { 0x01AE, 0x01C8 }, { 0x01B2, 0x01CC }, { 0x01B4, 0x01CE }, { 0x01B6, 0x01D0 }, { 0x01B8, 0x01D2 },
  { 0x0204, 0x0240 }, { 0x0206, 0x0242 }, { 0x0212, 0x024E }, { 0x0214, 0x0250 }, { 0x0226, 0x0262 },
  { 0x022C, 0x0268 }, { 0x0238, 0x0274 }, { 0x0374, 0x039C }, { 0x0376, 0x039E }, { 0x0396, 0x03BE },
  { 0x050A, 0x0512 }, { 0x059D, 0x05A0 }, { 0x0610, 0x0612 }, { 0x0611, 0x0613 }, { 0x061B, 0x061F },
  { 0x0634, 0x0636 }, { 0x0639, 0x063D }, { 0x0648, 0x064A }, { 0x064C, 0x064E }, { 0x0670, 0x0672 },
  { 0x0678, 0x069A }, { 0x0698, 0x06BA }, { 0x06C8, 0x06C9 }, { 0x0A1E, 0x0A3A },
  { 0xC418, 0xC458 }, { 0xC419, 0xC459 }, { 0xC41A, 0xC45A }, { 0xC42B, 0xC46B },
};

static bool g_p2;
uint32_t tg2_rumble_bump[2], tg2_rumble_nitro[2];
static uint8_t g_dp[256];
static TgRegs* g_r;

static void set_player(bool p2) {
  g_p2 = p2;
  for(int i = 0; i < 256; i++) g_dp[i] = (uint8_t) i;
  if(p2)
    for(size_t i = 0; i < sizeof kDp2 / sizeof kDp2[0]; i++) g_dp[kDp2[i][0]] = kDp2[i][1];
}
static uint16_t ad(uint16_t a) {
  if(g_p2)
    for(size_t i = 0; i < sizeof kAbs2 / sizeof kAbs2[0]; i++)
      if(kAbs2[i][0] == a) return kAbs2[i][1];
  return a;
}
static uint16_t P(uint8_t p1, uint8_t p2) { return g_p2 ? p2 : p1; }

static uint16_t z(uint8_t o) { return ram16(DPB + g_dp[o]); }
static void zs(uint8_t o, uint16_t v) { ram16_set(DPB + g_dp[o], v); }
static uint8_t zb(uint8_t o) { return g_ram[DPB + g_dp[o]]; }
static void zbs(uint8_t o, uint8_t v) { g_ram[DPB + g_dp[o]] = v; }
static void zor(uint8_t o, uint16_t m) { zs(o, (uint16_t) (z(o) | m)); }
static void zclr(uint8_t o, uint16_t m) { zs(o, (uint16_t) (z(o) & ~m)); }
static uint16_t w(uint16_t a) { return ram16(ad(a)); }
static void ws(uint16_t a, uint16_t v) { ram16_set(ad(a), v); }
static uint8_t wb(uint16_t a) { return g_ram[ad(a)]; }
static void wbs(uint16_t a, uint8_t v) { g_ram[ad(a)] = v; }
static uint16_t frames(void) { return ram16(0x00F8); }
static uint8_t frames8(void) { return g_ram[0x00F8]; }
// Track data for the car's position ($7E:9000, player 2's at $7E:9400).
static uint16_t track(uint16_t a) { return ram16((uint16_t) ((g_p2 ? 0x9400 : 0x9000) + a)); }
static uint8_t track8(uint16_t a) { return g_ram[(uint16_t) ((g_p2 ? 0x9400 : 0x9000) + a)]; }

static bool neg(uint16_t v) { return (v & 0x8000) != 0; }
static bool lt(uint16_t a, uint16_t b) { return neg((uint16_t) (a - b)); }
static uint16_t absw(uint16_t v) { return neg(v) ? (uint16_t) (0 - v) : v; }
static uint16_t asr(uint16_t v) { return (uint16_t) ((v >> 1) | (v & 0x8000)); }

// The multiplier: M7A (16 bits, written low then high) times M7B (8 bits, signed).
static uint8_t rd(uint16_t a) { return tg_bus_read8(a); }
static uint16_t rd16(uint16_t a) { return (uint16_t) (rd(a) | rd((uint16_t) (a + 1)) << 8); }
static void m7a(uint16_t v) {
  tg_bus_write8(0x211B, (uint8_t) v);
  tg_bus_write8(0x211B, (uint8_t) (v >> 8));
}
static void m7b(uint8_t v) { tg_bus_write8(0x211C, v); }
// The product / 256 rounded, times 2 (lda $2134; asl; lda $2135; rol).
static uint16_t prod_x2(void) {
  bool c = (rd(0x2134) & 0x80) != 0;
  return (uint16_t) (rd16(0x2135) << 1 | c);
}

// Calls player 1's (or 2's) copy of a helper in bank $00, from the call site p1_site.
static uint16_t call(uint16_t p1_target, uint16_t p1_site, uint16_t a, uint16_t x) {
  TgRegs* r = g_r;
  uint16_t target = p1_target, ret = (uint16_t) (p1_site + 3);
  if(g_p2) {
    if(p1_target != 0xBB07) target = (uint16_t) (p1_target + 0x1862);
    ret = (uint16_t) (ret + (p1_site < 0x9093 ? 0x1864 : 0x1862));
  }
  r->a = a;
  r->x = x;
  r->d = DPB;
  tg_call_jsr(r, target, ret, TG_MX_16);
  return r->a;
}
static void sound(uint16_t id, uint16_t site, uint16_t x) { call(0xA29B, site, id, x); }

// --- What the car meets on the road ($9BFD) ---
//
// The track data lists things by distance: roadside objects to hit, wall sections, and
// things lying on the road (pick-ups, puddles and the like, kept at $7E:6400).

static uint16_t g_y;  // the Y register, which the random numbers mix in

// The car's speed across the road and its heading ($1C-$1E, 24 bits) halved.
static void halve_turn(void) {
  uint32_t v = (uint32_t) z(0x1C) | (uint32_t) zb(0x1E) << 16;
  v = (v >> 1) | (v & 0x800000);
  zs(0x1C, (uint16_t) v);
  zbs(0x1E, (uint8_t) (v >> 16));
}

// A message for the panel (the picture, and how fast it plays).
static void say(uint16_t picture, uint16_t pace) {
  ws(0x018C, 0xFFFF);
  ws(0x0190, pace);
  ws(0x0184, picture);
  ws(0x017C, 0);
}

static void add_damage(uint16_t at, uint16_t v) {
  if(w(at) < 0x2000) ws(at, (uint16_t) (w(at) + v));
}

// $A1D5: scraping along a wall: it pushes the car away, and knocks count up ($40) to a
// spin or a crash.
static void scrape(void) {
  uint16_t a = (uint16_t) ((absw(z(0x10)) >> 3) + z(0x40));
  zs(0x40, a);
  bool left = neg(w(0x0206));
  uint16_t push = left ? 0x0600 : 0xFA00;
  if(a >= 0x0600) {
    zor(0x34, 0x0100);
    zs(0xCA, 0);
    zs(0x68, 0);
    halve_turn();
    say(left ? 3 : 0, 0);
  } else if(a >= 0x0200) {
    zor(0x34, 0x0600);
    halve_turn();
    say(left ? 0x000A : 0x0009, 0);
  } else {
    halve_turn();
  }
  g_y = push;
  zs(0x46, push);
}

// $A155: the car is through a wall's line: it is put back on the road side of it, turned
// back at half the speed.
static void bounce(void) {
  zclr(0x06, g_y);
  uint32_t v = (uint32_t) z(0x1C) | (uint32_t) zb(0x1E) << 16;
  v = (v >> 1) | (v & 0x800000);
  v = (0 - v) & 0xFFFFFF;
  zs(0x1C, (uint16_t) v);
  zbs(0x1E, (uint8_t) (v >> 16));
  if(!neg(z(0x10))) {
    uint32_t e2 = (uint16_t) (z(0x14) - 0x73FF);
    bool borrow = z(0x14) < 0x73FF;
    uint16_t e4 = (uint16_t) (z(0x16) - z(0xEA) - borrow - z(0xD8));
    zs(0xE2, (uint16_t) e2);
    zs(0xE4, e4);
    uint16_t lo = (uint16_t) (0x73FF - e2);
    borrow = 0x73FF < e2;
    zs(0x14, lo);
    zs(0x16, (uint16_t) (z(0xD8) + z(0xEA) - e4 - borrow));
  } else {
    bool carry = (uint32_t) z(0xD6) + 0x2000 > 0xFFFF;
    uint16_t e2 = (uint16_t) (z(0xD8) - z(0xEA) + carry);
    zs(0xE2, e2);
    zs(0x14, (uint16_t) (0 - z(0x14)));
    zs(0x16, e2);
  }
}

// $A103 / $A12C: a wall on the left at `line` (or on the right). False if the car is
// clear of it.
static bool wall_left(uint32_t line) {
  uint32_t pos = (uint32_t) w(0x0204) | (uint32_t) w(0x0206) << 16;
  uint32_t d = line - pos;
  zs(0xE2, (uint16_t) d);
  if(d & 0x80000000) {
    zs(0x46, 0xFC00);
    bounce();
    return true;
  }
  zs(0xE4, (uint16_t) (d >> 16));
  if(d >= 0x156C) return false;
  scrape();
  return true;
}
static bool wall_right(uint32_t line) {
  uint32_t pos = (uint32_t) w(0x0204) | (uint32_t) w(0x0206) << 16;
  uint32_t d = line - pos;
  zs(0xE2, (uint16_t) d);
  if(!(d & 0x80000000)) {
    zs(0x46, 0x0400);
    bounce();
    return true;
  }
  zs(0xE4, (uint16_t) (d >> 16));
  if(d < 0xFFFFEA94) return false;
  scrape();
  return true;
}

// $9C86: a wall section starts or ends ($06 keeps which walls the car is between).
static void wall_item(uint16_t item) {
  static const uint8_t kForwards[10] = { 0, 0, 1, 2, 3, 4, 5, 6, 7, 8 };
  static const uint8_t kBackwards[10] = { 0, 0, 2, 1, 4, 3, 6, 5, 8, 7 };
  uint16_t i = item & 0x000F;
  if(i >= 10) return;
  switch(neg(z(0x10)) ? kBackwards[i] : kForwards[i]) {
    case 1:
      zor(0x06, 1);
      g_y = 1;
      if(!wall_left(0x0000C000)) wall_right(0xFFFF4000);
      break;
    case 2: zclr(0x06, 1); break;
    case 3:
      g_y = 2;
      zor(0x06, 2);
      wall_right(0xFFFF1000);
      break;
    case 4: zclr(0x06, 2); break;
    case 5:
      g_y = 4;
      zor(0x06, 4);
      wall_left(0x0000F000);
      break;
    case 6: zclr(0x06, 4); break;
    case 7:
      g_y = 6;
      zor(0x06, 6);
      if(!wall_left(0x0000F000)) wall_right(0xFFFF1000);
      break;
    case 8: zclr(0x06, 6); break;
    default: break;
  }
}

// $9F7B: the car hits something solid: damage by where it was hit, and a bump, a spin
// or a crash by how hard and how often.
static void hit(void) {
  TgRegs* r = g_r;
  tg2_rumble_bump[g_p2]++;
  if(w(0x0610) >= 0x0200) {
    r->c = true;
    r->y = g_y;
    ws(0x0A1E, call(0xBB07, 0x9F89, 0x000E, 0xCB43));
  }
  zor(0x34, 0x2000);
  uint16_t speed = absw(z(0x10));
  uint16_t a = (uint16_t) (z(0xEA) + 0x0620);
  if(!neg(a)) a = (a >> 5) & 0xFFFC;
  zs(0xEC, a);
  m7a(speed);
  m7b(tg_bus_read8((0x80CC1B + (uint16_t) (a + w(0xFE3C))) & 0xFFFFFF));
  add_damage(0x01B4, rd16(0x2135));
  m7b(tg_bus_read8((0x80CBB7 + (uint16_t) (a + w(0xFE38))) & 0xFFFFFF));
  add_damage(0x01B2, rd16(0x2135));
  m7b(tg_bus_read8((0x80CB53 + (uint16_t) (a + w(0xFE38))) & 0xFFFFFF));
  add_damage(0x01B6, rd16(0x2135));
  bool left = neg(w(0x0206));
  uint16_t knocks = 0;
  if(ram16(0xFF1A) == 0) {
    knocks = (uint16_t) ((speed >> 2) + z(0x40));
    zs(0x40, knocks);
  }
  if(knocks >= 0x0600) {
    zor(0x34, 0x0100);
    zs(0xCA, 0);
    zs(0x68, 0);
    zs(0x40, 0);
    halve_turn();
    say(left ? 3 : 0, 0x0380);
  } else if(knocks >= 0x0200) {
    zor(0x34, 0x0600);
    halve_turn();
    say(left ? 0x000A : 0x0009, 0x0380);
  } else {
    halve_turn();
  }
  g_y = left ? 0x0000 : 0xFFFE;  // counted down once
  a = (uint16_t) ((absw(z(0x10)) >> 1) + 0x0C00);
  zs(0x46, left ? a : (uint16_t) (0 - a));
}

// Whether a thing on the road at x is under the car: within `reach` across the road.
static bool across(uint16_t x, uint16_t reach) {
  uint16_t a = (uint16_t) (asr((uint16_t) ((ram16(0x6400 + x) & 0x7F00) - 0x4000)) - z(0xE8));
  return neg(a) ? !lt(a, (uint16_t) (0 - reach)) : lt(a, reach);
}

// $9D4D: something lying on the road (its record is at $7E:6400 + x; bit 7 set once taken).
static void road_item(uint16_t x) {
  uint16_t at = (uint16_t) (0x6400 + x);
  uint16_t v = ram16(at);
  if(v & 0x0080) return;
  switch(v & 0x000F) {
    case 0: case 2: case 4: case 6:  // a nitro, up to nine
      if(!across(x, 0x0800) || (w(0x01A6) & 0x000F) == 9) return;
      ws(0x01A6, (uint16_t) (w(0x01A6) + 1));
      ram16_set(at, ram16(at) | 0x0080);
      zor(0x34, 0x4000);
      return;
    case 1: case 5: case 7:  // money
      if(!across(x, 0x0800)) return;
      ws(0xFE9A, (uint16_t) (w(0xFE9A) + 1));
      ram16_set(at, ram16(at) | 0x0080);
      zor(0x34, 0x4000);
      return;
    case 3:
      if(!across(x, 0x0800)) return;
      zs(0x64, 0x0168);
      zor(0x34, 0x4000);
      return;
    case 8: case 9:  // something that sends a fast car flying
      if((z(0x34) & 0x0002) || !across(x, 0x0900)) return;
      if(neg(z(0x10)) || lt(z(0x10), 0x0500)) return hit();
      say(6, 0x0600);
      zor(0x34, 0x0002);
      zs(0x68, 0);
      return;
    case 10: {  // something that breaks when run over
      if(g_ram[(uint16_t) (at + 1)] != 0) return;
      uint16_t a = zb(0xE9);
      if(a & 0x0080) a |= 0xFF00;
      a = (uint16_t) ((uint16_t) (a << 1) - ram16((uint16_t) (at + 2)));
      if(neg(a) ? lt(a, 0xFFF7) : !lt(a, 0x0009)) return;
      ram16_set(at, (uint16_t) (((ram16(at) | 0x0010) & 0x00FF) | 0x0100));
      ram16_set((uint16_t) (at + 4), ram16(0x001A));
      zor(0x34, 0x2000);
      uint16_t t = z(0x1D);
      for(int i = 0; i < 4; i++) t = asr(t);
      zs(0x1D, (uint16_t) (z(0x1D) - t));
      uint16_t speed = absw(z(0x10));
      m7a(speed);
      m7b(zb(0x96));
      add_damage(0x01B4, rd16(0x2135));
      m7a(speed);
      m7b(zb(0x97));
      add_damage(0x01B2, rd16(0x2135) >> 1);
      add_damage(0x01B6, rd16(0x2135) >> 1);
      return;
    }
    default: return;
  }
}

// $9BFD: one entry of the track's list.
static void track_item(uint16_t item) {
  zs(0xDE, item);
  if(item & 0x8000) {
    if(item & 0x4000) wall_item(item);
    else road_item(item & 0x3FFF);
    return;
  }
  // A roadside object: is the car within its width (tables by kind, for its two sides)?
  uint16_t kind = item & 0x00FF;
  uint16_t a = (uint16_t) ((item & 0x7F00) - 0x4000);
  zs(0xE4, a);
  a = (uint16_t) (a - z(0xE8));
  if(!neg(a)) {
    a = (uint16_t) (a - rom16(0x80BC83 + kind * 2));
    zs(0xEA, a);
    if(!neg(a)) return;
  } else {
    a = (uint16_t) (a - rom16(0x80BB23 + kind * 2));
    zs(0xEA, a);
    if(neg(a)) return;
  }
  if(kind == 0x1B || kind == 0x1C || kind == 0x3F) {
    // Soft things only slow the car and push it aside.
    zs(0x6C, 0x000F);
    zs(0x46, neg(z(0xEA)) ? 0x0800 : 0xF800);
    if(ram16(0x05A1) == 0) {
      ram16_set(0x05A1, 0xFF1F);
      ram16_set(0x05A3, 0x003F);
    }
  } else if(kind == 0x51) {
    if(lt(z(0x10), 0x0070)) return;
    zs(0x6C, 0x001E);
    zs(0x46, neg(z(0xEA)) ? 0x0C00 : 0xF400);
  } else {
    hit();
  }
}

// $9AE3: the engine note from the engine speed in A (pitch $059B, volume $059D).
static void engine_note(uint16_t a) {
  TgRegs* r = g_r;
  a = absw(a);
  if(a >= 0x3FFF) a = 0x3FFF;
  a = (uint16_t) (a << 1);
  m7a(a);
  m7b(g_ram[(uint16_t) (ad(0x0698) + 1)]);
  uint16_t pitch = (uint16_t) (rd16(0x2135) + 0x0600);
  ram16_set(P(0x9B, 0x9E) | 0x0500, pitch);
  uint8_t vol = g_ram[ad(0x0698)];
  wbs(0x059D, (uint8_t) (vol << 1));
  r->c = (vol & 0x80) != 0;
  r->z = (g_ram[0x05AE] & P(4, 8)) == 0;  // tsb tests before it sets
  g_ram[0x05AE] |= (uint8_t) P(4, 8);
  r->a = (uint16_t) ((pitch & 0xFF00) | P(4, 8));
}

// $9B21: the lap: past the lap length ($010A) the position wraps and the lap count $60 goes
// up (the race's distance drops what was run before the start, $0614, on the first lap).
static void lap(void) {
  TgRegs* r = g_r;
  uint16_t pos = z(0x16), len = ram16(0x010A);
  if(neg(pos)) {
    uint32_t s = (uint32_t) pos + len;
    zs(0x16, (uint16_t) s);
    r->c = s > 0xFFFF;
    zs(0x60, (uint16_t) (z(0x60) - 1));
    r->a = (uint16_t) s;
    return;
  }
  r->a = (uint16_t) (pos - len);
  r->c = pos >= len;
  if(!r->c) return;
  zs(0x16, r->a);
  zs(0x60, (uint16_t) (z(0x60) + 1));
  uint16_t a = (uint16_t) (z(0x60) - 1);
  r->a = a;
  r->c = a >= ram16(0x019E);
  if(a != ram16(0x019E)) return;
  uint16_t pre_lo = ram16(P(0x14, 0x18) | 0x0600), pre_hi = ram16(P(0x16, 0x1A) | 0x0600);
  uint16_t lo = w(0x0212), hi = w(0x0214);
  bool borrow = lo < pre_lo;
  ws(0x0212, (uint16_t) (lo - pre_lo));
  r->a = (uint16_t) (hi - pre_hi - borrow);
  r->c = (uint32_t) pre_hi + borrow <= hi;
  ws(0x0214, r->a);
  ram16_set(P(0x14, 0x18) | 0x0600, 0);
  ram16_set(P(0x16, 0x1A) | 0x0600, 0);
}

// $9B5B: A (signed) / the gear's ratio ($80:BDFB, signed), as 8.8 fixed point; Y gets the
// fraction byte too. Runs the divider as the original does, sign byte quirk and all.
static uint16_t ratio_div(uint16_t v) {
  TgRegs* r = g_r;
  ram16_set(DPB + 0xDE, v);
  uint16_t m = absw(v);
  tg_bus_write8(0x4204, (uint8_t) m);
  tg_bus_write8(0x4205, (uint8_t) (m >> 8));
  uint16_t g = z(0x8A);
  uint32_t sum = (uint32_t) z(0x30) + rom16(0x80BDF3 + (uint16_t) (g << 1)) + (g >> 15);
  uint16_t x = (uint16_t) sum;
  bool cy = sum > 0xFFFF;
  r->x = x;
  uint8_t ratio = tg_rom(0x80BDFB + x)[0];
  g_ram[DPB + 0xE0] = ratio;
  uint8_t d = (ratio & 0x80) ? (uint8_t) (0 - ratio) : ratio;
  tg_bus_write8(0x4206, d);
  g_ram[DPB + 0xDE] = d;
  uint8_t sign = (uint8_t) (g_ram[DPB + 0xDF] ^ ratio);
  if(rd(0x4215)) {
    r->c = cy;
    return r->a = (sign & 0x80) ? 0x8001 : 0x7FFF;
  }
  uint8_t q1 = rd(0x4214), rem = rd(0x4216);
  tg_bus_write8(0x4204, 0);
  tg_bus_write8(0x4205, rem);
  tg_bus_write8(0x4206, g_ram[DPB + 0xDE]);
  g_ram[DPB + 0xDE] = sign;
  uint8_t q2 = rd(0x4214);
  rem = rd(0x4216);
  tg_bus_write8(0x4204, 0);
  tg_bus_write8(0x4205, rem);
  tg_bus_write8(0x4206, sign);
  uint8_t e2 = rd(0x4214);
  uint16_t a = (uint16_t) (q1 << 8 | q2);
  if(sign & 0x80) {
    unsigned t = (uint8_t) ~e2 + 1u;
    e2 = (uint8_t) t;
    cy = t > 0xFF;
    uint32_t s = (uint32_t) (uint16_t) ~a + cy;
    a = (uint16_t) s;
    cy = s > 0xFFFF;
  }
  g_ram[DPB + 0xE2] = e2;
  r->c = cy;
  r->y = ram16(DPB + 0xE2);
  return r->a = a;
}

// The car moves: position $14:$16 += (speed + speed * $70/256) * frames * 8 / 256, signed.
static void advance(void) {
  m7a(z(0x10));
  m7b(0x70);
  uint16_t v = (uint16_t) (z(0x10) + rd16(0x2135));
  m7a(v);
  m7b((uint8_t) (frames8() << 3));
  uint8_t ext = neg(v) ? 0xFF : 0x00;
  g_ram[DPB + 0xE5] = ext;
  g_ram[DPB + 0xE2] = rd(0x2134);
  ram16_set(DPB + 0xE3, rd16(0x2135));
  uint32_t s = (uint32_t) z(0x14) + ram16(DPB + 0xE2);
  zs(0x14, (uint16_t) s);
  zs(0x16, (uint16_t) (z(0x16) + ram16(DPB + 0xE4) + (s >> 16)));
}

// The gear's ratio column ($80:BDF3 by gearbox $8A, plus gear $30).
static uint16_t gear_index(void) {
  uint16_t g = z(0x8A);
  return (uint16_t) (z(0x30) + rom16(0x80BDF3 + (uint16_t) (g << 1)) + (g >> 15));
}
// Engine speed for the road speed in the current gear.
static void engine_from_speed(void) {
  uint16_t x = gear_index();
  m7a((uint16_t) (zb(0x1D) | zb(0x1E) << 8));
  m7b(tg_rom(0x80BDFB + x)[0]);
  zs(0x10, rd16(0x2135));
}
// A gear change: the road speed for the new gear.
static void regear(uint16_t site) {
  uint16_t a = ratio_div(z(0x10));
  zs(0x1C, 0);
  zs(0x1D, a);
}

// Pre-race (countdown) and finish-line drive: rev the engine with the accelerator, and
// after the finish roll the car on to the line ($00:8BBE).
static void rev_engine(void) {
  for(uint16_t x = frames(); x; x--) {
    if(w(0x0028) & z(0x7E)) {
      uint16_t a = (uint16_t) (z(0x24) + 0x0200);
      if(a >= 0x2000) a = 0x2000;
      zs(0x24, a);
    }
    uint16_t a = (uint16_t) (z(0x24) - 0x0120);
    zs(0x24, neg(a) ? 0 : a);
  }
  m7a((uint16_t) (zb(0x24) | zb(0x25) << 8));
  m7b(0x70);
  ws(0x01A8, absw(rd16(0x2135)));
  engine_note(z(0x24));
  ws(0x050A, (w(0x0028) & z(0x80)) ? 0xFFFF : 0);
}

// Rolls the car up to the start line (bit $10 of $34): an eighth of the gap each frame.
static void roll_to_line(void) {
  uint32_t line = ((uint32_t) w(0xC41A) << 16 | w(0xC418)) - 0x2800;
  zs(0xE2, (uint16_t) line);
  zs(0xE4, (uint16_t) (line >> 16));
  uint32_t gap = line - ((uint32_t) z(0x16) << 16 | z(0x14));
  uint16_t de = (uint16_t) gap, hi = (uint16_t) ((gap >> 16) + 1);
  if(hi >= 8) {
    de = 0;
    hi = 8;
  }
  bool c = false;
  for(int i = 0; i < 3; i++) {
    bool out = hi & 1;
    hi >>= 1;
    c = de & 1;
    de = (uint16_t) (de >> 1 | out << 15);
  }
  zs(0xDE, de);
  zs(0xE0, hi);
  uint32_t s = (uint32_t) de + z(0x14) + c;
  zs(0x14, (uint16_t) s);
  zs(0x16, (uint16_t) (hi + z(0x16) + (s >> 16)));
  uint32_t pos = (uint32_t) z(0x16) << 16 | z(0x14);
  uint16_t hi_diff = (uint16_t) (z(0x16) - z(0xE4) - (z(0x14) < z(0xE2)));
  (void) pos;
  if(!neg(hi_diff)) {
    zs(0x14, z(0xE2));
    zs(0x16, z(0xE4));
    ws(0x0634, w(0x0634) | 0x0020);
    zclr(0x34, 0x0010);
  }
}

// Distance moved this frame for the race position ($0212:$0214), then $9B21.
static void report_moved(uint16_t site) {
  uint32_t d = ((uint32_t) z(0x16) << 16 | z(0x14)) - ((uint32_t) z(0xD8) << 16 | z(0xD6));
  ws(0x0212, (uint16_t) d);
  ws(0x0214, (uint16_t) (d >> 16));
  (void) site;
  lap();
}

// The autopilot block ($7E:C42B bit 1; not seen in the recordings): the car drives itself
// at its set speed.
static void autopilot(void) {
  if(!(z(0x34) & 0x1000)) {
    zor(0x34, 0x1000);
    zs(0xCA, 0);
    zs(0x68, 0);
  } else {
    uint16_t a = (uint16_t) (z(0x68) + frames());
    zs(0x68, a);
    if(w(0x05A1) == 0) {
      uint16_t x = z(0xCA);
      if(a >= rom16(0x80CCF6 + x)) {
        sound(rom16(0x80CCF8 + x), 0x8A90, x);
        zs(0xCA, (uint16_t) (g_r->x + 4));
      }
    }
  }
  if(z(0x14) | z(0x16)) advance();
  // Road speed from the car record's speed ($C419) by restoring division.
  uint16_t q = (uint16_t) (z(0x10) << 3), d = (uint16_t) (w(0xC419) >> 2), rem = 0;
  zs(0xE0, d);
  bool c = q >> 15;
  q = (uint16_t) (q << 1);
  rem = (uint16_t) (rem << 1 | c);
  for(int i = 0; i < 16; i++) {
    c = rem >= d;
    if(c) rem = (uint16_t) (rem - d);
    bool out = q >> 15;
    q = (uint16_t) (q << 1 | c);
    rem = (uint16_t) (rem << 1 | out);
  }
  uint16_t a = (uint16_t) (0x0100 - q);
  if(neg(a)) a = 0;
  a >>= 2;
  zs(0xDE, a);
  m7a((uint16_t) (zb(0x24) | zb(0x25) << 8));
  m7b(zb(0xDE));
  engine_note((uint16_t) (z(0x24) - rd16(0x2135)));
  a = (uint16_t) (0x001F - w(0xC41A));
  if(neg(a)) a = 0;
  a = (uint16_t) (a << 2);
  zs(0xDE, a);
  m7a((uint16_t) (a << 1));
  m7b(wb(0x0698));
  bool lo = (rd(0x2134) & 0x80) != 0;
  wbs(0x059D, (uint8_t) (rd(0x2135) << 1 | lo));
  if(((ram16(0x001E) ^ P(0, 1)) & 1) && z(0x2C)) {
    g_ram[0x05AD] = (uint8_t) ((z(0xDE) << 1) | P(4, 5));
    g_ram[0x05AE] |= 0x20;
  }
  uint16_t bit = (z(0x34) & 0x0800) ? 0x0800 : (z(0x34) & 0x0040) ? 0x0040 : 0;
  if(bit) {
    zclr(0x34, bit);
    ws(0x05A1, 0x0001);
    ws(0x05A3, P(4, 8));
  }
}

// The gear change ($8CA6-$8D94): automatic (bit 15 of $0678) shifts on engine speed,
// manual on the shift buttons ($7A up, $7C down; $C0 gears).
static void gears(void) {
  if(neg(w(0x0678))) {
    engine_from_speed();
    if(z(0x30) == 0) {
      if(w(0x0030) & z(0x7A)) {
        ws(0x0030, (uint16_t) (w(0x0030) & ~z(0x7A)));
        zs(0x30, (uint16_t) (z(0x30) + 1));
        regear(0x8CE2);
      }
      return;
    }
    if(z(0x30) == 1 && (w(0x0030) & z(0x7C))) {
      ws(0x0030, (uint16_t) (w(0x0030) & ~z(0x7C)));
      zs(0x30, (uint16_t) (z(0x30) - 1));
      regear(0x8CFA);
      return;
    }
    uint16_t x = (uint16_t) (gear_index() << 1);
    uint16_t a = asr(z(0x1D));
    if(!lt(a, rom16(0x80CC7F + x))) {
      zs(0x30, (uint16_t) (z(0x30) + 1));
      regear(0x8D20);
    } else if(lt(a, rom16(0x80CCAF + x))) {
      zs(0x30, (uint16_t) (z(0x30) - 1));
      regear(0x8D33);
    }
    return;
  }
  engine_from_speed();
  if(w(0x0030) & z(0x7A)) {
    ws(0x0030, (uint16_t) (w(0x0030) & ~z(0x7A)));
    uint16_t a = (uint16_t) (z(0x30) + 1);
    if(a != z(0xC0)) {
      zs(0x30, a);
      regear(0x8D73);
      return;
    }
  }
  if(w(0x0030) & z(0x7C)) {
    ws(0x0030, (uint16_t) (w(0x0030) & ~z(0x7C)));
    uint16_t a = (uint16_t) (z(0x30) - 1);
    if(!neg(a)) {
      zs(0x30, a);
      regear(0x8D8C);
    }
  }
}

// Adds the scaled product to the 24-bit road speed $1C-$1E.
static void add_speed(void) {
  unsigned s = rd(0x2134) + zb(0x1C);
  zbs(0x1C, (uint8_t) s);
  zs(0x1D, (uint16_t) (rd16(0x2135) + z(0x1D) + (s >> 8)));
}

// One of the curve tables scaled by the gear's torque ($D5) and the frame count, into M7A/B.
static void torque(uint16_t v, bool doubled) {
  m7a(v);
  m7b(zb(0xD5));
  m7a(prod_x2());
  uint8_t f = frames8();
  if(doubled) f = (uint8_t) (f << 1);
  m7b(f);
}

// $0B:9AE3 etc. are bank $00 helpers; see the call sites.

static void run(TgRegs* r) {
  g_r = r;
  uint16_t d0 = r->d;
  tg_push16(d0);  // phd
  r->d = DPB;
  zs(0x88, 0x0010);
  zclr(0x34, 0x0001);
  zs(0x50, 0);
  zs(0xD6, z(0x14));
  zs(0xD8, z(0x16));

  if(w(0xC42B) & 0x0002) {
    autopilot();
    report_moved(0x8CA1);
    tg_pull16();
  r->d = d0;
    return;
  }
  if(!lt(z(0xF4), 0x003C)) {
    rev_engine();
    if(!(z(0x34) & 0x0010)) {
      ws(0x0212, 0);
      ws(0x0214, 0);
      tg_pull16();
  r->d = d0;
      return;
    }
    roll_to_line();
    report_moved(0x8CA1);
    tg_pull16();
  r->d = d0;
    return;
  }

  gears();
  // $8D95: the gear's engine ($D4) and torque ($D5) factors.
  uint16_t gx = gear_index();
  bool cy = (uint32_t) z(0x30) + rom16(0x80BDF3 + (uint16_t) (z(0x8A) << 1)) + (z(0x8A) >> 15) > 0xFFFF;
  zbs(0xD4, tg_rom(0x80BDFB + gx)[0]);
  zbs(0xD5, tg_rom(0x80BE13 + gx)[0]);
  if(z(0x64)) {
    zs(0xBC, 0);
    uint16_t a = (uint16_t) (z(0x64) - frames());
    cy = z(0x64) >= frames();
    zs(0x64, a);
    if(a == 0 || !cy) {
      zs(0xBC, 0x1224);
      zs(0x64, 0);
    }
  }
  if(w(0x0030) & 0x2000) {
    ws(0x0030, (uint16_t) (w(0x0030) & ~0x2000));
    ws(0x05A1, g_p2 ? 0xFF21 : 0xFF20);
    ws(0x05A3, P(4, 8));
    zor(0x34, 0x0800);
  }
  if((z(0x34) & 0x0800) && !(w(0x0028) & 0x2000)) {
    zclr(0x34, 0x0800);
    zclr(0x34, 0x0080);
    ws(0x05A1, 0x0001);
    ws(0x05A3, P(4, 8));
  }
  // The turbo button ($86): a burst ($2C frames) while boosts are left in $01A6.
  if(z(0x2C) == 0 && (w(0x0030) & z(0x86))) {
    ws(0x0030, (uint16_t) (w(0x0030) & ~z(0x86)));
    if((w(0x01A6) & 0x000F) == 0) {
      ws(0x0376, (uint16_t) (w(0x0376) & ~0x3000));
      ws(0x0374, (uint16_t) (w(0x0374) | 0x3000));
      ws(0x05A1, 0xFF14);
      ws(0x05A3, P(0x10, 0x20));
    } else {
      wbs(0x01A6, (uint8_t) (wb(0x01A6) - 1));
      zs(0xBC, z(0xC4));
      zs(0x2C, 0x0230);
      tg2_rumble_nitro[g_p2]++;
      ws(0x01A6, (uint16_t) (w(0x01A6) | 0x8000));
      ws(0x0376, (uint16_t) (w(0x0376) & ~0x300C));
      ws(0x0374, (uint16_t) (w(0x0374) | 0x000C));
      ws(0x05A1, 0xFF13);
      ws(0x05A3, P(0x10, 0x20));
      ws(0x0A1E, call(0xBB07, 0x8E63, 0x000E, 0xCB33));
      cy = r->c;
    }
  }
  if(z(0x64) == 0) {
    if(z(0x2C) != 0) {
      uint16_t a = (uint16_t) (z(0x2C) - frames() - !cy);
      bool borrow = (uint32_t) frames() + !cy > z(0x2C);
      cy = !borrow;
      if(a == 0 || borrow) {
        zs(0x2C, 0);
        ws(0x05A3, P(0x10, 0x20));
        ws(0x05A1, 0x0001);
        ws(0x01A6, (uint16_t) (w(0x01A6) & ~0x8000));
        ws(0x0374, (uint16_t) (w(0x0374) & ~0x000C));
      } else {
        zs(0x2C, a);
        zs(0xBC, z(0xC4));
        if(z(0x2C) < 0x001F) {
          g_ram[0x05AD] = (uint8_t) ((z(0x2C) << 3) | P(4, 5));
          g_ram[0x05AE] |= 0x20;
          ws(0x01A6, (uint16_t) (w(0x01A6) & ~0x8000));
          ws(0x0374, (uint16_t) (w(0x0374) & ~0x000C));
        }
      }
    } else {
      zs(0xBC, 0x1224);
    }
  }
  {
    uint16_t x = 0;
    if(z(0x64)) {
      uint16_t a = (uint16_t) (z(0x64) - frames() - !cy);
      bool borrow = (uint32_t) frames() + !cy > z(0x64);
      if(a != 0 && !borrow) {
        x = a;
        zs(0xBC, 0);
      }
      zs(0x64, x);
    }
  }
  if(z(0x6C)) {
    uint16_t a = (uint16_t) (z(0x6C) - frames());
    zs(0x6C, z(0x6C) >= frames() ? a : 0);
  }
  // Grip loss from the road surface and the car's slope sensors.
  m7a(w(0x022C));
  m7b(0xF2);
  zs(0x5C, (uint16_t) (rd16(0x2135) + z(0x5C)));
  {
    uint32_t s = (uint32_t) w(0x01B2) + w(0x01B6);
    uint16_t a = (uint16_t) (s >> 1);
    s = (uint32_t) a + w(0x01B8);
    a = (uint16_t) (s >> 1);
    s = (uint32_t) a + w(0x01B4);
    a = (uint16_t) (s >> 1);
    a >>= 4;
    zs(0x5C, (uint16_t) (z(0x5C) - a));
  }
  {
    uint16_t a = z(0x5C);
    zs(0x5C, 0);
    a = ratio_div(a);
    m7a(a);
    m7b(frames8());
    add_speed();
  }

  // Accelerating ($7E pressed, not stalled $6C): the engine curve by gear.
  uint16_t v;
  if(z(0x6C) == 0 && (w(0x0028) & z(0x7E))) {
    zs(0xE0, 0);
    zor(0x34, 0x0001);
    uint16_t x = (uint16_t) (z(0x8C) << 1);
    uint16_t a = (z(0x34) & 0x0002) ? z(0x24) : z(0x1D);
    if(neg(a)) a = 0;
    a = (uint16_t) (((uint16_t) (a + z(0xBC)) >> 5) & 0xFFFE);
    zs(0xDE, a);
    if(a < 0x0200) {
      uint16_t i = (uint16_t) (a + rom16(0x80BE2B + x));
      torque(rom16(0x80BE33 + i), z(0x64) || z(0x2C));
      if(zb(0x34) & 0x02) {
        g_ram[DPB + 0xE1] = rd(0x2134);
        uint16_t p = rd16(0x2135), e = ram16(DPB + 0xE0);
        for(int k = 0; k < 3; k++) {
          p = (uint16_t) (p << 1 | e >> 15);
          e = (uint16_t) (e << 1);
        }
        ram16_set(DPB + 0xE0, e);
        zs(0x24, (uint16_t) (z(0x24) + p));
      } else {
        add_speed();
      }
    }
    a = z(0xDE);
    if(a < 0x00E0) goto drag_done;
    a = (uint16_t) (a - 0x00E0);
    if(a >= 0x0100) a = 0x00FE;
    v = rom16(0x80CA33 + a);
  } else {
    uint16_t a = z(0x1D);
    if(neg(a)) a = 0;
    a = (uint16_t) (((uint16_t) (a + z(0xBC)) >> 5) & 0xFFFE);
    v = a >= 0x0200 ? 0xB426 : rom16(0x80C633 + a);
  }
  // $901E: drag (or engine braking) slows the road speed, never below 0.
  torque(v, false);
  if(zb(0x1E) & 0x80) {
    unsigned lo = rd(0x2134);
    bool borrow = lo > zb(0x1C);
    zbs(0x1C, (uint8_t) (zb(0x1C) - lo));
    uint16_t a = (uint16_t) (z(0x1D) - rd16(0x2135) - borrow);
    if(!neg(a)) {
      a = 0;
      zs(0x1C, 0);
    }
    zs(0x1D, a);
  } else {
    unsigned s = rd(0x2134) + zb(0x1C);
    zbs(0x1C, (uint8_t) s);
    uint16_t a = (uint16_t) (rd16(0x2135) + z(0x1D) + (s >> 8));
    if(neg(a)) {
      a = 0;
      zs(0x1C, 0);
    }
    zs(0x1D, a);
  }
drag_done:
  // $907B: engine speed, then the air drag from the speed table ($C833) by $88.
  m7a((uint16_t) (zb(0x1D) | zb(0x1E) << 8));
  m7b(zb(0xD4));
  zs(0x10, rd16(0x2135));
  {
    uint16_t x = (uint16_t) ((absw(z(0x10)) >> 3) & 0xFFFE);
    m7a(rom16(0x80C833 + x));
    m7b(zb(0x88));
    (void) rd(0x2134);
    zs(0xDA, rd16(0x2135));
  }
  if(w(0x0028) & z(0x80)) {
    zs(0xDA, (uint16_t) (z(0xDA) + 0x0200));
    ws(0x050A, 0xFFFF);
  } else {
    ws(0x050A, 0);
  }
  zs(0xE2, z(0x1D));
  {
    uint16_t a = ratio_div(z(0xDA));
    if(!neg(a)) a = (uint16_t) (0 - a);
    if(neg(z(0x1D))) a = (uint16_t) (0 - a);
    m7a(a);
    m7b((uint8_t) (frames8() << 3));
    unsigned s = rd(0x2134) + zb(0x1C);
    s = (s & 0xFF) + zb(0x1C) + (s >> 8);
    zs(0x1D, (uint16_t) (rd16(0x2135) + z(0x1D) + (s >> 8)));
    if(neg((uint16_t) (z(0x1D) ^ z(0xE2)))) {
      zs(0x1C, 0);
      zs(0x1D, 0);
    }
  }
  m7a((uint16_t) (zb(0x1D) | zb(0x1E) << 8));
  m7b(zb(0xD4));
  zs(0x10, rd16(0x2135));
  advance();

  // $9192: the lateral position $0204:$0206 / 8, then the roadside objects passed.
  {
    uint32_t lat = (uint32_t) w(0x0206) << 16 | w(0x0204);
    zs(0xE8, (uint16_t) ((int32_t) lat >> 3));
  }
  if(!neg(z(0x10))) {
    zs(0xDE, (uint16_t) (z(0xD6) + 0x4000));
    bool c = (uint32_t) z(0xD6) + 0x4000 > 0xFFFF;
    uint16_t y = (uint16_t) ((z(0xD8) & 3) + 6 + c);
    zs(0xE0, y & 3);
    uint16_t t = (uint16_t) ((y & 0x3FFC) << 1);
    c = t >> 15;
    uint16_t x = (uint16_t) ((uint16_t) (t << 1) + w(0x0226) + c);
    zs(0xEA, 0);
    c = (uint32_t) z(0xDE) + z(0xE2) > 0xFFFF;
    uint16_t n = (uint16_t) (z(0xE4) + c);
    zs(0xE6, n);
    g_y = y;
    if(n) {
      x = (uint16_t) (((x >> 1) + z(0xE0) + (x & 1)) << 1);
      do {
        uint16_t o = track((uint16_t) (6 + x));
        if(o) {
          track_item(o);
        }
        uint16_t a = (uint16_t) (z(0xE0) + 1);
        if(a & 0xFFFC) {
          x = (uint16_t) (x + 8);
          a = 0;
        }
        zs(0xE0, a);
        x = (uint16_t) (x + 2);
        zs(0xEA, (uint16_t) (z(0xEA) + 1));
        zs(0xE6, (uint16_t) (z(0xE6) - 1));
      } while(z(0xE6));
    }
  } else {
    zs(0xDE, (uint16_t) (z(0xD6) + 0x2000));
    bool c = (uint32_t) z(0xD6) + 0x2000 > 0xFFFF;
    uint16_t y = (uint16_t) ((z(0xD8) & 3) + 5 + c);
    zs(0xE0, y & 3);
    uint16_t t = (uint16_t) ((y & 0xFFFC) << 1);
    c = t >> 15;
    uint16_t x = (uint16_t) ((uint16_t) (t << 1) + w(0x0226) + c);
    zs(0xEA, 0);
    c = (uint32_t) z(0xDE) + z(0xE2) > 0xFFFF;
    uint16_t n = (uint16_t) (z(0xE4) + c);
    zs(0xE6, n);
    g_y = y;
    if(n) {
      zs(0xE6, (uint16_t) (0 - n));
      x = (uint16_t) (((x >> 1) + z(0xE0) + (x & 1)) << 1);
      do {
        uint16_t a = (uint16_t) (z(0xE0) - 1);
        if(neg(a)) {
          x = (uint16_t) (x - 8);
          a = 3;
        }
        zs(0xE0, a);
        x = (uint16_t) (x - 2);
        uint16_t o = track((uint16_t) (6 + x));
        if(o) {
          track_item(o);
        }
        zs(0xEA, (uint16_t) (z(0xEA) + 1));
        zs(0xE6, (uint16_t) (z(0xE6) - 1));
      } while(z(0xE6));
    }
  }

  // $9277.
  if(z(0x40)) {
    uint16_t f2 = (uint16_t) (frames() << 1);
    zs(0x40, z(0x40) >= f2 ? (uint16_t) (z(0x40) - f2) : 0);
  }
  // Speed this frame into $D0-$D3 (24 bits, / 4).
  m7a(z(0x10));
  m7b(frames8());

  g_ram[DPB + 0xD0] = rd(0x2134);
  g_ram[DPB + 0xD3] = 0;
  {
    uint16_t p = rd16(0x2135);
    ram16_set(DPB + 0xD1, p);
    if(neg(p)) ram16_set(DPB + 0xD2, (uint16_t) (ram16(DPB + 0xD2) | 0xFF00));
    uint16_t hi = ram16(DPB + 0xD2), lo = ram16(DPB + 0xD0);
    for(int k = 0; k < 2; k++) {
      bool c = hi & 1;
      hi >>= 1;
      lo = (uint16_t) (lo >> 1 | c << 15);
    }
    ram16_set(DPB + 0xD2, hi);
    ram16_set(DPB + 0xD0, lo);
  }
  // Steering drift $38-$3A recovers towards 0.
  if(!neg(z(0x39))) {
    m7a((uint16_t) (absw(z(0xD0)) >> 2));
    m7b(0x0D);
    unsigned lo = rd(0x2134);
    bool borrow = lo > zb(0x38);
    zbs(0x38, (uint8_t) (zb(0x38) - lo));
    uint16_t a = (uint16_t) (z(0x39) - rd16(0x2135) - borrow);
    zs(0x39, a);
    if(neg(a)) {
      zs(0x38, 0);
      zs(0x39, 0);
    }
  } else {
    m7a((uint16_t) (absw(z(0xD0)) >> 2));
    m7b(0x0D);
    unsigned s = zb(0x38) + rd(0x2134);
    zbs(0x38, (uint8_t) s);
    uint16_t a = (uint16_t) (z(0x39) + rd16(0x2135) + (s >> 8));
    zs(0x39, a);
    if(!neg(a)) {
      zs(0x38, 0);
      zs(0x39, 0);
    }
  }
  // Steering left ($82) and right ($84).
  if(w(0x0028) & z(0x82)) {
    m7a((uint16_t) (absw(z(0xD0)) >> 2));
    m7b(0x1A);
    unsigned lo = rd(0x2134);
    bool borrow = lo > zb(0x38);
    zbs(0x38, (uint8_t) (zb(0x38) - lo));
    uint16_t a = (uint16_t) (z(0x39) - rd16(0x2135) - borrow);
    zs(0x39, a);
    if(lt(a, 0xFF81)) {
      zs(0x38, 0);
      zs(0x39, 0xFF82);
    }
    uint16_t x = w(0x0226);
    if(!neg(track((uint16_t) (0x10 + x)))) {
      m7a((uint16_t) (rd16(0x2135) << 3));
      m7b(zb(0x94));
      g_ram[DPB + 0xDF] = rd(0x2134);
      uint16_t p = rd16(0x2135), e = ram16(DPB + 0xDE);
      for(int k = 0; k < 3; k++) {
        p = (uint16_t) (p << 1 | e >> 15);
        e = (uint16_t) (e << 1);
      }
      ram16_set(DPB + 0xDE, e);
      uint16_t b = (uint16_t) (p + z(0x48));
      if(!lt(b, 0x7000)) b = 0x7000;
      zs(0x48, b);
      b = (uint16_t) (p + z(0x4C));
      if(!lt(b, 0x0300)) b = 0x0300;
      zs(0x4C, b);
    }
  }
  if(w(0x0028) & z(0x84)) {
    m7a((uint16_t) (absw(z(0xD0)) >> 2));
    m7b(0x1A);
    unsigned s = zb(0x38) + rd(0x2134) + 1;
    zbs(0x38, (uint8_t) s);
    uint16_t a = (uint16_t) (z(0x39) + rd16(0x2135) + (s >> 8));
    zs(0x39, a);
    if(!lt(a, 0x007F)) {
      zs(0x38, 0);
      zs(0x39, 0x007F);
    }
    uint16_t x = w(0x0226);
    uint16_t t = track((uint16_t) (0x10 + x));
    if(t != 0 && neg(t)) {
      m7a((uint16_t) (rd16(0x2135) << 3));
      m7b(zb(0x94));
      g_ram[DPB + 0xDF] = rd(0x2134);
      uint16_t p = rd16(0x2135), e = ram16(DPB + 0xDE);
      for(int k = 0; k < 3; k++) {
        p = (uint16_t) (p << 1 | e >> 15);
        e = (uint16_t) (e << 1);
      }
      ram16_set(DPB + 0xDE, e);
      p = (uint16_t) ~p;
      uint16_t b = (uint16_t) (p + z(0x48) + 1);
      if(lt(b, 0x9000)) b = 0x9000;
      zs(0x48, b);
      b = (uint16_t) (p + z(0x4C) + 1);
      if(lt(b, 0xFD00)) b = 0xFD00;
      zs(0x4C, b);
    }
  }
  // Skidding (|$4C| >= $100): flags in $70.
  if(!lt(absw(z(0x4C)), 0x0100)) {
    zor(0x70, 0x0001);
    zor(0x70, neg(z(0x4C)) ? 0x0010 : 0x0008);
  } else {
    ws(0x0670, (uint16_t) (w(0x0670) & ~0x0019));
  }
  for(uint16_t x = frames(); x; x--) {
    uint16_t a = w(0x0648);
    if(neg(a)) {
      a = (uint16_t) (a + 0x0100);
      if(!neg(a)) a = 0;
    } else if(a) {
      a = (uint16_t) (a - 0x0100);
      if(neg(a)) a = 0;
    }
    ws(0x0648, a);
    a = w(0x064C);
    if(neg(a)) {
      a = (uint16_t) (a + 0x0080);
      if(!neg(a)) a = 0;
    } else {
      a = (uint16_t) (a - 0x0080);
      if(neg(a)) a = 0;
    }
    ws(0x064C, a);
  }
  // $949F: steering limited by speed, then the sideways move.
  {
    uint16_t a = z(0x10);
    if(!neg(a)) a = (uint16_t) (0 - a);
    a = (uint16_t) (a + 0x2000);
    if(neg(a)) a = 0;
    m7a(a);
    m7b((zb(0x34) & 0x01) ? 5 : 6);
    a = z(0x39);
    if(!neg(a)) {
      if(!lt(a, rd16(0x2135))) zs(0x39, rd16(0x2135));
    } else {
      a = (uint16_t) (0 - a);
      if(!lt(a, rd16(0x2135))) zs(0x39, (uint16_t) (0 - rd16(0x2135)));
    }
  }
  {
    uint16_t a = z(0x39);
    zs(0xE2, (uint16_t) ((a >> 1) + (a & 1)));
    a = (uint16_t) (asr(z(0xD0)) + z(0xD0));
    m7a(a);
    m7b(zb(0xE2));
    g_ram[DPB + 0xE3] = rd(0x2134);
    uint16_t p = rd16(0x2135);
    ram16_set(DPB + 0xE4, p);
    uint16_t e = ram16(DPB + 0xE2);
    for(int k = 0; k < 2; k++) {
      p = (uint16_t) (p << 1 | e >> 15);
      e = (uint16_t) (e << 1);
    }
    ram16_set(DPB + 0xE2, e);
    uint16_t hi = neg(p) ? 0xFFFF : 0;
    uint32_t s = (uint32_t) p + w(0x0204);
    ws(0x0204, (uint16_t) s);
    ws(0x0206, (uint16_t) (hi + w(0x0206) + (s >> 16)));
    a = w(0x0648);
    hi = neg(a) ? 0xFFFF : 0;
    s = (uint32_t) a + w(0x0204);
    ws(0x0204, (uint16_t) s);
    ws(0x0206, (uint16_t) (hi + w(0x0206) + (s >> 16)));
  }
  // $954B: the camera turn from the road's curve ($9001) and $06D0.
  {
    uint16_t x = w(0x0226);
    uint16_t a = (uint16_t) (0 - asr(asr(w(0x06D0))));
    m7a(a);
    m7b(track8((uint16_t) (1 + x)));
    unsigned s = rd(0x2134) + wb(0x06C8);
    wbs(0x06C8, (uint8_t) s);
    ws(0x0238, (uint16_t) (rd16(0x2135) + w(0x0238) + (s >> 8)));
  }
  // $9582: the road's curve pushes the car outwards ($9010).
  {
    uint16_t x = w(0x0226);
    uint16_t c = track((uint16_t) (0x10 + x));
    if(c == 0 || neg((uint16_t) (c ^ z(0x4C)))) {
      zs(0x48, 0);
      zs(0x4C, 0);
    }
    m7a(track((uint16_t) (0x10 + x)));
    m7b(0x50);
    zs(0xDE, rd16(0x2135));
    uint16_t a = absw((uint16_t) (asr(z(0xD0)) + z(0xD0)));
    m7a(a);
    m7b(zb(0xDE));
    g_ram[DPB + 0xE3] = 0;
    g_ram[DPB + 0xE2] = rd(0x2136);
    if(g_ram[DPB + 0xE2] & 0x80) g_ram[DPB + 0xE3] = 0xFF;
    ram16_set(DPB + 0xE0, rd16(0x2134));
    uint8_t b = zb(0xDF);
    if(zb(0xDE) & 0x80) b++;
    m7b(b);
    unsigned s = rd(0x2134) + g_ram[DPB + 0xE1];
    g_ram[DPB + 0xE1] = (uint8_t) s;
    uint16_t p = (uint16_t) (rd16(0x2135) + ram16(DPB + 0xE2));  // clc: no carry from $E1
    ram16_set(DPB + 0xE2, p);
    ram16_set(DPB + 0xE4, neg(p) ? 0xFFFF : 0);
    uint32_t t = (uint32_t) ram16(DPB + 0xE1) + w(0x0204);
    ws(0x0204, (uint16_t) t);
    ws(0x0206, (uint16_t) (ram16(DPB + 0xE3) + w(0x0206) + (t >> 16)));
  }
  // The track edges.
  {
    uint16_t lo = w(0x0204), hi = w(0x0206);
    if(!neg((uint16_t) (hi - (lo < 0xF98A)))) {
      ws(0x0204, 0xF989);
      ws(0x0206, 0);
    } else if(neg((uint16_t) (hi - 0xFFFF - (lo < 0x0676)))) {
      ws(0x0204, 0x0675);
      ws(0x0206, 0xFFFF);
    }
  }
  // Walls ($06 bits 0-2) stop the car at their side.
  {
    uint16_t a = z(0x06);
    if(a & 7) {
      uint16_t lo = w(0x0204), hi = w(0x0206);
      if(a & 1) {
        if(!neg(hi)) {
          uint16_t h = (uint16_t) (hi - (lo < 0xAA94));
          if(!neg(h)) {
            call(0xA1D5, 0x9667, h, r->x);
            ws(0x0204, 0xAA94);
          }
        } else {
          uint16_t h = (uint16_t) (hi - 0xFFFF - (lo < 0x556C));
          if(neg(h)) {
            call(0xA1D5, 0x9680, h, r->x);
            ws(0x0204, 0x556C);
          }
        }
      } else if((a & 2) && neg(hi)) {
        uint16_t h = (uint16_t) (hi - 0xFFFF - (lo < 0x256C));
        if(neg(h)) {
          call(0xA1D5, 0x96A3, h, r->x);
          ws(0x0204, 0x256C);
        }
      } else if((a & 4) && !neg(hi)) {
        uint16_t h = (uint16_t) (hi - (lo < 0xDA94));
        if(!neg(h)) {
          call(0xA1D5, 0x96C6, h, r->x);
          ws(0x0204, 0xDA94);
        }
      }
    }
  }
  // $96CF: rumble strips and the grass on each side.
  {
    zs(0xE2, absw(z(0x10)));
    zs(0xDE, 0x5555);
    zs(0xE0, 0xAAAA);
    uint16_t x = 0, y = w(0x0204);
    if(neg(w(0x0206))) {
      zs(0xE0, 0x5555);
      zs(0xDE, 0xAAAA);
      y = (uint16_t) (0 - y);
      x = 2;
    }
    r->y = y;
    zs(0xE4, 0);
    for(int side = 0; side < 2; side++) {
      uint8_t m = side ? 0xDE : 0xE0;
      uint16_t edge = side ? 0xBA6E : 0x9BF8, far = side ? 0xD460 : 0xB71E;
      uint16_t site = side ? 0x9783 : 0x9732;
      if(y >= edge) {
        zs(0xE4, (uint16_t) (z(0xE4) + 1));
        if(y >= far) {
          zs(0xE4, (uint16_t) (z(0xE4) + 1));
          if(z(0xE2) >= 0x0040) zor(0x50, z(m) & 0x0C00);
          zclr(0x34, z(m) & 0x000C);
        } else if(w(0x05A1) == 0) {
          uint16_t a = z(m) & 0x000C;
          if(!(z(0x34) & a)) {
            zor(0x34, a);
            if(z(0xE2) >= 0x0400) {
              ram16_set((uint16_t) (DPB + g_dp[0x54] + x), 0x0008);
              zs(0xCC, 0x0008);
              sound(0xC012, site, x);
              x = r->x;
              y = r->y;
            }
          }
        }
      } else {
        zclr(0x34, z(m) & 0x000C);
      }
      if(!side) x ^= 2;
    }
  }
  // $979F: grip loss by surface.
  {
    uint16_t x = z(0xE4);
    m7a((uint16_t) (wb(0x0610) | wb(0x0611) << 8));
    m7b(tg_rom(0x80CCDF + x)[0]);
    zs(0x5C, (uint16_t) ((rd16(0x2135) << 1) + z(0x5C)));
  }
  if(z(0x56)) {
    zs(0x56, z(0x56) >= frames() ? (uint16_t) (z(0x56) - frames()) : 0);
    zor(0x50, 0x0001);
  }
  if(z(0x54)) {
    zs(0x54, z(0x54) >= frames() ? (uint16_t) (z(0x54) - frames()) : 0);
    zor(0x50, 0x0002);
  }
  if(z(0x34) & 0x000C) {
    uint16_t a = (uint16_t) (absw(z(0xD0)) >> 8);
    if(z(0xCC) >= a) zs(0xCC, (uint16_t) (z(0xCC) - a));
    else if(w(0x05A1) == 0) zs(0xCC, 0x0008);
  }
  // $980E: a sideways push ($46) wears off by $A0 a frame.
  for(uint16_t x = frames(); x; x--) {
    uint16_t a = z(0x46);
    uint32_t s = (uint32_t) a + w(0x0204);
    ws(0x0204, (uint16_t) s);
    ws(0x0206, (uint16_t) (w(0x0206) + (neg(a) ? 0xFFFF : 0) + (s >> 16)));
    if(!neg(a)) {
      a = (uint16_t) (a - 0x00A0);
      if(neg(a)) {
        zs(0x46, 0);
        break;
      }
    } else {
      a = (uint16_t) (a + 0x00A0);
      if(!neg(a)) {
        zs(0x46, 0);
        break;
      }
    }
    zs(0x46, a);
  }
  // $9863: the slope ($0639) for the engine sound.
  if(w(0x018C) == 0) {
    ws(0x017C, 0xFFFF);
    zclr(0x34, 0x0302);
    zclr(0x70, 0x0004);
    uint16_t a = w(0x0639), x;
    if(!neg(a)) x = !lt(a, 0x00B7) ? 6 : !lt(a, 0x0070) ? 5 : !lt(a, 0x0027) ? 4 : 3;
    else x = lt(a, 0xFF4A) ? 0 : lt(a, 0xFF91) ? 1 : lt(a, 0xFFDA) ? 2 : 3;
    ws(0x0178, x);
  }
  report_moved(0x98BB);
  // $98BE: the distance travelled ($90:$92).
  {
    uint16_t a = (uint16_t) (z(0x1D) + ((uint32_t) w(0x061B) + 0xFF00 > 0xFFFF));
    m7a(absw(a));
    m7b(zb(0x8E));
    g_ram[DPB + 0xE1] = 0xFF;
    uint8_t b = rd(0x2136);
    if(!(b & 0x80)) g_ram[DPB + 0xE1]++;
    g_ram[DPB + 0xE0] = b;
    zs(0xDE, rd16(0x2134));
    for(uint16_t x = frames(); x; x--) {
      uint32_t s = (uint32_t) z(0x90) + z(0xDE);
      zs(0x90, (uint16_t) s);
      uint16_t h = (uint16_t) (z(0x92) + ram16(DPB + 0xE0) + (s >> 16));
      zs(0x92, neg(h) ? 0 : h);
    }
  }
  if(z(0x34) & 0x0002) {
    // Airborne: landing sounds.
    uint16_t x = z(0x68), a = (uint16_t) (z(0x68) + frames());
    zs(0x68, a);
    if(a >= 0x0028) {
      if(x < 0x0028) {
        zclr(0x34, 0x0002);
        a = w(0x05A1);
        if(a == 0) {
          sound(0xFF12, 0x9931, x);
          goto landed;
        }
      }
      if(a >= 0x002B && x < 0x002B && w(0x05A1) == 0) sound(0xFF12, 0x9948, x);
    }
  } else {
    if(z(0x34) & 0x0100) {
      uint16_t a = (uint16_t) (z(0x68) + frames());
      zs(0x68, a);
      if(w(0x05A1) == 0) {
        uint16_t x = z(0xCA);
        if(a >= rom16(0x80CCE4 + x)) {
          sound(rom16(0x80CCE6 + x), 0x996D, x);
          zs(0xCA, (uint16_t) (r->x + 4));
        }
      }
    }
    if(w(0x05A1) == 0 && (z(0x34) & 0x0400)) {
      zclr(0x34, 0x0400);
      sound(0xFF1D, 0x9988, r->x);
    }
    // The engine follows the road speed by up to $C0 a frame.
    for(uint16_t x = frames(); x; x--) {
      uint16_t a = (uint16_t) (z(0x1D) - z(0x24));
      if(!neg(a)) {
        if(!lt(a, 0x00C0)) a = 0x00C0;
      } else if(lt(a, 0xFF40)) {
        a = 0xFF40;
      }
      zs(0x24, (uint16_t) (a + z(0x24)));
    }
  }
landed:
  if(!(z(0x34) & 0x0040)) {
    if(z(0x1D) < z(0x24)) {
      zor(0x70, 0x0002);
    } else {
      zclr(0x70, 0x0002);
      zor(0x34, 0x0040);
    }
  }
  // $99CD: the dashboard: gear, engine speed, speed, distance and steering.
  ws(0x01AA, (uint16_t) (rom16(0x80BDE3 + (uint16_t) (z(0x8A) << 1)) | z(0x30)));
  m7a((uint16_t) (zb(0x24) | zb(0x25) << 8));
  m7b(0x70);
  ws(0x01A8, absw(rd16(0x2135)));
  m7a((uint16_t) (zb(0x10) | zb(0x11) << 8));
  m7b(w(0x019A) == 0 ? 0x45 : 0x39);
  ws(0x01A4, (uint16_t) (absw(rd16(0x2135)) >> 2));
  ws(0x01AE, (uint16_t) (z(0x92) >> 3));
  {
    uint16_t a = z(0x10);
    if(neg(a)) {
      if(lt(a, 0xFC00)) a = 0xFC00;
    } else if(!lt(a, 0x0400)) {
      a = 0x0400;
    }
    ws(0x0396, a);
  }
  engine_note(z(0x24));
  if(w(0x0670) && (w(0x0670) & 0xFFE6)) zor(0x50, 0x0003);
  if(!(z(0x34) & 0x0800) && (w(0x05A1) & 0x00FF) == 0) {
    if(w(0x0670)) {
      if(!(z(0x34) & 0x0080)) {
        zor(0x34, 0x0080);
        ws(0x05A1, 0x8017);
        ws(0x05A3, P(4, 8));
      }
    } else if(z(0x34) & 0x0080) {
      zclr(0x34, 0x0080);
      ws(0x05A1, 0x0001);
      ws(0x05A3, P(4, 8));
    }
  }
  ws(0x0374, (uint16_t) (w(0x0374) & ~(~z(0x50) & 0x0FC3)));
  ws(0x0374, (uint16_t) (w(0x0374) | z(0x50)));
  if(w(0x05A1) == 0) {
    if(z(0x34) & 0x4000) {
      zclr(0x34, 0x4000);
      sound(0xFF18, 0x9AD1, r->x);
    }
    if(z(0x34) & 0x2000) {
      zclr(0x34, 0x2000);
      sound(0xFF15, 0x9ADE, r->x);
    }
  }
  tg_pull16();
  r->d = d0;
}

// $00:8A41 and $00:A2A5.
void Tg2DriveCar1(TgRegs* r) {
  set_player(false);
  run(r);
}
void Tg2DriveCar2(TgRegs* r) {
  set_player(true);
  run(r);
}
