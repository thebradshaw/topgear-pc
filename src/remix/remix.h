#ifndef TG_REMIX_H
#define TG_REMIX_H

// Horizon remix (Phase 6, a prototype): draws the race the game is running in a new,
// flat-shaded style at any resolution, from the game's own state in work RAM (the track
// tables, the cars' positions). Nothing here changes the game; it only reads RAM.

#include <stdbool.h>
#include <stdint.h>

// Once per frame the game runs: follows the cars between race updates for smooth drawing.
void tg_remix_tick(const uint8_t* wram);
// How far (0-1) the picture is past the last frame run, at high frame rates.
void tg_remix_set_subframe(float t);

// The numbers the race display shows for player 1.
typedef struct {
  int speed;          // as on the speedometer
  bool kph;
  int position;       // 1 = first
  int lap, laps;      // lap 1 to laps
  int minutes, seconds, hundredths;
  int gear;           // 1-5, or 0 for an automatic
  int nitros;
  int fuel;           // 0-100
} TgRemixHud;
void tg_remix_hud(const uint8_t* wram, TgRemixHud* hud);

// True when work RAM holds a race the remix can draw.
bool tg_remix_active(const uint8_t* wram);

// Draws player 1's view into out (XRGB, w x h). ss: supersampling (1 to 4) for smooth edges.
void tg_remix_draw(const uint8_t* wram, uint32_t* out, int w, int h, int ss);

// The same, in parts: rows row0 to row1 - 1 of out only, using work (tg_remix_work_size
// bytes, shared by all parts of a frame). Parts with different rows can be drawn at the
// same time on different threads.
#include <stddef.h>
size_t tg_remix_work_size(int w, int h, int ss);
void tg_remix_draw_rows(const uint8_t* wram, uint32_t* out, int w, int h, int ss, void* work, int row0, int row1);

#endif
