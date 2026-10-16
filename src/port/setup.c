// Race set-up routines (bank $00), run once before a race by $00:BF14.

#include "port.h"
#include "variables.h"
#include "race_common.h"

void DecompressBegin(TgRegs* r);
void DecompressNext(TgRegs* r);

static uint16_t dp16(TgRegs* r, uint16_t o) { return ram16((uint16_t) (r->d + o)); }
static void dp16_set(TgRegs* r, uint16_t o, uint16_t v) { ram16_set((uint16_t) (r->d + o), v); }

// A 16-bit read whose second byte may carry into the next bank, as indexed reads do.
static uint16_t long16(uint32_t adr) {
  return (uint16_t) (tg_read8(adr & 0xffffff) | tg_read8((adr + 1) & 0xffffff) << 8);
}

// One of the track's two strip tables: for each of count + 1 entries, a byte picks a
// 32-word strip from bank $0C through `index`; with the sign bit set the strip is negated.
// The byte is read as the high half of a word, so its sign also comes from the next byte.
static void build_strips(TgRegs* r, uint16_t list, uint16_t index, uint32_t dest) {
  uint16_t x = 0;
  dp16_set(r, 0x1E, 0);
  do {
    dp16_set(r, 0x20, 0x1F);
    uint16_t y = dp16(r, 0x1E);
    uint16_t a = long16(0x0C0000 + dp16(r, list) + y);
    dp16_set(r, 0x1E, (uint16_t) (y + 1));
    bool negate = a & 0x8000;
    a = (uint16_t) (a >> 8 | a << 8);
    if(negate) a = (uint16_t) -a;
    y = rom16(0x0C0000 | (uint16_t) (index + ((a << 1) & 0x01FF)));
    for(int i = 0; i < 0x20; i++, y += 2, x += 2) {
      uint16_t v = tg_read16(0x0C0000 | y);
      tg_bus_write16((dest + x) & 0xffffff, negate ? (uint16_t) -v : v);
    }
    dp16_set(r, 0x1C, (uint16_t) (dp16(r, 0x1C) - 1));
  } while(!(dp16(r, 0x1C) & 0x8000));
}

// $00:BF44. Copies the race renderer's code from $09:899A to $7E:2000 ($1C00 bytes),
// builds the course's road strips at $7E:4000 and $7E:4C00, its tile blocks at
// $7E:5800, and its tile map at $7E:6400 (from bank $0C, course $1F06), then decodes the
// per-segment values into $7E:3E00 and sets the second car's course start. Registers and
// flags are don't-care afterwards.
void RaceLoadCourse(TgRegs* r) {
  for(uint32_t i = 0; i < 0x1C00; i++) g_ram[0x2000 + i] = tg_read8(0x09899A + i);

  uint16_t course = ram16(CarTypeIndex);
  uint16_t e = (uint16_t) (course * 10);
  dp16_set(r, 0x28, rom16(0x0C840A + e));
  dp16_set(r, 0x26, rom16(0x0C8408 + e));
  dp16_set(r, 0x24, rom16(0x0C8406 + e));
  dp16_set(r, 0x22, rom16(0x0C8404 + e));
  uint16_t first = rom16(0x0C8402 + e);
  ram16_set(RaceCars, (uint16_t) ((first & 0x3F) << 10));
  uint16_t count = (uint16_t) (first + 1);
  dp16_set(r, 0x1A, count);
  dp16_set(r, 0x1C, count);
  build_strips(r, 0x22, 0x85D3, 0x7E4000);
  dp16_set(r, 0x1C, dp16(r, 0x1A));
  build_strips(r, 0x24, 0x8591, 0x7E4C00);

  // Tile blocks: one byte per entry picks a 64-byte block.
  dp16_set(r, 0x1C, dp16(r, 0x1A));
  dp16_set(r, 0x1E, 0);
  uint16_t y = 0x5800, x = dp16(r, 0x26);
  do {
    uint16_t a = tg_read16(0x0C0000 | x);
    x++;
    uint16_t src = rom16(0x0C0000 | (uint16_t) (0x8545 + ((a << 1) & 0x01FF)));
    for(int i = 0; i < 0x40; i++) g_ram[(uint16_t) (y + i)] = tg_read8(0x0C0000 | (uint16_t) (src + i));
    y = (uint16_t) (y + 0x40);
    dp16_set(r, 0x1C, (uint16_t) (dp16(r, 0x1C) - 1));
  } while(!(dp16(r, 0x1C) & 0x8000));

  // Tile map: 16 words per entry from the $8615 templates, each keeping only the parts
  // the course allows (mask from $8541) and recoloured from four words built at $ED-$F4.
  dp16_set(r, 0x24, rom16(0x0C0000 | (uint16_t) (0x8541 + ram16(0x1F10))) & 0xE000);
  dp16_set(r, 0x1C, (uint16_t) ((dp16(r, 0x1A) << 1) + 1));
  x = 0x6400;
  do {
    y = dp16(r, 0x28);
    uint16_t a = (uint16_t) (tg_read16(0x0C0000 | y) << 1);
    dp16_set(r, 0xEF, a & 0xFF00);
    dp16_set(r, 0xED, (uint16_t) (a << 8));
    y = (uint16_t) (y + 2);
    a = (uint16_t) (tg_read16(0x0C0000 | y) << 1);
    dp16_set(r, 0xF3, a & 0xFF00);
    dp16_set(r, 0xF1, (uint16_t) (a << 8));
    y++;
    a = tg_read16(0x0C0000 | y);
    dp16_set(r, 0x28, (uint16_t) (y + 2));
    y = (uint16_t) ((a & 0xFF00) >> 3);
    dp16_set(r, 0x20, 0x0F);
    do {
      a = rom16(0x0C0000 | (uint16_t) (0x8615 + y));
      if(a) {
        if(!(a & dp16(r, 0x24))) {
          a = 0;
        } else {
          dp16_set(r, 0x1E, a & 0x01FF);
          uint16_t colour = ram16((uint16_t) (0x00ED + ((a >> 8) & 6)));  // absolute, not direct page
          a = colour;
          if(colour & 0x8000) {
            dp16_set(r, 0x1E, (colour & 0x3E00) >> 8);
            a = (uint16_t) ((colour & 0xC000) | dp16(r, 0x1E));
          } else if(colour) {
            a = (uint16_t) (colour | dp16(r, 0x1E));
          }
        }
      }
      ram16_set(x, a);
      x = (uint16_t) (x + 2);
      y = (uint16_t) (y + 2);
      dp16_set(r, 0x20, (uint16_t) (dp16(r, 0x20) - 1));
    } while(!(dp16(r, 0x20) & 0x8000));
    dp16_set(r, 0x1C, (uint16_t) (dp16(r, 0x1C) - 1));
  } while(dp16(r, 0x1C));

  // Per-segment values: a delta-coded stream of byte pairs from $06:EDC6.
  dp16_set(r, 0x23, 0xEDC6);
  g_ram[(uint16_t) (r->d + 0x25)] = 0x06;
  uint32_t db = (uint32_t) r->db << 16;
  uint16_t ci = (uint16_t) (course << 1);
  uint16_t v = tg_read16(db | (uint16_t) (0xE0E4 + ci));
  ram16_set(RaceP1SegmentValue, v);
  ram16_set(RaceP2SegmentValue, v);
  ram16_set(RaceLeaderSegmentValue, v);
  uint16_t start = tg_read16(db | (uint16_t) (0xE124 + ci));
  uint16_t left = (uint16_t) (tg_read16(db | (uint16_t) (0xE126 + ci)) - start);
  DecompressBegin(r);
  for(uint16_t skip = start; skip; skip--) DecompressNext(r);
  x = 0;
  for(;;) {
    g_ram[RaceLeaderSegmentValue] = (uint8_t) (g_ram[RaceLeaderSegmentValue] + r->a);
    g_ram[0x3E00 + x] = g_ram[RaceLeaderSegmentValue];
    x++;
    if(!--left) break;
    DecompressNext(r);
    g_ram[RaceLeaderSegmentValue + 1] = (uint8_t) (g_ram[RaceLeaderSegmentValue + 1] + r->a);
    g_ram[0x3E00 + x] = g_ram[RaceLeaderSegmentValue + 1];
    x++;
    if(!--left) break;
    DecompressNext(r);
  }
  ram16_set(RaceLeaderSegmentValue, ram16(RaceP1SegmentValue));

  uint16_t rival = tg_read16(db | (uint16_t) (0xDF24 + ci));
  if(rival & 0x8000) ram16_set(0x1EF2, ram16(0x1EF2) | 4);
  else ram16_set(0x1EF2, ram16(0x1EF2) & ~4);
  ram16_set(0x1EF0, (uint16_t) ((rival & 0x7FFF) << 1));
}

// Decompressed bytes to VRAM in pairs (low byte, high byte) until the stream ends. A
// stream ending on a low byte still writes it to the high half.
static void decompress_words(TgRegs* r) {
  DecompressBegin(r);
  if(r->c) return;
  for(;;) {
    tg_bus_write8(0x2118, (uint8_t) r->a);
    DecompressNext(r);
    tg_bus_write8(0x2119, (uint8_t) r->a);
    if(r->c) return;
    DecompressNext(r);
    if(r->c) return;
  }
}

// $00:EA40. Decompresses the stream at bank (A low byte):X to VRAM word address Y. A's
// high byte is VMAIN; its bit 6 sends single bytes to $2118 (or $2119 with bit 7 set)
// instead of words. Leaves the registers and flags as the decompressor does, with 16-bit
// A.
void DecompressToVram(TgRegs* r) {
  uint8_t mode = (uint8_t) (r->a >> 8);
  dp16_set(r, 0x23, r->x);
  g_ram[(uint16_t) (r->d + 0x25)] = (uint8_t) r->a;
  r->x = (uint16_t) (r->a << 8 | r->a >> 8);
  tg_bus_write8(0x2115, mode & 0x8F);
  tg_bus_write16(0x2116, r->y);
  if(!(mode & 0x40)) {
    decompress_words(r);
    return;
  }
  uint32_t port = (mode & 0x80) ? 0x2119 : 0x2118;
  DecompressBegin(r);
  while(!r->c) {
    tg_bus_write8(port, (uint8_t) r->a);
    DecompressNext(r);
  }
}

// $00:C182. The skyline: clears the two BG maps' sky rows ($1A00 and $1E00, tile $80 with
// attributes $25), writes the course's 64-tile skyline (from bank $02) into row $E0 of
// each, and decompresses its tiles to VRAM $6800 (from bank $0A, or $0B for courses 16
// and up). Registers and flags are don't-care afterwards.
void RaceLoadSkyline(TgRegs* r) {
  static const uint16_t kMaps[2] = { 0x1A00, 0x1E00 };
  for(int m = 0; m < 2; m++) {
    tg_bus_write8(0x2116, 0x00);
    tg_bus_write8(0x2117, (uint8_t) (kMaps[m] >> 8));
    for(int i = 0; i < 0xE0; i++) {
      tg_bus_write8(0x2118, 0x80);
      tg_bus_write8(0x2119, 0x25);
    }
  }
  uint32_t db = (uint32_t) r->db << 16;
  uint16_t x = (uint16_t) (ram16(CarTypeIndex) << 1);
  dp16_set(r, 0x23, 0x0200);
  dp16_set(r, 0x22, tg_read16(db | (uint16_t) (0xDFA4 + x)));
  uint32_t skyline = (uint32_t) (g_ram[(uint16_t) (r->d + 0x22)] | g_ram[(uint16_t) (r->d + 0x23)] << 8 | g_ram[(uint16_t) (r->d + 0x24)] << 16);
  for(int m = 0; m < 2; m++) {
    tg_bus_write16(0x2116, (uint16_t) (kMaps[m] + 0xE0));
    for(uint16_t y = 0; y < 0x40; y++) {
      tg_bus_write8(0x2118, tg_read8((skyline + y) & 0xffffff));
      tg_bus_write8(0x2119, 0x05);
    }
  }
  tg_bus_write16(0x2116, 0x6800);
  dp16_set(r, 0x24, x >= 0x20 ? 0x0B00 : 0x0A00);
  dp16_set(r, 0x23, tg_read16(db | (uint16_t) (0xDF64 + x)));
  decompress_words(r);
}

// DMA channel 0 from bank:src to VRAM word address vram.
static void dma_vram(uint8_t bank, uint16_t src, uint16_t bytes, uint16_t vram) {
  tg_bus_write8(0x420B, 0x00);
  tg_bus_write8(0x4304, bank);
  tg_bus_write16(0x4300, 0x1801);
  tg_bus_write16(0x4302, src);
  tg_bus_write16(0x4305, bytes);
  tg_bus_write16(0x2116, vram);
  tg_bus_write8(0x420B, 0x01);
}

// One roadside object slot: picks entry (index) of the $C552 tables, records its values
// at values / values2, and returns the two graphics sources (bank $03).
static void roadside_slot(TgRegs* r, uint16_t index, uint16_t values, uint16_t values2, uint16_t* first, uint16_t* second) {
  uint32_t db = (uint32_t) r->db << 16;
  ram16_set(values, tg_read16(db | (uint16_t) (0xC572 + index)));
  ram16_set((uint16_t) (values + 2), tg_read16(db | (uint16_t) (0xC574 + index)));
  ram16_set(values2, tg_read16(db | (uint16_t) (0xC592 + index)));
  ram16_set((uint16_t) (values2 + 2), tg_read16(db | (uint16_t) (0xC594 + index)));
  *first = tg_read16(db | (uint16_t) (0xC552 + index));
  *second = tg_read16(db | (uint16_t) (0xC554 + index));
}

// $00:C230. Course graphics: the road tiles (DMA from $02:8000 to VRAM $5000, then
// $02:AC80 decompressed to $7000), the ground maps at $1B20 / $1F20 from $03:9800 (drawn
// mirrored, with $E0 = $FF, unless bit 15 of the course's $DEE4 word is set), and four
// roadside object slots picked by 3-bit fields of that word. Registers and flags are
// don't-care afterwards.
void RaceLoadRoadside(TgRegs* r) {
  dma_vram(0x02, 0x8000, 0x2C80, 0x5000);
  r->a = 0x8002;
  r->x = 0xAC80;
  r->y = 0x7000;
  DecompressToVram(r);

  uint32_t db = (uint32_t) r->db << 16;
  uint16_t v = tg_read16(db | (uint16_t) (0xDEE4 + (ram16(CarTypeIndex) << 1)));
  dp16_set(r, 0x1A, v);
  if(v & 0x8000) {
    ram16_set(0x1EF2, ram16(0x1EF2) | 2);
    tg_bus_write16(0x2116, 0x1B20);
    uint16_t x = 0;
    for(; x < 0xE0; x++) {
      tg_bus_write8(0x2118, tg_read8(0x039800 + x));
      tg_bus_write8(0x2119, 0x0A);
    }
    tg_bus_write16(0x2116, 0x1F20);
    for(; x < 0x1C0; x++) {
      tg_bus_write8(0x2118, tg_read8(0x039800 + x));
      tg_bus_write8(0x2119, 0x0A);
    }
    g_ram[(uint16_t) (r->d + 0xE0)] = 0x00;
  } else {
    ram16_set(0x1EF2, ram16(0x1EF2) & ~2);
    tg_bus_write16(0x2116, 0x1B20);
    int x = 0x1BF;
    for(; x != 0xDF; x--) {
      tg_bus_write8(0x2118, tg_read8(0x039800 + x));
      tg_bus_write8(0x2119, 0xCA);
    }
    tg_bus_write16(0x2116, 0x1F20);
    for(; x >= 0; x--) {
      tg_bus_write8(0x2118, tg_read8(0x039800 + x));
      tg_bus_write8(0x2119, 0xCA);
    }
    g_ram[(uint16_t) (r->d + 0xE0)] = 0xFF;
  }

  uint16_t a, b;
  roadside_slot(r, (uint16_t) ((v & 0x0007) << 2), 0x017B, 0x013F, &a, &b);
  dma_vram(0x03, a, 0x0300, 0x1000);
  dma_vram(0x03, b, 0x0300, 0x1400);
  roadside_slot(r, (uint16_t) ((v & 0x0038) >> 1), 0x017F, 0x0143, &a, &b);
  dma_vram(0x03, a, 0x0300, 0x1180);
  dma_vram(0x03, b, 0x0300, 0x1580);
  roadside_slot(r, (uint16_t) ((v & 0x01C0) >> 4), 0x0183, 0x0147, &a, &b);
  dma_vram(0x03, a, 0x0200, 0x1300);
  dma_vram(0x03, (uint16_t) (a + 0x200), 0x0100, 0x1800);
  dma_vram(0x03, b, 0x0200, 0x1700);
  dma_vram(0x03, (uint16_t) (b + 0x200), 0x0100, 0x1C00);
  roadside_slot(r, (uint16_t) ((v & 0x0E00) >> 7), 0x0187, 0x014B, &a, &b);
  dma_vram(0x03, a, 0x0300, 0x1880);
  dma_vram(0x03, b, 0x0300, 0x1C80);
}

// $00:C5B2. The cars' graphics: two $A0-byte blocks from bank $01 (to VRAM $27B0 and
// $2FB0), then each player's car ($F60 bytes from $04:8000 + $F60 x car) to $2000 and
// $2800, and each car's $97DB byte to $1EFD / $1EFE. Player 2 (or the computer car) takes
// the car $1FAC, or $1FF8 when that is player 1's too, moved on by one when it clashes
// again. Registers and flags are don't-care afterwards.
static uint16_t car_tiles(uint16_t car) {
  uint16_t a = 0x8000;
  for(int16_t y = (int16_t) (car - 1); y >= 0; y--) a = (uint16_t) (a + 0x0F60);
  return a;
}

void RaceLoadCars(TgRegs* r) {
  dma_vram(0x01, 0xF8C5, 0x00A0, 0x27B0);
  dma_vram(0x01, 0xF965, 0x00A0, 0x2FB0);
  uint16_t p1 = ram16(0x1F8F);
  uint16_t x = car_tiles(p1);
  uint16_t p2 = ram16(0x1FAC);
  if(p2 == p1) {
    ram16_set(0x1C0A, 0);
    ram16_set(0x1D58, 0);
    p2 = ram16(0x1FF8);
    if(p2 == p1) p2 = (uint16_t) ((p2 + 1) & 3);
  }
  uint16_t y = car_tiles(p2);
  dma_vram(0x04, x, 0x0F60, 0x2000);
  dma_vram(0x04, y, 0x0F60, 0x2800);
  uint32_t db = (uint32_t) r->db << 16;
  g_ram[0x1EFD] = tg_read8(db | (uint16_t) (0x97DB + ram16(0x1F8F)));
  y = g_ram[0x1F04] ? ram16(0x1FAC) : 0;
  g_ram[0x1EFE] = tg_read8(db | (uint16_t) (0x97DB + y));
}

// The PPU's signed 16 x 8 multiplier, through the bus as the original drives it.
static void mul_set(uint16_t a, uint8_t b) {
  tg_bus_write8(0x211B, (uint8_t) a);
  tg_bus_write8(0x211B, (uint8_t) (a >> 8));
  tg_bus_write8(0x211C, b);
}
static uint16_t mul_read(uint32_t adr) { return (uint16_t) (tg_bus_read8(adr) | tg_bus_read8(adr + 1) << 8); }

// A player's starting side-to-side scroll: (side >> 7, sign-extended) x $19 / 16, as a
// 12-bit signed value. The multiplier factor is written twice, as in the original.
static uint16_t start_scroll(uint16_t side) {
  uint16_t hi = (uint16_t) ((uint16_t) (side << 1) >> 8);
  tg_bus_write8(0x211B, (uint8_t) hi);
  tg_bus_write8(0x211B, (side & 0x8000) ? 0xFF : 0x00);
  tg_bus_write8(0x211C, 0x19);
  tg_bus_write8(0x211C, 0x19);
  uint16_t a = (uint16_t) (mul_read(0x2134) >> 4);
  if(a >= 0x0800) a |= 0xF000;
  return a;
}

// $00:A558. The starting grid: the players' grid places (from $1F00 / $1F02; 0 and 1 when
// $1FF2 is clear), the 20 car records copied from the templates at $A732 / $A744
// (players) and $A756 (the others), track positions $20 apart ending $2A0 before the
// line, random speeds and lane changes for cars that have them, and the players' start
// state. Registers and flags are don't-care afterwards; DB is $00 (MVN).
void RaceSetupGrid(TgRegs* r) {
  if(!ram16(0x1FF2)) {
    ram16_set(0x1F00, 0);
    ram16_set(0x1F02, 1);
  }
  ram16_set(0x1E7A, (uint16_t) (0x13 - ram16(0x1F00)));
  ram16_set(0x1E7C, (uint16_t) (0x13 - ram16(0x1F02)));
  mul_set(g_ram[0x1F00], 0x12);
  ram16_set(RaceP1Car, mul_read(0x2134));
  mul_set(g_ram[0x1F02], 0x12);
  ram16_set(RaceP2Car, mul_read(0x2134));

  uint16_t x = 0xA756;
  for(uint16_t y = 0x0212; y != 0x037A; y = (uint16_t) (y + 0x12)) {
    uint16_t off = (uint16_t) (y - 0x0212), src;
    if(off == ram16(RaceP1Car)) {
      src = 0xA732;
    } else if(off == ram16(RaceP2Car)) {
      src = 0xA744;
    } else {
      src = x;
      x = (uint16_t) (x + 0x12);
    }
    for(int i = 0; i < 0x12; i++) g_ram[(uint16_t) (y + i)] = tg_read8((uint16_t) (src + i));
  }
  r->db = 0x00;

  uint16_t course = ram16(CarTypeIndex);
  uint16_t e = (uint16_t) (course * 6);
  dp16_set(r, 0x1A, e);
  dp16_set(r, 0x1E, tg_read16((uint16_t) (0xDDE4 + (uint16_t) ((ram16(0x1F10) << 1) + e))));
  uint16_t y = (uint16_t) (ram16(RaceCars) - 0x02A0);
  for(x = 0; x != 0x0168; x = (uint16_t) (x + 0x12)) {
    ram16_set((uint16_t) (0x0219 + x), y);
    y = (uint16_t) (y + 0x20);
    ram16_set((uint16_t) (0x0214 + x), tg_read16((uint16_t) (0xA734 + x)));
    if(!ram16((uint16_t) (0x021C + x))) continue;
    uint16_t a = (uint16_t) ((race_random() << 1) + ram16((uint16_t) (0x021C + x)) + dp16(r, 0x1E));
    mul_set(a, g_ram[(uint16_t) (0x021E + x)]);
    ram16_set((uint16_t) (0x021E + x), mul_read(0x2135));
    ram16_set((uint16_t) (0x021C + x), 1);
    a = (uint16_t) (race_random() + 0x64);
    if(a & 1) a = (uint16_t) -a;
    ram16_set((uint16_t) (0x0216 + x), a);
  }

  x = ram16(RaceP1Car);
  ram16_set(0x1E8A, ram16((uint16_t) (0x0214 + x)));
  ram16_set(0x1ED7, ram16((uint16_t) (0x0219 + x)));
  ram16_set((uint16_t) (0x0212 + x), 0x0400);
  ram16_set(0x182E, 0x0400);
  x = ram16(RaceP2Car);
  ram16_set(0x1E8C, ram16((uint16_t) (0x0214 + x)));
  ram16_set(0x1EDA, ram16((uint16_t) (0x0219 + x)));
  ram16_set((uint16_t) (0x0212 + x), 0x2400);
  ram16_set(0x1830, 0x2400);
  dp16_set(r, 0x48, start_scroll(ram16(0x1E8A)));
  dp16_set(r, 0x5B, start_scroll(ram16(0x1E8C)));

  ram16_set(RaceP1Fuel, 0xA7FF);
  ram16_set(RaceP2Fuel, 0xA7FF);
  uint16_t pos = (uint16_t) (ram16(RaceCars) - 0x0800);
  dp16_set(r, 0x46, pos);
  dp16_set(r, 0x59, pos);
  ram16_set(0x1EE0, 0);
  ram16_set(0x1EE8, (uint16_t) (ram16(0x1F8F) << 4));
  ram16_set(0x1EEA, (uint16_t) (ram16(0x1FAC) << 4));
  ram16_set(0x1F00, 0xFFFF);
  ram16_set(0x1F02, 0xFFFF);
}

void RaceUpdateTileCache(TgRegs* r);

// $00:C6BB. Preloads the roadside tile caches: starts both views $5A0 back (wrapping at
// the course length) and steps them forward $20 at a time, 45 times, running the cache
// until each view's queue and transfer are done. Registers and flags are don't-care
// afterwards.
static uint16_t wrap_back(uint16_t pos) {
  uint16_t a = (uint16_t) (pos - 0x05A0);
  return pos < 0x05A0 ? (uint16_t) (a + ram16(RaceCars)) : a;
}
static uint16_t wrap_forward(uint16_t pos) {
  uint16_t a = (uint16_t) (pos + 0x20);
  return a >= ram16(RaceCars) ? (uint16_t) (a - ram16(RaceCars)) : a;
}

void RacePreloadTileCache(TgRegs* r) {
  dp16_set(r, 0x46, wrap_back(dp16(r, 0x46)));
  dp16_set(r, 0x59, wrap_back(dp16(r, 0x59)));
  for(int i = 0; i < 0x2D; i++) {
    do {
      RaceUpdateTileCache(r);
      RaceUpdateTileCache(r);
    } while(ram16(0x1BC2) != ram16(0x1BC4) || ram16(0x1D10) != ram16(0x1D12) || ram16(0x1BC0) || ram16(0x1D0E));
    dp16_set(r, 0x46, wrap_forward(dp16(r, 0x46)));
    dp16_set(r, 0x59, wrap_forward(dp16(r, 0x59)));
  }
}

// VRAM writes: an address, then words.
static void vram_words(uint16_t adr, int n, const uint16_t* words) {
  tg_bus_write16(0x2116, adr);
  for(int i = 0; i < n; i++) tg_bus_write16(0x2118, words[i]);
}
#define VRAM_WORDS(adr, ...) do { \
    static const uint16_t w_[] = { __VA_ARGS__ }; \
    vram_words(adr, (int) (sizeof w_ / sizeof w_[0]), w_); \
  } while(0)

// One player's HUD frame in the BG3 map (base: 0 for player 1, $1C0 for player 2).
static void hud_frame(uint16_t base) {
  VRAM_WORDS(base + 0x0C57, 0x2C17, 0x2C18, 0x2C17, 0x2C18, 0x2C17, 0x2C18);
  VRAM_WORDS(base + 0x0C77, 0x2C19, 0x2C1A, 0x2C19, 0x2C1A, 0x2C19, 0x2C1A);
  VRAM_WORDS(base + 0x0C97, 0x2C35, 0x2C35, 0x2C3F, 0x2C35, 0x2C35, 0x2C40, 0x2C35, 0x2C35);
  VRAM_WORDS(base + 0x0C34, 0x3002, 0x3003, 0x3004, 0x3005, 0x3006, 0x2C07, 0x3408, 0x3409, 0x340A, 0x340B, 0x340C);
  VRAM_WORDS(base + 0x0C54, 0x300D, 0x300E);
  VRAM_WORDS(base + 0x0C74, 0x300F);
}

// A player's gearbox label ("AUTO" or "MANU"-style tiles) at adr.
static void hud_gearbox(uint16_t adr, bool manual) {
  tg_bus_write16(0x2116, adr);
  static const uint16_t kAuto[4] = { 0x2C4B, 0x2C4C, 0x2C44, 0x2C35 };
  static const uint16_t kManual[4] = { 0x2C49, 0x2C4A, 0x2C4B, 0x2C47 };
  for(int i = 0; i < 4; i++) tg_bus_write16(0x2118, manual ? kManual[i] : kAuto[i]);
}

// A player's starting place (grid slot + 1, from 1E7A / 1E7C) with its suffix. base: 0
// for player 1, $1C0 for player 2.
static void hud_place(TgRegs* r, uint16_t base, uint16_t place) {
  uint16_t y = (uint16_t) ((place < 4 ? place : 3) << 2);
  uint16_t x = place;
  if(place >= 10) {
    uint16_t a = (uint16_t) (place - 10);
    if(a >= 10) {
      x = (uint16_t) (a - 10);
      VRAM_WORDS(base + 0x0D79, 0x2C52);
      VRAM_WORDS(base + 0x0D99, 0x2C53);
    } else {
      x = a;
      VRAM_WORDS(base + 0x0D79, 0x2C50);
      VRAM_WORDS(base + 0x0D99, 0x2C51);
    }
  }
  uint16_t a = (uint16_t) ((x << 1) + 0x2C4E);
  tg_bus_write16(0x2116, (uint16_t) (base + 0x0D7A));
  tg_bus_write16(0x2118, a);
  tg_bus_write16(0x2116, (uint16_t) (base + 0x0D9A));
  tg_bus_write16(0x2118, (uint16_t) (a + 1));
  uint32_t db = (uint32_t) r->db << 16;
  tg_bus_write16(0x2118, tg_read16(db | (uint16_t) (0xCC8B + y)));
  tg_bus_write16(0x2118, tg_read16(db | (uint16_t) (0xCC8D + y)));
}

// A player's driver badge: the two-row version when shown, the small one otherwise.
static void hud_badge(uint16_t adr, bool big) {
  if(big) {
    VRAM_WORDS(adr, 0x2C2B, 0x2C2C, 0x2C2D);
    VRAM_WORDS(adr + 0x20, 0x2C2E, 0x2C2F, 0x2C30);
  } else {
    VRAM_WORDS(adr, 0x2C31, 0x2C32);
    VRAM_WORDS(adr + 0x20, 0x2C33, 0x2C34);
  }
}

static void vram_fill(uint16_t adr, uint16_t word) {
  tg_bus_write16(0x2116, adr);
  for(int i = 0; i < 0x20; i++) tg_bus_write16(0x2118, word);
}

// $00:C722. The HUD: its tiles (DMA from $02:BC09 to VRAM 0), and both players' frames,
// gearbox labels, starting places and badges in the BG3 map, the dividing rows and the
// two vertical fuel labels. Registers and flags are don't-care afterwards.
void RaceDrawHudFrame(TgRegs* r) {
  dma_vram(0x02, 0xBC09, 0x0C40, 0x0000);
  tg_bus_write8(0x2115, 0x80);
  bool warm = ram16(0x1FF6) & 0x8000;

  hud_frame(0);
  hud_gearbox(0x0D81, !warm && ram16(0x1F7A));
  VRAM_WORDS(0x0D66, 0x2C50);
  VRAM_WORDS(0x0D85, 0x2C41, 0x2C51);
  VRAM_WORDS(0x0D41, 0x2C4D, 0x2C42, 0x2C38);
  hud_place(r, 0, (uint16_t) (ram16(0x1E7A) + 1));

  hud_frame(0x1C0);
  hud_gearbox(0x0F41, warm || ram16(0x1F97));
  VRAM_WORDS(0x0F26, 0x2C50);
  VRAM_WORDS(0x0F45, 0x2C41, 0x2C51);
  VRAM_WORDS(0x0F01, 0x2C4D, 0x2C42, 0x2C38);
  hud_place(r, 0x1C0, (uint16_t) (ram16(0x1E7C) + 1));

  hud_badge(0x0C5D, !warm && g_ram[0x1FB0]);
  hud_badge(0x0E1D, warm || g_ram[0x1FB1]);

  vram_fill(0x0C00, 0x2C01);
  vram_fill(0x0DA0, 0xAC01);
  vram_fill(0x0DC0, 0x2C01);
  vram_fill(0x0F60, 0xAC01);
  tg_bus_write8(0x2115, 0x81);
  VRAM_WORDS(0x0CDE, 0x3810, 0x3C11, 0x3C12, 0x3813, 0x3814, 0x3815, 0x3816);
  VRAM_WORDS(0x0E9E, 0x3810, 0x3C11, 0x3C12, 0x3813, 0x3814, 0x3815, 0x3816);
  tg_bus_write8(0x2115, 0x80);
}

// $00:CC9B. Clears the colour and window HDMA tables and fills both scroll tables:
// $180 and $300 down to $230 per line pair. Registers and flags are don't-care
// afterwards.
void RaceClearHdmaTables(TgRegs* r) {
  (void) r;
  g_ram[0x0E54] = 0;
  g_ram[0x0EAF] = 0;
  for(uint16_t x = 0; x < 0x0242; x++) g_ram[0x0C12 + x] = 0;
  for(uint16_t x = 0; x != 0x0340; x = (uint16_t) (x + 4)) {
    ram16_set((uint16_t) (0x0592 + x), 0x0180);
    ram16_set((uint16_t) (0x08D2 + x), 0x0180);
    uint16_t a = (uint16_t) (0x0300 - (x >> 2));
    ram16_set((uint16_t) (0x0594 + x), a);
    ram16_set((uint16_t) (0x08D4 + x), a);
  }
}

// One player's name banner: the length up to the last character above a space (at least
// one), then a word per character for the message tiles at dest, zero-terminated.
// Returns the last index.
static uint16_t name_tiles(TgRegs* r, uint16_t name, uint16_t dest) {
  uint16_t x = 7;
  while((uint8_t) (0x20 - g_ram[name + x]) < 0x80 && --x) {}
  dp16_set(r, 0x1A, x);
  uint16_t last = x;
  uint16_t y = 0;
  x = 0;
  do {
    uint16_t c = g_ram[name + x];
    x++;
    uint16_t a;
    if(c == 0x20) {
      a = 0xD049;
    } else {
      if(c < 0x41) c = (uint16_t) (c + 0x2F);
      a = (uint16_t) ((c << 6) + 0xC049);
    }
    ram16_set((uint16_t) (dest + y), a);
    y = (uint16_t) (y + 2);
    dp16_set(r, 0x1A, (uint16_t) (dp16(r, 0x1A) - 1));
  } while(!(dp16(r, 0x1A) & 0x8000));
  ram16_set((uint16_t) (dest + y), 0);
  return last;
}

// $00:D913. The players' name banners for the start of the race: the names ("DEMO" in
// the attract mode, "COMPUTER" for the computer's car), their message tiles at $050E /
// $052E and message timings from bank $06, full fuel and the starting places. Registers
// and flags are don't-care afterwards.
void RaceSetupNames(TgRegs* r) {
  static const uint8_t kDemo[8] = { 'D', 'E', 'M', 'O', ' ', ' ', ' ', ' ' };
  static const uint8_t kComputer[8] = { 'C', 'O', 'M', 'P', 'U', 'T', 'E', 'R' };
  if(ram16(0x1FF6)) {
    for(int i = 0; i < 8; i++) g_ram[0x054E + i] = g_ram[0x055E + i] = kDemo[i];
  } else {
    for(int i = 0; i < 8; i++) g_ram[0x054E + i] = g_ram[0x1F86 + i];
    for(int i = 0; i < 8; i++) g_ram[0x055E + i] = ram16(0x1F04) ? g_ram[0x1FA3 + i] : kComputer[i];
  }
  ram16_set(0x011A, ram16(0x1E7A));
  ram16_set(0x012C, ram16(0x1E7C));
  ram16_set(RaceP1Fuel, 0xA7FF);
  ram16_set(RaceP2Fuel, 0xA7FF);
  ram16_set(0x0122, 0x0620);
  ram16_set(0x0134, 0x0720);
  ram16_set(0x0124, (uint16_t) (name_tiles(r, 0x054E, 0x050E) + 0x37));
  ram16_set(0x0136, (uint16_t) (name_tiles(r, 0x055E, 0x052E) + 0x76));
  uint16_t x = (uint16_t) (ram16(0x0124) << 1);
  ram16_set(0x0126, 0);
  ram16_set(0x0128, tg_read16(0x060000 | (uint16_t) (0xDB80 + x)));
  ram16_set(0x0120, tg_read16(0x060000 | (uint16_t) (0xDC84 + x)));
  x = (uint16_t) (ram16(0x0136) << 1);
  ram16_set(0x0138, 0);
  ram16_set(0x013A, tg_read16(0x060000 | (uint16_t) (0xDB80 + x)));
  ram16_set(0x0132, tg_read16(0x060000 | (uint16_t) (0xDC84 + x)));
}
