// Ghost car (an enhancement, off by default): in 1-player races, the player's best race on
// each course is recorded, and later races there replay it as a see-through car that
// races alongside the player. It is only drawn: it is added to the top view's list of
// visible cars after the game builds it, and nothing else reads that list.
//
// Each race update records the race clock and the player's track position and side
// position; a race that finishes faster than the best so far replaces it. Playback finds
// the samples either side of the race clock and draws the ghost between them.

#include <stdlib.h>
#include <string.h>

#include "port.h"
#include "variables.h"

bool tg_port_ghost;
TgGhostLap tg_port_ghosts[TG_GHOST_COURSES];
bool tg_port_ghosts_changed;
uint16_t tg_port_ghost_key, tg_port_ghost_word;  // the ghost's list entry this update

static TgGhostSample g_race[TG_GHOST_SAMPLES];
static int g_count;
static bool g_recording;   // this race is being recorded
static uint16_t g_last_time, g_course = 0xFFFF;
static int g_play;         // playback position in the best race

static uint16_t track_length(void) { return ram16(RaceCars); }

// Track distance from a to b going forwards, wrapping at the track length.
static uint16_t ahead(uint16_t b, uint16_t a) {
  uint16_t d = (uint16_t) (b - a);
  if(b < a) d = (uint16_t) (d + track_length());
  return d;
}

static uint16_t car_field(uint16_t car, uint16_t f) { return ram16((uint16_t) (RaceCars + RaceCarSize + car + f)); }

static void record(uint16_t time) {
  if(!g_recording) return;
  uint16_t finish = ram16(RaceP1FinishTime);
  if(!(finish & 0x8000)) {
    // Finished: keep the race if it is the best on this course.
    g_recording = false;
    TgGhostLap* best = &tg_port_ghosts[g_course % TG_GHOST_COURSES];
    if(g_count > 1 && (best->count == 0 || finish < best->time)) {
      best->time = finish;
      best->count = g_count;
      memcpy(best->samples, g_race, (size_t) g_count * sizeof(TgGhostSample));
      tg_port_ghosts_changed = true;
    }
    return;
  }
  if(g_count == TG_GHOST_SAMPLES) {
    g_recording = false;  // a race this long is not a best race
    return;
  }
  uint16_t car = ram16(RaceP1Car);
  TgGhostSample* s = &g_race[g_count++];
  s->time = time;
  s->pos = car_field(car, 0x07);
  s->side = car_field(car, 0x02);
  s->kind = car_field(car, 0x00) & 0xFE00;
}

// Adds the ghost to the top view's car list, in order of distance.
static void show(uint16_t time) {
  const TgGhostLap* best = &tg_port_ghosts[g_course % TG_GHOST_COURSES];
  if(best->count < 2) return;
  uint16_t t = time;
  while(g_play + 1 < best->count && best->samples[g_play + 1].time <= t) g_play++;
  if(g_play + 1 >= best->count) return;  // the ghost has finished
  const TgGhostSample* a = &best->samples[g_play];
  const TgGhostSample* b = &best->samples[g_play + 1];
  int span = b->time - a->time;
  int into = t - a->time;
  if(span <= 0 || into < 0) return;
  uint16_t step = ahead(b->pos, a->pos);
  if(step > 0x400) step = 0;  // a pit stop or a crossing of the line: no step
  uint32_t p = a->pos + (uint32_t) step * (uint32_t) into / (uint32_t) span;
  if(p >= track_length()) p -= track_length();
  int16_t side = (int16_t) (a->side + ((int16_t) (b->side - a->side)) * into / span);
  uint16_t dist = ahead((uint16_t) p, ram16(RaceViewStart));
  if(dist >= 0x400) return;
  // The sprite word, as the list gives a computer car's.
  uint16_t s = (uint16_t) side;
  uint16_t sprite = (s & 0x8000) ? (uint16_t) ((((uint16_t) (-s) << 1) & 0xFFFF) >> 8 | 0x0100)
                                 : (uint16_t) (((s << 1) & 0xFFFF) >> 8);
  sprite |= a->kind;
  uint16_t turn = (uint16_t) (((s - ram16(0x1EE4)) & 0xFE00) >> 8);
  uint16_t w = (uint16_t) (rom16(0x00B08C + turn) + sprite);
  if((w & 0x1E00) >= 0x0A00) {
    bool high = w & 0x1000;
    w = (uint16_t) ((w + 0x400) & 0xE1FF);
    if(!high) w |= 0x0800;
  }
  // Insert it.
  uint16_t key = (uint16_t) (dist + 0x80), y = 0;
  while(ram16((uint16_t) (RaceView1Cars + y)) != 0x7FFF && ram16((uint16_t) (RaceView1Cars + y)) <= key) y += 4;
  uint16_t end = y;
  while(ram16((uint16_t) (RaceView1Cars + end)) != 0x7FFF) end += 4;
  if(end >= 0x4C) return;  // the list is full
  for(uint16_t i = (uint16_t) (end + 4); i >= y + 4; i -= 4) {
    ram16_set((uint16_t) (RaceView1Cars + i), ram16((uint16_t) (RaceView1Cars + i - 4)));
    ram16_set((uint16_t) (RaceView1Cars + i + 2), ram16((uint16_t) (RaceView1Cars + i - 2)));
    if(i < 0x50) tg_port_view_car[0][i >> 2] = tg_port_view_car[0][(i >> 2) - 1];
  }
  tg_port_view_car[0][y >> 2] = TG_GHOST_CAR;
  ram16_set((uint16_t) (RaceView1Cars + y), key);
  ram16_set((uint16_t) (RaceView1Cars + y + 2), w);
  tg_port_ghost_key = key;
  tg_port_ghost_word = w;
}

// Once per race update, after the visible cars are listed.
void tg_port_ghost_update(void) {
  tg_port_ghost_key = 0xFFFF;
  if(!tg_port_ghost || g_ram[0x1F04] != 0) return;
  uint16_t time = ram16(RaceTime);
  uint16_t course = ram16(CarTypeIndex);
  if(course != g_course || time < g_last_time) {
    // A new race.
    g_course = course;
    g_recording = true;
    g_count = 0;
    g_play = 0;
  }
  g_last_time = time;
  record(time);
  if(!(ram16(0x1EDB) & 0x8000)) show(time);  // not once the view stops following the player
}
