// Top Gear 3000: the 20 cars' turn each frame and the race order (bank $01).
//
// The race order is the list of record offsets at $1E6A-$1E90 (20 words, last = furthest
// along). Each car's record ($7E:FAB4 + offset) also links to its neighbours in the
// order: +$19 the car ahead, +$1B the car behind (wrapping round).
//
// $01:E812 links the list, runs each car's handler (its state at +$11 picks one from
// the table at $01:F876; the computer cars' is MoveObject) after saving its position, then
// sorts the order by position along the track (+$01..+$04), letting cars that come level
// sort out passing (01:F21E). A car past the end of the lap ($019A sections) goes back
// to the start with a lap more (+$1F, lap flag in +$2A bit 0); one behind the start the
// other way. Finally it records cars that have just finished (lap $1F06) in the finish
// list at $0446.

#include <stddef.h>

#include "../port/port.h"

#define REC 0xFAB4u

static uint16_t w(uint16_t a) { return ram16(a); }
static void ws(uint16_t a, uint16_t v) { ram16_set(a, v); }
static uint16_t s(int i) { return w((uint16_t) (0x1E6A + 2 * i)); }
static void ss(int i, uint16_t v) { ws((uint16_t) (0x1E6A + 2 * i), v); }
static uint16_t f(uint16_t x, uint16_t o) { return w((uint16_t) (REC + o + x)); }
static void fs(uint16_t x, uint16_t o, uint16_t v) { ws((uint16_t) (REC + o + x), v); }

// Both links, round the whole list.
static void link_all(void) {
  fs(s(19), 0x1B, s(18));
  fs(s(19), 0x19, s(0));
  for(int i = 18; i >= 1; i--) {
    fs(s(i), 0x1B, s(i - 1));
    fs(s(i), 0x19, s(i + 1));
  }
  fs(s(0), 0x1B, s(19));
  fs(s(0), 0x19, s(1));
}

// $01:F1FC: just the links to the car ahead.
static void link_ahead(void) {
  for(int i = 0; i < 19; i++) fs(s(i), 0x19, s(i + 1));
  fs(s(19), 0x19, s(0));
}

// A car's handler, in C when there is one.
static void handler(TgRegs* r, uint16_t y, uint16_t site) {
  uint16_t x = f(y, 0x11);
  r->a = f(y, 0x03);
  r->x = x;
  r->y = y;
  flags_nz16(r, x);
  uint16_t target = tg_read16(0x81F876 + x);
  const TgPortFunc* fn = tg_port_find(0x010000 | target);
  if(fn != NULL && fn->call == TG_CALL_JSR) {
    fn->fn(r);
    if(!tg_port_take_declined()) return;
  }
  tg_call_jsr(r, target, (uint16_t) (site + 3), TG_MX_16);
}

// Sorts the order (from $01:EAA2). The pair at level k is the cars at k-1 and k.
static void sort(TgRegs* r) {
  ram16_set((uint16_t) (r->d + 0x3C), 0);
  int k = 19;
  uint16_t x = s(18);
  for(;;) {
    uint16_t y = s(k);
    // LDA +$01,Y; CMP +$01,X; LDA +$03,Y; SBC +$03,X
    bool borrow = f(y, 0x01) < f(x, 0x01);
    uint32_t d = (uint32_t) f(y, 0x03) - f(x, 0x03) - borrow;
    uint16_t hi = (uint16_t) d;
    bool carry = d <= 0xFFFF;
    if(hi == 0) {
      // Level: passing is sorted out there.
      r->x = x;
      r->y = y;
      r->a = 0;
      r->c = carry;
      r->n = false;
      r->z = true;
      tg_call_jsr(r, 0xF21E, (uint16_t) (0xEACC + (19 - k) * 0x2B + 3), TG_MX_16);
    } else {
      ram16_set((uint16_t) (r->d + 0x3C), 0);
      if((hi & 0x8000) && !carry) {
        // The later car is behind: swap them, and check the swapped one further up.
        ss(k - 1, y);
        ss(k, x);
        link_ahead();
        if(k < 19) {
          k++;
          continue;
        }
      }
    }
    if(k == 1) return;
    k--;
    x = s(k - 1);
  }
}

void Tg3CarsTurn(TgRegs* r) {
  link_all();
  // Each car's turn.
  for(int c = 0; c < 20; c++) {
    uint16_t y = (uint16_t) (c * 0x35);
    fs(y, 0x21, f(y, 0x07));
    fs(y, 0x0D, f(y, 0x01));
    fs(y, 0x0F, f(y, 0x03));
    fs(y, 0x13, 0);
    r->d = 0x0100;
    r->db = 0x7E;
    handler(r, y, (uint16_t) (0xE865 + c * 0x1E));
  }
  r->d = 0x0100;
  r->db = 0x7E;
  for(;;) {
    sort(r);
    // Cars over the end of the lap, from the front: a lap more each, and round the order.
    uint16_t lap = w(0x019A), n = 0;
    r->c = true;
    for(int k = 19; k >= 0; k--) {
      uint16_t x = s(k);
      uint16_t a = sbc16(r, f(x, 0x02), lap);
      if(!r->c) break;
      fs(x, 0x02, a);
      n++;
      if(k == 6) k = 3;  // the original jumps on to the third car here, skipping three
    }
    if(n > 0) {
      for(; n != 0; n--) {
        uint16_t x = s(19);
        for(int i = 19; i >= 1; i--) ss(i, s(i - 1));
        ss(0, x);
        fs(x, 0x1F, (uint16_t) (f(x, 0x1F) + 1));
        fs(x, 0x2A, f(x, 0x2A) | 1);
        fs(x, 0x2C, w(0x019C));
      }
      continue;
    }
    // Cars before the start, from the back: a lap less.
    for(int k = 0; k < 20; k++) {
      uint16_t x = s(k);
      if(!(f(x, 0x02) & 0x8000)) break;
      r->c = false;
      fs(x, 0x02, adc16(r, f(x, 0x02), lap));
      n++;
    }
    if(n == 0) break;
    for(; n != 0; n--) {
      uint16_t x = s(0);
      for(int i = 0; i < 19; i++) ss(i, s(i + 1));
      ss(19, x);
      fs(x, 0x1F, (uint16_t) (f(x, 0x1F) - 1));
      fs(x, 0x2A, f(x, 0x2A) & 0xFFFE);
    }
  }
  // Cars that have just finished go on the finish list.
  for(int i = 0; i < 20; i++) {
    uint16_t x = s(i);
    uint16_t fl = f(x, 0x2A);
    if((fl & 1) && !(fl & 2) && f(x, 0x1F) == w(0x1F06)) {
      g_ram[(uint16_t) (REC + 0x2A + x)] = (uint8_t) (fl | 4);
      uint16_t at = w(0x1F08);
      g_ram[(uint16_t) (0x0446 + at)] = g_ram[(uint16_t) (REC + 0x29 + x)];
      ws(0x1F08, (uint16_t) (at + 1));
    }
    fs(x, 0x2A, f(x, 0x2A) & 0xFF7E);
  }
  link_all();
  r->a = s(1);
  r->x = s(0);
  r->y = 0;
  r->c = true;
  r->db = 0x83;
  r->n = true;  // PLB of $83
  r->z = false;
}
