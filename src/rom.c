#include "rom.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sha256.h"

const TgGameInfo tg_games[TG_GAME_COUNT] = {
  { "Top Gear", TG_ROM_SHA256, TG_ROM_SIZE },
  { "Top Gear 2", "76b2702c4be8b668c1017f2817c280283c275eaa41535bf6ffa2b8d2220b68c6", 0x100000 },
  { "Top Gear 3000", "6be49983976564f1fd9eff2f14f5bb41d3a0ff48573e39318088ecce286aca62", 0x100000 },
};

static TgRomStatus load(const char* path, TgRom* rom, bool any) {
  memset(rom, 0, sizeof(*rom));
  FILE* f = fopen(path, "rb");
  if(f == NULL) return TG_ROM_NOT_FOUND;
  fseek(f, 0, SEEK_END);
  long length = ftell(f);
  fseek(f, 0, SEEK_SET);
  if(length <= 0 || length > 16 * 1024 * 1024) {
    fclose(f);
    return TG_ROM_WRONG_SIZE;
  }
  uint8_t* data = malloc((size_t) length);
  if(data == NULL || fread(data, 1, (size_t) length, f) != (size_t) length) {
    free(data);
    fclose(f);
    return TG_ROM_READ_ERROR;
  }
  fclose(f);

  // Old copier dumps carry a 512-byte header in front of the ROM.
  size_t offset = (length % 1024 == 512) ? 512 : 0;
  size_t size = (size_t) length - offset;
  if(offset) memmove(data, data + offset, size);

  uint8_t digest[32];
  tg_sha256(data, size, digest);
  for(int i = 0; i < 32; i++) sprintf(rom->sha256 + i * 2, "%02x", digest[i]);

  int game = -1;
  for(int g = 0; g < (any ? TG_GAME_COUNT : 1); g++) {
    if(size == tg_games[g].size && strcmp(rom->sha256, tg_games[g].sha256) == 0) game = g;
  }
  if(game < 0) {
    bool sized = false;
    for(int g = 0; g < (any ? TG_GAME_COUNT : 1); g++) sized |= size == tg_games[g].size;
    free(data);
    return sized ? TG_ROM_WRONG_HASH : TG_ROM_WRONG_SIZE;
  }
  rom->game = (TgGameId) game;
  rom->data = data;
  rom->size = size;
  return TG_ROM_OK;
}

TgRomStatus tg_rom_load(const char* path, TgRom* rom) { return load(path, rom, false); }
TgRomStatus tg_rom_load_any(const char* path, TgRom* rom) { return load(path, rom, true); }

void tg_rom_free(TgRom* rom) {
  free(rom->data);
  rom->data = NULL;
  rom->size = 0;
}

const char* tg_rom_status_message(TgRomStatus status) {
  switch(status) {
    case TG_ROM_OK: return "OK";
    case TG_ROM_NOT_FOUND: return "ROM file not found.";
    case TG_ROM_READ_ERROR: return "Could not read the ROM file.";
    case TG_ROM_WRONG_SIZE: return "This is not a Top Gear (USA) ROM: wrong size.";
    case TG_ROM_WRONG_HASH: return "This is not an unmodified Top Gear (USA) ROM: SHA-256 does not match.";
  }
  return "Unknown error.";
}
