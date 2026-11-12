// Top Gear 2: setting a race up from the choices made in the menus ($7E:FE00-FEFF), for
// both players. Player 1's choices start at $FE1C, player 2's at $FE45.

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
