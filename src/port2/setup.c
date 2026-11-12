// Top Gear 2: setting a race up from the choices made in the menus ($7E:FE00-FEFF), for
// both players. Player 1's choices start at $FE1C, player 2's at $FE45.

#include <stddef.h>

#include "../port/port.h"

static uint16_t w(uint16_t adr) { return ram16(adr); }
static void ws(uint16_t adr, uint16_t v) { ram16_set(adr, v); }
static void wb(uint16_t adr, uint8_t v) { g_ram[adr] = v; }
// $7E:FE00+, the menus' choices.
static uint16_t m(uint16_t adr) { return ram16(adr); }
static uint16_t t16(uint32_t adr) { return tg_read16(adr); }
static uint8_t t8(uint32_t adr) { return tg_bus_read8(adr); }

// $00:CD09: the race's kind: one or two players ($0102), the track ($0108), and the
// demo's own choices.
void Tg2RaceOptions(TgRegs* r) {
  ws(0x0102, m(0xFE04) != 0 ? 0xFFFF : 0);
  ws(0x00F4, 0);
  ws(0x0104, m(0xFE3E));
  ws(0x0106, m(0xFE67));
  ws(0x0120, m(0xFE06));
  ws(0x0108, (uint16_t) ((uint16_t) (m(0xFE0A) << 2) + m(0xFE0E)));
  uint16_t x = m(0xFF1A);
  r->x = x;
  r->a = x;
  if(x != 0) {
    ws(0x0102, t16(0x80F2D7 + x));
    ws(0x0108, t16(0x80F2DD + x));
    ws(0x0120, 0);
    r->a = 0;
  }
}

// A player's name, centred in twelve characters.
static void name(uint16_t src, uint16_t dst) {
  int n = 0;
  while(n < 12 && g_ram[(uint16_t) (src + n)] != 0) n++;
  int y = 0;
  for(int pad = (12 - n) >> 1; pad > 0; pad--) wb((uint16_t) (dst + y++), 0x20);
  for(int i = 0; y < 12; i++) {
    uint8_t c = g_ram[(uint16_t) (src + i)];
    if(c == 0) break;
    wb((uint16_t) (dst + y++), c);
  }
  while(y < 12) wb((uint16_t) (dst + y++), 0x20);
}

// The parts of a player's car that come from a choice: a word from each of seven tables.
static void handling(uint16_t choice, uint16_t dst[7]) {
  uint16_t x = (uint16_t) ((choice & 0x7FFF) << 1);
  for(int i = 0; i < 7; i++) ws(dst[i], t16(0x80D048 + (uint16_t) (i * 10 + x)));
}

// $00:CD5E: each player's car, engine, gearbox, tyres and name.
void Tg2RacePlayers(TgRegs* r) {
  ws(0xFE9A, 0);
  ws(0xFE9C, 0);
  ws(0x068C, m(0xFE2C));
  ws(0x06AE, m(0xFE55));
  ws(0x068A, m(0xFE32));
  ws(0x06C0, t16(0x80D0D2 + (uint16_t) (m(0xFE32) << 1)));
  ws(0x06AC, m(0xFE5B));
  ws(0x06C2, t16(0x80D0D2 + (uint16_t) (m(0xFE5B) << 1)));
  uint16_t x = m(0xFE36);
  ws(0x068E, t16(0x80D08E + x) & 0x00FF);
  ws(0x0692, 0x6800);
  ws(0x0690, 0);
  // Player 2's comes from player 1's choice too, and all 16 bits of it.
  ws(0x06B0, t16(0x80D08E + x));
  ws(0x06B4, 0x6800);
  ws(0x06B2, 0);
  ws(0x06EE, 0);
  ws(0x06F0, 0);
  name(0xFE1C, 0x01E4);
  name(0xFE45, 0x01F0);

  uint16_t k = (uint16_t) (t16(0x8A8006 + (uint16_t) (w(0x0108) << 3)) & 0x0007);
  ws(0x06DE, (uint16_t) (k << 2));
  x = t16(0x80D092 + (uint16_t) (k << 1));
  ws(0x06E0, x);
  wb(0x0694, t8(0x80D0A2 + (uint16_t) (m((uint16_t) (0xFE2E + x)) | w(0x06DE))));
  x = w(0x06E0);
  wb(0x06B6, t8(0x80D0A2 + (uint16_t) (m((uint16_t) (0xFE57 + x)) | w(0x06DE))));
  // The number of laps: 64 / the track's length, plus one.
  tg_bus_write16(0x4204, 0x0040);
  tg_bus_write8(0x4206, (uint8_t) t16(0x8A8002 + (uint16_t) (w(0x0108) << 3)));
  ws(0x019E, (uint16_t) (tg_bus_read8(0x4214) + (tg_bus_read8(0x4215) << 8) + 1));

  x = (uint16_t) (m(0xFE34) << 1);
  ws(0x01A6, t16(0x80D0C2 + x));
  ws(0x06C4, t16(0x80D0CA + x));
  x = (uint16_t) (m(0xFE5D) << 1);
  ws(0x01C0, t16(0x80D0C2 + x));
  ws(0x06C6, t16(0x80D0CA + x));
  ws(0x019A, m(0xFE28) != 0 ? 0 : 0xFFFF);
  ws(0x019C, m(0xFE51) != 0 ? 0 : 0xFFFF);
  ws(0x0678, m(0xFE2A));
  ws(0x01AA, m(0xFE2A) & 0x8000);
  ws(0x069A, m(0xFE53));
  ws(0x01C4, m(0xFE53) & 0x8000);
  static uint16_t kHandling1[7] = { 0x067E, 0x067A, 0x067C, 0x0686, 0x0680, 0x0682, 0x0684 };
  static uint16_t kHandling2[7] = { 0x06A0, 0x069C, 0x069E, 0x06A8, 0x06A2, 0x06A4, 0x06A6 };
  handling(m(0xFE2A), kHandling1);
  handling(m(0xFE53), kHandling2);
  wb(0x0696, t8(0x80D03E + m(0xFE3C)));
  wb(0x0697, t8(0x80D03E + m(0xFE38)));
  wb(0x0695, t8(0x80D043 + m(0xFE3A)));
  wb(0x06B8, t8(0x80D03E + m(0xFE65)));
  wb(0x06B9, t8(0x80D03E + m(0xFE61)));
  wb(0x06B7, t8(0x80D043 + m(0xFE63)));
  x = m(0xFE2C);
  wb(0x0698, t8(0x80D036 + x));
  wb(0x0699, t8(0x80D03A + x));
  x = m(0xFE55);
  wb(0x06BA, t8(0x80D036 + x));
  wb(0x06BB, t8(0x80D03A + x));
  r->x = x;
  r->a = (uint16_t) ((r->a & 0xFF00) | g_ram[0x06BB]);
}

// $01:F5B9. The interrupt vectors in RAM (JMLs at $00-$0F, JSL-and-return stubs at
// $10 and $15 for the NMI's and IRQ's per-screen routines) and the frame's defaults:
// the DMA budget for the video standard ($E2), the palette and sprite buffers, the
// screen fully dark, and full brightness for colours loaded with $DBA1.
void Tg2InitVectors(TgRegs* r) {
  static const uint8_t kOps[][2] = {
    { 0x00, 0x5C }, { 0x04, 0x5C }, { 0x08, 0x5C }, { 0x0C, 0x5C }, { 0x10, 0x22 }, { 0x15, 0x22 },
    { 0x14, 0x60 }, { 0x19, 0x60 }, { 0x03, 0x81 }, { 0x07, 0x81 }, { 0x0B, 0x81 }, { 0x0F, 0x81 },
    { 0x13, 0x81 }, { 0x18, 0x81 },
  };
  for(size_t i = 0; i < sizeof kOps / sizeof kOps[0]; i++) wb(kOps[i][0], kOps[i][1]);
  ws(0x0001, 0xE2F2);
  ws(0x0005, 0xE43A);
  ws(0x0009, 0xE461);
  ws(0x000D, 0xE480);
  ws(0x0011, 0xE47F);
  ws(0x0016, 0xE47F);
  ws(0x0062, 0xFFFF);
  ws(0x0064, 0xFFFF);
  ws(0x00E4, 0);
  ws(0x00E8, 0);
  ws(0x00E6, 0);
  ws(0x00F4, 0);
  uint8_t stat = tg_bus_read8(0x213F);
  tg_apu_read8(0x2140);  // the 16-bit read's high byte
  r->x = (stat & 0x10) ? 0x2000 : 0x1400;  // PAL has more time in its vertical blank
  ws(0x00E2, r->x);
  ws(0x00DC, 0x0C00);
  ws(0x00DE, 0x007E);
  ws(0x00D6, 0x0A00);
  ws(0x00D8, 0x007E);
  ws(0x005B, 0);
  ws(0x0060, 0);
  ws(0x0059, 0x0080);
  ws(0x005E, 0);
  ws(0x00B2, 0x0020);
  ws(0x00B4, 0x0020);
  ws(0x00B6, 0x0020);
  ws(0x00B8, 0);
  ws(0x00BA, 0);
  ws(0x00BC, 0);
  r->y &= 0x00FF;  // SEP #$30 on the way
  r->a = 0;
  r->n = false;
  r->z = true;
}

// ---- $00:D0DA: the starting grid ----

void Tg2Stream1(TgRegs* r);
void Tg2Stream2(TgRegs* r);
void tg2_random(TgRegs* r);
void tg2_queue_body(uint8_t db);
void Tg2Decompress(TgRegs* r);

// The routine runs with the data bank at $7E and the direct page at $0600.
static uint16_t g16(uint16_t adr) { return tg_read16(0x7E0000 | adr); }
static void g16_set(uint16_t adr, uint16_t v) { tg_bus_write16(0x7E0000 | adr, v); }
static uint16_t z16(uint16_t o) { return ram16((uint16_t) (0x0600 + o)); }
static void z16_set(uint16_t o, uint16_t v) { ram16_set((uint16_t) (0x0600 + o), v); }
static uint16_t sext8(uint16_t v) { return (v & 0x80) ? (uint16_t) (v | 0xFF00) : v; }

// Runs a car's track stream (player 1's or 2's) one step, then the video queue until the
// stream's transfers are all sent.
static void stream_step(TgRegs* r, bool two, uint16_t speed) {
  ram16_set(two ? 0x0250 : 0x0214, speed);
  ram16_set(two ? 0x024E : 0x0212, 0);
  uint8_t old = tg_port_set_bank(0x88);
  r->d = 0x0600;
  r->db = 0x7E;
  (two ? Tg2Stream2 : Tg2Stream1)(r);
  tg_port_set_bank(old);
  ws(0x00E4, w(0x00EC));
  ws(0x00EC, 0);
  do {
    ws(0x00EA, 0);
    tg2_queue_body(0x7E);  // the data bank is still $7E here
  } while(w(0x00E4) != 0);
}

// The random numbers: $01:E25F with the registers as the original has them.
static uint16_t rnd(TgRegs* r, uint16_t a, uint16_t x, uint16_t y, bool c) {
  r->a = a;
  r->x = x;
  r->y = y;
  r->c = c;
  r->d = 0;
  tg2_random(r);
  return r->a;
}

// $00:D0DA: the twenty cars on the grid: the other cars' colours (skipping the players'),
// each car's place, start position and speed table, then each player's stream run up to
// the start line (so the road ahead is loaded) and back, and the road's edges noted.
void Tg2StartingGrid(TgRegs* r) {
  tg_push8(r->db);
  tg_push16(r->d);
  uint16_t demo = g16(0xFF1A);
  if(demo != 0) {
    // The demo unpacks its saved race to $7E:E150: the random number's state, the race's
    // kind ($0102) and the $22 words of the cars' grid at $0678.
    r->d = 0;
    r->db = 0x80;
    r->a = 0x7E80;
    r->x = t16(0x80F2D1 + demo);
    r->y = 0xE150;
    Tg2Decompress(r);
    for(uint16_t i = 0; i < 9; i++)
      ws((uint16_t) (0x00BE + 2 * i), g16((uint16_t) (0xE150 + 2 * i)));
    ws(0x0102, g16(0xE162));
    uint16_t x = 0;
    for(int i = 0; i < 0x22; i++, x = (uint16_t) (x + 2))
      g16_set((uint16_t) (0x0678 + x), g16((uint16_t) (0xE164 + x)));
    r->d = 0x0600;
    r->db = 0x7E;
  }
  bool two = (w(0x0102) & 0x8000) != 0;
  uint16_t a = 0;
  for(uint16_t x = 0; x < 0x28; x += 2) {
    if(a == w(0x0104)) a++;
    a &= 7;
    if(two) {
      if(a == w(0x0106)) a++;
      a &= 7;
    }
    g16_set((uint16_t) (0x2000 + x), a);
    a = (a + 1) & 7;
  }
  z16_set(0xDE, 0);
  z16_set(0xE0, (uint16_t) (w(0x010A) - 1));
  z16_set(0xE2, 0x0090);
  z16_set(0xE4, 0);
  z16_set(0xE8, (uint16_t) (rom16(0x80D5FE + (uint16_t) (w(0x0108) << 1)) +
                            rom16(0x80D67E + (uint16_t) (w(0x0120) << 1))));
  for(int xi = 0x26; xi >= 0; xi -= 2) {
    uint16_t x = (uint16_t) xi;
    uint8_t v = (uint8_t) g16((uint16_t) (0xFE9E + x));
    tg_bus_write8(0x211B, 0x40);
    tg_bus_write8(0x211B, 0x00);
    tg_bus_write8(0x211C, v);
    uint16_t y = (uint16_t) (tg_bus_read8(0x2134) | tg_bus_read8(0x2135) << 8);
    g16_set((uint16_t) (0xC3E8 + x), y);
    uint16_t x2 = (uint16_t) (v << 1);
    g16_set((uint16_t) (0xC418 + y), 0xA800);
    g16_set((uint16_t) (0xC41A + y), z16(0xE0));
    z16_set(0xE0, (uint16_t) (z16(0xE0) - 1));
    g16_set((uint16_t) (0xC426 + y), z16(0xE4));
    z16_set(0xE4, (uint16_t) (z16(0xE4) + 1));
    static const uint16_t kZero[] = { 0xC416, 0xC41C, 0xC41E, 0xC420, 0xC422, 0xC42B };
    for(int i = 0; i < 6; i++) g16_set((uint16_t) (kZero[i] + y), 0);
    g16_set((uint16_t) (0xC410 + y), v);
    g16_set((uint16_t) (0xC414 + y), z16(0xE2));
    uint16_t e2 = (uint16_t) (z16(0xE2) - 0x60);
    bool c = e2 >= 0xFF70;
    if((uint16_t) (e2 - 0xFF70) & 0x8000) {
      c = e2 + 0x180 > 0xFFFF;
      e2 = (uint16_t) (e2 + 0x180);
    }
    z16_set(0xE2, e2);
    g16_set((uint16_t) (0xC41C + y), 0x0008);
    tg_push16(x);
    tg_push16(x2);
    tg_push16(y);
    tg_push16(0x0600);
    tg_push8(0x7E);
    uint16_t first = rnd(r, 0, x2, y, c);
    ram16_set(0x06E6, first);
    rnd(r, first, r->x, y, r->c);
    tg_pull8();
    tg_pull16();
    tg_pull16();
    tg_pull16();
    g16_set((uint16_t) (0xC428 + y), (uint16_t) -(int) ((r->a & 0x7F) + 0x70));
    uint32_t sum = (uint32_t) (uint16_t) (rom16(0x80D684 + x2) - (z16(0xE6) & 0x03FF)) + z16(0xE8);
    g16_set((uint16_t) (0xC424 + y), (uint16_t) sum);
    g16_set((uint16_t) (0xC412 + y), g16((uint16_t) (0x2000 + x2)));
    tg_push16(y);
    tg_push16(0x0600);
    tg_push8(0x7E);
    rnd(r, 0, x2, y, sum > 0xFFFF);
    tg_pull8();
    tg_pull16();
    tg_pull16();
    tg_pull16();
    g16_set((uint16_t) (0xC430 + y), (uint16_t) ((r->a & 0xFF) + rom16(0x80D6D4 + x)));
    g16_set((uint16_t) (0xC432 + y), rom16(0x80D6FC + x));
  }
  g16_set(0xC412, g16(0x0104));
  if(two) g16_set(0xC452, g16(0x0106));
  for(uint16_t x = 0; x < 0x28; x += 2) {
    uint16_t v = g16((uint16_t) (0xC3E8 + x));
    if(v == 0) g16_set(0x0136, x);
    if(v == 0x40) g16_set(0x0138, x);
  }

  // Each player's stream run forward to the start, then back twenty steps.
  for(int p = 0; p < (two ? 2 : 1); p++) {
    if(p == 0) {
      z16_set(0xDE, (uint16_t) (w(0x010A) >> 1));
      z16_set(0xE0, 0);
      do {
        uint16_t x = (uint16_t) (g16(0x020E) << 1), y = g16(0x0226);
        for(int i = 0; i < 4; i++) {
          g16_set((uint16_t) (0x2000 + x + 2 * i), 0);
          g16_set((uint16_t) (0x2000 + x + 2 * i), g16((uint16_t) (0x9016 + y + 2 * i)));
        }
        stream_step(r, false, 0x0004);
        z16_set(0xDE, (uint16_t) (z16(0xDE) - 1));
      } while(z16(0xDE) != 0);
    } else {
      z16_set(0xDE, (uint16_t) (w(0x010A) >> 2));
      do {
        stream_step(r, true, 0x0004);
        z16_set(0xDE, (uint16_t) (z16(0xDE) - 1));
      } while(z16(0xDE) != 0);
    }
    z16_set(0xDE, 0x0014);
    do {
      stream_step(r, p == 1, 0xFFFC);
      z16_set(0xDE, (uint16_t) (z16(0xDE) - 1));
    } while(z16(0xDE) != 0);
  }

  // The players' start positions, as 24-bit values.
  for(int p = 0; p < 2; p++) {
    uint16_t at = p ? 0x0240 : 0x0204;
    uint16_t v = g16(p ? 0xC454 : 0xC414);
    g16_set(at, 0);
    g16_set((uint16_t) (at + 2), (v & 0x8000) ? 0xFFFF : 0);
    g16_set((uint16_t) (at + 1), v);
  }
  g16_set(0xC41C, 0x0004);
  if(two) g16_set(0xC45C, 0x0006);

  // The road's edges along the course: $7E:2000 (left) and $3300 (right), narrowed
  // gradually to keep the cars off the roadside objects.
  z16_set(0xDE, 0x0094);
  z16_set(0xE0, 0xFF6C);
  for(int yi = (w(0x010A) - 1) * 2; (yi & 0x8000) == 0 && yi >= 0; yi -= 2) {
    uint16_t y = (uint16_t) yi;
    uint16_t de = (uint16_t) (z16(0xDE) + 2);
    if(!((uint16_t) (de - 0x0094) & 0x8000)) de = 0x0094;
    z16_set(0xDE, de);
    uint16_t e0 = (uint16_t) (z16(0xE0) - 2);
    if((uint16_t) (e0 - 0xFF6C) & 0x8000) e0 = 0xFF6C;
    z16_set(0xE0, e0);
    uint16_t item = g16((uint16_t) (0x2000 + y));
    bool narrow = false;
    if(item != 0 && (item & 0x8000)) {
      if(!(item & 0x4000)) {
        uint16_t x = item & 0x3FFF;
        uint16_t obj = tg_read16(0x7E6400 + x);
        if(!(obj & 0x0080) && (obj & 0x000F) == 0x000A) {
          uint16_t at = tg_read16(0x7E6402 + x);
          if(((uint16_t) (at - 0x0080) & 0x8000) && !((uint16_t) (at - 0xFF80) & 0x8000)) {
            uint16_t s = (uint16_t) (at << 2);
            bool c = (at & 0x4000) != 0;
            z16_set(0xE4, (uint16_t) (s + 0x28 + c));
            z16_set(0xE2, (uint16_t) (s - 0x28));
            narrow = true;
          }
        }
      }
    } else if(item != 0) {
      uint16_t k = item & 0x00FF;
      if(k != 0x003F && k != 0x0051) {
        uint16_t e2 = (uint16_t) ((item & 0x7F00) - 0x4000);
        z16_set(0xE2, e2);
        uint16_t x = (uint16_t) (k << 1);
        uint16_t hi = (uint16_t) (rom16(0x80BC83 + x) + e2);
        z16_set(0xE4, (uint16_t) (sext8(hi >> 8) << 3));
        hi = (uint16_t) (rom16(0x80BB23 + x) + e2);
        z16_set(0xE2, (uint16_t) (sext8(hi >> 8) << 3));
        narrow = true;
      }
    }
    if(narrow) {
      uint16_t e6 = (uint16_t) (z16(0xE2) - z16(0xE0));
      z16_set(0xE6, e6);
      uint16_t d = (uint16_t) (z16(0xDE) - z16(0xE4));
      if((uint16_t) (d - e6) & 0x8000) {
        if((uint16_t) (z16(0xE2) - z16(0xDE)) & 0x8000) z16_set(0xDE, z16(0xE2));
      } else if(!((uint16_t) (z16(0xE4) - z16(0xE0)) & 0x8000)) {
        z16_set(0xE0, z16(0xE4));
      }
    }
    g16_set((uint16_t) (0x2000 + y), z16(0xE0));
    g16_set((uint16_t) (0x3300 + y), z16(0xDE));
  }

  for(uint16_t i = 0; i < 0x78; i++) tg_bus_write8(0x7E0600 + i, 0);
  ws(0x0614, w(0x020C));
  ws(0x0616, (uint16_t) (w(0x020E) + 4));
  ws(0x0618, w(0x0248));
  ws(0x061A, (uint16_t) (w(0x024A) + 4));
  ws(0x0630, 1);
  ws(0x0632, 1);
  uint16_t x = g16((uint16_t) (0xC3E8 + w(0x0136)));
  ws(0x01A0, g16((uint16_t) (0xC426 + x)));
  ws(0x0A1C, g16((uint16_t) (0xC426 + x)));
  x = g16((uint16_t) (0xC3E8 + w(0x0138)));
  ws(0x01BA, g16((uint16_t) (0xC426 + x)));
  ws(0x0A38, g16((uint16_t) (0xC426 + x)));
  ws(0x017C, 0xFFFF);
  ws(0x017E, 0xFFFF);
  ws(0x0178, 3);
  ws(0x017A, 3);
  ws(0x01AE, 0x0D00);
  ws(0x01C8, 0x0D00);
  ws(0x01A2, 1);
  ws(0x01BC, 1);
  ws(0x01AA, (uint16_t) (rom16(0x80BDE3 + (uint16_t) (z16(0x8A) << 1)) | z16(0x30)));
  ws(0x01C4, (uint16_t) (rom16(0x80BDE3 + (uint16_t) (z16(0xAC) << 1)) | z16(0x32)));
  ws(0x0A1A, (uint16_t) (w(0x019E) - 1));
  ws(0x0A36, (uint16_t) (w(0x019E) - 1));
  ws(0x0504, 0xFFFF);
  z16_set(0x34, z16(0x34) | 0x0010);
  if(two) {
    ws(0x050C, 0xFFFF);
    z16_set(0x36, z16(0x36) | 0x0010);
  }
  r->d = tg_pull16();
  r->db = tg_pull8();
}
