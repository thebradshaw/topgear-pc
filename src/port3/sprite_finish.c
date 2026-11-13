// Top Gear 3000: the end of the sprite list each frame (bank $02).
//
// $02:AE15 reads the DSP-4's last 16 words (command 6: the high table of the sprites it
// placed) into the OAM copy at $1776 + $8E, then hides the sprites left unused, from $86
// up to the end of the list at $0200, by moving them to Y=$E0.

#include <stddef.h>

#include "../port/port.h"

static uint16_t dget(void) {
  uint8_t lo = tg_bus_read8(0x308000);
  uint8_t hi = tg_bus_read8(0x308001);
  return (uint16_t) (lo | hi << 8);
}

// $02:AEA4 is the same for the second player's half screen: its sprites at $1BB6/$19B6
// (+$440) and its list end in $88.
static void sprite_finish(TgRegs* r, uint16_t oam, uint8_t end) {
  uint16_t d = r->d;
  // A tail that isn't whole sprites would make the original run on through memory.
  uint16_t left = (uint16_t) (0x0200 - (uint16_t) (ram16((uint16_t) (d + end)) - ram16((uint16_t) (d + 0x8E))));
  if(left != 0 && (left >> 2) == 0) {
    tg_port_decline();
    return;
  }
  tg_bus_write16(0x308000, 0x0006);
  uint16_t y = ram16((uint16_t) (d + 0x8E));
  for(int i = 0; i < 16; i++) ram16_set((uint16_t) (0x1776 + oam + y + 2 * i), dget());
  r->db = 0x83;  // the PLB of the $83 from PEA $8330
  y = ram16((uint16_t) (d + end));
  uint16_t n = (uint16_t) (0x0200 - (uint16_t) (y - ram16((uint16_t) (d + 0x8E))));
  r->y = y;
  r->a = n;
  if(n != 0) {
    r->x = n >> 2;
    r->a = 0xE0E0;
    do {
      ram16_set(0x1576 + oam + y, 0xE0E0);
      y = (uint16_t) (y + 4);
    } while(--r->x != 0);
    r->y = y;
  }
}

void Tg3SpriteFinish(TgRegs* r) { sprite_finish(r, 0, 0x86); }
void Tg3SpriteFinishBottom(TgRegs* r) { sprite_finish(r, 0x440, 0x88); }
