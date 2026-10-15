#ifndef TG_COVERAGE_H
#define TG_COVERAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "rom.h"

// Records which ROM bytes the original game executes or reads, and the CPU state
// (register widths, data bank, direct page) at each executed instruction. The
// disassembler uses this to separate code from data. Files accumulate across runs.

enum {
  // flags[]
  TG_COV_OPCODE = 1 << 0,   // first byte of an executed instruction
  TG_COV_OPERAND = 1 << 1,  // operand byte of an executed instruction
  TG_COV_READ = 1 << 2,     // read as data (CPU or DMA)
  TG_COV_M16X16 = 1 << 3,   // executed with these register widths (m = accumulator, x = index)
  TG_COV_M16X8 = 1 << 4,
  TG_COV_M8X16 = 1 << 5,
  TG_COV_M8X8 = 1 << 6,
  TG_COV_EMU = 1 << 7,      // executed in 6502 emulation mode
  // entry[]
  TG_COV_FLOW_TARGET = 1 << 0,  // reached by a jump, branch, call, return or interrupt
  TG_COV_CALL_TARGET = 1 << 1,  // reached by JSR/JSL
  TG_COV_DB_VARIES = 1 << 2,    // seen with more than one data bank
  TG_COV_DP_VARIES = 1 << 3,    // seen with more than one direct page
};

#define TG_COV_RAM_SIZE 0x20000

struct TgCoverage {
  uint8_t flags[TG_ROM_SIZE];
  uint8_t entry[TG_ROM_SIZE];
  uint8_t db[TG_ROM_SIZE];
  uint16_t dp[TG_ROM_SIZE];
  uint8_t ramFlags[TG_COV_RAM_SIZE];  // same bits as flags[], for code run from work RAM
  // tracking state, not saved
  uint32_t prevNext;  // address following the previous instruction
  uint8_t prevOpcode;
};
typedef struct TgCoverage TgCoverage;

void tg_coverage_init(TgCoverage* cov);
bool tg_coverage_load(TgCoverage* cov, const char* path);  // false if missing or invalid
bool tg_coverage_save(const TgCoverage* cov, const char* path);

// Hooks for the emulator; see game.c.
void tg_coverage_on_opcode(TgCoverage* cov, uint32_t adr, const uint8_t* bytes, bool m8, bool x8, bool emu, uint8_t db, uint16_t dp);
void tg_coverage_on_read(TgCoverage* cov, uint32_t adr);

// Length in bytes of a 65816 instruction given its opcode and register widths.
int tg_w65816_length(uint8_t opcode, bool m8, bool x8);
// LoROM CPU address to ROM offset, or -1.
int32_t tg_lorom_offset(uint32_t adr);

#endif
