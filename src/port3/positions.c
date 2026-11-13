// Top Gear 3000: race positions (bank $02).
//
// $02:AD2C counts, for each player, how many of the 20 cars ($7E:FAB4 + $35 each) are at
// or past them: the lap ($FAD3) and position along the track ($FAB5/$FAB7) compared as one
// number. When a player's count (their place) changes, the announcer is asked for the
// place's sample ($25 + place), which plays unless a more important one is already queued.

#include "../port/port.h"

static uint16_t w(uint16_t a) { return ram16(a); }
static void ws(uint16_t a, uint16_t v) { ram16_set(a, v); }

// Whether car x is at or ahead of the car whose record is at p: SBC through the three
// words, then the sign.
static bool ahead(uint16_t x, uint16_t p) {
  uint32_t lo = (uint32_t) w((uint16_t) (0xFAB5 + x)) - w((uint16_t) (0xFAB5 + p));
  int borrow = lo > 0xFFFF;
  uint32_t mid = (uint32_t) w((uint16_t) (0xFAB7 + x)) - w((uint16_t) (0xFAB7 + p)) - borrow;
  borrow = mid > 0xFFFF;
  uint16_t hi = (uint16_t) (w((uint16_t) (0xFAD3 + x)) - w((uint16_t) (0xFAD3 + p)) - borrow);
  return !(hi & 0x8000);
}

// $02:9BC9 / $02:9BE5: queue sample `id` in `slot` unless the queued one has a higher
// priority (the table at $83:A0A4).
static void announce(TgRegs* r, uint16_t slot, uint16_t id) {
  uint16_t cur = tg_read16(0x83A0A4 + (uint16_t) (w(slot) << 1));
  uint16_t want = tg_read16(0x83A0A4 + (uint16_t) (id << 1));
  r->x = (uint16_t) (id << 1);
  r->c = cur >= want;
  if(cur <= want) ws(slot, id);
  r->a = id;
}

void Tg3RacePlaces(TgRegs* r) {
  uint16_t first = 0, second = 0;
  ws(0x0142, 0);
  ws(0x0144, 0);
  for(uint16_t x = 0; x != 0x14 * 0x35; x = (uint16_t) (x + 0x35)) {
    if(ahead(x, 0)) ws(0x0142, ++first);
    if(ahead(x, 0x35)) ws(0x0144, ++second);
  }
  r->x = 0x14 * 0x35;
  r->y = 0;
  r->v = false;
  r->c = true;
  if(w(0x0142) != w(0x1ED2)) {
    ws(0x1ED2, w(0x0142));
    announce(r, 0x0233, (uint16_t) (w(0x0142) + 0x25));
  }
  r->a = w(0x0144);
  if(w(0x0144) != w(0x1ED4)) {
    ws(0x1ED4, w(0x0144));
    announce(r, 0x02E6, (uint16_t) (w(0x0144) + 0x25));
  } else {
    r->c = true;
  }
  r->db = 0x83;
  r->n = true;  // PLB of $83
  r->z = false;
}
