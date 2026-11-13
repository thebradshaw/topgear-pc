// Top Gear 3000: player 1's car for this frame, before the engine runs (bank $03).
//
// $03:E2D7 loads the car's settings from the parts fitted ($0393-$0397: engine, tyres,
// nitro, armour, ...) and the gear ($021F, through 03:EA9B), then turns the pad ($000C held,
// $000E just pressed) into the engine's flags in $1E9C: bit 0 accelerate, 1 brake,
// 2/3 steer, and the item or weapon buttons by the item picked ($014C). The item buttons
// also step the item choice ($4C) left or right past empty slots. A car the game drives
// itself (the demo, or after the finish) goes to 03:E8AB instead.

#include <stddef.h>

#include "../port/port.h"

static uint16_t w(uint16_t a) { return ram16(a); }
static void ws(uint16_t a, uint16_t v) { ram16_set(a, v); }
static void tsb(uint16_t a, uint16_t v) { ws(a, w(a) | v); }

static void call(TgRegs* r, uint16_t target, uint16_t site) {
  const TgPortFunc* fn = tg_port_find(0x030000 | target);
  if(fn != NULL && fn->call == TG_CALL_JSR) {
    fn->fn(r);
    if(!tg_port_take_declined()) return;
  }
  tg_call_jsr(r, target, (uint16_t) (site + 3), TG_MX_16);
}

void Tg3PlayerSetup(TgRegs* r) {
  // Absolute addresses below are low RAM, as in banks $00-$3F, $80-$BF and $7E.
  if((r->db & 0x40) && r->db != 0x7E) {
    tg_port_decline();
    return;
  }
  ws(0x1EBE, 0x8000);
  uint16_t a = w(0x0395) & 0xFF;
  ws(0x1EB2, a);
  r->x = (uint16_t) ((w(0x021F) << 1) + tg_read16(0x86DAD9u + (uint16_t) ((a & 0x7F) << 1)));
  r->a = r->x;
  call(r, 0xEA9B, 0xE2F5);
  ws(0x022B, r->a);
  uint16_t x = (uint16_t) ((w(0x0393) & 0xFF) << 1);
  ws(0x1EA8, tg_read16(0x86DCE1u + x));
  ws(0x1EC0, tg_read16(0x86DCEDu + x));
  x = (uint16_t) ((w(0x0397) & 0xFF) << 1);
  ws(0x1EB4, tg_read16(0x86DD29u + x));
  ws(0x1EB6, tg_read16(0x86DD35u + x));
  x = (uint16_t) ((w(0x0394) & 0xFF) << 1);
  ws(0x1EB8, tg_read16(0x86DD41u + x));
  x = (uint16_t) ((w(0x0396) & 0xFF) << 1);
  ws(0x1F30, tg_read16(0x86DD4Du + x));
  ws(0x1ECA, 0);
  ws(0x1EBA, 0);
  ws(0x1ECC, 0);
  ws(0x1F2C, w(0x1E66));
  ws(0x1EBC, (uint16_t) ((w(0x0226) << 1) & 0x3E));
  ws(0x1E9C, 0);

  if((w(0x0244) & 4) || !(tg_read16(0x8103DC) & 0x10)) {
    // Driven by the game.
    uint16_t d = r->d;
    r->d = 0x0200;
    call(r, 0xE8AB, 0xE37E);
    r->d = d;
    return;
  }
  uint16_t held = w(0x000C);
  if(held & w(0x039D)) tsb(0x1E9C, 1);
  x = 0x0058;
  if(held & w(0x039F)) {
    tsb(0x1E9C, 2);
    x = 0x109F;
  }
  ram16_set(0x2142, x);  // $7E:2142
  if(held & 0x0200) tsb(0x1E9C, 4);
  if(held & 0x0100) tsb(0x1E9C, 8);
  uint16_t t = w(0x03A1), e = w(0x000E);
  ws(0x000E, e & ~t);
  if(e & t) tsb(0x1E9C, tg_read16(0x86DD59u + (uint16_t) (w(0x014C) << 1)));
  if(w(0x03A1) & w(0x000C)) tsb(0x1E9C, tg_read16(0x86DD63u + (uint16_t) (w(0x014C) << 1)));

  // The item choice, left and right past empty slots.
  uint16_t dp4c = (uint16_t) (r->d + 0x4C);
  t = w(0x03A5);
  e = w(0x000E);
  ws(0x000E, e & ~t);
  if(e & t) {
    x = w(dp4c);
    for(;;) {
      x--;
      if(x == 0) {
        ws(dp4c, x);
        break;
      }
      if(x & 0x8000) break;
      if(w((uint16_t) (0x0397 + x)) & 0xFF) {
        ws(dp4c, x);
        break;
      }
    }
  }
  t = w(0x03A3);
  e = w(0x000E);
  ws(0x000E, e & ~t);
  if(e & t) {
    x = w(dp4c);
    for(;;) {
      x++;
      if(x == 5) break;
      if(w((uint16_t) (0x0397 + x)) & 0xFF) {
        ws(dp4c, x);
        break;
      }
    }
  }
  x = w(dp4c);
  a = w((uint16_t) (0x024E + x)) & 0xFF;
  ws(0x025C, a);
  ws(0x1F2E, x);
  r->a = a;
  r->x = x;
}
