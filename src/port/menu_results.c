// Menu results, standings, times, multiplication and palette fades (bank $0F).

#include "port.h"
#include "variables.h"

void MenuToDecimal(TgRegs* r);
void MenuDrawText(TgRegs* r);
void MenuDrawTextSpritesAt(TgRegs* r);
void MenuLoadPlayerName(TgRegs* r);
void MenuBlankBigRows(TgRegs* r);
void MenuBlankSmallRows(TgRegs* r);
void MenuDrawTenLines(TgRegs* r);
void MenuComparePassword(TgRegs* r);
void MenuClearFadeWork(TgRegs* r);

static uint8_t bus8(uint16_t adr) { return tg_bus_read8(adr); }
static uint16_t bus16(uint16_t adr) { return (uint16_t) (bus8(adr) | bus8((uint16_t) (adr + 1)) << 8); }

// $0F:8A09: a time A in frames as "M'SS"HH" at $68-$6F (template $0D:C3F8), using the
// hardware divider (by 60) and the PPU's multiplier (by 100).
void MenuFormatTime(TgRegs* r) {
  tg_bus_write16(0x4204, r->a);
  tg_bus_write8(0x4206, 0x3C);
  ram16_set(0x6C, rom16(0x0DC3FC));
  ram16_set(0x6E, rom16(0x0DC3FE));
  uint16_t seconds = bus16(0x4214);
  uint16_t frames = bus16(0x4216);
  tg_bus_write8(0x211B, (uint8_t) frames);
  tg_bus_write8(0x211B, (uint8_t) (frames >> 8));
  tg_bus_write8(0x211C, 0x64);
  r->x = bus16(0x2134);
  tg_bus_write16(0x4204, r->x);
  tg_bus_write8(0x4206, 0x3C);
  r->a = bus16(0x4214);
  MenuToDecimal(r);
  ram16_set(0x6E, ram16(0x7F));
  tg_bus_write16(0x4204, seconds);
  tg_bus_write8(0x4206, 0x3C);
  ram16_set(0x68, rom16(0x0DC3F8));
  ram16_set(0x6A, rom16(0x0DC3FA));
  r->a = bus16(0x4216);
  MenuToDecimal(r);
  ram16_set(0x6B, ram16(0x7F));
  r->a = bus16(0x4214);
  MenuToDecimal(r);
  r->a = ram16(0x7F);
  ram16_set(0x68, r->a);
}

// $0F:8ADC: the course length as sprites ("NN.N MILES", or KM): the course's length
// ($0D:C6F6) times 10 (MPH) or 15 (KPH) with the hardware multiplier.
void MenuDrawCourseLength(TgRegs* r) {
  for(int x = 0x10; x >= 0; x -= 2) ram16_set((uint16_t) (0x66 + x), rom16(0x0FAC70 + (uint32_t) x));
  bool kph = ram16(0x1FB0) != 0;
  if(kph) {
    ram16_set(0x6F, 0x4D4B);  // "KM"
    ram16_set(0x71, 0x2020);
    ram16_set(0x73, 0x0020);
    ram16_set(0x66, 0x0E13);
    ram16_set(0x68, 0x0003);
  }
  tg_bus_write8(0x4202, tg_rom(0x0DC6F6 + ram16(CarTypeIndex))[0]);
  tg_bus_write8(0x4203, g_ram[0x1FB0] != 0 ? 0x0F : 0x0A);  // only the low byte here
  uint8_t hi = bus8(0x4217);
  tg_bus_write8(0x4202, bus8(0x4216));
  tg_bus_write8(0x4203, 0x64);
  g_ram[0x6A] = hi | 0x30;
  r->a = bus8(0x4217);
  MenuToDecimal(r);
  ram16_set(0x6C, r->a);
  r->x = 0x0066;
  MenuDrawTextSpritesAt(r);
}

// $0F:8A81: the course's lap record ($1FB2) and its number as sprites.
void MenuDrawLapRecord(TgRegs* r) {
  r->a = ram16((uint16_t) (0x1FB2 + ((ram16(CarTypeIndex) & 0x00FF) << 1)));
  MenuFormatTime(r);
  uint16_t t68 = ram16(0x68), t6a = ram16(0x6A), t6c = ram16(0x6C), t6e = ram16(0x6E);
  for(int x = 0x0E; x >= 0; x -= 2) ram16_set((uint16_t) (0x66 + x), rom16(0x0FAC81 + (uint32_t) x));
  ram16_set(0x6F, t6e);
  ram16_set(0x6D, t6c);
  ram16_set(0x6B, t6a);
  g_ram[0x6A] = (uint8_t) (t68 >> 8);
  r->x = 0x0066;
  MenuDrawTextSpritesAt(r);
  ram16_set(0x66, 0x1616);
  ram16_set(0x6B, 0);
  ram16_set(0x6D, 0);
  g_ram[0x6A] = tg_rom(0x0DC6D6 + ram16(CarTypeIndex))[0] | 0x30;
  r->x = 0x0066;
  MenuDrawTextSpritesAt(r);
}

// A driver's name into $68-$6F: the players' own ($81 = 0 or 1), else the computer
// drivers' from $0D:C47B (player 2's slot is computer driver $12 in a one-player game).
static void driver_name(TgRegs* r, uint16_t driver) {
  if(driver == 0 || (driver == 1 && ram16(0x1F04) != 0)) {
    ram16_set(0x81, driver);
    uint16_t y = r->y;
    MenuLoadPlayerName(r);
    r->y = y;
    return;
  }
  uint16_t i = driver == 1 ? 0x12 : (uint16_t) (driver - 2);
  uint16_t x = (uint16_t) (i << 3);
  for(int k = 0; k < 8; k += 2) ram16_set((uint16_t) (0x68 + k), rom16(0x0DC47B + (uint32_t) (x + k)));
  r->a = rom16(0x0DC47B + (uint32_t) (x + 6));
  r->x = x;
}

// Draws the string at $66 from cell $48, then moves $48 on by step.
static void draw_line(TgRegs* r, uint16_t step) {
  uint16_t cell = ram16(0x48), y = r->y;
  ram16_set(0x66, cell);
  r->x = 0x0066;
  MenuDrawText(r);
  r->y = y;
  r->a = (uint16_t) (cell + step);
  ram16_set(0x48, r->a);
}

// $0F:882D: the race results: the finishing order ($1F12) with names, points ($0E:8024 by
// place, added to $1F4E) and times ($1F26, made strictly increasing; behind the winner,
// with '>' in front). The winner's time can set the course record ($1FB2).
void MenuDrawRaceResults(TgRegs* r) {
  r->y = 0x000A;
  r->a = 0x0602;
  MenuBlankBigRows(r);
  ram16_set(0x7D, 0);
  uint16_t x = (uint16_t) ((g_ram[0x1F12]) << 1);
  uint16_t rec = (uint16_t) (0x1FB2 + (uint16_t) (ram16(CarTypeIndex) << 1));
  uint16_t t = ram16((uint16_t) (0x1F26 + x));
  ram16_set(0x7B, t);
  if(t < ram16(rec)) ram16_set(rec, t);
  for(uint16_t y = 1; y != 10; y++) {
    x = (uint16_t) (g_ram[0x1F12 + y] << 1);
    uint16_t tx = (uint16_t) (0x1F26 + x);
    if(ram16(0x7B) >= ram16(tx)) ram16_set(0x7D, (uint16_t) (ram16(0x7D) + 1));
    uint16_t v = (uint16_t) (ram16(tx) + ram16(0x7D));
    ram16_set(tx, v);
    ram16_set(0x7B, v);
  }
  ram16_set(0x48, 0x0602);
  for(r->y = 0; r->y != 10; r->y++) {
    uint16_t driver = g_ram[0x1F12 + r->y];
    g_ram[0x1F4E + driver] = (uint8_t) (g_ram[0x1F4E + driver] + tg_rom(0x0E8024 + r->y)[0]);
    driver_name(r, driver);
    draw_line(r, 0x0200);
  }
  MenuDrawTenLines(r);
  ram16_set(0x48, 0x060D);
  ram16_set(0x7B, ram16((uint16_t) (0x1F26 + (uint16_t) (g_ram[0x1F12] << 1))));
  for(r->y = 0; r->y != 10; r->y++) {
    uint16_t a = r->y == 0 ? ram16(0x7B) : (uint16_t) (ram16((uint16_t) (0x1F26 + (uint16_t) (g_ram[0x1F12 + r->y] << 1))) - ram16(0x7B));
    r->a = a;
    uint16_t y = r->y;
    MenuFormatTime(r);
    r->y = y;
    if(y != 0) ram16_set(0x68, (uint16_t) ((r->a & 0xFF00) | 0x003E));
    draw_line(r, 0x0200);
  }
}

// $0F:894F: the championship standings: $1F62 sorted by points ($1F4E, a bubble sort that
// keeps ties in order), then 20 names and their points ("=" for a leading zero, "PT" for
// one point).
void MenuDrawStandings(TgRegs* r) {
  ram16_set(0x044C, 0x1312);
  for(int x = 0x13; x >= 0; x--) g_ram[0x1F62 + x] = (uint8_t) x;
  for(int pass = 0x13; pass >= 0; pass--) {
    for(int y = 0x12; y >= 0; y--) {
      uint8_t b = g_ram[0x1F63 + y], a = g_ram[0x1F62 + y];
      if(g_ram[0x1F4E + a] < g_ram[0x1F4E + b]) {
        g_ram[0x1F62 + y] = b;
        g_ram[0x1F63 + y] = a;
      }
    }
  }
  r->a = 0x2606;
  r->y = 0x0014;
  MenuBlankSmallRows(r);
  ram16_set(0x48, 0x2606);
  for(r->y = 0; r->y != 0x14; r->y++) {
    driver_name(r, g_ram[0x1F62 + r->y]);
    draw_line(r, 0x0100);
  }
  ram16_set(0x48, 0x2614);
  for(int k = 0; k < 8; k += 2) ram16_set((uint16_t) (0x68 + k), rom16(0x0DC42B + (uint32_t) k));
  for(r->y = 0; r->y != 0x14; r->y++) {
    r->a = g_ram[0x1F4E + g_ram[0x1F62 + r->y]];
    MenuToDecimal(r);
    uint16_t a = r->a;
    if((uint8_t) a == 0x30) {
      a = (uint16_t) ((a & 0xFF00) | 0x3D);
      if((a >> 8) == 0x31) ram16_set(0x6D, 0x2020);
    }
    ram16_set(0x68, a);
    draw_line(r, 0x0100);
    r->a = rom16(0x0DC42F);
    ram16_set(0x6C, r->a);
  }
}

// $0F:93DC: the password typed in at $68 (for country $5C) against each skill level's
// ($0D:C513, $38 bytes per level). A match sets the skill level $1F10; without one, the
// country goes back to the level's highest open one. The level's open countries ($1F0A)
// and the cursor ($5A) become $5C. Returns A = the skill level it was called with.
void MenuCheckPasswords(TgRegs* r) {
  uint16_t saved = ram16(0x1F10);
  ram16_set(0x1F10, 0);
  uint16_t offset = (uint16_t) ((uint16_t) (ram16(0x5C) - 1) << 3);
  bool found = false;
  for(int level = 0; level < 3; level++, offset = (uint16_t) (offset + 0x38)) {
    r->a = offset;
    MenuComparePassword(r);
    if(r->z) {
      found = true;
      break;
    }
    if(level < 2) ram16_set(0x1F10, (uint16_t) (ram16(0x1F10) + 1));
  }
  if(!found) {
    ram16_set(0x1F10, saved);
    ram16_set(0x5C, ram16((uint16_t) (0x1F0A + (uint16_t) (saved << 1))));
  }
  r->y = (uint16_t) (ram16(0x1F10) << 1);
  ram16_set(0x5A, ram16(0x5C));
  ram16_set((uint16_t) (0x1F0A + r->y), ram16(0x5C));
  r->a = saved;
  flags_nz16(r, saved);
}

// $0F:A39B: $32-$35 = $2E * $30, unsigned 16 x 16 bits, through the 8 x 8 hardware
// multiplier.
void MenuMultiply(TgRegs* r) {
  uint8_t a_lo = g_ram[0x2E], a_hi = g_ram[0x2F], b_lo = g_ram[0x30], b_hi = g_ram[0x31];
  tg_bus_write8(0x4202, a_lo);
  tg_bus_write8(0x4203, b_lo);
  ram16_set(0x32, bus16(0x4216));
  tg_bus_write8(0x4202, a_hi);
  tg_bus_write8(0x4203, b_hi);
  ram16_set(0x34, bus16(0x4216));
  for(int i = 0; i < 2; i++) {
    tg_bus_write8(0x4202, i == 0 ? a_hi : a_lo);
    tg_bus_write8(0x4203, i == 0 ? b_lo : b_hi);
    uint32_t sum = (uint32_t) bus16(0x4216) + ram16(0x33);
    ram16_set(0x33, (uint16_t) sum);
    r->a = (uint16_t) sum;
    if(sum > 0xFFFF) ram16_set(0x35, (uint16_t) (ram16(0x35) + 1));
  }
}

// $0F:A360: $32-$35 = A * X, signed.
void MenuMultiplySigned(TgRegs* r) {
  uint16_t a = r->a, x = r->x;
  bool negative = false;
  if(a & 0x8000) {
    a = (uint16_t) -a;
    negative = !negative;
  }
  ram16_set(0x2E, a);
  if(x & 0x8000) {
    x = (uint16_t) -x;
    negative = !negative;
  }
  ram16_set(0x30, x);
  MenuMultiply(r);
  if(negative) {
    uint16_t lo = (uint16_t) -ram16(0x32);
    uint16_t hi = ram16(0x34);
    ram16_set(0x32, lo);
    if(lo == 0) hi--;
    r->x = hi;
    hi = (uint16_t) ~hi;
    ram16_set(0x34, hi);
    r->a = hi;
  }
}

// One element of the mode 7 matrix: (A * X) >> 14, rounded.
static uint16_t matrix_term(TgRegs* r, uint16_t a, uint16_t x) {
  r->a = a;
  r->x = x;
  MenuMultiplySigned(r);
  uint32_t lo = (uint32_t) ram16(0x32) + 0x2000;
  uint16_t v = (uint16_t) (ram16(0x34) + (lo >> 16));
  lo &= 0xFFFF;
  for(int i = 0; i < 2; i++) {
    v = (uint16_t) (v << 1 | (lo >> 15));
    lo = (lo << 1) & 0xFFFF;
  }
  ram16_set(0x32, (uint16_t) lo);
  return v;
}

// $0F:A2AD: the mode 7 matrix ($1E-$25) for angle $12 and scales $16 / $1A (times $14),
// from the sine table at $0D:AC51.
void MenuMode7Matrix(TgRegs* r) {
  r->a = ram16(0x16);
  r->x = ram16(0x14);
  MenuMultiplySigned(r);
  ram16_set(0x18, ram16(0x33));
  r->a = ram16(0x1A);
  r->x = ram16(0x14);
  MenuMultiplySigned(r);
  ram16_set(0x1C, ram16(0x33));
  uint16_t i = (uint16_t) (((ram16(0x12) >> 2) & 0x01FF) << 1);
  uint16_t c = (uint16_t) ((i + 0x0080) & 0x01FF);
  ram16_set(0x1E, matrix_term(r, rom16(0x0DAC51 + c), ram16(0x18)));
  ram16_set(0x20, matrix_term(r, rom16(0x0DAC51 + i), ram16(0x18)));
  ram16_set(0x22, matrix_term(r, (uint16_t) -rom16(0x0DAC51 + i), ram16(0x1C)));
  r->a = matrix_term(r, rom16(0x0DAC51 + c), ram16(0x1C));
  ram16_set(0x24, r->a);
}

// $0F:9CBC: 16 colours from the palette buffer at X split into their red, green and blue
// at $066C + Y (each times 8).
void MenuFadeSplit(TgRegs* r) {
  uint16_t x = r->x, y = r->y;
  for(int i = 0; i < 16; i++, x += 2, y += 6) {
    uint16_t c = ram16((uint16_t) (0x046C + x));
    ram16_set((uint16_t) (0x066C + y), (uint16_t) ((c & 0x001F) << 3));
    ram16_set((uint16_t) (0x066E + y), (uint16_t) ((c & 0x03E0) >> 2));
    r->a = (uint16_t) ((c & 0x7C00) >> 7);
    ram16_set((uint16_t) (0x0670 + y), r->a);
  }
  ram16_set(0x042A, 0xFFFF);
  r->x = x;
  r->y = y;
}

// $0F:9C77: a fade's steps ($066C) for the palette rows marked in $042C: each component /
// 32 (in 8.3 fixed point), negated when fading in ($0422 non-zero).
void MenuFadePrepare(TgRegs* r) {
  MenuClearFadeWork(r);
  r->x = 0;
  r->y = 0;
  do {
    if(ram16((uint16_t) (0x042C + ram16(0x0426))) == 0) {
      r->x = (uint16_t) (r->x + 0x20);
      r->y = (uint16_t) (r->y + 0x60);
    } else {
      MenuFadeSplit(r);
    }
    ram16_set(0x0426, (uint16_t) (ram16(0x0426) + 2));
  } while(r->x < 0x0200);
  r->a = ram16(0x0422);
  if(r->a != 0) {
    for(r->y -= 2; !(r->y & 0x8000); r->y -= 2) {
      uint16_t o = (uint16_t) (0x066C + r->y);
      r->a = (uint16_t) -ram16(o);
      ram16_set(o, r->a);
    }
  }
}

// $0F:9D31: one fade step for the colours from X up to $0428: each component plus its
// step, in 8.3 fixed point (fractions at $146C).
void MenuFadeStep(TgRegs* r) {
  uint16_t x = r->x, y = r->y, a = 0;
  do {
    static const struct { uint16_t mask, keep; int shift; } kParts[3] = {
      { 0x001F, 0xFFE0, 0 }, { 0x03E0, 0xFC1F, 5 }, { 0x7C00, 0x83FF, 10 },
    };
    uint16_t p = (uint16_t) (0x046C + x);
    for(int k = 0; k < 3; k++, y += 2) {
      uint16_t c = ram16(p);
      ram16_set(p, c & kParts[k].keep);
      uint16_t v = (uint16_t) (((c & kParts[k].mask) >> kParts[k].shift) << 8 | ram16((uint16_t) (0x146C + y)));
      r->c = false;  // CLC for red; the shifts before green and blue leave it clear
      v = adc16(r, v, ram16((uint16_t) (0x066C + y)));  // V shows in the P an interrupt pushes
      ram16_set((uint16_t) (0x146C + y), v & 0x00FF);
      a = (uint16_t) (ram16(p) | (((v >> 8) & 0x1F) << kParts[k].shift));
      ram16_set(p, a);
    }
    x += 2;
  } while(x != ram16(0x0428));
  r->a = a;
  r->x = x;
  r->y = y;
  r->n = false;
  r->z = true;
  r->c = true;
}

// $0F:9E5E: when fading out, the palette rows marked in $042C set to black.
void MenuBlackOutRows(TgRegs* r) {
  r->a = ram16(0x0422);
  if(r->a != 0) return;
  uint16_t x = 0x01FE;
  for(int y = 0x1E; y >= 0; y -= 2) {
    r->a = ram16((uint16_t) (0x042C + y));
    if(r->a == 0) {
      x = (uint16_t) (x - 0x20);
      r->a = x;
    } else {
      for(int n = 0; n < 16; n++, x -= 2) ram16_set((uint16_t) (0x046C + x), 0);
    }
  }
  r->x = x;
  r->y = 0xFFFE;
}
