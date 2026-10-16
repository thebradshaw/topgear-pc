// Menu text and boxes (bank $0F). A screen's text is a list of strings, each a word
// giving its cell ($48: column in the low five bits, row from bit 8), the characters, and
// a zero byte; a zero word ends the list. The lists are read through DB = $0F, from the
// ROM or (below $2000) from strings built in work RAM.

#include "port.h"

void MenuSpriteOrigin(TgRegs* r);
void MenuTilemapAddr(TgRegs* r);
void MenuLoadBasePalette(TgRegs* r);
void MenuLoadPaletteRow1(TgRegs* r);

static uint8_t text8(uint16_t x) { return tg_read8(0x0F0000 | x); }
static uint16_t text16(uint16_t x) { return (uint16_t) (text8(x) | text8((uint16_t) (x + 1)) << 8); }

// The small font's tile for a character: blank below '!', digits and punctuation from
// $0C, letters from $01.
static uint16_t small_tile(uint8_t c) {
  if(c < 0x21) return 0;
  if(c < 0x41) return (uint16_t) (c - 0x15);
  return (uint16_t) (c - 0x40);
}

// $0F:9F3E: the strings at X into the tilemap buffer in the small font.
void MenuDrawText(TgRegs* r) {
  uint16_t x = r->x;
  for(uint16_t cell; (cell = text16(x)) != 0;) {
    ram16_set(0x48, cell);
    MenuTilemapAddr(r);
    x += 2;
    for(uint8_t c; (c = text8(x)) != 0; x++) {
      uint16_t y = ram16(0x4C);
      ram16_set(y, small_tile(c));
      ram16_set(0x4C, (uint16_t) (y + 2));
      r->y = (uint16_t) (y + 2);
    }
    x++;
  }
  r->a = 0;
  r->x = x;
  r->n = false;
  r->z = true;
}

// $0F:9F35: screen $46's small text ($0D:C716).
void MenuDrawScreenText(TgRegs* r) {
  r->x = rom16(0x0DC716 + (uint16_t) (ram16(0x46) << 1));
  MenuDrawText(r);
}

// $0F:9F93: the strings at X in the big font, two rows of tiles: each string is drawn
// once with $54 = 0 (top halves) and again a row down with $54 = $10 (bottom halves).
// Characters $A0 and up are tiles in palette $44 ($FF moves on to the next palette, on
// the first pass only); below $20 a character and the byte after it are a tile number.
void MenuDrawBigText(TgRegs* r) {
  uint16_t x = r->x;
  for(;;) {
    ram16_set(0x54, 0);
    uint16_t cell = text16(x);
    if(cell == 0) break;
    ram16_set(0x48, cell);
    MenuTilemapAddr(r);
    uint16_t row = r->a;
    x++;
    ram16_set(0x56, x);
    for(;;) {
      x++;
      uint8_t c = text8(x);
      if(c == 0) {
        x++;
        if(ram16(0x54) != 0) break;
        ram16_set(0x54, 0x0010);
        ram16_set(0x4C, (uint16_t) (row + 0x40));
        x = ram16(0x56);
        continue;
      }
      uint16_t attr = rom16(0x0DC5E0 + (uint16_t) (ram16(0x44) << 1));
      uint16_t tile;
      if(c >= 0xA0) {
        if(c == 0xFF) {
          if(ram16(0x54) == 0) ram16_set(0x44, (uint16_t) (ram16(0x44) + 1));
          continue;
        }
        tile = c | attr;
      } else if(c < 0x20) {
        tile = (uint16_t) ((c << 8 | text8((uint16_t) (x + 1))) | attr);
        x++;
      } else {
        uint16_t i = c == 0x20 ? 0 : c < 0x3B ? (uint16_t) (c - 0x15) : (uint16_t) (c - 0x40);
        tile = tg_rom(0x0DC5BB + i)[0];
      }
      tile = (uint16_t) (tile + 0x80 + ram16(0x54));
      uint16_t y = ram16(0x4C);
      ram16_set(y, tile);
      ram16_set((uint16_t) (y + 2), (uint16_t) (tile + 1));
      ram16_set(0x4C, (uint16_t) (y + 4));
      r->y = (uint16_t) (y + 4);
    }
  }
  r->a = 0;
  r->x = x;
  r->n = false;
  r->z = true;
}

// $0F:9F88: screen $46's big text ($0D:C738).
void MenuDrawScreenBigText(TgRegs* r) {
  r->x = rom16(0x0DC738 + (uint16_t) (ram16(0x46) << 1));
  MenuDrawBigText(r);
}

// $0F:9ED3 / $0F:9ED0 (from sprite 0): the strings at X as sprites from OAM entry Y / 4,
// each string a word for the cell ($48, via $58), a word added to it for the first
// sprite's position, then the characters 8 pixels apart. A sprite with Y = $F0 ends them.
void MenuDrawTextSpritesAt(TgRegs* r) {
  uint16_t x = r->x, y = r->y;
  for(uint16_t cell; (cell = text16(x)) != 0;) {
    ram16_set(0x48, cell);
    MenuSpriteOrigin(r);
    x += 2;
    ram16_set((uint16_t) (0x0202 + y), (uint16_t) (text16(x) + ram16(0x58)));
    x += 2;
    for(uint8_t c; (c = text8(x)) != 0; x++) {
      uint16_t tile = c < 0x21 ? 0x0100 : c < 0x41 ? (uint16_t) (c + 0xEB) : (uint16_t) (c + 0xC0);
      y += 2;
      ram16_set((uint16_t) (0x0202 + y), tile | 0x3E00);
      y += 2;
      ram16_set((uint16_t) (0x0202 + y), (uint16_t) (ram16((uint16_t) (0x01FE + y)) + 8));
    }
    x++;
  }
  r->a = 0xF000;
  ram16_set((uint16_t) (0x0202 + y), r->a);
  r->x = x;
  r->y = y;
}

void MenuDrawTextSprites(TgRegs* r) {
  r->y = 0;
  MenuDrawTextSpritesAt(r);
}

static void box(uint16_t x, uint16_t v) { ram16_set(x, v); }

// $0F:A131: screen $46's boxes ($0D:C75A): each a cell word and a size word (inner width
// in the low byte, height in the high byte), drawn into the tilemap buffer.
void MenuDrawBoxes(TgRegs* r) {
  uint16_t list = rom16(0x0DC75A + (uint16_t) (ram16(0x46) << 1));
  for(uint16_t cell; (cell = text16(list)) != 0; list += 4) {
    ram16_set(0x48, cell);
    ram16_set(0x4A, text16((uint16_t) (list + 2)));
    MenuTilemapAddr(r);
    uint16_t w = g_ram[0x4A], h = g_ram[0x4B];
    uint16_t x = ram16(0x4C), n;
    box(x, 0x2D);
    x += 2;
    n = w;
    do {
      box(x, 0x2C);
      x += 2;
    } while(--n);
    box(x, 0x2E);
    x = (uint16_t) (x + 0x40);
    n = h;
    do {
      box(x, 0x2B);
      x = (uint16_t) (x + 0x40);
    } while(--n);
    box(x, 0x30);
    x -= 2;
    n = w;
    do {
      box(x, 0x2C);
      x -= 2;
    } while(--n);
    box(x, 0x2F);
    x = (uint16_t) (x - 0x40);
    n = h;
    do {
      box(x, 0x2B);
      x = (uint16_t) (x - 0x40);
    } while(--n);
    r->a = x;
    r->x = x;
    r->y = 0;
  }
  r->a = 0;
  r->x = list;
}

// $0F:9C31: screen A: its boxes, small and big text, and the palette.
void MenuDrawScreen(TgRegs* r) {
  ram16_set(0x46, r->a);
  MenuDrawBoxes(r);
  MenuDrawScreenText(r);
  MenuDrawScreenBigText(r);
  MenuLoadBasePalette(r);
  MenuLoadPaletteRow1(r);
}

// $0F:8781 / $0F:87AD: rows of blanks from cell A down, through a string built at $66:
// 10 rows of the 13 characters at $0D:C400 two rows apart, or 20 of the 9 at $0D:C408.
static void blank_rows(TgRegs* r, uint32_t src, uint16_t last, uint16_t rows, uint16_t step) {
  ram16_set(0x48, r->a);
  ram16_set(0x66, r->a);
  for(int x = last; x >= 0; x -= 2) ram16_set((uint16_t) (0x68 + x), rom16(src + (uint32_t) x));
  for(uint16_t y = rows; y != 0; y--) {
    r->x = 0x0066;
    MenuDrawText(r);
    ram16_set(0x66, (uint16_t) (ram16(0x66) + step));
  }
  ram16_set(0x70, 0);
  ram16_set(0x72, 0);
  r->a = ram16(0x66);
  r->y = 0;
}

void MenuBlankBigRows(TgRegs* r) { blank_rows(r, 0x0DC400, 0x1A, 10, 0x0200); }
void MenuBlankSmallRows(TgRegs* r) { blank_rows(r, 0x0DC408, 0x12, 20, 0x0100); }

// $0F:8BD6: ten 8-character lines from $0D:C42B, two rows apart from cell $0618 (the
// record table's names, guessed from the layout).
void MenuDrawTenLines(TgRegs* r) {
  ram16_set(0x48, 0x0618);
  for(uint16_t y = 0; y != 10; y++) {
    uint32_t src = 0x0DC42B + (uint32_t) (y << 3);
    for(int i = 0; i < 8; i += 2) ram16_set((uint16_t) (0x68 + i), rom16(src + (uint32_t) i));
    uint16_t cell = ram16(0x48);
    ram16_set(0x66, cell);
    r->x = 0x0066;
    MenuDrawText(r);
    ram16_set(0x48, (uint16_t) (cell + 0x0200));
  }
  r->a = ram16(0x48);
  r->y = 10;
}

// $0F:A045: the selection box as sprites from OAM entry X / 4 at cell $48: $4A's low byte
// is its width in 16-pixel pieces and its high byte its height in 16-pixel rows (one row:
// a bar with rounded ends). The rest of the 128 sprites are cleared to tile $C0 at 0, 0.
void MenuDrawCursorBox(TgRegs* r) {
  MenuSpriteOrigin(r);
  uint16_t x = r->x;
  uint16_t a = r->a;
#define OAM(o) ((uint16_t) (0x0202 - 4 + (o) + x))
  ram16_set(OAM(0), a);
  uint16_t size = ram16(0x4A);
  uint16_t y;
  if(size < 0x0200) {
    y = (uint16_t) ((size & 0xFF) - 1);
    if(y == 0) {
      ram16_set(OAM(2), 0x0EC2);
      goto rest;
    }
    ram16_set(OAM(2), 0x0EC4);
    if(y == 1) y = 0;
    do {
      ram16_set(OAM(6), 0x0EC6);
      ram16_set(OAM(4), (uint16_t) (ram16(OAM(0)) + 0x10));
      x += 4;
    } while(!((--y) & 0x8000));
    ram16_set(OAM(2), 0x4EC4);
    goto rest;
  }
  y = (uint16_t) ((size & 0xFF) - 1);
  ram16_set(0x0204, 0x0EC8);
  do {
    ram16_set(OAM(6), 0x0EC6);
    ram16_set(OAM(4), (uint16_t) (ram16(OAM(0)) + 0x10));
    x += 4;
  } while(!((--y) & 0x8000));
  ram16_set(OAM(2), 0x4EC8);
  for(;;) {
    uint16_t o = (uint16_t) (ram16(0x58) + 0x1000);
    ram16_set(0x58, o);
    ram16_set(OAM(4), o);
    a = (uint16_t) (ram16(0x4A) - 0x0100);
    ram16_set(0x4A, a);
    y = (uint16_t) ((a & 0xFF) - 1);  // TAY with 8-bit index registers for a moment
    if(a < 0x0100) break;
    ram16_set(OAM(6), 0x0EC6);
    x += 4;
    do {
      ram16_set(OAM(6), 0x0EC6);
      ram16_set(OAM(4), (uint16_t) (ram16(OAM(0)) + 0x10));
      x += 4;
    } while(!((--y) & 0x8000));
  }
  ram16_set(OAM(6), 0x8EC8);
  x += 4;
  do {
    ram16_set(OAM(6), 0x0EC6);
    ram16_set(OAM(4), (uint16_t) (ram16(OAM(0)) + 0x10));
    x += 4;
  } while(!((--y) & 0x8000));
  ram16_set(OAM(2), 0xCEC8);
rest:
  do {
    ram16_set(OAM(4), 0);
    ram16_set(OAM(6), 0x30C0);
    x += 4;
  } while(x < 0x0200);
#undef OAM
  r->a = 0x30C0;
  r->x = x;
  r->y = y;
}

void MenuLoadPlayerName(TgRegs* r);
void MenuToDecimal(TgRegs* r);
void MenuQueueTilemap(TgRegs* r);
void MenuDrawCarPortrait(TgRegs* r);
void MenuDrawCarPortraitAt(TgRegs* r);

// $0F:9B3B: for name entry, $5E = the length of player $81's name (trailing blanks not
// counted, at least 1), and the entry string's cell offset $66 = $0708.
void MenuNameEntryStart(TgRegs* r) {
  MenuLoadPlayerName(r);
  uint16_t x = 8;
  while(g_ram[0x67 + x] == 0x20) {
    if(--x == 0) break;
  }
  r->a = 0x0708;
  ram16_set(0x66, r->a);
  ram16_set(0x5E, x);
  r->x = x;
}

// $0F:9B70: the name entry screen's heading ($0F:B4AD), then player $81's name as
// $0F:9B7B loads it.
void MenuDrawNameHeading(TgRegs* r) {
  r->x = 0xB4AD;
  MenuDrawBigText(r);
  ram16_set(0x66, 0x0108);
  MenuLoadPlayerName(r);
}

// $0F:95AC: the big-font string at $66 drawn, and the tilemap DMA set up.
void MenuDrawStringAndQueue(TgRegs* r) {
  r->x = 0x0066;
  MenuDrawBigText(r);
  MenuQueueTilemap(r);
}

// $0F:8693: player A's name and place ("12TH", "1ST") as sprites from the string at $64
// (cell at $64, offset $0402), for player 2 from OAM entry 14.
void MenuDrawPlayerPlace(TgRegs* r) {
  ram16_set(0x81, r->a);
  MenuLoadPlayerName(r);
  ram16_set(0x66, 0x0402);
  ram16_set(0x70, 0x2020);
  ram16_set(0x72, 0x2020);
  ram16_set(0x73, 0x4854);  // "TH"
  uint16_t place = ram16((uint16_t) (0x1FF8 + (uint16_t) (ram16(0x81) << 1)));
  if(place < 4) ram16_set(0x73, place == 2 ? 0x444E : place < 2 ? 0x5453 : 0x4452);  // "ND" "ST" "RD"
  r->a = place;
  MenuToDecimal(r);
  uint16_t a = r->a;
  if((uint8_t) a == 0x30) a = (uint16_t) ((a & 0xFF00) | 0x20);
  ram16_set(0x71, a);
  r->x = 0x0064;
  r->y = ram16(0x81) != 0 ? 0x0038 : 0;
  MenuDrawTextSpritesAt(r);
}

// $0F:93A3: the country flags as sprites: $5A + 1 of them from tile $0CE0, 16 pixels apart
// down from (X $1E, Y $2F); then $5A = the countries open at skill level $1F10. The first
// position step adds the carry the routine was called with.
void MenuDrawFlagSprites(TgRegs* r) {
  for(int x = 0x1E; x >= 0; x -= 2) ram16_set((uint16_t) (0x0202 + x), 0);
  uint16_t x = 0, y = 0x0CE0, a = 0x2F1E;
  for(;;) {
    ram16_set((uint16_t) (0x0202 + x), a);
    ram16_set((uint16_t) (0x0204 + x), y);
    x += 4;
    y += 2;
    uint16_t n = (uint16_t) (ram16(0x5A) - 1);
    ram16_set(0x5A, n);
    if(n & 0x8000) break;
    a = adc16(r, ram16((uint16_t) (0x01FE + x)), 0x1000);
    if(a == 0) break;
  }
  r->a = ram16((uint16_t) (0x1F0A + (uint16_t) (ram16(0x1F10) << 1)));
  ram16_set(0x5A, r->a);
  r->x = x;
}

// $0F:8C16: palette rows 2 and 3 for the two cars ($0E:FD06, with the colours of car
// $1F8F / $1FAC from $0F:A51C), and the first 16 colours from $0E:FB86.
void MenuLoadCarPalettes(TgRegs* r) {
  for(int x = 0x1E; x >= 0; x -= 2) {
    uint16_t v = rom16(0x0EFD06 + (uint32_t) x);
    ram16_set((uint16_t) (0x04AC + x), v);
    ram16_set((uint16_t) (0x04CC + x), v);
  }
  for(int x = 0x1E; x >= 0; x -= 2) ram16_set((uint16_t) (0x046C + x), rom16(0x0EFB86 + (uint32_t) x));
  for(int p = 0; p < 2; p++) {
    uint16_t y = (uint16_t) (ram16(p == 0 ? 0x1F8F : 0x1FAC) << 3);
    for(uint16_t x = 0; x != 8; x += 2) ram16_set((uint16_t) ((p == 0 ? 0x04B2 : 0x04D2) + x), rom16(0x0FA51C + (uint16_t) (y + x)));
    r->a = rom16(0x0FA51C + (uint16_t) (y + 6));
    r->y = (uint16_t) (y + 8);
  }
  r->x = 8;
}

// $0F:914E: the car select screen's portraits ($8E: tile base): two at the top, then for
// one player A's car and another, for two players car A for both (the second from base
// $0D20); then the car palettes.
void MenuDrawCarPortraits(TgRegs* r) {
  uint16_t car = r->a;
  ram16_set(0x8E, 0x0920);
  r->y = 0x00A0;
  MenuDrawCarPortrait(r);
  r->y = 0x00B0;
  MenuDrawCarPortrait(r);
  if(ram16(0x1F04) == 0) {
    r->y = 0x0080;
    MenuDrawCarPortrait(r);
    r->a = car;
    r->y = 0x0090;
    MenuDrawCarPortraitAt(r);
  } else {
    r->a = car;
    r->y = 0x0090;
    MenuDrawCarPortraitAt(r);
    ram16_set(0x8E, 0x0D20);
    r->a = car;
    r->y = 0x0080;
    MenuDrawCarPortraitAt(r);
  }
  MenuLoadCarPalettes(r);
}
