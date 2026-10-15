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

const uint8_t* tg_game_wram(const TgGame* game);  // 128 KiB
uint32_t tg_game_frame_count(const TgGame* game);

#endif
