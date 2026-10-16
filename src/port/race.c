// Race engine routines (bank $00).

#include "port.h"
#include "variables.h"

// $00:88A1. Engine revs on the start line: while a player holds accelerate, RPM rises by
// $80 per elapsed frame up to $1000, and it always falls by $20 per frame, not below 0.
// Entered with 16-bit A and index; leaves A = player 2 RPM, X = 0, and the flags of the
// last decay step.
void RaceRevEngines(TgRegs* r) {
  static const uint16_t rpm[2] = { RaceP1Rpm, RaceP2Rpm };
  static const uint16_t button[2] = { RaceP1AccelButton, RaceP2AccelButton };
  static const uint16_t joy[2] = { RaceJoy1Held, RaceJoy2Held };
  uint16_t frames = ram16(RaceFramesElapsed);
  uint16_t a = 0, x = 0;
  for(int p = 0; p < 2; p++) {
    if(ram16(button[p]) & ram16((uint16_t) (r->d + joy[p]))) {
      a = ram16(rpm[p]);
      x = frames;
      do a += 0x80; while(--x);  // X = 0 loops 65536 times, as on the SNES
      if(a >= 0x1000) a = 0x1000;
      ram16_set(rpm[p], a);
    }
    a = ram16(rpm[p]);
    x = frames;
    do {
      r->c = true;
      a = sbc16(r, a, 0x20);
      if(!r->c) {
        a = 0;
        x = 1;
      }
    } while(--x);
    ram16_set(rpm[p], a);
  }
  r->a = a;
  r->x = x;
  r->n = false;  // from the final DEX
  r->z = true;
}
