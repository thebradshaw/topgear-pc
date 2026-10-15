// Runs the game without a window, for tests and investigation.
//
//   tg_headless --rom <file> [--frames N] [--input script.txt]
//               [--screenshot out.ppm] [--shots-every N --shots-dir DIR]
//               [--expect-wram HEX] [--expect-frame HEX]
//
// Prints the SHA-256 of work RAM and of the final frame so runs can be compared.
// With --expect-*, exits non-zero when a hash differs (used by the regression tests).

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "input_script.h"
#include "rom.h"
#include "sha256.h"

static void writePpm(const char* path, const uint8_t* xrgb) {
  // Writes the visible 256x224 picture (every other pixel of the 512x448 area).
  FILE* f = fopen(path, "wb");
  if(f == NULL) {
    fprintf(stderr, "cannot write %s\n", path);
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
  long frames = 600;
  long shotsEvery = 0;
  for(int i = 1; i < argc; i++) {
    const char* next = i + 1 < argc ? argv[i + 1] : NULL;
    if(strcmp(argv[i], "--rom") == 0 && next) romPath = argv[++i];
    else if(strcmp(argv[i], "--frames") == 0 && next) frames = strtol(argv[++i], NULL, 10);
    else if(strcmp(argv[i], "--input") == 0 && next) inputPath = argv[++i];
    else if(strcmp(argv[i], "--screenshot") == 0 && next) screenshot = argv[++i];
    else if(strcmp(argv[i], "--shots-every") == 0 && next) shotsEvery = strtol(argv[++i], NULL, 10);
    else if(strcmp(argv[i], "--shots-dir") == 0 && next) shotsDir = argv[++i];
    else if(strcmp(argv[i], "--expect-wram") == 0 && next) expectWram = argv[++i];
    else if(strcmp(argv[i], "--expect-frame") == 0 && next) expectFrame = argv[++i];
    else {
      fprintf(stderr, "usage: %s --rom <file> [--frames N] [--input script] [--screenshot out.ppm] [--shots-every N --shots-dir DIR] [--expect-wram HEX] [--expect-frame HEX]\n", argv[0]);
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
  if(game == NULL) {
    fprintf(stderr, "failed to start game\n");
    return 1;
  }
  uint8_t* pixels = calloc(TG_FB_WIDTH * TG_FB_HEIGHT, 4);
  for(long frame = 0; frame < frames; frame++) {
    tg_input_script_advance(&script, (uint32_t) frame);
    tg_game_set_input(game, 1, script.state[1]);
    tg_game_set_input(game, 2, script.state[2]);
    tg_game_run_frame(game);
    if(shotsEvery > 0 && shotsDir && (frame + 1) % shotsEvery == 0) {
      char path[1024];
      snprintf(path, sizeof(path), "%s/frame%06ld.ppm", shotsDir, frame + 1);
      tg_game_get_pixels(game, pixels);
      writePpm(path, pixels);
    }
  }
  tg_game_get_pixels(game, pixels);
  if(screenshot) writePpm(screenshot, pixels);

  printf("frames %ld\n", frames);
  bool ok = checkHash("wram", tg_game_wram(game), 0x20000, expectWram);
  ok &= checkHash("frame", pixels, (size_t) TG_FB_WIDTH * TG_FB_HEIGHT * 4, expectFrame);

  free(pixels);
  tg_game_destroy(game);
  tg_input_script_free(&script);
  tg_rom_free(&rom);
  return ok ? 0 : 1;
}
