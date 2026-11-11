// Top Gear 2: the race's vertical-blank routine for one player ($01:AC79), called by the
// NMI handler ($01:E3EA, through the vector at $0010) with D = 0 and DB = $82.
//
// Every frame it sends the queued colours and the sprite table to the PPU and sets the
// raster IRQ for the top of the dashboard. When the game loop has finished a frame
// ($0122) it also swaps the double buffers (road HDMA lists $0284, sprites $0124,
// colours $0126), latches the scroll and window values the frame was built with, and
// steps the things that run on the frame clock: the lightning flash, the weather
// colours, the road-stripe cycle and the message at the top of the screen.

#include "../port/port.h"

static uint16_t w(uint16_t adr) { return ram16(adr); }
static void ws(uint16_t adr, uint16_t v) { ram16_set(adr, v); }
static uint8_t b(uint16_t adr) { return g_ram[adr]; }
static void wb(uint16_t adr, uint8_t v) { g_ram[adr] = v; }
static bool neg(uint16_t v) { return (v & 0x8000) != 0; }
static bool lt(uint16_t a, uint16_t c) { return neg((uint16_t) (a - c)); }
static void p8(uint16_t adr, uint8_t v) { tg_bus_write8(adr, v); }
static void p16(uint16_t adr, uint16_t v) { tg_bus_write16(adr, v); }

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
static void send_frame(void) {
  p16(0x4209, w(0x0128));
  ws(0x0016, 0xB31A);
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
// text is up ($0188), and its timing.
static void message_clock(void) {
  ws(0x0194, (uint16_t) (w(0x0194) + 0x0100));
  if(w(0x0174) != 0) return;
  if(neg(w(0x0506))) {
    if(w(0x0188) != 0xE940) {
      ws(0x0188, 0xE940);
      ws(0x0174, (uint16_t) (w(0x0174) - 1));
      ws(0x0180, 0);
    }
    return;
  }
  uint16_t a = w(0x017C);
  if(neg(a)) {
    a = (uint16_t) (w(0x0178) * 10 + 0xE94A);
    if(a != w(0x0188)) {
      ws(0x0188, a);
      ws(0x0174, (uint16_t) (w(0x0174) - 1));
      ws(0x0180, 0);
    }
    return;
  }
  if(a == 0) {
    ws(0x017C, 1);
    ws(0x0194, 0);
    uint16_t x = tg_read16(0x8CE990 + (uint16_t) (w(0x0184) << 1));
    ws(0x018C, tg_read16(0x8C0000 + x));
    ws(0x0188, (uint16_t) (x + 2));
    ws(0x0174, (uint16_t) (w(0x0174) - 1));
    ws(0x0180, 0xFFFF);
    return;
  }
  while(w(0x018C) != 0 && w(0x0194) >= w(0x0190)) {
    ws(0x0194, (uint16_t) (w(0x0194) - w(0x0190)));
    ws(0x018C, (uint16_t) (w(0x018C) - 1));
    ws(0x0188, (uint16_t) (w(0x0188) + 0x000A));
    ws(0x0174, (uint16_t) (w(0x0174) - 1));
    ws(0x0180, 0xFFFF);
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

// $A066: a message's tile map (13 rows of 16 tiles) into the panel's map copy at
// $7E:CCE0, as tile numbers from $3E08 up. (The additions chain their carry, as the
// original's do.)
static void message_map(void) {
  uint16_t x = w(0x0188);
  uint32_t src = (uint32_t) (rec(x, 6) & 0xFF) << 16;
  uint16_t y = (uint16_t) (rec(x, 2) + 0x001E);
  uint32_t carry = 0;
  for(int col = 0x1E; col >= 0; col -= 2, y = (uint16_t) (y - 2)) {
    for(int row = 0; row < 13; row++) {
      uint32_t t = (uint32_t) tg_read16(src | (uint16_t) (y + row * 0x20)) + 0x3E08 + carry;
      carry = t >> 16;
      ws((uint16_t) (0xCCE0 + row * 0x40 + col), (uint16_t) t);
    }
  }
}

// $A264: a message's tiles and its map to VRAM. Returns the second block's length.
static uint16_t message_upload(void) {
  uint16_t x = w(0x0188);
  p8(0x420B, 0x00);
  vram_dma(0x00, 0x5000, 0x1800, 0x8C, (uint16_t) (rec(x, 0) + 0x8B40), 0x0200);
  uint16_t len = rec(x, 8);
  vram_dma(0x80, 0x2080, 0x1801, (uint8_t) rec(x, 6), rec(x, 4), len);
  vram_dma(0x80, 0x79E0, 0x1801, 0x7E, 0xCCD0, 0x0340);
  return len;
}

// $A432: the dashboard's map copy ($7E:C990) to VRAM.
static void dashboard_upload(void) {
  p8(0x420B, 0x00);
  vram_dma(0x80, 0x7840, 0x1801, 0x7E, 0xC990, 0x0640);
}

// The queued VRAM transfers ($E904), if the frame's DMA budget ($EA against $E2) has
// room for `cost` more.
static void queued_transfers(TgRegs* r, uint16_t cost, uint16_t ret) {
  uint16_t a = (uint16_t) (cost + w(0x00EA));
  if(!lt(a, w(0x00E2))) return;
  ws(0x00EA, (uint16_t) (a - 0x0200));
  ws(0x00E0, 0);
  r->d = 0;
  r->db = 0x82;
  tg_call_jsr(r, 0xE904, ret, TG_MX_16);
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

static void notice_clear(void) {
  ws(N + 0x0C, 0xFFFF);
  ws(N + 0x00, 0x03D0);
  ws(N + 0x02, 0x2E40);
  notice_erase(w(N + 0x08));
}

static void notice_start(uint16_t picture, uint16_t kind) {
  ws(N + 0x08, picture);
  ws(N + 0x06, 1);
  ws(N + 0x04, kind);
}

// Picks the next notice to show ($BF90-$C089), most urgent first.
static void notice_pick(void) {
  if(w(0x01AE) >= 0x0300) ws(N + 0x18, 0);
  uint16_t a = w(N + 0x12);
  if(a != 0) {
    ws(N + 0x0E, a);
    notice_start((uint16_t) (a + 0x002F), 4);
    return;
  }
  a = w(N + 0x14);
  if(a != 0) {
    ws(N + 0x14, 0);
    ws(N + 0x0E, 5);
    ws(N + 0x10, 0x000C);
    notice_start((uint16_t) (a + 0x0035), 2);
    return;
  }
  a = w(N + 0x16);
  if(a != 0) {
    ws(N + 0x0E, a);
    ws(N + 0x10, 0x000C);
    notice_start((uint16_t) (a + 0x000E), 3);
    return;
  }
  if(w(N + 0x18) == 0 && w(0x01AE) < 0x0300) {
    // Low on fuel.
    ws(N + 0x18, 0xFFFF);
    ws(N + 0x0E, 3);
    ws(N + 0x10, 0x000C);
    notice_start(0x0026, 2);
    return;
  }
  a = (uint16_t) (w(0x019E) - w(0x01A2));
  if(a != w(N + 0x1A)) {
    ws(N + 0x1A, a);
    if(!neg(a)) {
      ws(N + 0x0E, 0x0020);
      notice_start((uint16_t) (a + 0x0027), 1);
      return;
    }
  }
  a = w(0x01A0);
  if(a != w(N + 0x1C)) {
    ws(N + 0x1C, a);
    ws(N + 0x0E, 0x0010);
    notice_start((uint16_t) (a + 0x000F), 1);
    return;
  }
  a = w(N + 0x1E);
  if(a != 0) {
    ws(N + 0x1E, 0);
    ws(N + 0x0E, 0x0020);
    notice_start((uint16_t) (a - 1), 1);
  }
}

static bool timer(uint16_t adr) {
  ws(adr, (uint16_t) (w(adr) - 1));
  return w(adr) == 0;
}

// The notice on screen runs its timing ($C08B-$C0E7).
static void notice_run(void) {
  bool done = false;
  switch(w(N + 0x04)) {
    case 1: // for $0E frames
      done = timer(N + 0x0E);
      break;
    case 2: // blinking $0E times
      if(!timer(N + 0x10)) return;
      ws(N + 0x10, 0x000C);
      ws(N + 0x06, (uint16_t) -w(N + 0x06));
      if(!neg(w(N + 0x06))) return;
      done = timer(N + 0x0E);
      break;
    case 3: // blinking while $16 stays as it was
      if(w(N + 0x16) == w(N + 0x0E)) {
        if(!timer(N + 0x10)) return;
        ws(N + 0x10, 0x000C);
        ws(N + 0x06, (uint16_t) -w(N + 0x06));
        return;
      }
      done = true;
      break;
    default: // while $12 stays as it was
      done = w(N + 0x12) != w(N + 0x0E);
      break;
  }
  if(!done) return;
  notice_clear();
  ws(N + 0x06, 0);
  ws(N + 0x04, 0);
  ws(N + 0x08, 0);
}

// The frame's panel work when no message is waiting to be uploaded ($BEE0-$C0E9).
static void panel_frame(TgRegs* r) {
  dashboard_upload();
  queued_transfers(r, 0x0F00, 0xBF06);
  r->d = 0x0100;
  r->db = 0x8C;
  Tg2RaceColours1P(r);
  if(neg(w(0x0174))) {
    message_map();
    ws(0x0174, 1);
    if(w(0x0180) != 0) {
      ws(N + 0x0A, 0xFFFF);
      ws(N + 0x0C, 0xFFFF);
      return;
    }
    if(w(N + 0x06) != 0) ws(N + 0x0C, 0xFFFF);
  } else if(w(0x0180) != 0) {
    return;
  }
  // Draw or blink the notice.
  uint16_t shown = w(N + 0x06);
  if(shown != 0) {
    uint16_t a = w(N + 0x08);
    if(a != w(N + 0x0A)) return;
    if(neg(shown)) {
      if(a == w(N + 0x0C)) notice_clear();
    } else if(a != w(N + 0x0C)) {
      ws(N + 0x0C, a);
      ws(N + 0x00, 0x03D0);
      ws(N + 0x02, 0x2E40);
      notice_draw(a);
    }
  }
  if(w(N + 0x04) == 0) notice_pick();
  else notice_run();
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
    uint16_t len = message_upload();
    ws(0x0174, 0);
    queued_transfers(r, (uint16_t) (len + 0x0E00), 0xC142);
    return;
  }
  if(w(0x0180) == 0 && w(N + 0x06) != 0 && w(N + 0x08) != w(N + 0x0A)) {
    // The notice's tiles.
    ws(N + 0x0A, w(N + 0x08));
    ws(N + 0x02, 0x2E40);
    notice_tiles(w(N + 0x0A));
    return;
  }
  panel_frame(r);
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

void Tg2RaceVblank1P(TgRegs* r) {
  uint32_t db = (uint32_t) r->db << 16;
  uint16_t mode = w(0x0100);
  bool changed = mode != w(0x00F2);
  if(changed) {
    // The screen mode changed: set the PPU up for it.
    ws(0x00F2, mode);
    r->a = mode;
    if(neg(mode)) tg_call_jsr(r, 0xC649, 0xAC89, TG_MX_16);
    else tg_call_jsr(r, 0xC7CE, 0xAC8E, TG_MX_16);
  }
  if(changed || neg(mode)) {
    // That frame, and every frame of a negative mode, only sends the buffers.
    send_frame();
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

  message_clock();

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
  send_frame();
  uint16_t d = r->d;
  uint8_t bank = r->db;
  panel(r);
  r->d = d;
  r->db = bank;
  turn_cycle_colours();
}
