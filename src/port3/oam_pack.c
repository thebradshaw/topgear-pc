// Top Gear 3000: packs the sprite buffer's size and X-high bits (bank $01).
//
// The buffer keeps each sprite's ninth X bit as bit 7 of byte 1 and its size bit as bit 0
// of byte 2 of a four-byte record at +$200 (sprites 0-63) and +$300 (64-127) from the
// sprite table. $01:8101 (table at $6C) and $01:80F6 (table at $0900) shift those bits out
// into the 32-byte high table at +$200, then fill the unused slots from $74 up to $72
// with sprites parked off screen ($E0E0).

#include "../port/port.h"

static uint8_t rd(uint16_t a) { return tg_bus_read8(a); }
static void wr(uint16_t a, uint8_t v) { tg_bus_write8(a, v); }

static void pack(TgRegs* r, uint16_t table) {
  uint16_t d = (uint16_t) (table + 0x200);
  uint8_t a = (uint8_t) r->a;
  for(int half = 0; half < 2; half++, d = (uint16_t) (d + 0x100))
    for(int k = 0; k < 16; k++) {
      for(int j = 0; j < 4; j++) {
        uint16_t p = (uint16_t) (d + 16 * k + 4 * j + 1);
        uint8_t b = rd(p);
        wr(p, (uint8_t) (b << 1));
        a = (uint8_t) ((a >> 1) | (b & 0x80));
        p++;
        b = rd(p);
        wr(p, (uint8_t) (b >> 1));
        a = (uint8_t) ((a >> 1) | ((b & 1) << 7));
      }
      if(half == 0) wr((uint16_t) (d + k), a);
      else tg_bus_write8(((uint32_t) r->db << 16) | (uint16_t) (table + 0x210 + k), a);
    }
  r->x = table;
  // Fill the slots past the last one used.
  uint16_t end = ram16((uint16_t) (r->d + 0x72)), at = ram16((uint16_t) (r->d + 0x74));
  uint32_t diff = (uint32_t) at - end;
  uint16_t res = (uint16_t) diff;
  r->v = ((at ^ end) & (at ^ res) & 0x8000) != 0;
  if(at >= end) {
    r->a = res;
    r->c = true;
    r->n = (res & 0x8000) != 0;
    r->z = res == 0;
    return;
  }
  uint16_t neg = (uint16_t) (res ^ 0xFFFF) + 1;
  uint16_t n = (uint16_t) (neg >> 2);
  r->c = (neg >> 1) & 1;
  uint16_t x = at;
  do {
    tg_bus_write16(((uint32_t) r->db << 16) | (uint16_t) (0x0900 + x), 0xE0E0);
    x = (uint16_t) (x + 4);
  } while(--n);
  r->a = 0xE0E0;
  r->x = x;
  r->y = 0;
  r->n = false;
  r->z = true;
}

static bool fill_ok(const TgRegs* r) {
  uint16_t end = ram16((uint16_t) (r->d + 0x72)), at = ram16((uint16_t) (r->d + 0x74));
  return at >= end || (uint16_t) (end - at) >= 4;
}

void Tg3PackSprites(TgRegs* r) {
  if(!fill_ok(r)) {
    tg_port_decline();
    return;
  }
  pack(r, ram16((uint16_t) (r->d + 0x6C)));
}

void Tg3PackSpritesMenu(TgRegs* r) {
  if(!fill_ok(r)) {
    tg_port_decline();
    return;
  }
  pack(r, 0x0900);
}
