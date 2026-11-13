// Top Gear 3000: menu text drawn as sprites (bank $00).
//
// $00:841A turns a zero-ended string (A: its bank, X: its address) into sprites, one a
// character, from the place in Y (low byte across, high byte down) into the sprite buffer
// at $0900 + the next slot ($74). Lowercase letters are tiles 0-25, capitals 26-51,
// digits 52-61 and fourteen punctuation marks 62-75 (the list at $00:84B0); each tile is
// offset by $14 and moves the place on by its width (the table at $80:85E5). A space moves
// on 6 pixels, a byte with bit 7 set is skipped. $00:84BE does the same and then centres
// the text on that place.

#include "../port/port.h"

static uint16_t z(const TgRegs* r, uint8_t o) { return ram16((uint16_t) (r->d + o)); }
static void zs(const TgRegs* r, uint8_t o, uint16_t v) { ram16_set((uint16_t) (r->d + o), v); }
static void zb(const TgRegs* r, uint8_t o, uint8_t v) { g_ram[(uint16_t) (r->d + o)] = v; }

// 8-bit ADC with the carry and overflow it leaves.
static uint8_t adc8(TgRegs* r, uint8_t a, uint8_t b) {
  unsigned s = (unsigned) a + b + (r->c ? 1 : 0);
  r->v = (~(a ^ b) & (a ^ s) & 0x80) != 0;
  r->c = s > 0xFF;
  return (uint8_t) s;
}

void Tg3DrawText(TgRegs* r) {
  uint8_t db = r->db;
  uint32_t bank = (uint32_t) (r->a & 0xFF) << 16;
  uint8_t hi = (uint8_t) (r->a >> 8);  // B, until a character is drawn
  zs(r, 0x16, r->y);
  uint16_t y = z(r, 0x74), x = r->x;
  for(;;) {
    uint8_t c = tg_read8(bank | x);
    if(c == 0) break;
    if(c & 0x80) {
      x++;
      continue;
    }
    if(c == 0x20) {
      r->c = false;
      zb(r, 0x16, adc8(r, (uint8_t) z(r, 0x16), 6));
      x++;
      continue;
    }
    uint16_t code = c;
    if(c >= 0x30 && c < 0x3A) code = (uint8_t) (c + 4);
    else if(c >= 0x41 && c < 0x5B) code = (uint8_t) (c - 0x27);
    else if(c >= 0x61 && c < 0x7B) code = (uint8_t) (c - 0x61);
    else {
      for(uint16_t i = 0x3E; i < 0x4C; i++)
        if(tg_read8(0x808472u + i) == c) {
          code = i;
          break;
        }
    }
    tg_bus_write16(bank | (uint16_t) (0x0902 + y), (uint16_t) (code + z(r, 0x14)));
    tg_bus_write16(bank | (uint16_t) (0x0900 + y), z(r, 0x16));
    tg_bus_write16(bank | (uint16_t) (0x0B01 + y), 0);
    hi = 0;
    r->c = false;
    zb(r, 0x16, adc8(r, tg_read8(0x8085E5u + code), (uint8_t) z(r, 0x16)));
    y = (uint16_t) (y + 4);
    x++;
  }
  zs(r, 0x74, y);
  r->y = z(r, 0x16);
  r->x = x;
  r->a = (uint16_t) (hi << 8);
  r->db = db;
  r->n = (r->y & 0x8000) != 0;
  r->z = r->y == 0;
}

void Tg3DrawTextCentred(TgRegs* r) {
  // With nothing drawn, the original's centring loop would run on round all of memory.
  uint32_t bank = (uint32_t) (r->a & 0xFF) << 16;
  bool any = false;
  for(uint16_t x = r->x;; x++) {
    uint8_t c = tg_read8(bank | x);
    if(c == 0) break;
    if(!(c & 0x80) && c != 0x20) {
      any = true;
      break;
    }
  }
  if(!any) {
    tg_port_decline();
    return;
  }
  zs(r, 0x16, r->a);
  uint16_t first = z(r, 0x74), start = r->y;
  r->a = z(r, 0x16);
  Tg3DrawText(r);
  zs(r, 0x14, r->y & 0xFF);
  uint16_t a = (uint16_t) (((uint16_t) ((start & 0xFF) - z(r, 0x14))) >> 1);
  zs(r, 0x14, a);
  uint16_t y = first;
  uint32_t db = (uint32_t) r->db << 16;
  uint8_t lo = 0;
  do {
    r->c = false;
    lo = adc8(r, tg_bus_read8(db | (uint16_t) (0x0900 + y)), (uint8_t) a);
    tg_bus_write8(db | (uint16_t) (0x0900 + y), lo);
    y = (uint16_t) (y + 4);
  } while(y != z(r, 0x74));
  r->y = y;
  r->a = (uint16_t) ((a & 0xFF00) | lo);
  r->n = false;
  r->z = true;
  r->c = true;
}
