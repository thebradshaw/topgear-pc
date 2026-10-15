// Core tests. Without arguments they need no ROM; with --rom they boot the game.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "input_script.h"
#include "rom.h"
#include "sha256.h"

static int failures = 0;

#define CHECK(cond) do { \
  if(!(cond)) { fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while(0)

static void hex(const uint8_t* d, char* out) {
  for(int i = 0; i < 32; i++) sprintf(out + i * 2, "%02x", d[i]);
}

static void testSha256(void) {
  uint8_t d[32];
  char h[65];
  tg_sha256((const uint8_t*) "", 0, d);
  hex(d, h);
  CHECK(strcmp(h, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0);
  tg_sha256((const uint8_t*) "abc", 3, d);
  hex(d, h);
  CHECK(strcmp(h, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
  const char* s = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  tg_sha256((const uint8_t*) s, strlen(s), d);
  hex(d, h);
  CHECK(strcmp(h, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1") == 0);
}

static void testButtons(void) {
  uint16_t b;
  CHECK(tg_parse_buttons("-", &b) && b == 0);
  CHECK(tg_parse_buttons("start", &b) && b == TG_BTN_START);
  CHECK(tg_parse_buttons("B+RIGHT+R", &b) && b == (TG_BTN_B | TG_BTN_RIGHT | TG_BTN_R));
  CHECK(!tg_parse_buttons("TURBO", &b));
}

static void testBoot(const char* romPath) {
  TgRom rom;
  TgRomStatus status = tg_rom_load(romPath, &rom);
  CHECK(status == TG_ROM_OK);
  if(status != TG_ROM_OK) return;
  TgGame* game = tg_game_create(&rom);
  CHECK(game != NULL);
  if(game == NULL) return;
  for(int i = 0; i < 300; i++) tg_game_run_frame(game);
  CHECK(tg_game_frame_count(game) >= 300);
  uint8_t* pixels = calloc(TG_FB_WIDTH * TG_FB_HEIGHT, 4);
  tg_game_get_pixels(game, pixels);
  int lit = 0;
  for(int i = 0; i < TG_FB_WIDTH * TG_FB_HEIGHT; i++) lit += (pixels[i * 4] | pixels[i * 4 + 1] | pixels[i * 4 + 2]) != 0;
  CHECK(lit > 1000);  // something is on screen after 5 seconds
  free(pixels);
  tg_game_destroy(game);
  tg_rom_free(&rom);
}

int main(int argc, char** argv) {
  testSha256();
  testButtons();
  if(argc == 3 && strcmp(argv[1], "--rom") == 0) testBoot(argv[2]);
  if(failures) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  printf("all checks passed\n");
  return 0;
}
