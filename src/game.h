#ifndef TG_GAME_H
#define TG_GAME_H

#include <stdbool.h>
#include <stdint.h>

#include "rom.h"

// The game, as seen by a frontend. Today every frame runs the original ROM on the
// embedded reference emulator. As routines are translated to C, they take over here
// and the emulator remains as the fallback and as the lockstep referee.

#define TG_FB_WIDTH 512
#define TG_FB_HEIGHT 480

// SNES joypad bits, in the order the hardware shifts them out.
enum {
  TG_BTN_B = 1 << 0,
  TG_BTN_Y = 1 << 1,
  TG_BTN_SELECT = 1 << 2,
  TG_BTN_START = 1 << 3,
  TG_BTN_UP = 1 << 4,
  TG_BTN_DOWN = 1 << 5,
  TG_BTN_LEFT = 1 << 6,
  TG_BTN_RIGHT = 1 << 7,
  TG_BTN_A = 1 << 8,
  TG_BTN_X = 1 << 9,
  TG_BTN_L = 1 << 10,
  TG_BTN_R = 1 << 11,
};

typedef struct TgGame TgGame;

TgGame* tg_game_create(const TgRom* rom);
void tg_game_destroy(TgGame* game);
void tg_game_reset(TgGame* game, bool hard);

// player is 1 or 2; buttons is a mask of TG_BTN_*.
void tg_game_set_input(TgGame* game, int player, uint16_t buttons);
void tg_game_run_frame(TgGame* game);

// XRGB8888, TG_FB_WIDTH x TG_FB_HEIGHT (each SNES pixel is 2x2; 224 visible lines start at y=16).
void tg_game_get_pixels(TgGame* game, uint8_t* pixels);
// Interleaved stereo int16, count samples per channel.
void tg_game_get_samples(TgGame* game, int16_t* samples, int count);

// Starts recording execution coverage into cov (NULL stops). cov must outlive the game.
typedef struct TgCoverage TgCoverage;
void tg_game_set_coverage(TgGame* game, TgCoverage* cov);

// Writes an instruction-level trace (every instruction and every bus write) to f
// while set; NULL stops. Used to see what a routine reads and writes in one frame.
void tg_game_set_trace(TgGame* game, void* file);

// Whether translated routines (src/port) run, and whether each call is checked against
// the original. See lockstep.h.
#include <stdio.h>
#include "lockstep.h"
void tg_game_set_port_mode(TgGame* game, TgPortMode mode);
// Enhancement: race objects grow smoothly instead of stepping between sprite sizes. Only
// drawing changes; the game runs the same.
void tg_game_set_smooth_sprites(TgGame* game, bool on);
// The full-screen 1-player race view (an enhancement, off by default).
void tg_game_set_full_view(TgGame* game, bool on);
// Full view and side by side draw the road 1.3 times taller, with the horizon low as in the
// original; tall road draws it twice as tall, filling the screen.
void tg_game_set_tall_road(TgGame* game, bool on);
// High frame rate: keeps what is needed to draw in-between race frames; tg_game_interp
// then redraws the picture (pixels or wide pixels) t (0-1) of the way from the frame
// before the last to the last. False (picture unchanged) outside races.
void tg_game_set_interp(TgGame* game, bool on);
bool tg_game_interp(TgGame* game, float t);
// Auto-save: the countries open at each skill level (0-7), and opening them up to the
// given ones (never closing any).
void tg_game_progress(TgGame* game, uint8_t open[3]);
void tg_game_unlock(TgGame* game, const uint8_t open[3]);
// Widescreen (an enhancement, off by default): races are drawn margin pixels wider on each
// side (at most 96), other screens in the middle with black borders. The picture is then
// tg_game_wide_pixels: XRGB, (256 + 2 * margin) x 224, one pixel per SNES pixel (NULL
// before the first frame).
void tg_game_set_wide(TgGame* game, int margin);
// 2-player races in widescreen with the views side by side, each full height (an
// enhancement, off by default).
void tg_game_set_side_by_side(TgGame* game, bool on);
// Ghost car (an enhancement, off by default): the best lap on each course replayed as a
// see-through car in 1-player races. Best laps can be saved and loaded; changed reports
// (once) that a new best lap was recorded.
void tg_game_set_ghost(TgGame* game, bool on);
bool tg_game_ghosts_changed(TgGame* game);
bool tg_game_save_ghosts(TgGame* game, const char* path);
bool tg_game_load_ghosts(TgGame* game, const char* path);
int tg_game_wide_margin(const TgGame* game);
const uint32_t* tg_game_wide_pixels(TgGame* game);
// Debugging: VRAM, CGRAM and OAM to a file.
void tg_game_dump_ppu(TgGame* game, const char* path);
// Prints what the translated routines did; returns the number of mismatches found.
int tg_game_port_report(TgGame* game, FILE* f);

const uint8_t* tg_game_wram(const TgGame* game);  // 128 KiB
uint32_t tg_game_frame_count(const TgGame* game);

#endif
