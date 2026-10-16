// Race engine routines (bank $00).

#include "port.h"
#include "variables.h"
#include "race_common.h"

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

// $00:B33B. Overtaking calls between the two players: when one gets ahead of the other
// (a lap ahead, or ahead on the same lap by car order), it asks for message $13 once,
// until the order changes back. Then the bump-sound timers ($1EFB / $1EFC, counting up
// from a bump) play a random bump sound when they reach $50. Registers and flags are
// don't-care afterwards.
void RaceCallOvertakes(TgRegs* r) {
  static const uint16_t kMessages[2] = { 0x0118, 0x012A };
  int16_t d = (int16_t) (ram16(0x1E76) - ram16(0x1E78));  // laps: player 1 less player 2
  if(d == 0) {
    ram16_set(0x1EF5, ram16(0x1EF5) & ~0x0002);
    ram16_set(0x1EF6, ram16(0x1EF6) & ~0x0002);
  } else {
    // ahead: 0 when player 1 is ahead, 1 for player 2.
    int ahead = d > 0 ? 0 : 1;
    uint16_t laps = (uint16_t) (d > 0 ? d : -d);
    uint16_t mine = (uint16_t) (0x1EF5 + ahead), theirs = (uint16_t) (0x1EF6 - ahead);
    ram16_set(theirs, ram16(theirs) & ~0x0002);
    uint16_t car = ram16((uint16_t) (0x0204 + 2 * ahead)), other = ram16((uint16_t) (0x0206 - 2 * ahead));
    if(laps < 2 && car < other) {
      ram16_set(0x1EF5, ram16(0x1EF5) & ~0x0002);
      ram16_set(0x1EF6, ram16(0x1EF6) & ~0x0002);
    } else if(!(ram16(mine) & 0x0002)) {
      ram16_set(mine, ram16(mine) | 0x0002);
      ram16_set(kMessages[ahead], 0x0013);
    }
  }
  for(int i = 0; i < 2; i++) {
    uint16_t timer = (uint16_t) (0x1EFB + i);
    uint8_t t = g_ram[timer];
    if(t == 0) continue;
    if(t >= 0x50) {
      g_ram[timer] = 0;
      uint16_t x = (uint16_t) ((race_random() & 0x00FF) >> 4);
      ram16_set(kMessages[i], tg_read8((uint32_t) r->db << 16 | (uint16_t) (0xE1ED + x)));
    } else {
      g_ram[timer] = (uint8_t) (t - 1);
    }
  }
}

// One player's pit stop registers. The two copies of the sequence differ only in these
// addresses, except that player 2 also needs $1F04 set before a button press counts.
typedef struct PitStop {
  uint16_t state;       // word: -1 waits to start, counting down; >= 0 when not in the pits
  uint16_t view_flag;   // direct page byte; bit 7 = the view can show the pit
  uint16_t pit_shown;   // direct page byte
  uint16_t scroll;      // direct page words: the pit backdrop's scroll and its fixed setup
  uint16_t view_hold;
  uint16_t pos, c8, ce, ca, d0;
  uint16_t sprites_off, oam, oam_high, oam_table;
  uint16_t extra, messages, message_wait;
  uint16_t gear, speed, fuel, palette;
  uint16_t joy_pressed, button, flags;
  bool needs_1f04;
} PitStop;

static const PitStop kPitStops[2] = {
  { 0x1E86, 0x52, 0xE1, 0xA6, 0x40, 0xA8, 0xC8, 0xCE, 0xCA, 0xD0, 0x110A, 0x0F0A, 0x108A, 0x8C26,
    0x0572, RaceP1Messages, 0x0128, RaceP1Gear, RaceP1Speed, RaceP1Fuel, 0x038E, 0x14, RaceP1AccelButton, 0x1EF5, false },
  { 0x1E88, 0x65, 0xE2, 0xBA, 0x53, 0xBC, 0xD4, 0xDA, 0xD6, 0xDC, 0x132A, 0x112A, 0x12AA, 0x8D26,
    0x0580, RaceP2Messages, 0x013A, RaceP2Gear, RaceP2Speed, RaceP2Fuel, 0x044E, 0x16, RaceP2AccelButton, 0x1EF6, true },
};

// $00:8B26 / $00:8B67: hides the view's sprites ($FFFF words, $0101 OAM words in both
// buffers) and loads the pit crew's high OAM entries, mirrored when $E0 bit 7 is set.
static void pit_clear_sprites(TgRegs* r, const PitStop* p) {
  for(int x = 0x1E; x >= 0; x -= 2) {
    ram16_set((uint16_t) (p->sprites_off + x), 0xFFFF);
    ram16_set((uint16_t) (p->sprites_off + 0x440 + x), 0xFFFF);
  }
  for(int x = 0x17E; x >= 0; x -= 2) {
    ram16_set((uint16_t) (p->oam + x), 0x0101);
    ram16_set((uint16_t) (p->oam + 0x440 + x), 0x0101);
  }
  uint16_t y = (g_ram[(uint16_t) (r->d + 0xE0)] & 0x80) ? (uint16_t) (p->oam_table + 0x80) : p->oam_table;
  for(int x = 0x7E; x >= 0; x -= 2, y -= 2) {
    uint16_t v = tg_read16((uint32_t) r->db << 16 | y);
    ram16_set((uint16_t) (p->oam_high + x), v);
    ram16_set((uint16_t) (p->oam_high + 0x440 + x), v);
  }
}

// A press of accelerate (cleared once read) ends the stop, as long as there is fuel.
static bool pit_leave_pressed(TgRegs* r, const PitStop* p) {
  if(p->needs_1f04 && !ram16(0x1F04)) return false;
  uint16_t joy = (uint16_t) (r->d + p->joy_pressed);
  uint16_t pressed = ram16(joy) & ram16(p->button);
  ram16_set(joy, ram16(joy) & ~ram16(p->button));
  return pressed && ram16(p->fuel);
}

static void pit_stop(TgRegs* r, const PitStop* p) {
  uint16_t s = ram16(p->state);
  if(!(s & 0x8000)) return;
  uint8_t* shown = &g_ram[(uint16_t) (r->d + p->pit_shown)];
  bool view = g_ram[(uint16_t) (r->d + p->view_flag)] & 0x80;
  uint16_t next = (uint16_t) (s - 1);
  switch((uint16_t) ~s) {
  case 0:  // waiting for the view to reach the pit, then set the pit scene up
    if(!view) {
      if(!(*shown & 0x80)) *shown = 0xFF;
      return;
    }
    ram16_set((uint16_t) (r->d + p->scroll), (g_ram[(uint16_t) (r->d + 0xE0)] & 0x80) ? 0x0000 : 0x0300);
    ram16_set((uint16_t) (r->d + p->view_hold), 0xFFFF);
    pit_clear_sprites(r, p);
    ram16_set((uint16_t) (r->d + p->pos), 0x0280);
    ram16_set((uint16_t) (r->d + p->c8), 0x0080);
    ram16_set((uint16_t) (r->d + p->ce), 0x0080);
    ram16_set((uint16_t) (r->d + p->ca), 0);
    ram16_set((uint16_t) (r->d + p->d0), 0);
    ram16_set(p->extra, 0);
    ram16_set(p->messages, 0);
    ram16_set(p->message_wait, 0);
    ram16_set(p->gear, 4);
    for(int i = 0; i < 0x20; i++) g_ram[p->palette + i] = tg_read8((uint32_t) (0x088A3A + i));
    r->db = 0x00;  // MVN leaves the destination bank
    ram16_set(p->speed, 0x1700);
    break;
  case 1:
    break;
  case 2:  // tiles stream in (RaceUploadBank3Tiles moves the state on)
    return;
  case 3:
    *shown = 0x01;
    ram16_set((uint16_t) (r->d + p->joy_pressed), 0);
    break;
  case 4:  // waiting for accelerate before refuelling; with fuel left it can skip refuelling
    if(!pit_leave_pressed(r, p)) return;
    next = (uint16_t) (s - 2);
    break;
  case 5: {  // refuelling
    uint16_t fuel = (uint16_t) (ram16(p->fuel) + 0x7F);
    if(fuel >= 0xA7FF) {
      ram16_set(p->fuel, 0xA7FF);
      break;
    }
    ram16_set(p->fuel, fuel);
    if(!pit_leave_pressed(r, p)) return;
    break;
  }
  case 6: {  // driving out: wait for the backdrop scroll to pass $300
    uint16_t pos = ram16((uint16_t) (r->d + p->pos)) & 0x03FF;
    if(pos < 0x0300 || pos >= 0x0380) return;
    *shown = 0xFF;
    break;
  }
  case 7:
    if(!view) return;
    for(int i = 0; i < 0x20; i += 2) ram16_set((uint16_t) (p->palette + i), ram16(RaceWindowMode));
    ram16_set(p->extra, 0);
    break;
  case 8:
    return;
  case 9:
    ram16_set((uint16_t) (r->d + p->view_hold), 0);
    g_ram[p->flags] &= ~0x20;
    break;
  default:
    *shown = 0x01;
    next = 0;
    break;
  }
  ram16_set(p->state, next);
}

// $00:90CC. Each frame of a race, steps both players' pit stop sequences. Registers and
// flags are don't-care afterwards; DB is $00 after a stop starts (its MVN).
void RaceUpdatePitStops(TgRegs* r) {
  pit_stop(r, &kPitStops[0]);
  pit_stop(r, &kPitStops[1]);
}

// The pit scene's scroll HDMA table for one player, written to the back buffer (the
// direct page pointer `back`, in the data bank) before the buffers are swapped with
// `front`: 32 scroll bytes from $180, a 3-entry header and 8 bytes from $8AFE. A is the
// backdrop position (mirrored when $E0 bit 7 is set).
typedef struct PitScroll {
  uint16_t scroll;  // direct page word: the BG2 scroll for the scene
  uint16_t back, front;
  uint16_t header[6];
  uint16_t add;
} PitScroll;

static const PitScroll kPitScrolls[2] = {
  { 0xA6, 0xCC, 0xD2, { RaceP1SegmentValue, 0x307F, RaceLeaderSegmentValue, 0x30FF, RaceP2SegmentValue, 0x327F }, 0x0000 },
  { 0xBA, 0xD8, 0xDE, { RaceP2SegmentValue, 0x327F, RaceLeaderSegmentValue, 0x32FF, RaceP1SegmentValue, 0x307F }, 0x7000 },
};

static void pit_scroll(TgRegs* r, const PitScroll* p) {
  uint16_t a = r->a, x;
  if(g_ram[(uint16_t) (r->d + 0xE0)] & 0x80) {
    a = (uint16_t) (((a ^ 0x03FF) + 0x0101) & 0x03FF);
    x = a;
    a = (uint16_t) (a + 0x0180);
  } else {
    a &= 0x03FF;
    x = a;
    a = (uint16_t) (a + 0x0080);
  }
  ram16_set((uint16_t) (r->d + p->scroll), a);
  uint32_t table = (uint32_t) r->db << 16 | ram16((uint16_t) (r->d + p->back));
  uint16_t half = (uint16_t) (x >> 1);
  uint8_t v = (uint8_t) half;
  uint16_t y = 0x0180;
  for(int i = 0; i < 8; i++) {
    for(int k = 0; k < 4; k++, y = (uint16_t) (y + 4)) tg_bus_write8((table + y) & 0xffffff, v);
    v = (uint8_t) (v + 0x10);
  }
  v = (uint8_t) (v - 0x80);
  x = (uint16_t) (((half & 0xFF00) | v) >> 4);
  for(int i = 0; i < 6; i += 2) {
    tg_bus_write16((table + 2 * i) & 0xffffff, (uint16_t) (ram16(p->header[i]) + p->add));
    tg_bus_write16((table + 2 * i + 2) & 0xffffff, p->header[i + 1]);
  }
  for(int k = 7; k >= 0; k--) tg_bus_write8((table + 0x0218 + k) & 0xffffff, tg_read8((uint32_t) r->db << 16 | (uint16_t) (0x8AFE + x + k)));
  tg_bus_write8((table + 0x0200) & 0xffffff, 0x00);
  uint16_t back = ram16((uint16_t) (r->d + p->back));
  ram16_set((uint16_t) (r->d + p->back), ram16((uint16_t) (r->d + p->front)));
  ram16_set((uint16_t) (r->d + p->front), back);
  r->a = ram16((uint16_t) (r->d + p->back));
  r->x = back;
  r->y = 0x0200;
}

// $00:8966 (player 1) and $00:8A2C (player 2). Leave A = the new back buffer, X = the
// new front one, Y = $200, with 16-bit registers; flags are don't-care.
void RacePitScrollP1(TgRegs* r) { pit_scroll(r, &kPitScrolls[0]); }
void RacePitScrollP2(TgRegs* r) { pit_scroll(r, &kPitScrolls[1]); }
