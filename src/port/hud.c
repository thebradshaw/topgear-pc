// Race HUD (bank $00).

#include "port.h"
#include "variables.h"

static void vram_address(uint16_t word_adr) { tg_bus_write16(0x2116, word_adr); }
static void vram_write(uint16_t word) { tg_bus_write16(0x2118, word); }

// The original divides with the CPU's divider; the writes are kept so the hardware
// registers end up the same.
static void divide(uint16_t dividend, uint8_t divisor) {
  tg_bus_write16(0x4204, dividend);
  tg_bus_write8(0x4206, divisor);
}

// Speed shown on the speedometer: speed x factor / 1024, using the PPU multiplier
// (signed 16 x signed 8, middle 16 bits of the product) then two right shifts.
static uint16_t shown_speed(uint16_t speed, uint8_t factor) {
  if(speed & 0x8000) return 0;
  tg_bus_write8(0x211b, (uint8_t) speed);
  tg_bus_write8(0x211b, (uint8_t) (speed >> 8));
  tg_bus_write8(0x211c, 0);
  tg_bus_write8(0x211c, factor);
  int32_t product = (int16_t) speed * (int8_t) factor;
  return (uint16_t) ((uint16_t) (product >> 8) >> 2);
}

// A 2x2-tile digit from the table at $00:D666 (8 bytes per digit).
static void draw_big_digit(uint16_t top, uint16_t digit) {
  uint32_t t = 0x00D666 + digit * 8u;
  vram_address(top);
  vram_write(rom16(t));
  vram_write(rom16(t + 2));
  vram_address((uint16_t) (top + 0x20));
  vram_write(rom16(t + 4));
  vram_write(rom16(t + 6));
}

// Three digits, ones first, at VRAM word addresses right, right-2, right-4.
static void draw_speed(uint16_t right, uint16_t value) {
  divide(value, 10);
  uint16_t ones = value % 10, rest = value / 10;
  divide(rest, 10);
  draw_big_digit(right, ones);
  draw_big_digit((uint16_t) (right - 2), rest % 10);
  draw_big_digit((uint16_t) (right - 4), rest / 10);  // not wrapped at 10, as in the original
}

static void draw_gear(uint16_t top, uint16_t gear) {
  if(gear >= 5) gear = 0;  // never happens in the recordings
  uint16_t tile = (uint16_t) (gear * 2 + 0x2C50);
  vram_address(top);
  vram_write(tile);
  vram_address((uint16_t) (top + 0x20));
  vram_write((uint16_t) (tile + 1));
}

static void draw_nitros(uint16_t adr, uint16_t n) {
  vram_address(adr);
  uint16_t d = n & 3;
  if((n & 0x8000) && d != 0) d--;  // negative counts show one less
  vram_write((uint16_t) (0x2C35 + d));
}

// Position 1-20 as up to two 1x2-tile digits plus a two-tile suffix (ST, ND, RD, TH).
static void draw_position(uint16_t top, uint16_t shown_adr, uint16_t actual_adr) {
  uint16_t pos = ram16(shown_adr);
  if(pos & 0x8000) pos = ram16(actual_adr);
  uint16_t suffix = (pos >= 4 ? 3 : pos) * 4;
  uint16_t n = (uint16_t) (pos + 1), tens_top = 0x2C00, tens_bottom = 0x2C00;
  if(n >= 10) {
    n -= 10;
    tens_top = 0x2C50;  // "1"
    tens_bottom = 0x2C51;
    if(n >= 10) {
      n -= 10;
      tens_top = 0x2C52;  // "2"
      tens_bottom = 0x2C53;
    }
  }
  vram_address(top);
  vram_write(tens_top);
  vram_address((uint16_t) (top + 0x20));
  vram_write(tens_bottom);
  uint16_t ones = (uint16_t) (n * 2 + 0x2C4E);
  vram_address((uint16_t) (top + 1));
  vram_write(ones);
  vram_address((uint16_t) (top + 0x21));
  vram_write((uint16_t) (ones + 1));
  vram_write(rom16(0x00CC8B + suffix));
  vram_write(rom16(0x00CC8D + suffix));
}

// m'ss"hh from a count of frames: the player's finishing time, or the race clock while
// it's still negative.
static void draw_time(TgRegs* r, uint16_t left, uint16_t frames_adr) {
  uint16_t t = ram16(frames_adr);
  if(t & 0x8000) t = ram16(RaceTime);
  uint16_t scratch = (uint16_t) (r->d + 0x1A);
  divide(t, 60);
  ram16_set(scratch, t % 60);
  uint16_t total_seconds = t / 60;
  divide(total_seconds, 60);
  uint16_t seconds = total_seconds % 60;
  vram_address(left);
  vram_write((uint16_t) (0x2C35 + total_seconds / 60));
  divide(seconds, 10);
  vram_address((uint16_t) (left + 2));
  vram_write((uint16_t) (0x2C35 + seconds / 10));
  vram_write((uint16_t) (0x2C35 + seconds % 10));
  vram_address((uint16_t) (left + 5));
  uint16_t i = (uint16_t) (ram16(scratch) * 2);
  vram_write(rom16(0x00D72E + i));
  vram_write(rom16(0x00D6B6 + i));
}

// Copies words from a ROM table to RAM at the same running offset y; returns the new y.
static uint16_t copy_words(uint16_t dst, uint16_t src, uint16_t y, int count) {
  for(int i = 0; i < count; i++, y += 2) ram16_set((uint16_t) (dst + y), rom16(src + y));
  return y;
}

// Fuel gauge: 20 segments, 12 in the first row of the buffer and 8 in the second.
// Full segments come from $D7CE and empty ones from $D7A6.
static void draw_fuel(uint16_t buf, uint16_t fuel) {
  uint16_t full = (uint16_t) ((fuel >> 8) >> 3);
  if(full >= 0x15) full = 0x14;  // never happens in the recordings
  uint16_t y = 0;
  if(full < 13) {
    y = copy_words(buf, 0xD7CE, y, full);
    y = copy_words(buf, 0xD7A6, y, 12 - full);
    copy_words((uint16_t) (buf + 0x10), 0xD7A6, y, 8);
  } else {
    uint16_t second = (uint16_t) (full - 12);
    y = copy_words(buf, 0xD7CE, y, 12);
    y = copy_words((uint16_t) (buf + 0x10), 0xD7CE, y, second);
    copy_words((uint16_t) (buf + 0x10), 0xD7A6, y, 8 - second);
  }
}

static void copy_rom(uint16_t dst, uint16_t src, int bytes) {
  for(int i = 0; i < bytes; i++) g_ram[dst + i] = tg_rom(0x000000 | src)[i];
}

// RPM bar: three words at buf, then rows of 13 words at buf+$16 and buf+$36. Segment
// tiles: empty $D7F6 / $D810 / $D816, full $D830 / $D84A / $D850.
static void draw_rpm(uint16_t buf, uint16_t rpm) {
  uint16_t top = buf, row1 = (uint16_t) (buf + 0x16), row2 = (uint16_t) (buf + 0x36);
  uint16_t n = (rpm & 0x8000) ? 0 : (rpm >> 7) & 0x1FF;
  if(n == 0) {
    copy_rom(row1, 0xD7F6, 26);
    copy_words(top, 0xD810, 0, 3);
    copy_rom(row2, 0xD816, 26);
  } else if(n >= 0x1E) {
    copy_rom(row1, 0xD830, 26);
    copy_words(top, 0xD84A, 0, 3);
    copy_rom(row2, 0xD850, 26);
  } else if(n >= 0x11) {
    copy_rom(row1, 0xD830, 26);
    copy_words(top, 0xD84A, 0, 3);
    uint16_t y = copy_words(row2, 0xD850, 0, n - 0x10);
    copy_words(row2, 0xD816, y, 0x1D - n);
  } else if(n >= 0x0E) {
    copy_rom(row1, 0xD830, 26);
    copy_rom(row2, 0xD816, 26);
    uint16_t y = copy_words(top, 0xD84A, 0, n - 0x0D);
    copy_words(top, 0xD810, y, 0x10 - n);
  } else {
    copy_words(top, 0xD810, 0, 3);
    copy_rom(row2, 0xD816, 26);
    uint16_t y = copy_words(row1, 0xD830, 0, n);
    copy_words(row1, 0xD7F6, y, 0x0D - n);
  }
}

// $00:CE61. Redraws the HUD of both views once per race update: speedometers, gears,
// nitro counts, positions and lap times straight into VRAM, and the fuel and RPM bars
// into RAM buffers. Assumes DB = 0 (bank 0 tables); leaves DB = 0 and 16-bit A and index.
// A, X, Y and N/V/Z/C are left as don't-care: no caller reads them.
void RaceDrawHud(TgRegs* r) {
  // Player 1 and 2 pick km/h differently when bit 7 of WarmBootFlag is set; kept as is.
  bool warm = g_ram[WarmBootFlag] & 0x80;
  uint8_t factor1 = warm ? 0x0F : g_ram[RaceP1SpeedKph] ? 0x18 : 0x0F;
  uint8_t factor2 = warm ? 0x18 : g_ram[RaceP2SpeedKph] ? 0x18 : 0x0F;
  draw_speed(0x0C5B, shown_speed(ram16(RaceP1Speed), factor1));
  draw_speed(0x0E1B, shown_speed(ram16(RaceP2Speed), factor2));
  draw_gear(0x0D66, ram16(RaceP1Gear));
  draw_gear(0x0F26, ram16(RaceP2Gear));
  draw_nitros(0x0D43, ram16(RaceP1Nitros));
  draw_nitros(0x0F03, ram16(RaceP2Nitros));
  draw_position(0x0D79, RaceP1PositionShown, RaceP1Position);
  draw_position(0x0F39, RaceP2PositionShown, RaceP2Position);
  draw_time(r, 0x0C98, RaceP1FinishTime);
  draw_time(r, 0x0E58, RaceP2FinishTime);
  draw_fuel(RaceP1FuelBar, ram16(RaceP1Fuel));
  draw_fuel(RaceP2FuelBar, ram16(RaceP2Fuel));
  draw_rpm(RaceP1RpmBar, ram16(RaceP1Rpm));
  draw_rpm(RaceP2RpmBar, ram16(RaceP2Rpm));
  r->db = 0;  // from MVN
}
