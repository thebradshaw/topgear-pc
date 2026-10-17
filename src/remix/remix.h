#ifndef TG_REMIX_H
#define TG_REMIX_H

// Horizon remix (Phase 6, a prototype): draws the race the game is running in a new,
// flat-shaded style at any resolution, from the game's own state in work RAM (the track
// tables, the cars' positions). Nothing here changes the game; it only reads RAM.

#include <stdbool.h>
#include <stdint.h>

// True when work RAM holds a race the remix can draw.
bool tg_remix_active(const uint8_t* wram);

// Draws player 1's view into out (XRGB, w x h). ss: supersampling (1 to 4) for smooth edges.
void tg_remix_draw(const uint8_t* wram, uint32_t* out, int w, int h, int ss);

#endif
