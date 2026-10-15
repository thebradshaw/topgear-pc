#include "game.h"

#include <stdlib.h>
#include <string.h>

#include "coverage.h"
#include "snes.h"

struct TgGame {
  Snes* snes;
  uint8_t rom[TG_ROM_SIZE];
  TgCoverage* coverage;
};

TgGame* tg_game_create(const TgRom* rom) {
  TgGame* game = calloc(1, sizeof(TgGame));
  if(game == NULL) return NULL;
  game->snes = snes_init();
  snes_setPixelFormat(game->snes, pixelFormatXRGB);
  memcpy(game->rom, rom->data, TG_ROM_SIZE);
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

static uint8_t peek(TgGame* game, uint32_t adr) {
  int32_t off = tg_lorom_offset(adr);
  if(off >= 0) return game->rom[off];
  uint8_t bank = (uint8_t) (adr >> 16);
  if(bank == 0x7e || bank == 0x7f) return game->snes->ram[adr & 0x1ffff];
  if((bank & 0x40) == 0 && (adr & 0xffff) < 0x2000) return game->snes->ram[adr & 0x1fff];
  return 0;
}

static void traceOpcode(void* ctx, Cpu* cpu) {
  TgGame* game = ctx;
  uint32_t adr = ((uint32_t) cpu->k << 16) | cpu->pc;
  uint8_t bytes[4];
  for(int i = 0; i < 4; i++) bytes[i] = peek(game, (adr & 0xff0000) | ((adr + i) & 0xffff));
  tg_coverage_on_opcode(game->coverage, adr, bytes, cpu->mf, cpu->xf, cpu->e, cpu->db, cpu->dp);
}

static void traceRead(void* ctx, uint32_t adr) {
  TgGame* game = ctx;
  tg_coverage_on_read(game->coverage, adr);
}

void tg_game_set_coverage(TgGame* game, TgCoverage* cov) {
  game->coverage = cov;
  game->snes->cpu->traceHook = cov ? traceOpcode : NULL;
  game->snes->cpu->traceCtx = game;
  game->snes->readHook = cov ? traceRead : NULL;
  game->snes->readHookCtx = game;
}

const uint8_t* tg_game_wram(const TgGame* game) {
  return game->snes->ram;
}

uint32_t tg_game_frame_count(const TgGame* game) {
  return game->snes->frames;
}
