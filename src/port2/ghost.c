// Top Gear 2: the ghost car (an enhancement, off by default), as in Top Gear 1 (ghost.c).
// In 1-player races, the player's best race on each course is recorded, and later races
// there replay it as a see-through car alongside the player. It is only drawn: it takes
// the place of the camera's own car in the view's list of visible cars ($C340), which the
// object pass skips, so nothing else changes.
//
// The race clock is the frames since the green light ($001A - $0602). The car records
// ($C410, $40 bytes each) give the track position ($C418, 32 bits: section and fraction),
// the side position ($C414) and the height ($C416). A sample is kept every third frame or
// so (the original's pace); a race that finishes faster than the best so far replaces it.

#include <string.h>

#include "../port/port.h"

#define NCARS 20
#define WINDOW_BACK 0x2E00u   // the view starts this far behind the camera car
#define WINDOW_SECTIONS 0x44  // and lists cars this many sections ahead of that

bool tg2_sprite_ghost;  // the object pass is drawing the ghost (sprites.c)
int tg2_ghost_entry = -1;  // its offset in this view's visible-car list, or -1

static TgGhostSample g_race[TG_GHOST_SAMPLES];
static int g_count;
static bool g_recording, g_started;
static uint16_t g_course = 0xFFFF, g_start;
static int g_play;

static uint16_t w(uint16_t a) { return ram16(a); }
static void ws(uint16_t a, uint16_t v) { ram16_set(a, v); }
static uint32_t lap_length(void) { return (uint32_t) w(0x010A) << 16; }

// The player's car record, or -1.
static int player_record(void) {
  for(uint16_t c = 0; c < NCARS * 0x40; c = (uint16_t) (c + 0x40))
    if(w((uint16_t) (0xC410 + c)) == 0) return c;
  return -1;
}

static uint32_t record_pos(uint16_t c) {
  return (uint32_t) w((uint16_t) (0xC418 + c)) | (uint32_t) w((uint16_t) (0xC41A + c)) << 16;
}

static void record(uint16_t c, uint16_t time) {
  bool finished = (w(0x0634) & 0x8000) != 0;
  if(!g_started) {
    // Until the green light the player's car is held ($0634 bit 15).
    g_started = !finished;
    if(!g_started) return;
  }
  if(finished) {
    g_recording = false;
    TgGhostLap* best = &tg_port_ghosts[g_course % TG_GHOST_COURSES];
    if(g_count > 1 && (best->count == 0 || time < best->time)) {
      best->time = time;
      best->count = g_count;
      memcpy(best->samples, g_race, (size_t) g_count * sizeof(TgGhostSample));
      tg_port_ghosts_changed = true;
    }
    return;
  }
  if(g_count > 0 && (uint16_t) (time - g_race[g_count - 1].time) < 3) return;
  if(g_count == TG_GHOST_SAMPLES) {
    g_recording = false;  // a race this long is not a best race
    return;
  }
  uint32_t p = record_pos(c);
  TgGhostSample* s = &g_race[g_count++];
  s->time = time;
  s->pos = (uint16_t) p;
  s->pos_hi = (uint16_t) (p >> 16);
  s->side = w((uint16_t) (0xC414 + c));
  s->kind = w((uint16_t) (0xC416 + c));
}

// Once per race update (race.c), 1-player races only.
void tg2_ghost_update(void) {
  tg2_ghost_entry = -1;
  if(!tg_port_ghost || w(0xFF1A) != 0) return;
  uint16_t course = w(0x0108) & 0x3F;  // the track (RaceSetup's $0108)
  uint16_t start = w(0x0602);
  if(course != g_course || start != g_start) {
    // A new race.
    g_course = course;
    g_start = start;
    g_recording = true;
    g_started = false;
    g_count = 0;
    g_play = 0;
  }
  int c = player_record();
  if(c < 0 || !g_recording) return;
  record((uint16_t) c, (uint16_t) (w(0x001A) - start));
}

// After the view's visible cars are listed (objects.c): the ghost replaces the camera's
// own car (an entry of $FFFF) in the list, at its place by distance.
void tg2_ghost_show(void) {
  tg2_ghost_entry = -1;
  if(!tg_port_ghost || w(0xFF1A) != 0 || !g_started) return;
  const TgGhostLap* best = &tg_port_ghosts[g_course % TG_GHOST_COURSES];
  if(best->count < 2) return;
  int c = player_record();
  if(c < 0) return;
  uint16_t t = (uint16_t) (w(0x001A) - g_start);
  if(g_play >= best->count || best->samples[g_play].time > t) g_play = 0;
  while(g_play + 1 < best->count && best->samples[g_play + 1].time <= t) g_play++;
  if(g_play + 1 >= best->count) return;  // the ghost has finished
  const TgGhostSample* a = &best->samples[g_play];
  const TgGhostSample* b = &best->samples[g_play + 1];
  int span = b->time - a->time, into = t - a->time;
  if(span <= 0 || into < 0) return;
  uint32_t lap = lap_length();
  if(lap == 0) return;
  uint32_t pa = (uint32_t) a->pos | (uint32_t) a->pos_hi << 16;
  uint32_t pb = (uint32_t) b->pos | (uint32_t) b->pos_hi << 16;
  uint32_t step = pb >= pa ? pb - pa : pb + lap - pa;
  if(step > 0x00100000) step = 0;  // a jump (back on the road after a crash): no step
  uint32_t p = pa + (uint32_t) ((uint64_t) step * (uint32_t) into / (uint32_t) span);
  if(p >= lap) p -= lap;
  int16_t side = (int16_t) (a->side + (int16_t) ((int16_t) (b->side - a->side) * into / span));
  int16_t height = (int16_t) (a->kind + (int16_t) ((int16_t) (b->kind - a->kind) * into / span));

  // Its distance from the start of the view's window, as the list measures the cars.
  uint32_t me = record_pos((uint16_t) c);
  uint32_t from = me >= WINDOW_BACK ? me - WINDOW_BACK : me + lap - WINDOW_BACK;
  uint32_t d = p >= from ? p - from : p + lap - from;
  if(d >= (uint32_t) WINDOW_SECTIONS << 16) return;
  uint16_t key = (uint16_t) ((d >> 10) & 0x3FFE);

  // Take out the camera car's entry, then insert the ghost by distance.
  uint16_t y = 0, mine = 0xFFFF;
  for(; w((uint16_t) (0xC346 + y)) != 0x7FFE && y < NCARS * 8; y = (uint16_t) (y + 8))
    if(w((uint16_t) (0xC340 + y)) == 0xFFFF) mine = y;
  if(mine == 0xFFFF) return;
  uint16_t end = y;
  for(y = mine; y + 8 <= end; y = (uint16_t) (y + 8))
    for(int k = 0; k < 8; k += 2) ws((uint16_t) (0xC340 + y + k), w((uint16_t) (0xC348 + y + k)));
  end = (uint16_t) (end - 8);
  for(y = 0; y < end && w((uint16_t) (0xC346 + y)) <= key; y = (uint16_t) (y + 8)) {}
  for(uint16_t i = (uint16_t) (end + 8); i > y; i = (uint16_t) (i - 8))
    for(int k = 0; k < 8; k += 2) ws((uint16_t) (0xC340 + i + k), w((uint16_t) (0xC338 + i + k)));
  ws((uint16_t) (0xC340 + y), w((uint16_t) (0xC412 + c)));
  ws((uint16_t) (0xC342 + y), (uint16_t) side);
  ws((uint16_t) (0xC344 + y), (uint16_t) height);
  ws((uint16_t) (0xC346 + y), key);
  tg2_ghost_entry = y;
}
