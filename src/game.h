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
// Debugging: VRAM, CGRAM and OAM to a file.
void tg_game_dump_ppu(TgGame* game, const char* path);
// Prints what the translated routines did; returns the number of mismatches found.
int tg_game_port_report(TgGame* game, FILE* f);

const uint8_t* tg_game_wram(const TgGame* game);  // 128 KiB
uint32_t tg_game_frame_count(const TgGame* game);

#endif
