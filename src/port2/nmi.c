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
      r->a = 0x8787;
      tg_call_jsr(r, 0xCE9D, 0xB238, TG_MX_16);
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
  tg_call_jsr(r, 0xBEBB, 0xB316, TG_MX_16);
  tg_call_jsr(r, 0xCECF, 0xB319, TG_MX_16);
}
