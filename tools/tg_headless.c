// Runs the game without a window, for tests and investigation.
//
//   tg_headless --rom <file> [--frames N] [--input script.txt]
//               [--screenshot out.ppm] [--shots-every N --shots-dir DIR [--shots-from F]]
//               [--expect-wram HEX] [--expect-frame HEX] [--coverage FILE] [--dump-wram FILE]
//               [--trace-frame N [--trace-count K] --trace-out FILE] [--port off|verify|native]
//               [--frame-hashes FILE] [--race-frames N] [--ghosts FILE]
//
// Prints the SHA-256 of work RAM and of the final frame so runs can be compared.
// With --expect-*, exits non-zero when a hash differs (used by the regression tests).
// With --coverage, adds this run's code/data coverage to FILE (created if missing).
// With --port verify, every call to a translated routine is checked against the original
// (see src/lockstep.h); any mismatch makes the run fail.
// With --frame-hashes, writes a 64-bit hash of every frame's picture, one line per frame,
// for comparing runs whose timing differs (tools/shift_compare.py).
// --race-frames sets the video frames per race update when the race loop runs in C
// (native mode); the default is 3, the original's pace without its slowdown.
// --ghosts loads ghost-car races from FILE and saves them back if a better race is driven.

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "coverage.h"
#include "game.h"
#include "port/port.h"
#include "input_script.h"
#include "rom.h"
#include "sha256.h"

static TgGame* g_game;

static void writePpm(const char* path, const uint8_t* xrgb) {
  // Writes the visible 256x224 picture (every other pixel of the 512x448 area), or the
  // wide picture in widescreen.
  const uint32_t* wide = tg_game_wide_pixels(g_game);
  int w = 256 + 2 * tg_game_wide_margin(g_game);
  FILE* f = fopen(path, "wb");
  if(f == NULL) {
    fprintf(stderr, "cannot write %s\n", path);
    return;
  }
  if(wide) {
    fprintf(f, "P6\n%d 224\n255\n", w);
    for(int i = 0; i < w * 224; i++) {
      uint8_t rgb[3] = {(uint8_t) (wide[i] >> 16), (uint8_t) (wide[i] >> 8), (uint8_t) wide[i]};
      fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    return;
  }
  fprintf(f, "P6\n256 224\n255\n");
  for(int y = 0; y < 224; y++) {
    for(int x = 0; x < 256; x++) {
      const uint8_t* p = xrgb + ((16 + y * 2) * TG_FB_WIDTH + x * 2) * 4;
      uint8_t rgb[3] = {p[2], p[1], p[0]};
      fwrite(rgb, 1, 3, f);
    }
  }
  fclose(f);
}

// Prints the hash; returns false if expected is set and differs.
static bool checkHash(const char* label, const uint8_t* data, size_t len, const char* expected) {
  uint8_t d[32];
  char hex[65];
  tg_sha256(data, len, d);
  for(int i = 0; i < 32; i++) sprintf(hex + i * 2, "%02x", d[i]);
  bool ok = expected == NULL || strcmp(hex, expected) == 0;
  printf("%s %s%s\n", label, hex, ok ? "" : "  MISMATCH");
  if(!ok) printf("%s expected %s\n", label, expected);
  return ok;
}

int main(int argc, char** argv) {
  const char* romPath = NULL;
  const char* inputPath = NULL;
  const char* screenshot = NULL;
  const char* shotsDir = NULL;
  const char* expectWram = NULL;
  const char* expectFrame = NULL;
  const char* coveragePath = NULL;
  const char* dumpWram = NULL;
  const char* frameHashes = NULL;
  const char* ghostsPath = NULL;
  const char* traceOut = NULL;
  long traceFrame = -1;
  long traceCount = 1;
  FILE* trace = NULL;
  long frames = 600;
  long shotsEvery = 0;
  long shotsFrom = 0;
  const char* dumpPpu = NULL;
  TgPortMode portMode = TG_PORT_OFF;
  for(int i = 1; i < argc; i++) {
    const char* next = i + 1 < argc ? argv[i + 1] : NULL;
    if(strcmp(argv[i], "--rom") == 0 && next) romPath = argv[++i];
    else if(strcmp(argv[i], "--frames") == 0 && next) frames = strtol(argv[++i], NULL, 10);
    else if(strcmp(argv[i], "--input") == 0 && next) inputPath = argv[++i];
    else if(strcmp(argv[i], "--screenshot") == 0 && next) screenshot = argv[++i];
    else if(strcmp(argv[i], "--shots-every") == 0 && next) shotsEvery = strtol(argv[++i], NULL, 10);
    else if(strcmp(argv[i], "--shots-dir") == 0 && next) shotsDir = argv[++i];
    else if(strcmp(argv[i], "--dump-ppu") == 0 && next) dumpPpu = argv[++i];
    else if(strcmp(argv[i], "--shots-from") == 0 && next) shotsFrom = strtol(argv[++i], NULL, 10);
    else if(strcmp(argv[i], "--expect-wram") == 0 && next) expectWram = argv[++i];
    else if(strcmp(argv[i], "--expect-frame") == 0 && next) expectFrame = argv[++i];
    else if(strcmp(argv[i], "--coverage") == 0 && next) coveragePath = argv[++i];
    else if(strcmp(argv[i], "--dump-wram") == 0 && next) dumpWram = argv[++i];
    else if(strcmp(argv[i], "--frame-hashes") == 0 && next) frameHashes = argv[++i];
    else if(strcmp(argv[i], "--ghosts") == 0 && next) ghostsPath = argv[++i];
    else if(strcmp(argv[i], "--race-frames") == 0 && next) tg_port_race_frames = (int) strtol(argv[++i], NULL, 10);
    else if(strcmp(argv[i], "--trace-frame") == 0 && next) traceFrame = strtol(argv[++i], NULL, 10);
    else if(strcmp(argv[i], "--trace-out") == 0 && next) traceOut = argv[++i];
    else if(strcmp(argv[i], "--trace-count") == 0 && next) traceCount = strtol(argv[++i], NULL, 10);
    else if(strcmp(argv[i], "--port") == 0 && next && strcmp(next, "off") == 0) { portMode = TG_PORT_OFF; i++; }
    else if(strcmp(argv[i], "--port") == 0 && next && strcmp(next, "verify") == 0) { portMode = TG_PORT_VERIFY; i++; }
    else if(strcmp(argv[i], "--port") == 0 && next && strcmp(next, "native") == 0) { portMode = TG_PORT_NATIVE; i++; }
    else {
      fprintf(stderr, "usage: %s --rom <file> [--frames N] [--input script] [--screenshot out.ppm] [--shots-every N --shots-dir DIR [--shots-from F]] [--expect-wram HEX] [--expect-frame HEX] [--coverage FILE] [--dump-wram FILE] [--trace-frame N [--trace-count K] --trace-out FILE] [--port off|verify|native] [--frame-hashes FILE] [--race-frames N] [--ghosts FILE]\n", argv[0]);
      return 2;
    }
  }
  if(romPath == NULL) {
    fprintf(stderr, "--rom is required\n");
    return 2;
  }

  TgRom rom;
  TgRomStatus status = tg_rom_load(romPath, &rom);
  if(status != TG_ROM_OK) {
    fprintf(stderr, "%s: %s\n", romPath, tg_rom_status_message(status));
    return 1;
  }
  TgInputScript script = {0};
  if(inputPath) {
    char error[256];
    if(!tg_input_script_load(inputPath, &script, error, sizeof(error))) {
      fprintf(stderr, "%s\n", error);
      return 1;
    }
  }

  TgGame* game = tg_game_create(&rom);

  g_game = game;
  if(ghostsPath != NULL) tg_game_load_ghosts(game, ghostsPath);
  if(game == NULL) {
    fprintf(stderr, "failed to start game\n");
    return 1;
  }
  TgCoverage* coverage = NULL;
  if(coveragePath) {
    coverage = malloc(sizeof(TgCoverage));
    tg_coverage_load(coverage, coveragePath);
    tg_game_set_coverage(game, coverage);
  }
  tg_game_set_port_mode(game, portMode);
  int raceFrames = tg_port_race_frames;
  uint8_t* pixels = calloc(TG_FB_WIDTH * TG_FB_HEIGHT, 4);
  FILE* hashes = frameHashes ? fopen(frameHashes, "w") : NULL;
  for(long frame = 0; frame < frames; frame++) {
    tg_input_script_advance(&script, (uint32_t) frame);
    if(script.resetPending) {
      tg_game_reset(game, false);
      script.resetPending = false;
    }
    if(script.unlockPending) {
      uint8_t open[3] = {script.unlock & 15, (script.unlock >> 4) & 15, (script.unlock >> 8) & 15};
      tg_game_unlock(game, open);
      script.unlockPending = false;
    }
    tg_port_race_frames = script.smooth ? 1 : raceFrames;
    if(script.scaling != tg_port_smooth_sprites) tg_game_set_smooth_sprites(game, script.scaling);
    if(script.fullview != tg_port_full_view) tg_game_set_full_view(game, script.fullview);
    tg_game_set_tall_road(game, script.tallRoad);
    if(script.wide != tg_port_wide_margin) tg_game_set_wide(game, script.wide);
    if(script.sideBySide != tg_port_side_by_side) tg_game_set_side_by_side(game, script.sideBySide);
    if(script.ghost != tg_port_ghost) tg_game_set_ghost(game, script.ghost);
    tg_game_set_input(game, 1, script.state[1]);
    tg_game_set_input(game, 2, script.state[2]);
    if(frame == traceFrame && traceOut) {
      trace = fopen(traceOut, "w");
      tg_game_set_trace(game, trace);
    }
    if(trace) fprintf(trace, "# frame %ld\n", frame);
    tg_game_run_frame(game);
    if(trace && frame == traceFrame + traceCount - 1) {
      tg_game_set_trace(game, NULL);
      fclose(trace);
      trace = NULL;
    }
    if(hashes) {
      tg_game_get_pixels(game, pixels);
      uint64_t h = 0xcbf29ce484222325ull;  // FNV-1a
      for(size_t j = 0; j < (size_t) TG_FB_WIDTH * TG_FB_HEIGHT * 4; j++) h = (h ^ pixels[j]) * 0x100000001b3ull;
      fprintf(hashes, "%016llx\n", (unsigned long long) h);
    }
    if(shotsEvery > 0 && shotsDir && frame + 1 >= shotsFrom && (frame + 1) % shotsEvery == 0) {
      char path[1024];
      snprintf(path, sizeof(path), "%s/frame%06ld.ppm", shotsDir, frame + 1);
      tg_game_get_pixels(game, pixels);
      writePpm(path, pixels);
    }
  }
  if(hashes) fclose(hashes);
  tg_game_get_pixels(game, pixels);
  if(screenshot) writePpm(screenshot, pixels);
  if(dumpPpu) tg_game_dump_ppu(game, dumpPpu);
  if(dumpWram) {
    FILE* f = fopen(dumpWram, "wb");
    if(f == NULL || fwrite(tg_game_wram(game), 1, 0x20000, f) != 0x20000) fprintf(stderr, "cannot write %s\n", dumpWram);
    if(f) fclose(f);
  }

  printf("frames %ld\n", frames);
  bool ok = checkHash("wram", tg_game_wram(game), 0x20000, expectWram);
  ok &= checkHash("frame", pixels, (size_t) TG_FB_WIDTH * TG_FB_HEIGHT * 4, expectFrame);
  if(portMode != TG_PORT_OFF && tg_game_port_report(game, stdout) > 0) ok = false;

  if(coverage) {
    if(!tg_coverage_save(coverage, coveragePath)) {
      fprintf(stderr, "cannot write %s\n", coveragePath);
      ok = false;
    }
    free(coverage);
  }
  free(pixels);
  if(ghostsPath != NULL && tg_game_ghosts_changed(game)) tg_game_save_ghosts(game, ghostsPath);
  tg_game_destroy(game);
  tg_input_script_free(&script);
  tg_rom_free(&rom);
  return ok ? 0 : 1;
}
