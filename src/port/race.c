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

// A 5-byte window table entry: a line count, then the window words for the two windows.
// Written as three overlapping 16-bit stores, as in the original.
static uint16_t window_entry5(uint32_t table, uint16_t y, uint16_t count, uint16_t w1, uint16_t w2) {
  tg_bus_write16((table + y) & 0xffffff, count);
  tg_bus_write16((table + y + 1) & 0xffffff, w1);
  tg_bus_write16((table + y + 3) & 0xffffff, w2);
  return (uint16_t) (y + 5);
}

// One view's part of the 2-player window table (see RaceBuildSplitWindowTable).
typedef struct SplitWindowView {
  uint16_t top, bottom_left;  // heights of the bands before the road (fully closed, open)
  uint16_t road;           // height of the road band
  uint16_t extra;          // height added to the current slot
  uint16_t edges;          // window word for the road bands
  uint16_t slot;           // index of the current slot (words)
  uint16_t heights;        // per-slot band heights, minus 2
  uint16_t counts;         // per-slot band heights
  uint16_t words;          // per-slot window words
  uint16_t total;          // lines the view covers
  bool last_count_one;     // player 2's closing band is written as 1 line
} SplitWindowView;

static const SplitWindowView kSplitViews[2] = {
  { 0x0169, 0x016B, 0x016D, 0x016F, 0x0167, 0x1EC0, 0x0151, 0x0153, 0x015D, 0x70, false },
  { 0x0173, 0x0175, 0x0177, 0x0179, 0x0171, 0x1EC2, 0x013D, 0x013F, 0x0149, 0x68, true },
};

static uint16_t split_view(TgRegs* r, uint32_t table, uint16_t y, const SplitWindowView* v, uint16_t* x_out) {
  uint16_t n = ram16(v->top);
  if(n != 0) y = window_entry5(table, y, n, 0xFF00, 0xFF00);
  n = ram16(v->bottom_left);
  if(n != 0) y = window_entry5(table, y, n, 0x00FF, 0x00FF);
  uint16_t x = ram16(v->slot);
  ram16_set((uint16_t) (v->counts + x), ram16(v->extra));
  ram16_set((uint16_t) (v->words + x), 0x00FF);
  if(x != 0) {
    uint16_t h = (uint16_t) (ram16((uint16_t) (v->heights + x)) - ram16(v->bottom_left) - ram16(v->top) - ram16(v->extra));
    ram16_set((uint16_t) (v->heights + x), (h & 0x8000) ? 0 : h);
  }
  uint16_t scratch = (uint16_t) (r->d + 0x1A);
  ram16_set(scratch, ram16(v->road));
  for(;;) {
    uint16_t left = ram16(scratch), count = ram16((uint16_t) (v->counts + x));
    if(left <= count) {
      if(left != 0) y = window_entry5(table, y, left, ram16(v->edges), ram16((uint16_t) (v->words + x)));
      break;
    }
    ram16_set(scratch, (uint16_t) (left - count));
    if(count != 0) y = window_entry5(table, y, count, ram16(v->edges), ram16((uint16_t) (v->words + x)));
    x = (uint16_t) (x - 2);
    if(x & 0x8000) {
      left = ram16(scratch);
      if(left != 0) y = window_entry5(table, y, left, ram16(v->edges), ram16(v->edges));
      break;
    }
  }
  uint16_t rest = (uint16_t) (v->total - ram16(v->top) - ram16(v->bottom_left) - ram16(v->road));
  if(rest != 0) y = window_entry5(table, y, v->last_count_one ? 1 : rest, 0xFF00, 0xFF00);
  *x_out = x;
  return y;
}

// $00:B18C. In 2-player races, builds the channel 6 window table through the pointer at
// direct page $EB from the band heights RaceBuildSprites leaves at $0151-$0179. The
// original hangs if RaceSplitSlot1 reaches 9; that call runs emulated. Leaves A = 0;
// N/V/Z/C are don't-care.
void RaceBuildSplitWindowTable(TgRegs* r) {
  if(ram16(kSplitViews[0].slot) >= 9) {
    tg_port_decline();
    return;
  }
  uint32_t table = (uint32_t) r->db << 16 | ram16((uint16_t) (r->d + RaceWindowTablePtr));
  uint16_t x = 0;
  uint16_t y = window_entry5(table, 0, 4, 0xFF00, 0xFF00);
  y = split_view(r, table, y, &kSplitViews[0], &x);
  y = split_view(r, table, y, &kSplitViews[1], &x);
  tg_bus_write16((table + y) & 0xffffff, 0x0000);
  r->a = 0;
  r->x = x;
  r->y = y;
}
