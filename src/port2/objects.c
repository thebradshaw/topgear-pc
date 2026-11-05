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

// The cars, in track order ($C3E8 holds 20 offsets into the car records at $C410).
static uint16_t car_at(uint16_t i) { return w((uint16_t) (0xC3E8 + i)); }
static uint32_t car_pos(uint16_t c) { return (uint32_t) w((uint16_t) (0xC418 + c)) | (uint32_t) w((uint16_t) (0xC41A + c)) << 16; }

// Appends car number i (in track order) to the visible-car list ($C340, 8 bytes each): sprite set, two words, and
// its distance ahead ($F6-$F8, in 1/1024ths) for the depth sort. The camera's own car
// gets $FFFF.
static void add_visible_car(uint16_t i) {
  uint16_t c = car_at(i), x = z(0x46);
  zs(0x44, i);
  if(w((uint16_t) (0xC410 + c)) == z(0x42)) {
    ws((uint16_t) (0xC340 + x), 0xFFFF);
    ws((uint16_t) (0xC342 + x), 0);
    ws((uint16_t) (0xC344 + x), 0);
  } else {
    ws((uint16_t) (0xC340 + x), w((uint16_t) (0xC412 + c)));
    ws((uint16_t) (0xC342 + x), w((uint16_t) (0xC414 + c)));
    ws((uint16_t) (0xC344 + x), w((uint16_t) (0xC416 + c)));
  }
  ws((uint16_t) (0xC346 + x), (uint16_t) ((z(0xF7) >> 2) & 0x3FFE));
  zs(0x46, (uint16_t) (x + 8));
}

// The distance from the window start $EE:$F0 into $F6 (and $F8 when ahead). True when
// the car is at or past the start.
static bool car_distance(uint16_t c, bool always) {
  uint32_t p = car_pos(c), s = (uint32_t) z(0xEE) | (uint32_t) z(0xF0) << 16;
  zs(0xF6, (uint16_t) (p - s));
  if(always || p >= s) zs(0xF8, (uint16_t) ((p - s) >> 16));
  return p >= s;
}

// $0B:A49B. Lists the cars between the camera and $44 sections ahead, nearest first,
// with the lap's wrap-around ($010A sections a lap).
void Tg2ListVisibleCars(TgRegs* r) {
  (void) r;
  uint16_t lap = w(0x010A), hi;
  zs(0x46, 0);
  if(!(z(0xCE) & 0x8000)) {
    uint16_t c = car_at(z(0x40));
    zs(0x42, w((uint16_t) (0xC410 + c)));
    uint32_t p = car_pos(c) - 0x2E00;
    zs(0xEE, (uint16_t) p);
    zs(0xF2, (uint16_t) p);
    hi = (uint16_t) (p >> 16);
    if(car_pos(c) < 0x2E00) hi = (uint16_t) (hi + lap);
  } else {
    zs(0x42, 0xFFFF);
    zs(0xEE, z(0xD0));
    zs(0xF2, z(0xD0));
    hi = (uint16_t) (z(0xD2) + 4);
    if(hi >= lap) hi = (uint16_t) (hi - lap);
  }
  zs(0xF0, hi);
  uint16_t end = (uint16_t) (hi + 0x44);
  if(end < lap) {
    zs(0xF4, end);
    for(uint16_t i = 0; i != 0x28; i += 2) {
      uint16_t c = car_at(i);
      if(car_pos(c) >= ((uint32_t) z(0xF2) | (uint32_t) z(0xF4) << 16)) break;
      if(car_distance(c, false)) add_visible_car(i);
    }
  } else {
    // The window wraps past the finish line.
    zs(0xF4, (uint16_t) (end - lap));
    int i = 0x26;
    for(; i >= 0; i -= 2) {
      if(car_pos(car_at((uint16_t) i)) < ((uint32_t) z(0xEE) | (uint32_t) z(0xF0) << 16)) break;
    }
    if(i < 0) {  // every car is ahead
      for(uint16_t j = 0; j != 0x28; j += 2) {
        car_distance(car_at(j), true);
        add_visible_car(j);
      }
    } else {
      for(uint16_t j = (uint16_t) (i + 2); j != 0x28; j += 2) {
        car_distance(car_at(j), true);
        add_visible_car(j);
      }
      zs(0xF0, (uint16_t) (z(0xF0) - lap));
      for(uint16_t j = 0; j != 0x28; j += 2) {
        uint16_t c = car_at(j);
        if(car_pos(c) >= ((uint32_t) z(0xF2) | (uint32_t) z(0xF4) << 16)) break;
        car_distance(c, true);
        add_visible_car(j);
      }
    }
  }
  uint16_t x = z(0x46);
  ws((uint16_t) (0xC340 + x), 0);
  ws((uint16_t) (0xC342 + x), 0);
  ws((uint16_t) (0xC344 + x), 0);
  ws((uint16_t) (0xC346 + x), 0x7FFE);
}

// Roadside object types (the $9F97 table): from the object record y in $6400 (flags,
// position, start frame), the frame to draw (A) and its x (X) and y (Y) offsets.

// Types 0-9: a still object; its frame is the type's animation word.
static void still_object(TgRegs* r, uint16_t anim) {
  uint16_t y = r->y;
  r->x = (uint16_t) ((w((uint16_t) (0x6401 + y)) & 0x007F) - 0x0040);
  r->y = 0;
  r->a = w(anim);
}

// Types 10-15: objects that move once triggered (high byte of the flags: the direction).
// They travel for $A0 frames along a bounce curve ($0B:A35B), then stop (types 10-11) or
// restart from an edge (12-15, sweeping across in direction dir between triggers).
static void moving_object(TgRegs* r, uint16_t anim, int sweep) {
  uint16_t y = r->y, rec = (uint16_t) (0x6400 + y);
  uint16_t dir = w(rec) & 0xFF00, now = w(0x001A);
  r->a = w(anim);
  r->y = 0;
  if(dir == 0) {
    if(sweep == 0) {
      r->x = w((uint16_t) (rec + 2));
      return;
    }
    uint16_t t = sweep > 0 ? (uint16_t) (w((uint16_t) (rec + 4)) - now) : (uint16_t) (now - w((uint16_t) (rec + 4)));
    uint16_t v = (uint16_t) (((t & 0x01FF) << 1) - 0x0200);
    ws((uint16_t) (rec + 2), v);
    r->x = v;
    return;
  }
  uint16_t t = (uint16_t) (now - w((uint16_t) (rec + 4)));
  if(!((uint16_t) (t - 0x00A0) & 0x8000)) {  // finished
    if(sweep == 0) {
      ws(rec, w(rec) | 0x0080);
      r->a = r->x = 0;
      return;
    }
    ws(rec, w(rec) & 0x00FF);
    ws((uint16_t) (rec + 4), now);
    uint16_t v = sweep > 0 ? 0x01FE : 0xFE00;
    ws((uint16_t) (rec + 2), v);
    r->x = v;
    return;
  }
  zs(0xFE, t);
  uint16_t pos = (dir & 0x8000) ? (uint16_t) (w((uint16_t) (rec + 2)) - t) : (uint16_t) (w((uint16_t) (rec + 2)) + t);
  zs(0xFE, pos);
  r->y = rom16(0x8BA35B + (uint16_t) (t << 1));
  r->x = pos;
}

void Tg2ObjectType0(TgRegs* r) { still_object(r, 0x03D2); }
void Tg2ObjectType1(TgRegs* r) { still_object(r, 0x03D4); }
void Tg2ObjectType2(TgRegs* r) { still_object(r, 0x03D0); }
void Tg2ObjectType3(TgRegs* r) { still_object(r, 0x03CE); }
void Tg2ObjectType4(TgRegs* r) { still_object(r, 0x03D2); }
void Tg2ObjectType5(TgRegs* r) { still_object(r, 0x03D4); }
void Tg2ObjectType6(TgRegs* r) { still_object(r, 0x03D0); }
void Tg2ObjectType7(TgRegs* r) { still_object(r, 0x03CC); }
void Tg2ObjectType8(TgRegs* r) { still_object(r, 0x03C4); }
void Tg2ObjectType9(TgRegs* r) { still_object(r, 0x03C2); }
void Tg2ObjectType10(TgRegs* r) { moving_object(r, 0x03CA, 0); }
void Tg2ObjectType11(TgRegs* r) { moving_object(r, 0x03C8, 0); }
void Tg2ObjectType12(TgRegs* r) { moving_object(r, 0x03C6, 1); }
void Tg2ObjectType13(TgRegs* r) { moving_object(r, 0x03C6, -1); }
void Tg2ObjectType14(TgRegs* r) { moving_object(r, 0x03C8, 1); }
void Tg2ObjectType15(TgRegs* r) { moving_object(r, 0x03C8, -1); }
