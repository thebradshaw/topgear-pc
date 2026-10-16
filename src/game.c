#include "game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "coverage.h"
#include "lockstep.h"
#include "snes.h"
#include "port/port.h"

struct TgGame {
  Snes* snes;
  uint8_t rom[TG_ROM_SIZE];
  TgCoverage* coverage;
  FILE* trace;
  TgLockstep* lockstep;
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
  tg_lockstep_destroy(game->lockstep);
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
  if(game->coverage) tg_coverage_on_opcode(game->coverage, adr, bytes, cpu->mf, cpu->xf, cpu->e, cpu->db, cpu->dp);
  if(game->trace) {
    int len = tg_w65816_length(bytes[0], cpu->mf, cpu->xf);
    fprintf(game->trace, "%06X ", adr);
    for(int i = 0; i < 4; i++) fprintf(game->trace, i < len ? "%02X" : "  ", bytes[i]);
    fprintf(game->trace, " A=%04X X=%04X Y=%04X S=%04X D=%04X DB=%02X %c%c V=%d\n", cpu->a, cpu->x, cpu->y,
      cpu->sp, cpu->dp, cpu->db, cpu->mf ? 'm' : 'M', cpu->xf ? 'x' : 'X', game->snes->vPos);
  }
}

static void traceRead(void* ctx, uint32_t adr, uint8_t val) {
  TgGame* game = ctx;
  if(game->lockstep) tg_lockstep_on_read(game->lockstep, adr, val);
  if(game->lockstep && tg_lockstep_in_c(game->lockstep)) return;
  if(game->coverage) tg_coverage_on_read(game->coverage, adr);
}

static void traceWrite(void* ctx, uint32_t adr, uint8_t val) {
  TgGame* game = ctx;
  if(game->lockstep) tg_lockstep_on_write(game->lockstep, adr, val);
  if(game->trace) fprintf(game->trace, "  W %06X=%02X\n", adr, val);
}

static void hdmaWrite(void* ctx, uint8_t adr, uint8_t val) {
  TgGame* game = ctx;
  tg_lockstep_on_hdma_write(game->lockstep, adr, val);
}

static bool portCall(void* ctx, Cpu* cpu) {
  TgGame* game = ctx;
  return tg_lockstep_on_opcode(game->lockstep, cpu);
}

static void updateHooks(TgGame* game) {
  bool any = game->coverage || game->trace;
  game->snes->cpu->traceHook = any ? traceOpcode : NULL;
  game->snes->cpu->traceCtx = game;
  game->snes->readHook = game->coverage || game->lockstep ? traceRead : NULL;
  game->snes->writeHook = game->trace || game->lockstep ? traceWrite : NULL;
  game->snes->readHookCtx = game;
  game->snes->cpu->callHook = game->lockstep ? portCall : NULL;
  game->snes->hdmaWriteHook = game->lockstep ? hdmaWrite : NULL;
  game->snes->cpu->callCtx = game;
}

void tg_game_set_port_mode(TgGame* game, TgPortMode mode) {
  tg_lockstep_destroy(game->lockstep);
  game->lockstep = mode == TG_PORT_OFF ? NULL : tg_lockstep_create(game->snes, game->rom, mode);
  updateHooks(game);
}

int tg_game_port_report(TgGame* game, FILE* f) {
  return game->lockstep ? tg_lockstep_report(game->lockstep, f) : 0;
}

void tg_game_dump_ppu(TgGame* game, const char* path) {
  FILE* f = fopen(path, "wb");
  if(f == NULL) return;
  Ppu* ppu = game->snes->ppu;
  fwrite(ppu->vram, sizeof(ppu->vram), 1, f);
  fwrite(ppu->cgram, sizeof(ppu->cgram), 1, f);
  fwrite(ppu->oam, sizeof(ppu->oam), 1, f);
  fwrite(ppu->highOam, sizeof(ppu->highOam), 1, f);
  fclose(f);
}

void tg_game_set_smooth_sprites(TgGame* game, bool on) {
  extern PpuObjScale tg_port_obj_scales[];
  tg_port_smooth_sprites = on;
  game->snes->ppu->objScaleTags = on ? tg_port_obj_tags : NULL;
  game->snes->ppu->objScales = tg_port_obj_scales;
}

static void updateRedraw(TgGame* game) {
  bool on = tg_port_full_view || tg_port_wide_margin > 0;
  tg_port_race_frame = 0;
  game->snes->ppu->objGrounds = on ? tg_port_obj_grounds : NULL;
  game->snes->ppu->raceFrame = on ? &tg_port_race_frame : NULL;
  game->snes->ppu->wideMargin = tg_port_wide_margin;
}

void tg_game_set_full_view(TgGame* game, bool on) {
  tg_port_full_view = on;
  updateRedraw(game);
}

void tg_game_set_ghost(TgGame* game, bool on) {
  tg_port_ghost = on;
  game->snes->ppu->objFlags = on ? tg_port_obj_flags : NULL;
}

bool tg_game_ghosts_changed(TgGame* game) {
  (void) game;
  bool c = tg_port_ghosts_changed;
  tg_port_ghosts_changed = false;
  return c;
}

// File: "TGGHOST1", then per course with a lap: course byte, lap time word, sample count
// word, samples (4 little-endian words each).
bool tg_game_save_ghosts(TgGame* game, const char* path) {
  (void) game;
  FILE* f = fopen(path, "wb");
  if(f == NULL) return false;
  fwrite("TGGHOST1", 1, 8, f);
  for(int c = 0; c < TG_GHOST_COURSES; c++) {
    const TgGhostLap* l = &tg_port_ghosts[c];
    if(l->count == 0) continue;
    uint8_t h[5] = {(uint8_t) c, (uint8_t) l->time, (uint8_t) (l->time >> 8), (uint8_t) l->count, (uint8_t) (l->count >> 8)};
    fwrite(h, 1, 5, f);
    for(int i = 0; i < l->count; i++) {
      const TgGhostSample* s = &l->samples[i];
      uint16_t w[4] = {s->time, s->pos, s->side, s->kind};
      for(int k = 0; k < 4; k++) {
        uint8_t b[2] = {(uint8_t) w[k], (uint8_t) (w[k] >> 8)};
        fwrite(b, 1, 2, f);
      }
    }
  }
  return fclose(f) == 0;
}

bool tg_game_load_ghosts(TgGame* game, const char* path) {
  (void) game;
  FILE* f = fopen(path, "rb");
  if(f == NULL) return false;
  char magic[8];
  bool ok = fread(magic, 1, 8, f) == 8 && memcmp(magic, "TGGHOST1", 8) == 0;
  uint8_t h[5];
  while(ok && fread(h, 1, 5, f) == 5) {
    int c = h[0], count = h[3] | h[4] << 8;
    if(c >= TG_GHOST_COURSES || count > TG_GHOST_SAMPLES) { ok = false; break; }
    TgGhostLap* l = &tg_port_ghosts[c];
    l->time = (uint16_t) (h[1] | h[2] << 8);
    for(int i = 0; i < count && ok; i++) {
      uint8_t b[8];
      ok = fread(b, 1, 8, f) == 8;
      l->samples[i] = (TgGhostSample) {(uint16_t) (b[0] | b[1] << 8), (uint16_t) (b[2] | b[3] << 8),
                                       (uint16_t) (b[4] | b[5] << 8), (uint16_t) (b[6] | b[7] << 8)};
    }
    l->count = ok ? count : 0;
  }
  fclose(f);
  return ok;
}

void tg_game_set_side_by_side(TgGame* game, bool on) {
  (void) game;
  tg_port_side_by_side = on;
}

void tg_game_set_wide(TgGame* game, int margin) {
  if(margin < 0) margin = 0;
  if(margin > PPU_WIDE_MAX) margin = PPU_WIDE_MAX;
  tg_port_wide_margin = margin;
  updateRedraw(game);
}

int tg_game_wide_margin(const TgGame* game) {
  (void) game;
  return tg_port_wide_margin;
}

const uint32_t* tg_game_wide_pixels(TgGame* game) {
  return tg_port_wide_margin > 0 ? game->snes->ppu->wideBuffer : NULL;
}

void tg_game_set_coverage(TgGame* game, TgCoverage* cov) {
  game->coverage = cov;
  updateHooks(game);
}

void tg_game_set_trace(TgGame* game, void* file) {
  game->trace = file;
  updateHooks(game);
}

const uint8_t* tg_game_wram(const TgGame* game) {
  return game->snes->ram;
}

uint32_t tg_game_frame_count(const TgGame* game) {
  return game->snes->frames;
}
