#include "game.h"

#include <stdlib.h>

#include "snes.h"

struct TgGame {
  Snes* snes;
};

TgGame* tg_game_create(const TgRom* rom) {
  TgGame* game = calloc(1, sizeof(TgGame));
  if(game == NULL) return NULL;
  game->snes = snes_init();
  snes_setPixelFormat(game->snes, pixelFormatXRGB);
  if(!snes_loadRom(game->snes, rom->data, (int) rom->size)) {
    tg_game_destroy(game);
    return NULL;
  }
  return game;
}

void tg_game_destroy(TgGame* game) {
  if(game == NULL) return;
  if(game->snes) snes_free(game->snes);
  free(game);
}

void tg_game_reset(TgGame* game, bool hard) {
  snes_reset(game->snes, hard);
}

void tg_game_set_input(TgGame* game, int player, uint16_t buttons) {
  for(int i = 0; i < 12; i++) snes_setButtonState(game->snes, player, i, (buttons >> i) & 1);
}

void tg_game_run_frame(TgGame* game) {
  snes_runFrame(game->snes);
}

void tg_game_get_pixels(TgGame* game, uint8_t* pixels) {
  snes_setPixels(game->snes, pixels);
}

void tg_game_get_samples(TgGame* game, int16_t* samples, int count) {
  snes_setSamples(game->snes, samples, count);
}

const uint8_t* tg_game_wram(const TgGame* game) {
  return game->snes->ram;
}

uint32_t tg_game_frame_count(const TgGame* game) {
  return game->snes->frames;
}
