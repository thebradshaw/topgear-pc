// Which cars each view can see (bank $00).

#include "port.h"
#include "variables.h"

// Field f of the record after offset x (see cars.c): the routines here walk offsets x
// but look at the record that follows.
#define NEXT(x, f) ((uint16_t) (RaceCars + RaceCarSize + (x) + (f)))

// Track distance from a to b going forwards, wrapping at the track length.
static uint16_t ahead(uint16_t b, uint16_t a) {
  uint16_t d = (uint16_t) (b - a);
  if(b < a) d = (uint16_t) (d + ram16(RaceCars));
  return d;
}

static uint16_t asr16(uint16_t v) { return (uint16_t) ((int16_t) v >> 1); }

// Computer cars drift side to side between the edges given by the $7E:7000 table for
// their track position, bouncing off them. Kept exactly, including that a car moving
// left past the right edge is then checked against the left edge using its speed.
static void steer_car(TgRegs* r, uint16_t x) {
  uint16_t edges = ram16((uint16_t) (0x7000 + ((ram16(NEXT(x, 0x07)) >> 4) & 0xFFFE)));
  uint16_t right = asr16(edges & 0xFF00);
  uint16_t left = asr16((uint16_t) (edges << 8));
  ram16_set((uint16_t) (r->d + 0x1A), right);  // direct page scratch, as in the original
  ram16_set((uint16_t) (r->d + 0x1C), left);
  uint16_t side = (uint16_t) (ram16(NEXT(x, 0x02)) + ram16(NEXT(x, 0x04)));
  ram16_set(NEXT(x, 0x02), side);
  uint16_t a = asr16(side);
  bool reverse;
  if(!((uint16_t) (a - right) & 0x8000)) {
    ram16_set(NEXT(x, 0x02), (uint16_t) (right << 1));
    a = ram16(NEXT(x, 0x04));
    if(!(a & 0x8000)) {
      reverse = true;
      goto done;
    }
  }
  if(!((uint16_t) (a - left) & 0x8000)) return;
  ram16_set(NEXT(x, 0x02), (uint16_t) (left << 1));
  a = ram16(NEXT(x, 0x04));
  reverse = a & 0x8000;
done:
  if(reverse) ram16_set(NEXT(x, 0x04), (uint16_t) -a);
}

typedef struct View {
  uint16_t hidden_word;  // bit 15: view not following a player car
  uint16_t camera;       // direct page word with the camera's track position
  uint16_t player_car;   // RaceP1Car / RaceP2Car
  uint16_t list;         // output: [distance + $80, sprite word] per car, then $7FFF
  uint16_t side_offset;  // RaceP1Side-style word subtracted from each car's side position
} View;

static const View kViews[2] = {
  { 0x1EDB, 0x46, RaceP1Car, RaceView1Cars, 0x1EE4 },
  { 0x1EDC, 0x59, RaceP2Car, RaceView2Cars, 0x1EE6 },
};

// first: the first view of RaceListVisibleCars; fly_in: RaceListGridCars, which always
// starts from the camera and moves no cars.
static void list_view(TgRegs* r, const View* v, bool first, bool fly_in) {
  uint16_t x, y = 0;
  bool hidden = fly_in || (ram16(v->hidden_word) & 0x8000);
  uint16_t start;
  if(hidden) {
    // Start at the first car at or past the camera, less $10.
    start = (uint16_t) (ram16((uint16_t) (r->d + v->camera)) - 0x10);
    if(ram16((uint16_t) (r->d + v->camera)) < 0x10) start = (uint16_t) (start + ram16(RaceCars));
    ram16_set(RaceViewStart, start);
    for(x = 0;; ) {
      if(start <= ram16(NEXT(x, 0x07))) break;
      x += RaceCarSize;
      if(x == 0x168) {
        x = 0;
        break;
      }
    }
  } else {
    // Start at the player's car, then back up to the first car within $10 behind it.
    x = ram16(v->player_car);
    uint16_t pos = ram16(NEXT(x, 0x07));
    start = (uint16_t) (pos - 0x10);
    if(pos < 0x10) start = (uint16_t) (start + ram16(RaceCars));
    ram16_set(RaceViewStart, start);
    do {
      x = x < RaceCarSize ? 0x156 : (uint16_t) (x - RaceCarSize);
    } while(ahead(ram16(NEXT(x, 0x07)), start) < 0x11);
    goto next_car;
  }
  for(;;) {
    if(fly_in) {
      // no steering
    } else if(first) {
      if(ram16(NEXT(x, 0x0A)) != 0) {
        steer_car(r, x);
        ram16_set(NEXT(x, 0x10), 0xFFFF);
      }
    } else if(!(ram16(NEXT(x, 0x10)) & 0x8000) && ram16(NEXT(x, 0x0A)) != 0) {
      steer_car(r, x);
    }
    uint16_t kind = ram16(NEXT(x, 0x00)) & 0xFE00;
    ram16_set((uint16_t) (r->d + 0x1A), kind);
    uint16_t dist = ahead(ram16(NEXT(x, 0x07)), ram16(RaceViewStart));
    if(dist >= 0x400) break;
    ram16_set((uint16_t) (v->list + y), (uint16_t) (dist + 0x80));
    // Which car this is, for high frame rate: its number, which moves with it when
    // RaceSortCars swaps records as cars overtake (the record's slot doesn't).
    tg_port_view_car[v->list == RaceView1Cars ? 0 : 1][y >> 2] = (uint8_t) (g_ram[NEXT(x, 0x00)] & 0x1F);
    uint16_t sprite;
    if(x == ram16(v->player_car) && !hidden) {
      sprite = 0x8000;
    } else {
      uint16_t side = ram16(NEXT(x, 0x02));
      if(side & 0x8000) sprite = (uint16_t) ((((uint16_t) (-side) << 1) & 0xFFFF) >> 8 | 0x0100);
      else sprite = (uint16_t) (((side << 1) & 0xFFFF) >> 8);
      sprite |= kind;
    }
    ram16_set((uint16_t) (v->list + 2 + y), sprite);
    uint16_t turn = (uint16_t) (((ram16(NEXT(x, 0x02)) - ram16(v->side_offset)) & 0xFE00) >> 8);
    uint16_t w = (uint16_t) (rom16(0x00B08C + turn) + sprite);
    if((w & 0x1E00) >= 0x0A00) {
      // Bits 9-12 past 4: step on by 2 and fold back, setting bit 11 unless bit 12 was set.
      bool high = w & 0x1000;
      w = (uint16_t) ((w + 0x400) & 0xE1FF);
      if(!high) w |= 0x0800;
    }
    ram16_set((uint16_t) (v->list + 2 + y), w);
    y += 4;
    if(y == 0x50) break;
  next_car:
    x = (uint16_t) (x + RaceCarSize);
    if(x >= 0x168) x = (uint16_t) (x - 0x168);
  }
  ram16_set((uint16_t) (v->list + y), 0x7FFF);
}

// $00:ABFC. Once per race update: for each view, lists the cars up to $400 ahead of the
// camera with their sprite words, and drifts the computer cars side to side (once each,
// in the first view that reaches them). A, X, Y and N/V/Z/C are don't-care.
void RaceListVisibleCars(TgRegs* r) {
  list_view(r, &kViews[0], true, false);
  list_view(r, &kViews[1], false, false);
}

static const View kGridViews[2] = {
  { 0x1EDB, 0x46, RaceP1Car, RaceView1Cars, 0x1E8A },
  { 0x1EDC, 0x59, RaceP2Car, RaceView2Cars, 0x1E8C },
};

// $00:AEF3. The same lists while the cameras fly in to the starting grid: always from
// each camera, with no player car singled out and no cars moved, and sprite turns taken
// against the players' own side positions. A, X, Y and N/V/Z/C are don't-care.
void RaceListGridCars(TgRegs* r) {
  list_view(r, &kGridViews[0], false, true);
  list_view(r, &kGridViews[1], false, true);
}
