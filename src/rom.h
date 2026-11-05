#ifndef TG_ROM_H
#define TG_ROM_H

#include <stddef.h>
#include <stdint.h>

// The supported dumps, all USA, without a copier header. Top Gear is the game this port
// runs; Top Gear 2 and Top Gear 3000 only run emulated so far (tg_rom_load_any).
#define TG_ROM_SIZE 0x80000
#define TG_ROM_SHA256 "ca9889f17f184b3d99a2eaaa82af73e366f03ed00313fdd369e5e023b208e788"
#define TG_ROM_MAX 0x100000  // the largest of the three

typedef enum {
  TG_GAME_TOPGEAR = 0,
  TG_GAME_TOPGEAR2,
  TG_GAME_TOPGEAR3000,
  TG_GAME_COUNT,
} TgGameId;

typedef struct {
  const char* name;    // "Top Gear 2"
  const char* sha256;  // of the dump without a copier header
  size_t size;
} TgGameInfo;

extern const TgGameInfo tg_games[TG_GAME_COUNT];

typedef enum {
  TG_ROM_OK = 0,
  TG_ROM_NOT_FOUND,
  TG_ROM_READ_ERROR,
  TG_ROM_WRONG_SIZE,
  TG_ROM_WRONG_HASH,
} TgRomStatus;

typedef struct {
  uint8_t* data;  // TG_ROM_SIZE bytes, copier header removed
  size_t size;
  char sha256[65];  // hex digest of data, filled whenever the file was readable
  TgGameId game;    // which game, on success
} TgRom;

// Loads and verifies the ROM at path. On success rom->data must be freed with tg_rom_free.
TgRomStatus tg_rom_load(const char* path, TgRom* rom);
// The same for any of the three games; rom->game says which.
TgRomStatus tg_rom_load_any(const char* path, TgRom* rom);
void tg_rom_free(TgRom* rom);
const char* tg_rom_status_message(TgRomStatus status);

#endif
