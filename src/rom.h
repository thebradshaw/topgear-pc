#ifndef TG_ROM_H
#define TG_ROM_H

#include <stddef.h>
#include <stdint.h>

// The only supported dump: Top Gear (USA), 524,288 bytes, no copier header.
#define TG_ROM_SIZE 0x80000
#define TG_ROM_SHA256 "ca9889f17f184b3d99a2eaaa82af73e366f03ed00313fdd369e5e023b208e788"

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
} TgRom;

// Loads and verifies the ROM at path. On success rom->data must be freed with tg_rom_free.
TgRomStatus tg_rom_load(const char* path, TgRom* rom);
void tg_rom_free(TgRom* rom);
const char* tg_rom_status_message(TgRomStatus status);

#endif
