// Top Gear 3000: the race loop's wait for the next frame (bank $02, in the 1-player loop).
//
// After the race's logic the original waits ($02:8653) for the NMI to take the last frame's
// picture (bit 0 of $B8), then builds the next one and runs the logic for the frames that
// went by meanwhile ($BA, into $1E9E). It usually manages every third frame. Run in C the
// rest takes no time, so the race would move every frame: this wait also lets
// tg_port_race_frames go by (3, the original's usual pace; 2 with smooth motion), as the
// C race loops of the other two games do.

#include "../port/port.h"

uint8_t tg3_race_flags = 1;  // the PPU flags of this race's frames (road.c)

// Top Gear 3000's own code doesn't allow for a step of one frame (its timer and some of its
// tables start at two), so smooth motion here is every second frame.
static void pace(TgRegs* r, uint16_t bit, uint16_t resume) {
  uint16_t frames = (uint16_t) (tg_port_race_frames < 2 ? 2 : tg_port_race_frames);
  for(;;) {
    // Widescreen and high frame rate: every frame shown while the race waits is a race
    // frame too (the picture built before the wait keeps its PPU_NEW_PICTURE).
    if(tg_port_wide_margin || tg_port_interp) tg_port_race_frame |= tg3_race_flags;
    uint16_t b8 = ram16(0x00B8);
    if(!(b8 & bit) && ram16(0x00BA) >= frames) {
      r->a = bit;
      r->z = true;  // BIT $B8
      r->n = (b8 & 0x8000) != 0;
      r->v = (b8 & 0x4000) != 0;
      break;
    }
    tg_port_wait();
  }
  uint8_t k = tg_port_set_bank(0);
  tg_port_set_bank(k);
  tg_port_leave((uint32_t) k << 16 | resume);
}

void Tg3RacePace(TgRegs* r) { pace(r, 1, 0x865B); }

// $02:8C48: the same in the 2-player loop, which waits for the bottom half's picture (bit 1).
void Tg3RacePace2P(TgRegs* r) { pace(r, 2, 0x8C50); }
