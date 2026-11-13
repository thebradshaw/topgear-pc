// Top Gear 3000: text on the title and option screens' background (bank $0A).
//
// $0A:B83C puts one character (A) at column $3A, row $3C of the tilemap buffer at
// $7F:0000 as two tiles, one over the other, and moves the column on. With $0D7C set it
// uses the big font (tiles from $81:CCD9: letters and a 15-mark punctuation list at
// $8A:B8DA, digits in palette $3C00); otherwise the small one (tiles from $C9C4/$CA3A).

#include <stddef.h>

#include "../port/port.h"

static uint16_t z(const TgRegs* r, uint8_t o) { return ram16((uint16_t) (r->d + o)); }
static void zs(const TgRegs* r, uint8_t o, uint16_t v) { ram16_set((uint16_t) (r->d + o), v); }

static uint32_t at(const TgRegs* r, uint16_t a) { return ((uint32_t) r->db << 16) | a; }
static uint16_t rd16(uint32_t a) { return (uint16_t) (tg_bus_read8(a) | tg_bus_read8(a + 1) << 8); }

void Tg3TitleChar(TgRegs* r) {
  uint16_t font = rd16(at(r, 0x0D7C));
  bool big = font != 0;
  // The flags come back as the font test left them (PHP after LDX).
  r->n = (font & 0x8000) != 0;
  r->z = font == 0;
  uint16_t ch = r->a & 0xFF;
  zs(r, 0x36, z(r, 0x3A));
  zs(r, 0x38, z(r, 0x3C));
  zs(r, 0x36, (uint16_t) (z(r, 0x36) << 1));
  uint16_t pos = (uint16_t) ((z(r, 0x38) << 6) + z(r, 0x36));
  uint16_t a;
  if(big) {
    uint16_t pal = 0x3800, code;
    if(ch >= 0x30 && ch < 0x3A) {
      code = (uint16_t) (ch - 0x16);
      pal = 0x3C00;
    } else if(ch >= 0x41 && ch < 0x5B) {
      code = (uint16_t) (ch - 0x41);
    } else if(ch == 0x20) {
      code = 0x33;
    } else {
      code = 0x24;
      while(tg_bus_read8(0x8AB8D6u + code) != (uint8_t) ch && code != 0x32) code++;
      if(tg_bus_read8(0x8AB8D6u + code) != (uint8_t) ch) code = 0x33;
    }
    zs(r, 0x42, pal);
    uint16_t idx = (uint16_t) (code << 2);
    tg_bus_write16(0x7F0000u + pos, (uint16_t) ((rd16(0x81CCD9u + idx) | pal) + 0x100));
    a = (uint16_t) ((rd16(0x81CCDBu + idx) | pal) + 0x100);
    tg_bus_write16(0x7F0040u + pos, a);
  } else {
    uint16_t y = (uint16_t) ((ch - 0x20) << 1);
    tg_bus_write16(0x7F0000u + pos, rd16(at(r, (uint16_t) (0xC9C4 + y))) | 0x3C00);
    a = rd16(at(r, (uint16_t) (0xCA3A + y))) | 0x3C00;
    tg_bus_write16(0x7F0040u + pos, a);
  }
  zs(r, 0x3A, (uint16_t) (z(r, 0x3A) + 1));
  r->a = a;
  r->x = pos;
}

static void put_char(TgRegs* r, uint8_t c, uint16_t site) {
  r->a = (uint16_t) ((r->a & 0xFF00) | c);
  const TgPortFunc* fn = tg_port_find(0x0AB83C);
  if(fn != NULL) {
    fn->fn(r);
    if(!tg_port_take_declined()) return;
  }
  tg_call_jsr(r, 0xB83C, (uint16_t) (site + 3), TG_MX_A8_I16);
}

// $0A:B6AA: a zero-ended string (A, in the data bank) centred between columns $3E and
// $40 on row $3C, or blanked with spaces while $68 is set. Unless $86 is set, it then
// marks row $3C's entry at $0D04 by whether the string's length is odd.
void Tg3TitleLine(TgRegs* r) {
  uint16_t x = r->x, str = r->a;
  if(z(r, 0x86) == 0 && ((uint16_t) (z(r, 0x3C) - 0x0A) & 0x8000)) {
    tg_port_decline();  // the original returns through the pushed Y here
    return;
  }
  uint16_t span = (uint16_t) (z(r, 0x40) - z(r, 0x3E));
  zs(r, 0x42, (uint16_t) (span >> 1));
  zs(r, 0x44, str);
  uint16_t len = 0;
  while(tg_bus_read8(at(r, (uint16_t) (str + len))) != 0) len++;
  zs(r, 0x5A, len);
  zs(r, 0x46, (uint16_t) (len >> 1));
  uint16_t a = (uint16_t) (z(r, 0x42) - z(r, 0x46));
  uint32_t s = (uint32_t) a + z(r, 0x3E);
  uint16_t res = (uint16_t) s;
  r->c = s > 0xFFFF;
  r->v = (~(a ^ z(r, 0x3E)) & (a ^ res) & 0x8000) != 0;
  zs(r, 0x3A, (uint16_t) (res + 1));
  r->a = (uint16_t) (res + 1);
  for(uint16_t y = 0;; y++) {
    uint8_t c = tg_bus_read8(at(r, (uint16_t) (str + y)));
    r->y = y;
    if(c == 0) {
      r->a = (r->a & 0xFF00);
      break;
    }
    bool blank = g_ram[(uint16_t) (r->d + 0x68)] != 0;
    if(blank) put_char(r, 0x20, 0xB6E5);
    else put_char(r, c, 0xB6EF);
  }
  uint16_t flag = z(r, 0x86);
  r->a = flag;
  if(flag == 0) {
    uint16_t row = z(r, 0x3C);
    uint16_t d = (uint16_t) (row - 0x0A);
    r->c = row >= 0x0A;
    r->v = ((row ^ 0x0A) & (row ^ d) & 0x8000) != 0;
    r->a = (z(r, 0x5A) & 1) ? 4 : 0;
    tg_bus_write16(at(r, (uint16_t) (0x0D04 + d)), r->a);
  }
  r->x = x;
  r->n = (x & 0x8000) != 0;
  r->z = x == 0;
}
