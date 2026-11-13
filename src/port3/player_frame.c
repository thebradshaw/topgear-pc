// Top Gear 3000: one frame of the player's car (bank $03).
//
// $03:E241 counts the frame for the current step length ($7E:4CDD by $1E9E), then runs
// the player's setup (03:E2D7) and, with the direct page on the player's record at $0200,
// its engine (03:EAC6) and its place on the track (03:F7F9).

#include <stddef.h>

#include "../port/port.h"

static void call(TgRegs* r, uint16_t target, uint16_t site) {
  const TgPortFunc* fn = tg_port_find(0x030000u | target);
  if(fn != NULL && fn->call == TG_CALL_JSR) {
    fn->fn(r);
    if(!tg_port_take_declined()) return;
  }
  tg_call_jsr(r, target, (uint16_t) (site + 3), TG_MX_16);
}

void Tg3PlayerFrame(TgRegs* r) {
  uint16_t x = (uint16_t) (ram16(0x1E9E) << 1);
  r->x = x;
  uint32_t a = (0x4CDDu + x) & 0x1FFFF;
  r->a = (uint16_t) (ram16(a) + 1);
  ram16_set(a, r->a);
  call(r, 0xE2D7, 0xE24F);
  uint16_t d = r->d;
  tg_push16(d);
  r->a = 0x0200;
  r->d = 0x0200;
  call(r, 0xEAC6, 0xE257);
  call(r, 0xF7F9, 0xE25A);
  r->d = tg_pull16();
  r->n = (r->d & 0x8000) != 0;
  r->z = r->d == 0;
}
