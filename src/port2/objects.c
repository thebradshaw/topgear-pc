// Top Gear 2: drawing the cars and roadside objects (bank $0B). One view per player: the
// view setup fills the direct page at $0400 and runs the object pass ($0B:8578).

#include "../port/port.h"

#define DPB 0x0400u

static uint16_t z(uint8_t o) { return ram16(DPB + o); }
static void zs(uint8_t o, uint16_t v) { ram16_set(DPB + o, v); }
// Bank $7E words.
static uint16_t w(uint16_t adr) { return ram16(adr); }
static void ws(uint16_t adr, uint16_t v) { ram16_set(adr, v); }

// The three player-car slots ($DC, $E2, $E8): the car's sprite set, a tag, and its palette
// bits for OAM ((pal & 7) << 9).
static void car_slot(uint8_t o, uint16_t car, uint16_t tag) {
  uint16_t y = w((uint16_t) (0xC3E8 + car));
  zs(o, w((uint16_t) (0xC41A + y)));
  zs((uint8_t) (o + 2), tag);
  zs((uint8_t) (o + 4), (uint16_t) ((w((uint16_t) (0xC412 + y)) & 7) << 9));
}
static void car_slot_none(uint8_t o) {
  zs(o, 0);
  zs((uint8_t) (o + 2), 0);
  zs((uint8_t) (o + 4), 0);
}

// The buffers this frame draws into ($0124's sign picks one of two).
static void select_buffers(uint16_t oam_lo, uint16_t oam_hi) {
  if(!(w(0x0124) & 0x8000)) {
    ws(0xAD7E, 0);
    ws(0xB67E, 0);
    zs(0x9A, 0xAD7F);
    zs(0x9C, 0xB200);
    zs(0xC4, 0xB67F);
    zs(0xC6, 0xBB00);
    zs(0x2C, oam_lo);
  } else {
    ws(0xAE7E, 0);
    ws(0xB77E, 0);
    zs(0x9A, 0xAE7F);
    zs(0x9C, 0xB580);
    zs(0xC4, 0xB77F);
    zs(0xC6, 0xBE80);
    zs(0x2C, oam_hi);
  }
}

// Player p's camera and track state for the view.
static void view_setup(int p, uint16_t d8, uint16_t da) {
  uint16_t pd = p ? 0x003C : 0;  // player 2's block at $0200 is $3C on
  zs(0xCC, w((uint16_t) (0x015E + 2 * p)));
  zs(0x50, w((uint16_t) (0x013C + 2 * p)));
  ws(0x0440, w((uint16_t) (0x0136 + 2 * p)));
  ws(0x045C, w((uint16_t) (0x0200 + 2 * p)));
  zs(0x48, (uint16_t) (w((uint16_t) (0x0226 + pd)) + (p ? 0x9400 : 0x9000)));
  zs(0x4A, w((uint16_t) (0x020E + pd)) & 0x0003);
  zs(0x54, (uint16_t) ((w((uint16_t) (0x020D + pd)) & 0x00F8) >> 2));
  zs(0x4C, w((uint16_t) (0x027C + 2 * p)));
  zs(0x4E, p ? 0xBFA0 : 0xBE80);
  zs(0xD8, d8);
  zs(0xDA, da);
}

// The rest of player p's view state, then the object pass with the clip window
// (top $3A, bottom $38, left $34, right $36) and the screen's bottom line $2E.
static void view_run(TgRegs* r, int p, uint16_t bottom, uint16_t height, uint16_t ret) {
  uint16_t pd = p ? 0x003C : 0;
  zs(0xD4, (uint16_t) (0 - w((uint16_t) (0x0209 + pd))));
  zs(0xCE, w((uint16_t) (0x0504 + 8 * p)));
  zs(0xD0, w((uint16_t) (0x020C + pd)));
  zs(0xD2, w((uint16_t) (0x020E + pd)));
  uint16_t a = w((uint16_t) (0x017C + 2 * p));
  if(!(a & 0x8000)) a = w((uint16_t) (0x0184 + 2 * p)) == 6 ? w((uint16_t) (0x018C + 2 * p)) : 0;
  uint16_t blk = p ? 0x039A : 0x0372;
  ws(blk, a);
  ws((uint16_t) (blk + 6), w((uint16_t) (0x0178 + 2 * p)));
  zs(0xD6, blk);
  ws(p ? 0x1EFC : 0x1DF4, w((uint16_t) (0x036E + 2 * p)));
  zs(0x10, p ? 0x0108 : 0);
  zs(0x2E, bottom);
  zs(0x32, height);
  zs(0x34, 0);
  zs(0x36, 0x0100);
  zs(0x38, height);
  zs(0x3A, 0);
  uint16_t slots = (uint16_t) (0x0400 + 4 * p);
  zs(0x2A, w(slots));
  r->d = DPB;
  r->db = 0x7E;
  tg_call_jsl(r, 0x0B8578, ret, TG_MX_16);
  ws(p ? 0x015C : 0x015A, z(0x52));
  uint16_t used = z(0x28), prev = w((uint16_t) (slots + 2));
  ws((uint16_t) (slots + 2), used);
  ws(slots, prev);
  ws(p ? 0x011E : 0x011A, z(0xC8));
}

// Ends a view's HDMA lists: two $FF00 words, then count n with the list, and the window
// entry (n, v).
static void close_lists(uint16_t n, uint16_t v) {
  uint16_t x = (uint16_t) (z(0x9C) - 2);
  ws(x, 0xFF00);
  x = (uint16_t) (x - 2);
  ws(x, 0xFF00);
  zs(0x9C, x);
  x = (uint16_t) (z(0x9A) - 3);
  ws(x, n);
  ws((uint16_t) (x + 1), z(0x9C));
  zs(0x9A, x);
  x = (uint16_t) (z(0xC4) - 3);
  ws(x, n);
  ws((uint16_t) (x + 1), v);
  zs(0xC4, x);
}

// $0B:8000. One player: the full-screen view.
void Tg2DrawObjects1P(TgRegs* r) {
  uint16_t d = r->d;
  uint8_t db = r->db;
  select_buffers(0x0BF0, 0x0FF0);
  zs(0xCA, 0x0140);
  zs(0x30, 0x00E0);
  view_setup(0, 0x00A8, 0x00BF);
  uint16_t me = w(0x0136), other = w(0x013A);
  if(me == other) {
    car_slot(0xDC, me, 0x84);
    car_slot_none(0xE2);
  } else {
    car_slot(0xDC, me, 0x85);
    car_slot(0xE2, other, 0x84);
  }
  car_slot_none(0xE8);
  view_run(r, 0, 0x00DF, 0x00E0, 0x8172);
  r->d = d;
  r->db = db;
}

// $0B:818A. Two players: player 2's view (bottom half) first, then player 1's (top).
void Tg2DrawObjects2P(TgRegs* r) {
  uint16_t d = r->d;
  uint8_t db = r->db;
  select_buffers(0x13F0, 0x17F0);
  zs(0xCA, 0x0144);
  uint16_t x = (uint16_t) (z(0xC4) - 3);
  ws(x, 0x0004);
  ws((uint16_t) (x + 1), 0x9FB0);
  zs(0xC4, x);
  zs(0x30, 0x0068);
  view_setup(1, 0x00C0, 0x0057);
  uint16_t p1 = w(0x0136), p2 = w(0x0138), other = w(0x013A);
  if(p2 == other) {
    car_slot(0xDC, p2, 0x84);
    car_slot(0xE2, p1, 0x85);
    car_slot_none(0xE8);
  } else {
    car_slot(0xDC, p2, 0x86);
    if(other == p1) {
      car_slot(0xE2, other, 0x84);
      car_slot_none(0xE8);
    } else {
      car_slot(0xE2, other, 0x84);
      car_slot(0xE8, p1, 0x85);
    }
  }
  view_run(r, 1, 0x00DB, 0x0068, 0x8361);
  close_lists(0x0008, 0x9DF0);

  zs(0x2C, (w(0x0124) & 0x8000) ? 0x0FF0 : 0x0BF0);
  zs(0xCA, 0x0140);
  view_setup(0, 0x00C0, 0x0057);
  if(p1 == other) {
    car_slot(0xDC, p1, 0x84);
    car_slot(0xE2, p2, 0x86);
    car_slot_none(0xE8);
  } else {
    car_slot(0xDC, p1, 0x85);
    if(other == p2) {
      car_slot(0xE2, other, 0x84);
      car_slot_none(0xE8);
    } else {
      car_slot(0xE2, other, 0x84);
      car_slot(0xE8, p2, 0x86);
    }
  }
  view_run(r, 0, 0x006B, 0x0068, 0x852A);
  close_lists(0x0004, 0x9C40);
  r->d = d;
  r->db = db;
}
