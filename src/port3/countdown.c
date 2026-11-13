// Top Gear 3000: the start lights' countdown (bank $02).
//
// $02:AB9D runs each frame of a race. $1ECE counts down the start ($D2 frames before the
// first light); each light that comes on ($D2, $8C, $46 and finally $02, the go) sets the
// light's picture in the four players' blocks ($0233/$02E6/$0295/$0348) and asks for its
// sound (02:AD12). At the go the cars are let off ($0008 in each car's flags) and $1ECE is
// left at 1. Negative $1ECE (the race not started yet) shows the waiting picture.

#include <stddef.h>

#include "../port/port.h"

static uint16_t w(uint16_t a) { return ram16(a); }
static void ws(uint16_t a, uint16_t v) { ram16_set(a, v); }
static bool neg(uint16_t v) { return (v & 0x8000) != 0; }

void Tg3Countdown(TgRegs* r) {
  // Absolute addresses below are low RAM, as in banks $00-$3F, $80-$BF and $7E.
  if((r->db & 0x40) && r->db != 0x7E) {
    tg_port_decline();
    return;
  }
  uint16_t t = w(0x1ECE);
  if(neg(t)) {
    if(!neg(w(0x047C))) {
      ws(0x0233, 0x3D);
      ws(0x02E6, 0x3E);
      if((w(0x03DC) & 0xF0) != 0x30) ws(0x02E6, 0x45);
    }
    return;
  }
  if(t == 0) return;
  uint16_t x = t, y = (uint16_t) (t - w(0x1E9E));
  if(neg(y)) y = 0;
  ws(0x1ECE, y);
  uint16_t s = 0x1E, a = 0;
  // Each light comes on as the count passes its mark (signed compares, as CPX/BMI).
  if(!neg((uint16_t) (x - 0xD2)) && neg((uint16_t) (y - 0xD2))) a = 2;
  if(!neg((uint16_t) (x - 0x8C)) && neg((uint16_t) (y - 0x8C))) a = 3;
  if(!neg((uint16_t) (x - 0x46)) && neg((uint16_t) (y - 0x46))) a = 4;
  if(!neg((uint16_t) (x - 0x02)) && neg((uint16_t) (y - 0x02))) {
    s = 0x22;
    ws(0x1ECE, 1);
    ws(0x0244, w(0x0244) | 8);
    ws(0x02F7, w(0x02F7) | 8);
    ws(0x02A6, w(0x02A6) | 8);
    ws(0x0359, w(0x0359) | 8);
    a = 5;
  }
  ram16_set((uint16_t) (r->d + 0x14), s);
  ws(0x0233, a);
  ws(0x02E6, a);
  ws(0x0295, a);
  ws(0x0348, a);
  if(a == 0) return;
  r->x = (uint16_t) (((s >> 8) & 0xFF) | 0x2000);
  r->a = (uint16_t) (s | 0xFF00);
  r->y = y;
  const TgPortFunc* fn = tg_port_find(0x02AD12);
  if(fn != NULL && fn->call == TG_CALL_JSR) {
    fn->fn(r);
    if(!tg_port_take_declined()) return;
  }
  tg_call_jsr(r, 0xAD12, 0xAC51, TG_MX_16);
}
