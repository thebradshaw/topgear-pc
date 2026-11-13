// Top Gear 3000: the ghost car (an enhancement, off by default), as in Top Gear 1 and 2.
// In 1-player races the player's best race on each course is recorded, and later races
// there replay it as a see-through car. It is only drawn: the scenery pass (scenery_loop.c)
// sends it to the DSP-4 as one more car, from a record made up here, and its pieces are
// marked PPU_OBJ_GHOST (dsp_objects.c). Nothing the game keeps changes.
//
// The car records are at $7E:FAB4, $35 bytes each, player 1's first. A record's track
// position is +$01..+$04 (the section is +$02, the lap $019A sections long), its side
// position +$07 and height +$0B; +$1F is the lap (-1 before the line is first crossed),
// and the race is over for the car when that reaches $1F06. The start's countdown is
// $1ECE (negative while waiting, down to 1 at the go). The race clock is the frames the
// race has been stepped by ($1E9E each update), so a pause doesn't count.

#include <stdlib.h>
#include <string.h>

#include "../port/port.h"

#define REC 0xFAB4u

uint8_t tg3_ghost_record[0x36];  // the record the scenery pass draws the ghost from
uint16_t tg3_ghost_section = 0xFFFF;  // its section, or $FFFF for no ghost this update

static TgGhostSample g_race[TG_GHOST_SAMPLES];
static int g_count, g_play;
static bool g_recording, g_started;
static uint16_t g_course = 0xFFFF, g_clock;
static bool g_keep_partial;

static uint16_t w(uint16_t a) { return ram16(a); }
static uint16_t f(uint16_t o) { return ram16((uint16_t) (REC + o)); }
static uint32_t pos(void) { return (uint32_t) f(0x01) | (uint32_t) f(0x03) << 16; }

// The course: the number of sections and the course's piece table, folded into a slot.
static uint16_t course(void) {
  return (uint16_t) (((uint32_t) w(0x019C) * 31u + w(0x019A)) % TG_GHOST_COURSES);
}

static void record(void) {
  if(f(0x1F) == w(0x1F06)) {
    // Over the line for the last time.
    g_recording = false;
    TgGhostLap* best = &tg_port_ghosts[g_course];
    if(g_count > 1 && (best->count == 0 || g_clock < best->time)) {
      best->time = g_clock;
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
  uint32_t p = pos();
  TgGhostSample* s = &g_race[g_count++];
  s->time = g_clock;
  s->pos = (uint16_t) p;
  s->pos_hi = (uint16_t) (p >> 16);
  s->side = f(0x07);
  s->kind = f(0x0B);
  if(g_keep_partial) {
    // Testing (TG_GHOST_PARTIAL): the race so far is kept as the best, unfinished.
    TgGhostLap* best = &tg_port_ghosts[g_course];
    best->time = 0xFFFF;
    best->count = g_count;
    memcpy(best->samples, g_race, (size_t) g_count * sizeof(TgGhostSample));
    tg_port_ghosts_changed = true;
  }
}

// Where the ghost is now: fills in tg3_ghost_record and tg3_ghost_section.
static void place(void) {
  const TgGhostLap* best = &tg_port_ghosts[g_course];
  if(best->count < 2) return;
  if(g_play >= best->count || best->samples[g_play].time > g_clock) g_play = 0;
  while(g_play + 1 < best->count && best->samples[g_play + 1].time <= g_clock) g_play++;
  if(g_play + 1 >= best->count) return;  // the ghost has finished
  const TgGhostSample* a = &best->samples[g_play];
  const TgGhostSample* b = &best->samples[g_play + 1];
  int span = b->time - a->time, into = g_clock - a->time;
  if(span <= 0 || into < 0) return;
  uint32_t lap = (uint32_t) w(0x019A) << 8;
  if(lap == 0) return;
  uint32_t pa = (uint32_t) a->pos | (uint32_t) a->pos_hi << 16;
  uint32_t pb = (uint32_t) b->pos | (uint32_t) b->pos_hi << 16;
  pa %= lap;
  pb %= lap;
  uint32_t step = pb >= pa ? pb - pa : pb + lap - pa;
  if(step > 0x2000) step = 0;  // a jump (put back on the road): no step
  uint32_t p = pa + (uint32_t) ((uint64_t) step * (uint32_t) into / (uint32_t) span);
  if(p >= lap) p -= lap;
  int16_t side = (int16_t) (a->side + (int16_t) ((int16_t) (b->side - a->side) * into / span));
  int16_t height = (int16_t) (a->kind + (int16_t) ((int16_t) (b->kind - a->kind) * into / span));

  // A copy of the player's record, moved to the ghost's place, not in the air.
  for(int i = 0; i < 0x35; i++) tg3_ghost_record[i] = g_ram[(REC + i) & 0x1FFFF];
  tg3_ghost_record[0x01] = (uint8_t) p;
  tg3_ghost_record[0x02] = (uint8_t) (p >> 8);
  tg3_ghost_record[0x03] = (uint8_t) (p >> 16);
  tg3_ghost_record[0x04] = (uint8_t) (p >> 24);
  tg3_ghost_record[0x07] = (uint8_t) side;
  tg3_ghost_record[0x08] = (uint8_t) ((uint16_t) side >> 8);
  tg3_ghost_record[0x0B] = (uint8_t) height;
  tg3_ghost_record[0x0C] = (uint8_t) ((uint16_t) height >> 8);
  tg3_ghost_record[0x2A] &= (uint8_t) ~0x22;
  tg3_ghost_section = (uint16_t) (p >> 8);
}

// Once per race update, before the 1-player view's scenery pass (scenery_loop.c).
void tg3_ghost_update(void) {
  tg3_ghost_section = 0xFFFF;
  if(!tg_port_ghost) return;
  uint16_t countdown = w(0x1ECE);
  if((countdown & 0x8000) || countdown > 1) {
    // Waiting for the go: a new race.
    g_course = course();
    g_started = false;
    return;
  }
  if(!g_started) {
    g_started = true;
    g_keep_partial = getenv("TG_GHOST_PARTIAL") != NULL;
    g_recording = true;
    g_count = 0;
    g_play = 0;
    g_clock = 0;
  } else {
    g_clock = (uint16_t) (g_clock + w(0x1E9E));
  }
  if(g_course >= TG_GHOST_COURSES) return;
  if(g_recording) record();
  place();
}
