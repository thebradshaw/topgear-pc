#ifndef TG_LOCKSTEP_H
#define TG_LOCKSTEP_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

// Runs translated routines (src/port) in place of the original code they replace.
//
// Off: everything is emulated. Native: each translated routine runs instead of the
// original. Verify: each call runs both from the same saved machine state, the original
// first, and the results are compared: CPU registers, work RAM, VRAM, CGRAM, OAM and the
// order of writes to PPU and CPU registers. HDMA writes made while the original ran are
// replayed after the C version, since they come from the clock, not from the routine.
// The game always continues from the original's result, so verify mode never changes
// what the game does, and the recording tests' hashes still apply.
//
// A call is only compared when the original returns within the same frame and without
// being interrupted; otherwise it counts as skipped. The C version runs in zero emulated
// cycles, so native mode shifts the timing of everything after it until the frame loop
// itself is translated.

typedef enum { TG_PORT_OFF, TG_PORT_VERIFY, TG_PORT_NATIVE } TgPortMode;

typedef struct Snes Snes;
typedef struct Cpu Cpu;
typedef struct TgLockstep TgLockstep;

TgLockstep* tg_lockstep_create(Snes* snes, const uint8_t* rom, TgPortMode mode);
void tg_lockstep_destroy(TgLockstep* ls);
TgPortMode tg_lockstep_mode(const TgLockstep* ls);

// From the CPU's call hook: true when a translated routine handled the call.
bool tg_lockstep_on_opcode(TgLockstep* ls, Cpu* cpu);
// From the bus write hooks.
void tg_lockstep_on_write(TgLockstep* ls, uint32_t adr, uint8_t val);
void tg_lockstep_on_hdma_write(TgLockstep* ls, uint8_t adr, uint8_t val);
// True while C code is running, so tracing and coverage can ignore its bus accesses.
bool tg_lockstep_in_c(const TgLockstep* ls);

// Prints per-routine counts and details of mismatches; returns the number of mismatches.
int tg_lockstep_report(const TgLockstep* ls, FILE* f);

#endif
