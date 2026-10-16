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

// One HDMA table entry: a line count, then a 16-bit pointer. Written as two overlapping
// 16-bit stores, as in the original.
static uint16_t window_entry(uint32_t table, uint16_t y, uint16_t count, uint16_t source) {
  tg_bus_write16((table + y) & 0xffffff, count);
  tg_bus_write16((table + y + 1) & 0xffffff, source);
  return (uint16_t) (y + 3);
}

// One view's part of the 1-player window table. Bit 15 of hidden_word shows nothing;
// otherwise bits 9-11 of shape_word pick the window shape.
static uint16_t window_view(TgRegs* r, uint32_t table, uint16_t y, uint16_t hidden_word, uint16_t shape_word) {
  uint16_t hidden = ram16(hidden_word);
  r->n = hidden >> 15;  // BIT
  r->v = (hidden >> 14) & 1;
  if(r->n) return window_entry(table, y, 0x70, 0x945A);
  uint16_t shape = ram16(shape_word) & 0x0E00;
  y = window_entry(table, y, 0x3E, 0x945A);
  if(shape == 0) {
    y = window_entry(table, y, 0x9B, 0x945C);
    return window_entry(table, y, 0x17, 0x945A);
  }
  cmp16(r, shape, 0x0800);
  if(shape == 0x0800) {
    y = window_entry(table, y, 0x9B, 0x9522);
    return window_entry(table, y, 0x17, 0x945A);
  }
  cmp16(r, shape, 0x0400);
  if(shape < 0x0400) {
    y = window_entry(table, y, 0x99, 0x9492);
    return window_entry(table, y, 0x19, 0x945A);
  }
  if(shape == 0x0400) {
    y = window_entry(table, y, 0x96, 0x94C4);
    return window_entry(table, y, 0x1C, 0x945A);
  }
  y = window_entry(table, y, 0x99, 0x94F0);
  return window_entry(table, y, 0x19, 0x945A);
}

// $00:9381. Writes the channel 6 window HDMA table through the pointer at direct page
// $EB in the data bank. When RaceWindowMode is $C0DF each view gets its own shaped
// window; otherwise one plain entry. Leaves 16-bit A and index.
void RaceBuildWindowTable(TgRegs* r) {
  uint16_t mode = ram16(RaceWindowMode);
  uint32_t table = (uint32_t) r->db << 16 | ram16((uint16_t) (r->d + RaceWindowTablePtr));
  uint16_t y = 0;
  cmp16(r, mode, 0xC0DF);
  if(mode != 0xC0DF) {
    y = window_entry(table, y, 0x7F, 0x945A);
  } else {
    y = window_view(r, table, y, RaceWindowHidden1, RaceWindowShape1);
    y = window_view(r, table, y, RaceWindowHidden2, RaceWindowShape2);
  }
  tg_bus_write16((table + y) & 0xffffff, 0x0000);
  r->a = 0;
  r->y = y;
  flags_nz16(r, 0);
}

static uint16_t segment_value(uint16_t track_pos) {
  return ram16(RaceSegmentTable + (track_pos >> 8) * 2u);
}

// $00:8910. Looks up the $7E:3E00 per-segment table for each player's segment and for
// the car furthest along the track (highest byte at offset $1A of its record; the later
// car wins ties). Entered with 16-bit index; leaves 16-bit A.
void RaceUpdateSegmentValues(TgRegs* r) {
  ram16_set(RaceP1SegmentValue, segment_value(ram16(RaceP1TrackPos)));
  ram16_set(RaceP2SegmentValue, segment_value(ram16(RaceP2TrackPos)));
  uint16_t best = ram16(RaceCars + 0x1A), best_y = 0;
  uint16_t y = RaceCarSize;
  for(int i = 0; i < 19; i++, y += RaceCarSize) {
    uint16_t v = ram16(RaceCars + 0x1A + y);
    if(v >= best) {
      best = v;
      best_y = y;
    }
  }
  ram16_set((uint16_t) (r->d + 0x1A), best);
  ram16_set((uint16_t) (r->d + 0x1C), best_y);
  uint16_t seg = (uint16_t) ((ram16(RaceCars + 0x19 + best_y) >> 8) * 2);
  uint16_t v = ram16(RaceSegmentTable + seg);
  ram16_set(RaceLeaderSegmentValue, v);
  r->a = v;
  r->x = seg;
  r->y = best_y;
  flags_nz16(r, v);
  r->c = false;
  r->v = false;
}
