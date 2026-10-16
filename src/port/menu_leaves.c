// Menu routines (bank $0F) that call nothing: buffer fills and copies, names, small
// sums. The menus run with DB = $0F, so absolute addresses below $2000 are work RAM.
// docs/notes/menus.md maps the bank.

#include <stddef.h>

#include "port.h"
#include "variables.h"

// Palette, tilemap and OAM buffers.
enum {
  kMenuPalette = 0x046C,  // $200 bytes, CGRAM 0-255
  kMenuTilemap = 0x0C6C,  // $1000 bytes, VRAM 0
  kMenuOam = 0x0202,      // $220 bytes
  kPlayer1Name = 0x1F86,  // 8 characters
  kPlayer2Name = 0x1FA3,
};

static void copy_rom(uint16_t dst, uint32_t src, uint16_t last) {
  for(int x = last; x >= 0; x -= 2) ram16_set((uint16_t) (dst + x), rom16(src + (uint32_t) x));
}

static void fill(uint16_t dst, uint16_t value, uint16_t last) {
  for(int x = last; x >= 0; x -= 2) ram16_set((uint16_t) (dst + x), value);
}

// The end of a copy or fill loop counting X down by two from an even value.
static void loop_end(TgRegs* r) {
  r->x = 0xFFFE;
}

// $0F:A1C4: the sprite origin $58 = $48 * 8 - $100.
void MenuSpriteOrigin(TgRegs* r) {
  r->a = (uint16_t) ((uint16_t) (ram16(0x48) << 3) - 0x0100);
  ram16_set(0x58, r->a);
}

// $0F:A1D0: $4C = the tilemap buffer address of the cell $48 (column in the low five
// bits, row in bits 8-12).
void MenuTilemapAddr(TgRegs* r) {
  uint16_t t = (uint16_t) ((ram16(0x48) >> 3) & 0xFFE0);
  t = (uint16_t) (t + g_ram[0x48]);
  r->a = (uint16_t) ((uint16_t) (t << 1) + kMenuTilemap);
  ram16_set(0x4C, r->a);
}

// $0F:A1EB: palette buffer to black.
void MenuClearPalette(TgRegs* r) {
  fill(kMenuPalette, 0, 0x01FE);
  loop_end(r);
}

// $0F:A1F6 and $0F:A21A (the NMI's copy): DMA channel 1 set up to copy the palette buffer
// to CGRAM.
void MenuSetPaletteDma(TgRegs* r) {
  tg_bus_write8(0x420B, 0x00);
  tg_bus_write8(0x2121, 0x00);
  tg_bus_write8(0x4314, 0x00);
  tg_bus_write16(0x4312, kMenuPalette);
  tg_bus_write16(0x4315, 0x0200);
  tg_bus_write16(0x4310, 0x2202);
  r->a = 0x2202;
}

void MenuSetPaletteDmaNmi(TgRegs* r) { MenuSetPaletteDma(r); }

// $0F:A23E: the palette buffer from $0E:FB86.
void MenuLoadBasePalette(TgRegs* r) {
  copy_rom(kMenuPalette, 0x0EFB86, 0x01FE);
  loop_end(r);
}

// $0F:A258: the mode 7 matrix and BG1 scroll from direct page $1E-$2D.
void MenuWriteMode7Regs(TgRegs* r) {
  static const uint16_t regs[8] = { 0x211B, 0x211C, 0x211D, 0x211E, 0x211F, 0x2120, 0x210D, 0x210E };
  for(int i = 0; i < 8; i++) {
    tg_bus_write8(regs[i], g_ram[0x1E + 2 * i]);
    tg_bus_write8(regs[i], g_ram[0x1F + 2 * i]);
  }
  r->a = (uint16_t) ((r->a & 0xFF00) | g_ram[0x2D]);
}

// $0F:90D7, $0F:820E, $0F:8C68, $0F:8C86: 16 palette or fade words from bank $0E.
void MenuLoadPaletteRow1(TgRegs* r) { copy_rom(0x048C, 0x0EFCA6, 0x1E); loop_end(r); }
void MenuSetFadeRowsIntro(TgRegs* r) { copy_rom(0x042C, 0x0EFDC6, 0x1E); loop_end(r); }
void MenuLoadPaletteRow9(TgRegs* r) { copy_rom(0x058C, 0x0EFCC6, 0x1E); loop_end(r); }
void MenuLoadPaletteRow15b(TgRegs* r) { copy_rom(0x064C, 0x0EFD66, 0x1E); loop_end(r); }

// $0F:8705: the same as $0F:8C86, with colour 1 changed.
void MenuLoadPaletteRow15(TgRegs* r) {
  copy_rom(0x064C, 0x0EFD66, 0x1E);
  loop_end(r);
  r->a = 0x233D;
  ram16_set(0x064E, r->a);
}

// $0F:8F80: no rows fading.
void MenuClearFadeRows(TgRegs* r) { fill(0x042C, 0, 0x1E); loop_end(r); }

// $0F:8FB5: high OAM for the first 28 sprites: large, X high bit clear.
void MenuOamHighFill(TgRegs* r) {
  fill(0x0402, 0xAAAA, 0x0C);
  loop_end(r);
  r->a = 0xAAAA;
}

// $0F:9C5B: the tilemap buffer cleared.
void MenuClearTilemapBuffer(TgRegs* r) { fill(kMenuTilemap, 0, 0x0FFE); loop_end(r); }

// $0F:9C66: the fade work areas cleared.
void MenuClearFadeWork(TgRegs* r) {
  ram16_set(0x0426, 0);
  fill(0x066C, 0, 0x05FE);
  fill(0x146C, 0, 0x05FE);
  loop_end(r);
}

// $0F:A691: every sprite off screen (Y = $F0), high OAM cleared.
void MenuClearOam(TgRegs* r) {
  fill(kMenuOam, 0xF000, 0x01FE);
  fill(0x0402, 0, 0x1E);
  loop_end(r);
  r->a = 0xF000;
}

// $0F:8BB6: A (normally 0-99) as two ASCII digits, tens in the low byte, in A and $7F.
void MenuToDecimal(TgRegs* r) {
  uint16_t a = r->a, x = 0;
  for(;;) {
    bool borrow = a < 10;
    a = (uint16_t) (a - 10);
    if(borrow) break;
    if(++x == 0) break;
  }
  a = (uint16_t) (a + 10);
  a |= 0x3030;
  a = (uint16_t) (a << 8 | a >> 8);
  for(x--; !(x & 0x8000); x--) {
    if(++a == 0) break;
  }
  ram16_set(0x7F, a);
  r->a = a;
  r->x = x;
}

static uint16_t player_name(void) { return ram16(0x81) != 0 ? kPlayer2Name : kPlayer1Name; }

// $0F:9B7B: player $81's name into direct page $68-$6F; $66 = 8 + the number of spaces
// at its end (not counting the first character).
void MenuLoadPlayerName(TgRegs* r) {
  uint16_t name = player_name();
  for(int i = 0; i < 8; i += 2) ram16_set((uint16_t) (0x68 + i), ram16((uint16_t) (name + i)));
  uint16_t y = 0, x = 7;
  while(g_ram[0x68 + x] == 0x20) {
    y++;
    if(--x == 0) break;
  }
  uint8_t a = (uint8_t) (y + 8);
  g_ram[0x66] = a;
  r->a = (uint16_t) ((ram16((uint16_t) (name + 6)) & 0xFF00) | a);
  r->x = x;
  r->y = y;
}

// $0F:83A1: player $81's name back to the default.
void MenuDefaultPlayerName(TgRegs* r) {
  uint16_t name = player_name();
  uint32_t src = ram16(0x81) != 0 ? 0x0FB4C9 : 0x0FB4BC;
  for(int i = 0; i < 8; i += 2) ram16_set((uint16_t) (name + i), rom16(src + (uint32_t) i));
  r->a = ram16((uint16_t) (name + 6));
}

// $0F:9BC4: direct page $68-$6F back to player $81's name, or with $5E = 0 (nothing
// entered) the default name.
void MenuStorePlayerName(TgRegs* r) {
  if(ram16(0x5E) == 0) {
    MenuDefaultPlayerName(r);
    return;
  }
  uint16_t name = player_name();
  for(int i = 0; i < 8; i += 2) ram16_set((uint16_t) (name + i), ram16((uint16_t) (0x68 + i)));
  r->a = ram16(0x6E);
}

// $0F:9B56: sprite 0 (the cursor) under letter $5E of the name being entered.
void MenuPlaceEntryCursor(TgRegs* r) {
  uint16_t a = ram16(0x5E);
  if(a == 8) a--;
  ram16_set(kMenuOam, (uint16_t) ((uint16_t) (a << 4) + 0x3840));
  r->a = 0x0EC2;
  ram16_set(kMenuOam + 2, r->a);
}

// $0F:80F4: blank names, default controls ($0D:C656) and lap records ($0D:C696), and
// $1FB0 = $FFFF.
void ResetNamesAndRecords(TgRegs* r) {
  for(int i = 0; i < 8; i += 2) {
    ram16_set((uint16_t) (kPlayer1Name + i), 0x2020);
    ram16_set((uint16_t) (kPlayer2Name + i), 0x2020);
  }
  for(int x = 0x0E; x >= 0; x -= 2) {
    uint16_t v = rom16(0x0DC656 + (uint32_t) x);
    ram16_set((uint16_t) (0x1F76 + x), v);
    ram16_set((uint16_t) (0x1F93 + x), v);
  }
  copy_rom(0x1FB2, 0x0DC696, 0x3E);
  ram16_set(0x1FB0, 0xFFFF);  // INX after the loop: X = $FFFF
  r->a = rom16(0x0DC696);
  r->x = 0xFFFF;
}

// $0F:90E6: before the demo race: the course saved, a course and the race's random seed
// from the frame counter.
void MenuStartDemo(TgRegs* r) {
  ram16_set(0x1F08, ram16(CarTypeIndex));
  ram16_set(0x1F00, 0);
  ram16_set(0x1F02, 1);
  uint16_t t = ram16(0x1FFC);
  ram16_set(0x1FF4, t);
  r->a = t & 0x001F;
  ram16_set(CarTypeIndex, r->a);
}

// $0F:9426: compares the password typed in at $68 with the one at $0D:C513 + A. Z set if
// they're the same.
void MenuComparePassword(TgRegs* r) {
  uint16_t x = r->a, y = 0, a;
  for(;;) {
    a = rom16(0x0DC513 + (uint32_t) x);
    cmp16(r, a, ram16((uint16_t) (0x68 + y)));
    if(!r->z) break;
    x += 2;
    y += 2;
    cmp16(r, y, 0x0008);
    if(r->z) break;
  }
  r->a = a;
  r->x = x;
  r->y = y;
}

// $0F:911C / $0F:9102: an 8 x 19 block of tiles from $7E:2000 + X into the tilemap buffer
// at Y, adding $8E (the tile base). From $0F:9102 the block and base depend on A (the
// car type).
static void draw_portrait(TgRegs* r, uint16_t a, uint16_t x) {
  uint16_t base = (uint16_t) (a + ram16(0x8E));
  ram16_set(0x8E, base);
  uint16_t y = r->y;
  for(int row = 0; row < 19; row++) {
    for(int col = 0; col < 8; col++, x += 2, y += 2) {
      ram16_set((uint16_t) (kMenuTilemap + y), (uint16_t) (ram16((uint16_t) (0x2000 + x)) + base));
    }
    y = (uint16_t) (y + 0x30);
  }
  ram16_set(0x8A, 0xFFFF);
  ram16_set(0x8C, 0xFFFF);
  r->a = y;
  r->x = x;
  r->y = y;
}

void MenuDrawCarPortrait(TgRegs* r) { draw_portrait(r, 0, 0); }

void MenuDrawCarPortraitAt(TgRegs* r) {
  if(r->a == 0 || r->a == 3) draw_portrait(r, 0x0025, 0x0130);
  else draw_portrait(r, 0x00A1, 0x0260);
}

// $0F:9BF9: the control pad picture for option $5A from $7E:2000 into the tilemap buffer.
void MenuDrawControlPad(TgRegs* r) {
  for(uint16_t x = 0x07FE; x != 0; x -= 2) ram16_set((uint16_t) (kMenuTilemap + x), 0);
  uint16_t i = (uint16_t) (ram16(0x5A) << 1);
  uint16_t base = rom16(0x0DC622 + i);
  ram16_set(0x83, base);
  uint16_t y = rom16(0x0DC61A + i);
  uint16_t x = rom16(0x0DC62A + i);
  for(int n = 0; n < 0x140; n++, x += 2, y += 2) {
    ram16_set((uint16_t) (kMenuTilemap + y), (uint16_t) (ram16((uint16_t) (0x2000 + x)) + base));
  }
  ram16_set(0x044C, 0xFFFF);
  r->a = (uint16_t) (ram16((uint16_t) (0x2000 + x - 2)) + base);
  r->x = x;
  r->y = y;
}

// $0F:9AD6: in KPH, the car statistics screen's numbers from $0D:C642 / $0D:C64E (stored
// as characters + $15).
void MenuDrawCarStats(TgRegs* r) {
  if(ram16(0x1FB0) == 0) {
    r->a = 0;
    return;
  }
  ram16_set(0x11DC, 0x000B);
  ram16_set(0x1200, 0x001C);
  ram16_set(0x1202, 0x001B);
  ram16_set(0x1204, 0x001B);
  uint16_t y = (uint16_t) (ram16(0x5A) << 1);
  uint16_t x = rom16(0x0FB4F6 + y);
  static const uint16_t dst[5] = { 0x11D4, 0x11D6, 0x11D8, 0x1214, 0x1218 };
  r->c = true;
  for(int i = 0; i < 5; i++) {
    uint32_t src = i < 3 ? 0x0DC642 + (uint32_t) x + (uint32_t) i : 0x0DC64E + (uint32_t) y + (uint32_t) (i - 3);
    uint16_t v = sbc16(r, tg_rom(src)[0], 0x0015);
    ram16_set(dst[i], v);
    r->a = v;
  }
  r->x = y;
  r->y = y;
}

// $0F:8F8B: the title logo's sprites: 13 tiles $0300-$030C in a row from (X $48, Y $67).
void MenuTitleLogoSprites(TgRegs* r) {
  uint16_t y = 0, a = 0x0300;
  ram16_set(kMenuOam, 0x6748);
  for(;;) {
    ram16_set((uint16_t) (kMenuOam + 2 + y), a);
    y += 4;
    ram16_set((uint16_t) (kMenuOam + y), (uint16_t) (ram16((uint16_t) (kMenuOam - 4 + y)) + 0x10));
    a += 2;
    if(a >= 0x030C) break;
  }
  ram16_set((uint16_t) (kMenuOam + 2 + y), a);
  r->a = a;
  r->y = y;
}

// $0F:8FFB: sprites for the non-blank tiles of the 8 x 16 text block at $7E:7F00, rows
// 16 pixels apart from (X 0, Y $37).
void MenuTitleTextSprites(TgRegs* r) {
  uint16_t y = 0;
  ram16_set(kMenuOam, 0x3700);
  ram16_set(0x044C, 0x0010);
  uint16_t a = 0;
  for(uint16_t x = 0; x != 0x0080; x += 2) {
    a = ram16((uint16_t) (0x7F00 + x)) | 0x3000;
    ram16_set((uint16_t) (kMenuOam + 2 + y), a);
    if(a != 0x3000) {
      ram16_set((uint16_t) (kMenuOam + 4 + y), ram16((uint16_t) (kMenuOam + y)));
      y += 4;
    }
    a = (uint16_t) (ram16((uint16_t) (kMenuOam + y)) + 0x10);
    ram16_set((uint16_t) (kMenuOam + y), a);
    uint16_t n = (uint16_t) (ram16(0x044C) - 1);
    ram16_set(0x044C, n);
    if(n == 0) {
      ram16_set(0x044C, 0x0010);
      a = (uint16_t) (0x0F00 + ram16((uint16_t) (kMenuOam + y)));
      ram16_set((uint16_t) (kMenuOam + y), a);
    }
    r->x = (uint16_t) (x + 2);
  }
  r->a = a;
  r->y = y;
}

// $0F:9DDD: DMA channel 7 set up for the OAM buffer, then $0F:9DFC: channel 2 for the
// tilemap buffer to VRAM 0. The caller starts them through $0424 (in the NMI).
void MenuQueueTilemap(TgRegs* r) {
  tg_bus_write16(0x2116, 0x0000);
  tg_bus_write8(0x4324, 0x00);
  tg_bus_write16(0x4320, 0x1801);
  tg_bus_write16(0x4322, kMenuTilemap);
  tg_bus_write16(0x4325, 0x1000);
  r->a = 0x1000;
}

void MenuQueueOamAndMap(TgRegs* r) {
  tg_bus_write16(0x2102, 0x0000);
  tg_bus_write8(0x4374, 0x00);
  tg_bus_write16(0x4370, 0x0402);
  tg_bus_write16(0x4372, kMenuOam);
  tg_bus_write16(0x4375, 0x0220);
  MenuQueueTilemap(r);
}

// $0F:87D9: blanks at the end of each name become '=' (the name entry screen's empty
// marker).
void MenuMarkNameEnds(TgRegs* r) {
  static const uint16_t names[2] = { kPlayer1Name, kPlayer2Name };
  uint16_t x = 0;
  for(int p = 0; p < 2; p++) {
    for(x = 7; !(x & 0x8000) && g_ram[names[p] + x] == 0x20; x--) g_ram[names[p] + x] = 0x3D;
  }
  r->x = x;
}

// $0F:8802: '=' in the names back to blanks.
void MenuUnmarkNameEnds(TgRegs* r) {
  static const uint16_t names[2] = { kPlayer1Name, kPlayer2Name };
  for(int p = 0; p < 2; p++) {
    for(int x = 7; x >= 0; x--) {
      if(g_ram[names[p] + x] == 0x3D) g_ram[names[p] + x] = 0x20;
    }
  }
  r->x = 0xFFFF;
}

// $0F:86E7: each player's place in the standings ($1F62, by driver) at $1FF8 / $1FFA,
// counting from 1. The index uses A's high byte as it came in (TAX with 8-bit A).
void MenuRecordPlayerPlaces(TgRegs* r) {
  uint8_t hi = (uint8_t) (r->a >> 8);
  uint8_t a = (uint8_t) r->a;
  for(int y = 0x13; y >= 0; y--) {
    a = g_ram[0x1F62 + y];
    if(a < 2) {
      uint16_t x = (uint16_t) (hi << 8 | (uint8_t) (a << 1));
      a = (uint8_t) y;
      g_ram[(uint16_t) (0x1FF8 + x)] = a;
    }
  }
  g_ram[0x1FF8]++;
  g_ram[0x1FFA]++;
  r->a = (uint16_t) (hi << 8 | a);
  r->y = 0xFFFF;
}

// $0F:871A: carry clear if a player ($1F04 is the number of players minus one) finished
// in the first five ($1F12, the finishing order).
void MenuCheckQualified(TgRegs* r) {
  uint16_t players = (uint16_t) (ram16(0x1F04) + 1);
  uint16_t y = 4, a = 0;
  r->c = true;
  for(;;) {
    a = g_ram[0x1F12 + y];
    if(a < players) {
      r->c = false;
      break;
    }
    if(y-- == 0) {
      r->c = true;
      break;
    }
  }
  ram16_set(0x1F04, (uint16_t) (players - 1));
  flags_nz16(r, (uint16_t) (players - 1));
  r->a = a;
  r->y = y;
}

// $0F:8733: on to the next course. A = 1 (Z clear) within a country; at the end of one
// ($0F:8750), the points are cleared and A = 0, with the carry clear if a player is among
// the first ones in the standings.
void MenuAdvanceCourse(TgRegs* r) {
  uint16_t players = (uint16_t) (ram16(0x1F04) + 1);
  ram16_set(0x1F04, players);
  uint16_t course = (uint16_t) ((ram16(CarTypeIndex) + 1) & 0x001F);
  ram16_set(CarTypeIndex, course);
  if(course & 0x0003) {
    ram16_set(0x1F04, (uint16_t) (players - 1));
    r->c = false;
    r->a = 1;
    r->n = false;
    r->z = false;
    return;
  }
  for(int x = 0x12; x >= 0; x--) ram16_set((uint16_t) (0x1F4E + x), 0);
  uint16_t y = 2;
  while(g_ram[0x1F62 + y] == g_ram[0x1F63 + y]) {
    if(++y == 0) break;
  }
  r->c = true;
  for(;;) {
    if(g_ram[0x1F62 + y] < players) {
      r->c = false;
      break;
    }
    if((--y) & 0x8000) break;
  }
  ram16_set(0x1F04, (uint16_t) (players - 1));
  r->a = 0;
  r->n = false;
  r->z = true;
  r->x = 0xFFFF;
  r->y = y;
}

void MenuDecompressBegin(TgRegs* r);
void MenuDecompressNext(TgRegs* r);

// $0F:B306: decompresses the stream at bank (A low byte):X to VRAM word address Y, VMAIN
// from A's high byte. Every word is XORed with $4E (cleared here; $0F:B2F6 sets it).
static void decompress_to_vram(TgRegs* r) {
  uint8_t vmain = (uint8_t) (r->a >> 8);
  ram16_set(0x37, r->x);
  g_ram[0x39] = (uint8_t) r->a;
  tg_bus_write8(0x2115, vmain & 0x8F);
  tg_bus_write16(0x2116, r->y);
  MenuDecompressBegin(r);
  for(;;) {
    uint8_t lo = (uint8_t) r->a;
    g_ram[0x85] = lo;
    MenuDecompressNext(r);
    uint16_t w = (uint16_t) (((uint8_t) r->a << 8 | lo) ^ ram16(0x4E));
    r->a = w;
    flags_nz16(r, w);
    tg_bus_write16(0x2118, w);
    if(r->c) return;
    MenuDecompressNext(r);
  }
}

void MenuDecompressToVram(TgRegs* r) {
  ram16_set(0x4E, 0);
  decompress_to_vram(r);
}

// $0F:B2F6: the same, XORing each word with the tile attributes for palette $44
// ($0D:C5E0).
void MenuDecompressToVramPal(TgRegs* r) {
  ram16_set(0x4E, rom16(0x0DC5E0 + (uint16_t) (ram16(0x44) << 1)));
  decompress_to_vram(r);
}

// $0F:B334: decompresses the stream at bank (A low byte):X to $7E:2000 + Y, a word at a
// time.
void MenuDecompressToRam(TgRegs* r) {
  ram16_set(0x37, r->x);
  g_ram[0x39] = (uint8_t) r->a;
  uint16_t x = r->y;
  MenuDecompressBegin(r);
  for(;;) {
    uint8_t lo = (uint8_t) r->a;
    g_ram[0x85] = lo;
    MenuDecompressNext(r);
    uint16_t w = (uint16_t) ((uint8_t) r->a << 8 | lo);
    r->a = w;
    ram16_set((uint16_t) (0x2000 + x), w);
    flags_nz16(r, x);  // from the PLX
    if(r->c) break;
    x += 2;
    MenuDecompressNext(r);
  }
  r->x = x;
}

// $0F:A402: $D0 4-bit-per-pixel tiles at $7E:6000 + Y converted to mode 7's byte per
// pixel at $7E:2000 + $40 (from 0), each non-zero pixel moved to colours $10-$1F. Each tile
// is copied to $044C first and rotated out a bit at a time; the carry into the first
// rotation is set (from the copy loop's CPY), so the copies' leftovers are kept exactly.
void MenuTilesToMode7(TgRegs* r) {
  ram16_set(0x40, 0);
  uint16_t src = r->y, x = 0;
  for(int tile = 0; tile < 0xD0; tile++) {
    for(int i = 0; i < 0x20; i += 2) ram16_set((uint16_t) (0x044C + i), ram16((uint16_t) (0x6000 + src + i)));
    src = (uint16_t) (src + 0x20);
    bool c = true;
    x = 0;
    for(int row = 0; row < 8; row++, x += 2) {
      for(int px = 0; px < 8; px++) {
        static const uint16_t planes[4] = { 0x045D, 0x045C, 0x044D, 0x044C };
        uint8_t a = 0;
        for(int p = 0; p < 4; p++) {
          uint8_t m = g_ram[planes[p] + x];
          g_ram[planes[p] + x] = (uint8_t) (m << 1 | c);
          c = m >> 7;
          bool ca = a >> 7;
          a = (uint8_t) (a << 1 | c);
          c = ca;
        }
        if(a != 0) a |= 0x10;
        uint16_t out = ram16(0x40);
        g_ram[(uint16_t) (0x2000 + out)] = a;
        ram16_set(0x40, (uint16_t) (out + 1));
      }
    }
  }
  g_ram[0x2E] = 0;
  g_ram[0x2F] = 0;
  g_ram[0x30] = 0;
  r->x = src;
  r->y = 0x000E;  // the last row's index, kept in Y across the store
}

// $0F:9C42: VRAM words 0-$3FF cleared, then the tilemap buffer.
void MenuClearScreen(TgRegs* r) {
  tg_bus_write16(0x2116, 0x0000);
  for(int i = 0; i < 0x400; i++) tg_bus_write16(0x2118, 0x0000);
  MenuClearTilemapBuffer(r);
  r->a = 0;
}

// $0F:836D: one 8 x 8 tile of the course map ($7E:7000 + X, 16 words) to VRAM, and its
// entry in the map's tilemap ($146C + Y): the next tile number ($044C), or the empty tile
// $2C90 if it's blank (not uploaded). X moves on to the next tile.
void MenuUploadMapTile(TgRegs* r) {
  uint16_t x = r->x;
  bool blank = true;
  for(int i = 0; i < 0x20; i += 2) {
    if(ram16((uint16_t) (0x7000 + x + i)) != 0) {
      blank = false;
      break;
    }
  }
  if(blank) {
    r->a = 0x2C90;
    ram16_set((uint16_t) (0x146C + r->y), r->a);
  } else {
    for(int i = 0; i < 0x20; i += 2) tg_bus_write16(0x2118, ram16((uint16_t) (0x7000 + x + i)));
    r->a = ram16(0x044C);
    ram16_set((uint16_t) (0x146C + r->y), r->a);
    ram16_set(0x044C, (uint16_t) (r->a + 1));
  }
  r->x = (uint16_t) (x + 0x20);
}

static void upload_map_tilemap(uint16_t vram) {
  uint16_t y = 0;
  for(int row = 0; row < 8; row++, vram = (uint16_t) (vram + 0x20)) {
    tg_bus_write16(0x2116, vram);
    ram16_set(0x044C, vram);
    for(int col = 0; col < 8; col++, y += 2) tg_bus_write16(0x2118, ram16((uint16_t) (0x146C + y)) | 0x1C00);
  }
}

// $0F:82F0: the course map for the race HUD: tile $2C90 blank and the map's 64 tiles from
// VRAM $0900 (blank ones shared), then its two 8 x 8 tilemaps at VRAM $0C21 and $0DE1.
void MenuUploadHudMap(TgRegs* r) {
  tg_bus_write16(0x2116, 0x0900);
  ram16_set(0x044C, 0x2C91);
  for(int i = 0; i < 0x10; i++) tg_bus_write16(0x2118, 0x0000);
  r->x = 0;
  for(r->y = 0; r->y < 0x80; r->y += 2) MenuUploadMapTile(r);
  upload_map_tilemap(0x0C21);
  upload_map_tilemap(0x0DE1);
  r->x = 0;
  r->y = 0x80;
}

// $0F:A644: OAM, all of VRAM and CGRAM cleared by DMA from a zero byte ($0F:A67D), and
// the OAM buffer cleared.
void MenuClearVideo(TgRegs* r) {
  tg_bus_write8(0x420B, 0x00);
  tg_bus_write8(0x4304, 0x0F);
  tg_bus_write8(0x4314, 0x0F);
  tg_bus_write8(0x4324, 0x0F);
  tg_bus_write16(0x4302, 0xA67D);
  tg_bus_write16(0x4312, 0xA67D);
  tg_bus_write16(0x4322, 0xA67D);
  tg_bus_write16(0x4300, 0x040A);
  tg_bus_write16(0x4310, 0x1809);
  tg_bus_write16(0x4320, 0x220A);
  tg_bus_write16(0x4305, 0x0220);
  tg_bus_write16(0x4315, 0x0000);
  tg_bus_write16(0x4325, 0x0200);
  tg_bus_write8(0x420B, 0x07);
  MenuClearOam(r);
}

// $0F:A53C: the PPU's registers set for the menus (screen off, mode 1), the IRQ timer at
// line 1, interrupts on, then $0F:A644. The 8-bit index registers clear Y's high byte.
void MenuInitVideo(TgRegs* r) {
  static const uint16_t writes[][2] = {
    { 0x2100, 0x8F }, { 0x2101, 0x6B }, { 0x2102, 0x00 }, { 0x2103, 0x00 }, { 0x2105, 0x01 },
    { 0x2106, 0x00 }, { 0x2107, 0x00 }, { 0x2108, 0x08 }, { 0x2109, 0x0C }, { 0x210A, 0x00 },
    { 0x210B, 0x41 }, { 0x210C, 0x44 },
    { 0x210D, 0x00 }, { 0x210D, 0x00 }, { 0x210E, 0x00 }, { 0x210E, 0x00 }, { 0x210F, 0x00 },
    { 0x210F, 0x00 }, { 0x2110, 0x00 }, { 0x2110, 0x00 }, { 0x2111, 0x00 }, { 0x2111, 0x00 },
    { 0x2112, 0x00 }, { 0x2112, 0x00 }, { 0x2113, 0x00 }, { 0x2113, 0x00 }, { 0x2114, 0x00 },
    { 0x2114, 0x00 }, { 0x2115, 0x80 }, { 0x2116, 0x00 }, { 0x2117, 0x00 }, { 0x211A, 0xC0 },
    { 0x211B, 0x00 }, { 0x211B, 0x00 }, { 0x211C, 0x00 }, { 0x211C, 0x00 }, { 0x211D, 0x00 },
    { 0x211D, 0x00 }, { 0x211E, 0x00 }, { 0x211E, 0x00 }, { 0x211F, 0x00 }, { 0x211F, 0x00 },
    { 0x2120, 0x00 }, { 0x2120, 0x00 }, { 0x2121, 0x00 }, { 0x2123, 0x00 }, { 0x2124, 0x00 },
    { 0x2125, 0x00 }, { 0x2126, 0x00 }, { 0x2127, 0x00 }, { 0x2128, 0x00 }, { 0x2129, 0x00 },
    { 0x212A, 0x00 }, { 0x212B, 0x00 }, { 0x212C, 0x17 }, { 0x212D, 0x02 }, { 0x212E, 0x00 },
    { 0x212F, 0x00 }, { 0x2130, 0x02 }, { 0x2131, 0x20 }, { 0x2132, 0xE0 }, { 0x2133, 0x00 },
    { 0x213E, 0x00 }, { 0x213F, 0x00 }, { 0x4207, 0x84 }, { 0x4208, 0x00 }, { 0x4209, 0x01 },
    { 0x420A, 0x00 }, { 0x420D, 0x01 },
  };
  for(size_t i = 0; i < sizeof(writes) / sizeof(writes[0]); i++) tg_bus_write8(writes[i][0], (uint8_t) writes[i][1]);
  tg_cpu_cli();
  r->y &= 0x00FF;
  MenuClearVideo(r);
}
