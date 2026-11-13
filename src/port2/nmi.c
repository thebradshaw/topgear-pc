// Top Gear 2: the race's vertical-blank routine for one player ($01:AC79), called by the
// NMI handler ($01:E3EA, through the vector at $0010) with D = 0 and DB = $82.
//
// Every frame it sends the queued colours and the sprite table to the PPU and sets the
// raster IRQ for the top of the dashboard. When the game loop has finished a frame
// ($0122) it also swaps the double buffers (road HDMA lists $0284, sprites $0124,
// colours $0126), latches the scroll and window values the frame was built with, and
// steps the things that run on the frame clock: the lightning flash, the weather
// colours, the road-stripe cycle and the message at the top of the screen.

#include <stddef.h>
#include "../port/port.h"

static uint16_t w(uint16_t adr) { return ram16(adr); }
static void ws(uint16_t adr, uint16_t v) { ram16_set(adr, v); }
static uint8_t b(uint16_t adr) { return g_ram[adr]; }
static void wb(uint16_t adr, uint8_t v) { g_ram[adr] = v; }
static bool neg(uint16_t v) { return (v & 0x8000) != 0; }
static bool lt(uint16_t a, uint16_t c) { return neg((uint16_t) (a - c)); }
// The data bank the register writes go through: the queue is once run with it at $7E
// (by the starting grid), where its "register" writes land in RAM instead.
static uint32_t g_bank;
static void p8(uint16_t adr, uint8_t v) { tg_bus_write8(g_bank | adr, v); }
static void p16(uint16_t adr, uint16_t v) { tg_bus_write16(g_bank | adr, v); }

// The window and colour-math registers for the frame, and the backdrop colour.
static void window_registers(void) {
  p8(0x2123, b(0x014E));
  p8(0x2124, b(0x014F));
  p8(0x2125, b(0x0150));
  p8(0x212A, b(0x0151));
  p8(0x212B, b(0x0152));
  p8(0x2132, b(0x0153));
  p8(0x2121, 0x1D);
  p8(0x2122, b(0x0118));
  p8(0x2122, b(0x0119));
}

// The raster IRQ for the dashboard ($0128), then two DMAs: this frame's queued colours
// ($6C bytes to CGRAM from $4A) and the sprite table.
static void send_frame(uint16_t irq) {
  p16(0x4209, w(0x0128));
  ws(0x0016, irq);
  p8(0x2101, 0x02);
  p8(0x2105, b(0x012E));
  p8(0x420B, 0x00);
  p8(0x4374, 0x7E);
  p16(0x4370, 0x2202);
  p16(0x4372, neg(w(0x0126)) ? 0x0A40 : 0x0AAC);
  p16(0x4375, 0x006C);
  p8(0x2121, 0x4A);
  p8(0x420B, 0x80);
  p8(0x420B, 0x00);
  p8(0x4374, 0x7E);
  p16(0x2102, 0x8000);
  p16(0x4370, 0x0402);
  p16(0x4372, neg(w(0x0124)) ? 0x0BF0 : 0x0FF0);
  p16(0x4375, 0x0220);
  p8(0x420B, 0x80);
  p16(0x2102, 0x8000);
}

// The HDMA sources for channels 5 and 6 (the per-line tables), with their banks.
static void hdma_tables(uint16_t t5, uint16_t t6, uint8_t bank5, uint8_t bank6) {
  p16(0x4352, t5);
  p16(0x4362, t6);
  p8(0x4354, bank5);
  p8(0x4357, bank5);
  p8(0x4364, bank6);
  p8(0x4367, bank6);
}

// Patches up to two HDMA entries from a record at y (count, list address, then a line
// count and a pointer per entry). In a lightning flash the line counts lose their repeat
// bit and the pointers go to the flat table at $85FC. Returns the address after it.
static uint16_t patch_one(uint16_t y, bool flash, bool advance) {
  uint16_t n = w(y);
  if(n == 0) return y;
  uint16_t x = w((uint16_t) (y + 2));
  uint8_t mask = flash ? 0x7F : 0xFF;
  wb(x, b((uint16_t) (y + 4)) & mask);
  if(n != 1) {
    wb((uint16_t) (x + 3), b((uint16_t) (y + 7)) & mask);
    ws((uint16_t) (x + 1), flash ? 0x85FC : w((uint16_t) (y + 5)));
    ws((uint16_t) (x + 4), flash ? 0x85FC : w((uint16_t) (y + 8)));
    return advance ? (uint16_t) (y + 8) : y;
  }
  ws((uint16_t) (x + 1), flash ? 0x85FC : w((uint16_t) (y + 5)));
  return advance ? (uint16_t) (y + 5) : y;
}

// The frame clock's part of the message at the top of the screen ($B01E-$B0BC): which
// text is up ($0188), and its timing. Player 2's copy of each value is the next word.
static void message_clock(uint16_t p, uint16_t rain) {
  ws((uint16_t) (0x0194 + p), (uint16_t) (w((uint16_t) (0x0194 + p)) + 0x0100));
  if(w((uint16_t) (0x0174 + p)) != 0) return;
  if(neg(w(rain))) {
    if(w((uint16_t) (0x0188 + p)) != 0xE940) {
      ws((uint16_t) (0x0188 + p), 0xE940);
      ws((uint16_t) (0x0174 + p), (uint16_t) (w((uint16_t) (0x0174 + p)) - 1));
      ws((uint16_t) (0x0180 + p), 0);
    }
    return;
  }
  uint16_t a = w((uint16_t) (0x017C + p));
  if(neg(a)) {
    a = (uint16_t) (w((uint16_t) (0x0178 + p)) * 10 + 0xE94A);
    if(a != w((uint16_t) (0x0188 + p))) {
      ws((uint16_t) (0x0188 + p), a);
      ws((uint16_t) (0x0174 + p), (uint16_t) (w((uint16_t) (0x0174 + p)) - 1));
      ws((uint16_t) (0x0180 + p), 0);
    }
    return;
  }
  if(a == 0) {
    ws((uint16_t) (0x017C + p), 1);
    ws((uint16_t) (0x0194 + p), 0);
    uint16_t x = tg_read16(0x8CE990 + (uint16_t) (w((uint16_t) (0x0184 + p)) << 1));
    ws((uint16_t) (0x018C + p), tg_read16(0x8C0000 + x));
    ws((uint16_t) (0x0188 + p), (uint16_t) (x + 2));
    ws((uint16_t) (0x0174 + p), (uint16_t) (w((uint16_t) (0x0174 + p)) - 1));
    ws((uint16_t) (0x0180 + p), 0xFFFF);
    return;
  }
  while(w((uint16_t) (0x018C + p)) != 0 && w((uint16_t) (0x0194 + p)) >= w((uint16_t) (0x0190 + p))) {
    ws((uint16_t) (0x0194 + p), (uint16_t) (w((uint16_t) (0x0194 + p)) - w((uint16_t) (0x0190 + p))));
    ws((uint16_t) (0x018C + p), (uint16_t) (w((uint16_t) (0x018C + p)) - 1));
    ws((uint16_t) (0x0188 + p), (uint16_t) (w((uint16_t) (0x0188 + p)) + 0x000A));
    ws((uint16_t) (0x0174 + p), (uint16_t) (w((uint16_t) (0x0174 + p)) - 1));
    ws((uint16_t) (0x0180 + p), 0xFFFF);
  }
}

// --- The message panel ($01:BEBB) ---
//
// Messages (position, lap, "FINAL LAP" and the like) are tile pictures drawn in a panel
// over the view. A record in bank $8C describes one: +0 its tiles' offset, +2 its tile
// map, +4 a second tile block and +6 that block's bank, +8 its length. Smaller notices
// are drawn by the routines at $A5FC (show) and $A68C (clear), with their state in the
// direct page at $0A00: +4 the kind of timing, +6 shown (1) or blinked off (-1), +8 the
// picture, +A the picture wanted, +C the picture drawn, +E / +10 timers.

void Tg2RaceColours1P(TgRegs* r);
void Tg2RaceColours2PTop(TgRegs* r);
void Tg2RaceColours2PBottom(TgRegs* r);
void Tg2UploadQueue1(TgRegs* r);
void Tg2UploadQueue0(TgRegs* r);

static uint16_t rec(uint16_t x, uint16_t o) { return tg_read16(0x8C0000 + (uint16_t) (x + o)); }

// A DMA to VRAM from bank:src.
static void vram_dma(uint8_t vmain, uint16_t vadr, uint16_t mode, uint8_t bank, uint16_t src, uint16_t len) {
  p8(0x4374, bank);
  p8(0x2115, vmain);
  p16(0x2116, vadr);
  p16(0x4370, mode);
  p16(0x4372, src);
  p16(0x4375, len);
  p8(0x420B, 0x80);
}

// A message's tile map (13 rows of 16 tiles) into a panel's map copy, as tile numbers
// from `base` up ($A066; $A110 and $A1BA for two players). (The additions chain their
// carry, as the original's do.)
static void message_map(uint16_t text, uint16_t dst, uint16_t base) {
  uint16_t x = w(text);
  uint32_t src = (uint32_t) (rec(x, 6) & 0xFF) << 16;
  uint16_t y = (uint16_t) (rec(x, 2) + 0x001E);
  uint32_t carry = 0;
  for(int col = 0x1E; col >= 0; col -= 2, y = (uint16_t) (y - 2)) {
    for(int row = 0; row < 13; row++) {
      uint32_t t = (uint32_t) tg_read16(src | (uint16_t) (y + row * 0x20)) + base + carry;
      carry = t >> 16;
      ws((uint16_t) (dst + row * 0x40 + col), (uint16_t) t);
    }
  }
}

// A message's tiles and its map to VRAM ($A264; $A2FE and $A398 for two players).
// Returns the second block's length.
static uint16_t message_upload(uint16_t text, uint16_t tiles, uint16_t block, uint16_t map, uint16_t map_src) {
  uint16_t x = w(text);
  p8(0x420B, 0x00);
  vram_dma(0x00, tiles, 0x1800, 0x8C, (uint16_t) (rec(x, 0) + 0x8B40), 0x0200);
  uint16_t len = rec(x, 8);
  vram_dma(0x80, block, 0x1801, (uint8_t) rec(x, 6), rec(x, 4), len);
  vram_dma(0x80, map, 0x1801, 0x7E, map_src, 0x0340);
  return len;
}

// $A432: the dashboard's map copy ($7E:C990) to VRAM.
static void dashboard_upload(void) {
  p8(0x420B, 0x00);
  vram_dma(0x80, 0x7840, 0x1801, 0x7E, 0xC990, 0x0640);
}

// The queue of VRAM transfers ($0800, 32 entries of 16 bytes; $E6 next, $E4 how many).
// An entry: flags (bit 6 words rather than bytes, bit 5 a block of rows, bit 7 of a byte
// entry the high bytes, low bits the address step), source bank, source, VRAM address,
// length (words or bytes; of a row for a block), how much of the row is sent, the gaps to
// the next row in the source and in VRAM, and the rows left.
enum { kQueue = 0x0800 };

static void spend(uint16_t cost) { ws(0x00EA, (uint16_t) (w(0x00EA) + cost)); }

static void queue_dma(uint16_t x, uint16_t len) {
  p16(0x4372, w(kQueue + 2 + x));
  p16(0x4375, len);
  p16(0x2116, w(kQueue + 4 + x));
  p8(0x420B, 0x80);
}

static void queue_add(uint16_t field, uint16_t x, uint16_t v) {
  ws(kQueue + field + x, (uint16_t) (w(kQueue + field + x) + v));
}

// $E904: sends queued transfers until the frame's time ($EA, counted in CPU cycles
// against $E2) runs out; the one that doesn't fit is cut and carries on next frame.
// The costs added to $EA are the original's own cycle counts.
static void queue_body(void);
static void run_queue(void) {
  if(neg(w(0x00E0))) return spend(0x1C);
  queue_body();
}

// $01:E913 (also reached by the long call $E900): the queue's transfers that fit.
static void queue_body(void) {
  if(w(0x00E4) == 0) return spend(0x23);
  spend(0x22);
  uint16_t x = w(0x00E6);
  for(;;) {
    x &= 0x01F0;
    p8(0x420B, 0x00);
    p8(0x4374, b(kQueue + 1 + x));
    uint8_t flags = b(kQueue + x);
    bool words = (flags & 0x40) != 0, block = (flags & 0x20) != 0;
    int sh = words ? 1 : 0; // bytes to a unit
    uint16_t left = w(0x00E2);
    if(words) {
      p8(0x2115, (uint8_t) ((flags & 0x0F) | 0x80));
      p16(0x4370, 0x1801);
      spend(block ? 0x63 : 0x5A);
    } else {
      p8(0x2115, flags & 0x8F);
      p16(0x4370, (flags & 0x80) ? 0x1900 : 0x1800);
      spend((uint16_t) ((block ? 0x67 : 0x5C) + ((flags & 0x80) ? 2 : 0)));
    }
    bool next = false;
    for(;;) {
      uint16_t len = (uint16_t) ((block ? w(kQueue + 6 + x) - w(kQueue + 8 + x) : w(kQueue + 6 + x)) << sh);
      uint16_t t = (uint16_t) (len + w(0x00EA));
      if(t > left) {
        // Only part of it fits.
        uint16_t ea = w(0x00EA);
        uint16_t part = (uint16_t) (left - ea);
        if(words) part &= 0xFFFE;
        if(left < ea) return spend(words ? (block ? 0x2B : 0x28) : (block ? 0x28 : 0x25));
        if(part == 0) return spend(words ? (block ? 0x2D : 0x2A) : (block ? 0x2A : 0x27));
        ws(0x00EA, (uint16_t) (part + ea + (words ? (block ? 0x87 : 0x89) : 0x84)));
        queue_dma(x, part);
        queue_add(2, x, part);
        queue_add(4, x, part >> sh);
        if(block) queue_add(8, x, part >> sh);
        else queue_add(6, x, (uint16_t) -(part >> sh));
        return;
      }
      bool last = t == left;
      if(!block) {
        ws(0x00EA, (uint16_t) (t + (last ? 0x4B : words ? 0x46 : 0x4A)));
        queue_dma(x, len);
        next = true;
      } else {
        ws(0x00EA, (uint16_t) (t + (words ? (last ? 0x7D : 0x8D) : (last ? 0x7B : 0x89))));
        queue_dma(x, len);
        queue_add(2, x, (uint16_t) (((len >> sh) + w(kQueue + 0x0A + x)) << sh));
        queue_add(4, x, (uint16_t) ((len >> sh) + w(kQueue + 0x0C + x)));
        ws(kQueue + 8 + x, 0);
        uint16_t rows = (uint16_t) (w(kQueue + 0x0E + x) - 1);
        ws(kQueue + 0x0E + x, rows);
        if(rows == 0) {
          spend(last ? 0x1E : words ? 0x0F : 0x11);
          next = true;
        } else if(!last) {
          continue;
        }
      }
      if(next) {
        x = (uint16_t) (x + 0x10);
        ws(0x00E6, x);
        ws(0x00E4, (uint16_t) (w(0x00E4) - 1));
      }
      if(last) return;
      break;
    }
    if(w(0x00E4) == 0) return spend(!block && words ? 0x12 : 0x10);
  }
}

// The queued transfers, if the frame's budget has `cost` cycles to spare.
static void queued_transfers(uint16_t cost) {
  uint16_t a = (uint16_t) (cost + w(0x00EA));
  if(!lt(a, w(0x00E2))) return;
  ws(0x00EA, (uint16_t) (a - 0x0200));
  ws(0x00E0, 0);
  run_queue();
  ws(0x00E0, (uint16_t) (w(0x00E0) - 1));
}

#define N 0x0A00u // the notices' direct page

// A notice's place in the dashboard's map copy ($7E:C990+): rows of tile numbers from a
// table in bank $8F (one table a row, one after the other), 64 bytes apart on screen.
typedef struct {
  uint16_t src, dst;
  uint8_t rows, words;
} Notice;
static const Notice kNotices[15] = {
  { 0xDCBC, 0xCA10, 3, 6 }, { 0xDEA0, 0xCA10, 3, 6 }, { 0xE0A4, 0xCA12, 3, 5 }, { 0xE262, 0xC9D0, 4, 6 },
  { 0xE4D2, 0xC9D2, 4, 5 }, { 0xE6FA, 0xC9D0, 4, 6 }, { 0xE96A, 0xCA10, 3, 7 }, { 0xEBB4, 0xCA22, 3, 7 },
  { 0xEDFE, 0xCA24, 3, 6 }, { 0xF002, 0xCA22, 3, 7 }, { 0xF24C, 0xC9E4, 4, 5 }, { 0xF474, 0xC9E4, 4, 4 },
  { 0xF654, 0xC9E4, 4, 6 }, { 0xF8C4, 0xC9E4, 4, 6 }, { 0xFB34, 0xC9E0, 4, 8 },
};

// $A5FC: draws notice `a` into the map copy at offset $0A00, with tile numbers from $0A02.
// Notices 15 and up are two rows of twelve tiles in order.
static void notice_draw(uint16_t a) {
  uint16_t at = w(N + 0x00), tile = w(N + 0x02);
  if(a >= 15) {
    for(int i = 0; i < 12; i++) {
      ws((uint16_t) (0xC9D4 + at + i * 2), (uint16_t) (tile + i * 2));
      ws((uint16_t) (0xCA14 + at + i * 2), (uint16_t) (tile + i * 2 + 1));
    }
    return;
  }
  const Notice* n = &kNotices[a];
  for(int row = 0; row < n->rows; row++)
    for(int i = 0; i < n->words; i++)
      ws((uint16_t) (n->dst + row * 0x40 + at + i * 2),
         (uint16_t) (rom16(0x8F0000 + (uint16_t) (n->src + row * n->words * 2 + i * 2)) + tile));
}

// $A68C: blanks notice `a`'s place (the blank tile is $38 before its tiles).
static void notice_erase(uint16_t a) {
  uint16_t at = w(N + 0x00), blank = (uint16_t) (w(N + 0x02) - 0x0038);
  if(a >= 15) {
    for(int i = 0; i < 12; i++) {
      ws((uint16_t) (0xC9D4 + at + i * 2), blank);
      ws((uint16_t) (0xCA14 + at + i * 2), blank);
    }
    return;
  }
  const Notice* n = &kNotices[a];
  for(int row = 0; row < n->rows; row++)
    for(int i = 0; i < n->words; i++) ws((uint16_t) (n->dst + row * 0x40 + at + i * 2), blank);
}

// $A539: notice `a`'s tiles to VRAM at tile number $0A02: one block from bank $8F, or for
// 15 and up twelve characters of a name from the font at $82:CBF6.
static void notice_tiles(uint16_t a) {
  uint16_t vadr = (uint16_t) ((w(N + 0x02) & 0x03FF) << 4);
  if(a < 15) {
    p8(0x420B, 0x00);
    p8(0x4374, 0x8F);
    p8(0x2115, 0x80);
    p16(0x4370, 0x1801);
    p16(0x4372, rom16(0x8FDC80 + a * 4));
    p16(0x4375, rom16(0x8FDC82 + a * 4));
    p16(0x2116, vadr);
    p8(0x420B, 0x80);
    return;
  }
  uint16_t x = tg_read16(0x82C9D0 + (uint16_t) ((a - 15) << 1));
  for(int i = 0; i < 12; i++, x++) {
    ws(N + 0x3C, vadr);
    p8(0x420B, 0x00);
    p8(0x4374, 0x82);
    p8(0x2115, 0x80);
    p16(0x4370, 0x1801);
    uint16_t c = (uint16_t) ((tg_read16(0x820000 | x) - 0x001E) & 0x00FF);
    p16(0x4372, (uint16_t) ((c << 6) + 0xCBF6));
    p16(0x4375, 0x0040);
    p16(0x2116, vadr);
    vadr = (uint16_t) (vadr + 0x0020);
    ws(N + 0x3C, vadr);
    p8(0x420B, 0x80);
  }
}

// Whose notices: the state's place in the $0A00 page (+4 on, as described above), where
// they go in the map copy and which tiles they use, the player's fuel, lap and place, and
// the blink's half period.
typedef struct {
  uint16_t n, at, tile, fuel, lap, place, blink;
} Notices;
static const Notices kNotices1P = { N, 0x03D0, 0x2E40, 0x01AE, 0x01A2, 0x01A0, 12 };
static const Notices kNoticesTop = { N, 0x0050, 0x2E40, 0x01AE, 0x01A2, 0x01A0, 6 };
static const Notices kNoticesBottom = { N + 0x1C, 0x03D0, 0x2E90, 0x01C8, 0x01BC, 0x01BA, 6 };

static void notice_clear(const Notices* k) {
  ws(k->n + 0x0C, 0xFFFF);
  ws(N + 0x00, k->at);
  ws(N + 0x02, k->tile);
  notice_erase(w(k->n + 0x08));
}

static void notice_start(const Notices* k, uint16_t picture, uint16_t kind) {
  ws(k->n + 0x08, picture);
  ws(k->n + 0x06, 1);
  ws(k->n + 0x04, kind);
}

// Picks the next notice to show ($BF90-$C089), most urgent first.
static void notice_pick(const Notices* k) {
  uint16_t n = k->n;
  if(w(k->fuel) >= 0x0300) ws(n + 0x18, 0);
  uint16_t a = w(n + 0x12);
  if(a != 0) {
    ws(n + 0x0E, a);
    notice_start(k, (uint16_t) (a + 0x002F), 4);
    return;
  }
  a = w(n + 0x14);
  if(a != 0) {
    ws(n + 0x14, 0);
    ws(n + 0x0E, 5);
    ws(n + 0x10, 0x000C);
    notice_start(k, (uint16_t) (a + 0x0035), 2);
    return;
  }
  a = w(n + 0x16);
  if(a != 0) {
    ws(n + 0x0E, a);
    ws(n + 0x10, k->blink);
    notice_start(k, (uint16_t) (a + 0x000E), 3);
    return;
  }
  if(w(n + 0x18) == 0 && w(k->fuel) < 0x0300) {
    // Low on fuel.
    ws(n + 0x18, 0xFFFF);
    ws(n + 0x0E, 3);
    ws(n + 0x10, k->blink);
    notice_start(k, 0x0026, 2);
    return;
  }
  a = (uint16_t) (w(0x019E) - w(k->lap));
  if(a != w(n + 0x1A)) {
    ws(n + 0x1A, a);
    if(!neg(a)) {
      ws(n + 0x0E, 0x0020);
      notice_start(k, (uint16_t) (a + 0x0027), 1);
      return;
    }
  }
  a = w(k->place);
  if(a != w(n + 0x1C)) {
    ws(n + 0x1C, a);
    ws(n + 0x0E, 0x0010);
    notice_start(k, (uint16_t) (a + 0x000F), 1);
    return;
  }
  a = w(n + 0x1E);
  if(a != 0) {
    ws(n + 0x1E, 0);
    ws(n + 0x0E, 0x0020);
    notice_start(k, (uint16_t) (a - 1), 1);
  }
}

static bool timer(uint16_t adr) {
  ws(adr, (uint16_t) (w(adr) - 1));
  return w(adr) == 0;
}

// The notice on screen runs its timing ($C08B-$C0E7).
static void notice_run(const Notices* k) {
  uint16_t n = k->n;
  bool done = false;
  switch(w(n + 0x04)) {
    case 1: // for $0E frames
      done = timer(n + 0x0E);
      break;
    case 2: // blinking $0E times
      if(!timer(n + 0x10)) return;
      ws(n + 0x10, k->blink);
      ws(n + 0x06, (uint16_t) -w(n + 0x06));
      if(!neg(w(n + 0x06))) return;
      done = timer(n + 0x0E);
      break;
    case 3: // blinking while $16 stays as it was
      if(w(n + 0x16) == w(n + 0x0E)) {
        if(!timer(n + 0x10)) return;
        ws(n + 0x10, k->blink);
        ws(n + 0x06, (uint16_t) -w(n + 0x06));
        return;
      }
      done = true;
      break;
    default: // while $12 stays as it was
      done = w(n + 0x12) != w(n + 0x0E);
      break;
  }
  if(!done) return;
  notice_clear(k);
  ws(n + 0x06, 0);
  ws(n + 0x04, 0);
  ws(n + 0x08, 0);
}

// Whether the player's notice needs its tiles sent; if so sends them.
static bool notice_wants_tiles(const Notices* k, uint16_t busy) {
  uint16_t n = k->n;
  if(w(busy) != 0 || w(n + 0x06) == 0 || w(n + 0x08) == w(n + 0x0A)) return false;
  ws(n + 0x0A, w(n + 0x08));
  ws(N + 0x02, k->tile);
  notice_tiles(w(n + 0x0A));
  return true;
}

// A player's message and notice for the frame: a new message's map (`msg` negative) is
// built, then the notice is drawn or blinked off and its timing run. A message that
// covers the notices (`busy`) stops them.
static void notice_frame(const Notices* k, uint16_t msg, uint16_t busy, uint16_t text, uint16_t dst,
                         uint16_t base) {
  uint16_t n = k->n;
  if(neg(w(msg))) {
    message_map(text, dst, base);
    ws(msg, 1);
    if(w(busy) != 0) {
      ws(n + 0x0A, 0xFFFF);
      ws(n + 0x0C, 0xFFFF);
      return;
    }
    if(w(n + 0x06) != 0) ws(n + 0x0C, 0xFFFF);
  } else if(w(busy) != 0) {
    return;
  }
  // Draw or blink the notice.
  uint16_t shown = w(n + 0x06);
  if(shown != 0) {
    uint16_t a = w(n + 0x08);
    if(a != w(n + 0x0A)) return;
    if(neg(shown)) {
      if(a == w(n + 0x0C)) notice_clear(k);
    } else if(a != w(n + 0x0C)) {
      ws(n + 0x0C, a);
      ws(N + 0x00, k->at);
      ws(N + 0x02, k->tile);
      notice_draw(a);
    }
  }
  if(w(n + 0x04) == 0) notice_pick(k);
  else notice_run(k);
}

// The frame's panel work when no message is waiting to be uploaded ($BEE0-$C0E9).
static void panel_frame(TgRegs* r) {
  dashboard_upload();
  queued_transfers(0x0F00);
  r->d = 0x0100;
  r->db = 0x8C;
  Tg2RaceColours1P(r);
  notice_frame(&kNotices1P, 0x0174, 0x0180, 0x0188, 0xCCE0, 0x3E08);
}

static void panel(TgRegs* r) {
  if(w(0x0164) != 0) {
    r->d = 0x0100;
    r->db = 0x8C;
    Tg2UploadQueue0(r);
    ws(0x0164, 0);
    return;
  }
  if(!neg(w(0x0162))) {
    ws(0x0162, 0xFFFF);
    panel_frame(r);
    return;
  }
  uint16_t a = w(0x0174);
  if(a != 0 && !neg(a)) {
    // A message's map is ready: upload it.
    ws(0x0162, 0);
    uint16_t len = message_upload(0x0188, 0x5000, 0x2080, 0x79E0, 0xCCD0);
    ws(0x0174, 0);
    queued_transfers((uint16_t) (len + 0x0E00));
    return;
  }
  if(notice_wants_tiles(&kNotices1P, 0x0180)) return;
  panel_frame(r);
}

// --- Two players ($01:C147) ---

// The two views take turns ($0162): a frame sends one player's finished panel to VRAM and
// builds the other's.
typedef struct {
  const Notices* notices;
  uint16_t msg, busy, text;      // $0174 / $0180 / $0188 for player 1
  uint16_t map, base;            // the message map's copy and its first tile number
  uint16_t tiles, block;         // VRAM for the message's two tile blocks
  uint16_t panel_vram, panel;    // the panel's map in VRAM and its copy
  void (*colours)(TgRegs* r);
} View;
static const View kTop = { &kNoticesTop, 0x0174, 0x0180, 0x0188, 0xC960, 0x3E08, 0x5000, 0x2080, 0x7820, 0xC950,
                           Tg2RaceColours2PTop };
static const View kBottom = { &kNoticesBottom, 0x0176, 0x0182, 0x018A, 0xCCE0, 0x3E58, 0x6000, 0x2580, 0x79E0,
                              0xCCD0, Tg2RaceColours2PBottom };

static void panel_2p(TgRegs* r) {
  r->d = 0x0100;
  r->db = 0x8C;
  if(w(0x0164) != 0 || w(0x0166) != 0) {
    if(w(0x0164) != 0) {
      Tg2UploadQueue0(r);
      ws(0x0164, 0);
    }
    if(w(0x0166) != 0) {
      Tg2UploadQueue1(r);
      ws(0x0166, 0);
    }
    return;
  }
  bool second = neg(w(0x0162));
  ws(0x0162, second ? 0 : 0xFFFF);
  const View* send = second ? &kBottom : &kTop;
  const View* build = second ? &kTop : &kBottom;

  uint16_t a = w(send->msg);
  if(a != 0 && !neg(a)) {
    uint16_t len = message_upload(send->text, send->tiles, send->block, send->panel_vram, send->panel);
    ws(send->msg, 0);
    queued_transfers((uint16_t) (len + 0x0E00));
  } else if(!notice_wants_tiles(send->notices, send->busy)) {
    p8(0x420B, 0x00);
    vram_dma(0x80, send->panel_vram, 0x1801, 0x7E, send->panel, 0x0340);
    queued_transfers(0x0C00);
  }
  build->colours(r);
  notice_frame(build->notices, build->msg, build->busy, build->text, build->map, build->base);
}

// $CE9D: sends the $40 cycling colours at $7E:D110 to the start of CGRAM (and counts the
// bytes in $EA, the frame's DMA budget).
static void send_cycle_colours(void) {
  p8(0x2121, 0x00);
  p8(0x420B, 0x00);
  p8(0x4374, 0x7E);
  p16(0x4370, 0x2202);
  p16(0x4372, 0xD110);
  p16(0x4375, 0x0040);
  ws(0x00EA, (uint16_t) (0x0040 + w(0x00EA)));
  p8(0x420B, 0x80);
}

// $CECF: in that kind of view ($010C = 4), turns the colour cycle one step: 24 colours in
// eight groups of three (each group skips the first colour of a four-colour row).
static void turn_cycle_colours(void) {
  if(w(0x010C) != 4) return;
  uint16_t first = w(0xD112);
  uint16_t dst = 0xD112;
  for(int i = 1; i < 24; i++) {
    uint16_t src = (uint16_t) (0xD112 + (i / 3) * 8 + (i % 3) * 2);
    ws(dst, w(src));
    dst = src;
  }
  ws(dst, first);
}

// $C649/$C7CE (one player) and $C953/$CBF8 (two players): when the screen mode changes,
// the tiles and dashboard pieces for it go to VRAM. A negative mode (the pause screen) takes
// the bank $94/$8C art; the race takes the race's own copies from $7E. Each row is one DMA:
// bank, VRAM address, source, length.
typedef struct { uint8_t bank; uint16_t vram, src, len; } ModeDma;
static const ModeDma kMode1PPause[] = {
  { 0x94, 0x2B70, 0xF44B, 0x00E0 }, { 0x94, 0x3240, 0xF62B, 0x0180 }, { 0x8C, 0x7875, 0xF288, 0x000E },
  { 0x8C, 0x7843, 0xF296, 0x0010 }, { 0x8C, 0x7863, 0xF2A6, 0x0010 }, { 0x8C, 0x7883, 0xF2B6, 0x0010 },
  { 0x8C, 0x78A3, 0xF2C6, 0x0010 }, { 0x8C, 0x78C3, 0xF2D6, 0x0010 },
};
static const ModeDma kMode1PRace[] = {
  { 0x94, 0x2B70, 0xF52B, 0x00E0 }, { 0x94, 0x3240, 0xF7AB, 0x0180 }, { 0x7E, 0x7875, 0xC9FA, 0x000E },
  { 0x7E, 0x7843, 0xC996, 0x0010 }, { 0x7E, 0x7863, 0xC9D6, 0x0010 }, { 0x7E, 0x7883, 0xCA16, 0x0010 },
  { 0x7E, 0x78A3, 0xCA56, 0x0010 }, { 0x7E, 0x78C3, 0xCA96, 0x0010 },
};
static const ModeDma kMode2PPause[] = {
  { 0x94, 0x2B70, 0xF44B, 0x00E0 }, { 0x94, 0x3240, 0xF62B, 0x0180 }, { 0x8C, 0x7858, 0xF288, 0x000E },
  { 0x8C, 0x7820, 0xF2E6, 0x0010 }, { 0x8C, 0x7840, 0xF2F6, 0x0010 }, { 0x8C, 0x7860, 0xF306, 0x0010 },
  { 0x8C, 0x7880, 0xF316, 0x0010 }, { 0x8C, 0x78A0, 0xF326, 0x0010 }, { 0x8C, 0x7A18, 0xF288, 0x000E },
  { 0x8C, 0x79E0, 0xF2E6, 0x0010 }, { 0x8C, 0x7A00, 0xF2F6, 0x0010 }, { 0x8C, 0x7A20, 0xF306, 0x0010 },
  { 0x8C, 0x7A40, 0xF316, 0x0010 }, { 0x8C, 0x7A60, 0xF326, 0x0010 },
};
static const ModeDma kMode2PRace[] = {
  { 0x94, 0x2B70, 0xF52B, 0x00E0 }, { 0x94, 0x3240, 0xF7AB, 0x0180 }, { 0x7E, 0x7858, 0xC9C0, 0x000E },
  { 0x7E, 0x7820, 0xC950, 0x0010 }, { 0x7E, 0x7840, 0xC990, 0x0010 }, { 0x7E, 0x7860, 0xC9D0, 0x0010 },
  { 0x7E, 0x7880, 0xCA10, 0x0010 }, { 0x7E, 0x78A0, 0xCA50, 0x0010 }, { 0x7E, 0x7A18, 0xCD40, 0x000E },
  { 0x7E, 0x79E0, 0xCCD0, 0x0010 }, { 0x7E, 0x7A00, 0xCD10, 0x0010 }, { 0x7E, 0x7A20, 0xCD50, 0x0010 },
  { 0x7E, 0x7A40, 0xCD90, 0x0010 }, { 0x7E, 0x7A60, 0xCDD0, 0x0010 },
};
static void mode_setup(TgRegs* r, const ModeDma* d, size_t n) {
  for(size_t i = 0; i < n; i++) {
    p8(0x420B, 0x00);
    vram_dma(0x80, d[i].vram, 0x1801, d[i].bank, d[i].src, d[i].len);
  }
  // It returns from `lda #$80` (8-bit) then `rep #$20`, after a 16-bit `lda #$0010`/`#$000E`.
  r->a = 0x0080;
  r->n = true;
  r->z = false;
}
#define MODE_SETUP(r, t) mode_setup(r, t, sizeof t / sizeof t[0])

void Tg2RaceVblank1P(TgRegs* r) {
  // Widescreen: this frame is a race frame, which the PPU redraws wider (PPU_RACE_FRAME).
  if(tg_port_wide_margin || tg_port_interp) tg_port_race_frame = 1;
  uint32_t db = (uint32_t) r->db << 16;
  uint16_t mode = w(0x0100);
  bool changed = mode != w(0x00F2);
  if(changed) {
    // The screen mode changed: set the PPU up for it.
    ws(0x00F2, mode);
    r->a = mode;
    if(neg(mode)) MODE_SETUP(r, kMode1PPause);
    else MODE_SETUP(r, kMode1PRace);
  }
  if(changed || neg(mode)) {
    // That frame, and every frame of a negative mode, only sends the buffers.
    send_frame(0xB31A);
    if(w(0x010C) == 1) window_registers();
    return;
  }

  if(!neg(w(0x0634))) ws(0x01AC, (uint16_t) (w(0x001A) - w(0x0602)));
  if(w(0x0122) != 0) {
    // The game loop finished a frame: swap the buffers and latch its values.
    ws(0x0122, 0);
    ws(0x0284, (uint16_t) ~w(0x0284));
    if(w(0x0170) != 0) ws(0x0164, 0xFFFF);
    ws(0x0124, (uint16_t) ~w(0x0124));
    ws(0x0126, (uint16_t) ~w(0x0126));
    ws(0x00E4, (uint16_t) (w(0x00EC) + w(0x00E4)));
    ws(0x00EC, 0);
    ws(0x0148, w(0x028E));
    p16(0x4302, w(0x028E));
    p16(0x4312, w(0x028C));
    p16(0x4322, w(0x028C));
    p16(0x4332, w(0x028C));
    p16(0x4342, w(0x028C));
    ws(0x023A, w(0x023C));
    ws(0x0132, w(0x023E));
    ws(0x014A, w(0x049A));
    ws(0x014C, w(0x04C4));
    ws(0x0118, w(0x011A));
    // The window registers for what the object pass saw ($015A).
    uint8_t seen = b(0x015A);
    uint16_t x = 0x00F0, y = 0x00F0, a = 0xFF08;
    if(seen == 0 || (seen & 0x01)) {
    } else if(seen & 0x06) {
      a = 0xE608;
    } else if(seen & 0x18) {
      y = 0x0000;
      a = 0xE000;
    } else if(seen & 0x20) {
      x = 0x0020;
      y = 0x0002;
      a = 0xE000;
    }
    ws(0x014E, x);
    ws(0x0150, y);
    ws(0x0152, a);
    // The dashboard's first line, from how far the view is pushed down ($0280).
    a = (uint16_t) (0x00E0 - w(0x0280));
    if(neg(a)) {
      a = 0x0070;
      x = 0x2121;
    } else if(!lt(a, 0x00E0)) {
      a = 0x0070;
      x = 0x0101;
    } else {
      x = 0x2101;
    }
    ws(0x0128, a);
    ws(0x012E, x | w(0x012C));
  }

  // The lightning flash ($0500 on): its brightness steps from the table at $82:DD96.
  uint16_t flash = 0;
  if(neg(w(0x0500))) {
    flash = w(0x0502);
    if(flash != 0) {
      flash = rom16(0x82DD96 + (uint16_t) (flash << 1));
      ws(0x0502, (uint16_t) (w(0x0502) - 1));
    }
  }
  ws(0x0508, flash);
  uint16_t x, y;
  if(neg(w(0x050A))) {
    x = w(0x010C) == 4 ? 0x7E60 : 0x019F;
    y = w(0x010C) == 4 ? 0x7F20 : 0x00DF;
  } else {
    x = w(0x010C) == 4 ? 0x7F4A : 0x00B5;
    y = w(0x010C) == 4 ? 0x7FEC : 0x0013;
  }
  ws(0x0AA6, x);
  ws(0x0B12, x);
  ws(0x0AA8, y);
  ws(0x0B14, y);
  y = neg(w(0x0284)) ? 0x0516 : 0x0536;
  bool flashing = neg(w(0x0508));
  y = patch_one(y, flashing, true);
  y = (uint16_t) (y + 2);
  patch_one(y, flashing, false);

  message_clock(0, 0x0506);

  // The road-stripe cycle ($0398, stepped by the speed at $0396): three colours from the
  // table at $C9B0 into this frame's queued colours.
  uint16_t a = w(0x0396);
  if(a == 0) {
    ws(0x0398, 0x0800);
  } else {
    if(a >= 0x0401) a = 0x0400;
    ws(0x0198, a);
    ws(0x0398, (uint16_t) (a + w(0x0398)));
  }
  x = neg(w(0x0126)) ? 0x0A40 : 0x0AAC;
  a = w(0x0398);
  y = (uint16_t) (((uint16_t) (a << 8 | a >> 8) << 1) & 0x001E);
  ws((uint16_t) (x + 0x60), tg_read16(db | (uint16_t) (0xC9B0 + y)));
  y = (uint16_t) ((y + 8) & 0x001E);
  ws((uint16_t) (x + 0x62), tg_read16(db | (uint16_t) (0xC9B0 + y)));
  y = (uint16_t) ((y + 8) & 0x001E);
  ws((uint16_t) (x + 0x64), tg_read16(db | (uint16_t) (0xC9B0 + y)));

  // The per-line tables for the kind of view ($010C).
  switch(w(0x010C)) {
    case 1:
      ws(0x0142, (uint16_t) (w(0x0132) + 0x0034));
      window_registers();
      hdma_tables(w(0x014A), w(0x014C), 0x7E, 0x7E);
      break;
    case 4:
      ws(0x0140, w(0x001A));
      ws(0x0142, (uint16_t) (w(0x0142) - 0x001E));
      hdma_tables(0xF5FB, 0xF637, 0x87, 0x87);
      send_cycle_colours();
      break;
    case 5:
      ws(0x0140, w(0x001A) >> 1);
      ws(0x0142, (uint16_t) (w(0x0142) + 0x0016));
      hdma_tables(0xF5FB, 0xF637, 0x87, 0x87);
      break;
    case 6:
      ws(0x0142, w(0x0132));
      hdma_tables(0xF5FA, 0xF637, 0x87, 0x87);
      break;
    default:
      hdma_tables(0xF5FA, w(0x0148), 0x87, 0x7E);
      break;
  }
  send_frame(0xB31A);
  uint16_t d = r->d;
  uint8_t bank = r->db;
  panel(r);
  r->d = d;
  r->db = bank;
  turn_cycle_colours();
}

// The window registers' values for what a view's object pass saw.
static void window_values(uint8_t seen, uint16_t dst) {
  uint16_t x = 0x00F0, y = 0x00F0, a = 0xFF08;
  if(seen == 0 || (seen & 0x01)) {
  } else if(seen & 0x06) {
    a = 0xE608;
  } else if(seen & 0x18) {
    y = 0x0000;
    a = 0xE000;
  } else if(seen & 0x20) {
    x = 0x0020;
    y = 0x0002;
    a = 0xE000;
  }
  ws(dst, x);
  ws((uint16_t) (dst + 2), y);
  ws((uint16_t) (dst + 4), a);
}

// The line a view's raster IRQ falls on and its screen mode, from how far the view is
// pushed down.
static void view_line(uint16_t line, uint16_t first, uint16_t fallback, uint16_t at, uint16_t mode) {
  uint16_t x = 0x2101;
  if(lt(line, first)) {
    line = fallback;
    x = 0x2121;
  } else if(!lt(line, (uint16_t) (first + 0x0067))) {
    line = fallback;
    x = 0x0101;
  }
  ws(at, line);
  ws(mode, x | w(0x012C));
}

// A view's sky colours for a lightning flash or not.
static void sky_colours(uint16_t flash, uint16_t dst) {
  bool cycle = w(0x010C) == 4;
  uint16_t x, y;
  if(neg(w(flash))) {
    x = cycle ? 0x7E60 : 0x019F;
    y = cycle ? 0x7F20 : 0x00DF;
  } else {
    x = cycle ? 0x7F4A : 0x00B5;
    y = cycle ? 0x7FEC : 0x0013;
  }
  ws(dst, x);
  ws((uint16_t) (dst + 0x6C), x);
  ws((uint16_t) (dst + 2), y);
  ws((uint16_t) (dst + 0x6E), y);
}

// A view's road-stripe cycle (position `at`, stepped by the speed before it) into this
// frame's queued colours.
static void stripes(uint32_t db, uint16_t at, uint16_t q_a, uint16_t q_b) {
  uint16_t a = w((uint16_t) (at - 2));
  if(a == 0) {
    ws(at, 0x0800);
  } else {
    if(a >= 0x0401) a = 0x0400;
    ws(0x0198, a);
    ws(at, (uint16_t) (a + w(at)));
  }
  uint16_t x = neg(w(0x0126)) ? q_a : q_b;
  a = w(at);
  uint16_t y = (uint16_t) (((uint16_t) (a << 8 | a >> 8) << 1) & 0x001E);
  for(int i = 0; i < 3; i++, y = (uint16_t) ((y + 8) & 0x001E))
    ws((uint16_t) (x + 0x60 + i * 2), tg_read16(db | (uint16_t) (0xC9B0 + y)));
}

// $01:B345: the same for two players, with a raster IRQ between the views ($BD44).
void Tg2RaceVblank2P(TgRegs* r) {
  // widescreen: see Tg2RaceVblank1P; side by side: each view full height in half the width
  if(tg_port_wide_margin || tg_port_interp) tg_port_race_frame = (uint8_t) (1 | (tg_port_side_by_side && tg_port_wide_margin && !neg(w(0x0100)) ? 4 : 0));
  uint32_t db = (uint32_t) r->db << 16;
  uint16_t mode = w(0x0100);
  bool changed = mode != w(0x00F2);
  if(changed) {
    ws(0x00F2, mode);
    r->a = mode;
    if(neg(mode)) MODE_SETUP(r, kMode2PPause);
    else MODE_SETUP(r, kMode2PRace);
  }
  if(changed || neg(mode)) {
    send_frame(0xBD44);
    if(w(0x010C) == 1) window_registers();
    return;
  }

  uint16_t a = (uint16_t) (w(0x001A) - w(0x0602));
  if(!neg(w(0x0634))) ws(0x01AC, a);
  if(!neg(w(0x0636))) ws(0x01C6, a);
  if(w(0x0122) != 0) {
    ws(0x0122, 0);
    ws(0x0284, (uint16_t) ~w(0x0284));
    if(w(0x0170) != 0) ws(0x0164, 0xFFFF);
    if(w(0x0172) != 0) ws(0x0166, 0xFFFF);
    ws(0x0124, (uint16_t) ~w(0x0124));
    ws(0x0126, (uint16_t) ~w(0x0126));
    ws(0x00E4, (uint16_t) (w(0x00EC) + w(0x00E4)));
    ws(0x00EC, 0);
    ws(0x0148, w(0x028E));
    p16(0x4302, w(0x028E));
    p16(0x4312, w(0x028C));
    p16(0x4322, w(0x028C));
    p16(0x4332, w(0x028C));
    p16(0x4342, w(0x028C));
    ws(0x023A, w(0x023C));
    ws(0x0276, w(0x0278));
    ws(0x0132, w(0x023E));
    ws(0x0134, w(0x027A));
    ws(0x014A, w(0x049A));
    ws(0x014C, w(0x04C4));
    ws(0x0118, w(0x011A));
    ws(0x011C, w(0x011E));
    window_values(b(0x015A), 0x014E);
    window_values(b(0x015C), 0x0154);
    view_line((uint16_t) (0x006C - w(0x0280)), 0x0005, 0x0038, 0x0128, 0x012E);
    view_line((uint16_t) (0x00DC - w(0x0282)), 0x0075, 0x00A8, 0x012A, 0x0130);
  }

  uint16_t flash = 0;
  if(neg(w(0x0500))) {
    flash = w(0x0502);
    if(flash != 0) {
      flash = rom16(0x82DD96 + (uint16_t) (flash << 1));
      ws(0x0502, (uint16_t) (w(0x0502) - 1));
    }
  }
  ws(0x0508, flash);
  ws(0x0510, flash);
  sky_colours(0x050A, 0x0AA6);
  sky_colours(0x0512, 0x0B7E);
  uint16_t y = neg(w(0x0284)) ? 0x0516 : 0x0536;
  y = patch_one(y, neg(w(0x0508)), true);
  patch_one((uint16_t) (y + 2), neg(w(0x0508)), false);
  y = neg(w(0x0284)) ? 0x0556 : 0x0576;
  y = patch_one(y, neg(w(0x0510)), true);
  patch_one((uint16_t) (y + 2), neg(w(0x0510)), false);

  message_clock(0, 0x0506);
  message_clock(2, 0x050E);
  stripes(db, 0x0398, 0x0A40, 0x0AAC);
  stripes(db, 0x03C0, 0x0B18, 0x0B84);

  switch(w(0x010C)) {
    case 1:
      ws(0x0142, (uint16_t) (w(0x0132) + 0x0034));
      ws(0x0146, (uint16_t) (w(0x0134) + 0x0034));
      window_registers();
      hdma_tables(w(0x014A), w(0x014C), 0x7E, 0x7E);
      break;
    case 4:
      ws(0x0140, w(0x001A));
      ws(0x0144, (uint16_t) (w(0x001A) + 0x0043));
      ws(0x0142, (uint16_t) (w(0x0142) - 0x001E));
      ws(0x0146, w(0x0142));
      hdma_tables(0xF60B, 0xF63B, 0x87, 0x87);
      send_cycle_colours();
      break;
    case 5:
      ws(0x0140, w(0x001A) >> 1);
      ws(0x0144, (uint16_t) ((w(0x001A) >> 1) + 0x0043 + (w(0x001A) & 1))); // the shift's carry is added too
      ws(0x0142, (uint16_t) (w(0x0142) + 0x0016));
      ws(0x0146, w(0x0142));
      hdma_tables(0xF60B, 0xF63B, 0x87, 0x87);
      break;
    case 6:
      ws(0x0142, w(0x0132));
      ws(0x0146, w(0x0134));
      hdma_tables(0xF5FA, 0xF63B, 0x87, 0x87);
      break;
    default:
      hdma_tables(0xF5FA, w(0x0148), 0x87, 0x7E);
      break;
  }
  send_frame(0xBD44);
  uint16_t d = r->d;
  uint8_t bank = r->db;
  panel_2p(r);
  r->d = d;
  r->db = bank;
  turn_cycle_colours();
}

// --- The NMI handler's own transfers ($01:E2F2 calls these every frame) ---

// $E74F: a palette waiting at $D8:$D6 ($D4 set) goes to CGRAM, all 256 colours.
void Tg2SendPalette(TgRegs* r) {
  (void) r;
  if(w(0x00D4) == 0) return spend(0x001D);
  ws(0x00D4, 0);
  p8(0x2121, 0x00);
  p8(0x420B, 0x00);
  p8(0x4374, b(0x00D8));
  p16(0x4370, 0x2202);
  p16(0x4372, w(0x00D6));
  p16(0x4375, 0x0200);
  spend(0x025C);
  p8(0x420B, 0x80);
}

// $E821: a sprite table waiting at $DE:$DC ($DA set) goes to OAM.
void Tg2SendSprites(TgRegs* r) {
  (void) r;
  if(w(0x00DA) == 0) return spend(0x001D);
  ws(0x00DA, 0);
  p16(0x2102, 0x0000);
  p8(0x420B, 0x00);
  p8(0x4374, b(0x00DE));
  p16(0x4370, 0x0402);
  p16(0x4372, w(0x00DC));
  p16(0x4375, 0x0220);
  spend(0x027E);
  p8(0x420B, 0x80);
}

// $E904.
void Tg2RunQueue(TgRegs* r) {
  (void) r;
  run_queue();
}

void tg2_queue_body(uint8_t db) {
  if(db == 0x7E || db == 0x7F) g_bank = (uint32_t) db << 16;
  queue_body();
  g_bank = 0;
}
