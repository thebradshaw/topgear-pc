// Top Gear 3000: the title screen's sprite list (bank $0A).
//
// $0A:8E1B copies the title's objects (128 slots: flags $0500, X $0600, Y $0700, tile
// $0800) into the sprite buffer at $0900, skipping empty slots and those below line
// $F0, sets their size and X-high bits in the high table at $0B00, and parks the
// sprites left over from the frame before ($30).

#include "../port/port.h"

static uint16_t z(const TgRegs* r, uint8_t o) { return ram16((uint16_t) (r->d + o)); }
static void zs(const TgRegs* r, uint8_t o, uint16_t v) { ram16_set((uint16_t) (r->d + o), v); }

static uint32_t at(const TgRegs* r, uint16_t a) { return ((uint32_t) r->db << 16) | a; }
static uint16_t rd16(const TgRegs* r, uint16_t a) {
  return (uint16_t) (tg_bus_read8(at(r, a)) | tg_bus_read8(at(r, (uint16_t) (a + 1))) << 8);
}
static void wr16(const TgRegs* r, uint16_t a, uint16_t v) { tg_bus_write16(at(r, a), v); }

static uint16_t twice(uint16_t v, bool* c) {
  uint16_t hi = v >> 15;
  *c = (v >> 14) & 1;
  return (uint16_t) ((v << 2) | hi);
}

void Tg3TitleSprites(TgRegs* r) {
  for(uint16_t i = 0; i < 0x20; i += 2) wr16(r, (uint16_t) (0x0B00 + i), 0);
  zs(r, 0x28, 0);
  zs(r, 0x2A, 0);
  zs(r, 0x2C, 1);
  zs(r, 0x2E, 2);
  for(uint16_t x = 0; x < 0x100; x += 2) {
    if(rd16(r, (uint16_t) (0x0500 + x)) == 0) continue;
    zs(r, 0x32, rd16(r, (uint16_t) (0x0600 + x)) & 0x1FF);
    uint16_t a = rd16(r, (uint16_t) (0x0700 + x));
    if(a & 0x8000) continue;
    if(!((uint16_t) (a - 0xF0) & 0x8000)) continue;
    zs(r, 0x26, a);
    uint16_t y = z(r, 0x28);
    if(z(r, 0x32) & 0xFF00) wr16(r, (uint16_t) (0x0B00 + y), rd16(r, (uint16_t) (0x0B00 + y)) | z(r, 0x2C));
    if(!(rd16(r, (uint16_t) (0x0500 + x)) & 0x8000))
      wr16(r, (uint16_t) (0x0B00 + y), rd16(r, (uint16_t) (0x0B00 + y)) | z(r, 0x2E));
    y = z(r, 0x2A);
    wr16(r, (uint16_t) (0x0902 + y), rd16(r, (uint16_t) (0x0800 + x)));
    tg_bus_write8(at(r, (uint16_t) (0x0900 + y)), (uint8_t) z(r, 0x32));
    tg_bus_write8(at(r, (uint16_t) (0x0901 + y)), (uint8_t) z(r, 0x26));
    zs(r, 0x2A, (uint16_t) (y + 4));
    bool c;
    zs(r, 0x2E, twice(z(r, 0x2E), &c));
    zs(r, 0x2C, twice(z(r, 0x2C), &c));
    if(c) {
      zs(r, 0x2C, 1);
      zs(r, 0x2E, 2);
      zs(r, 0x28, (uint16_t) (z(r, 0x28) + 2));
    }
  }
  r->x = 0x100;
  uint16_t used = z(r, 0x2A), last = z(r, 0x30);
  uint16_t diff = (uint16_t) (used - last);
  r->a = used;
  r->c = used >= last;
  if(diff != 0 && (diff & 0x8000)) {
    for(uint16_t y = used; y != last; y = (uint16_t) (y + 4)) wr16(r, (uint16_t) (0x0900 + y), 0xE000);
    r->a = 0xE000;
    r->c = true;
  }
  zs(r, 0x30, used);
  r->y = used;
  r->n = (used & 0x8000) != 0;
  r->z = used == 0;
}

// $0A:A47E: empties the object list.
void Tg3TitleClearObjects(TgRegs* r) {
  zs(r, 0x8C, 0);
  for(int x = 0xF6; x >= 0; x -= 2) wr16(r, (uint16_t) (0x0500 + x), 0);
  r->n = (r->x & 0x8000) != 0;
  r->z = r->x == 0;
}

// $0A:A216: adds the cursor X (two objects, the second mirrored and lowered by twice
// $4C) and moves it 8 pixels right.
void Tg3TitleCursor(TgRegs* r) {
  uint16_t x = r->x;
  for(int i = 0; i < 2; i++) {
    uint16_t y = z(r, 0x8C);
    zs(r, 0x8C, (uint16_t) (y + 2));
    r->y = y;
    wr16(r, (uint16_t) (0x0500 + y), 0xFFFF);
    wr16(r, (uint16_t) (0x0600 + y), rd16(r, (uint16_t) (0x0D60 + x)));
    uint16_t v = rd16(r, (uint16_t) (0x0D68 + x));
    if(i) v = (uint16_t) (v + z(r, 0x4C) + z(r, 0x4C));
    wr16(r, (uint16_t) (0x0700 + y), v);
    wr16(r, (uint16_t) (0x0800 + y), i ? 0xB002 : 0x3002);
  }
  uint16_t a = rd16(r, (uint16_t) (0x0D60 + x));
  uint32_t s = (uint32_t) a + 8;
  uint16_t res = (uint16_t) s;
  wr16(r, (uint16_t) (0x0D60 + x), res);
  r->a = res;
  r->c = s > 0xFFFF;
  r->v = (~a & res & 0x8000) != 0;
  r->n = (res & 0x8000) != 0;
  r->z = res == 0;
}
