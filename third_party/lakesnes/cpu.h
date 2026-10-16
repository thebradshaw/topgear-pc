
#ifndef CPU_H
#define CPU_H

#include <stdint.h>
#include <stdbool.h>

#include "statehandler.h"

typedef uint8_t (*CpuReadHandler)(void* mem, uint32_t adr);
typedef void (*CpuWriteHandler)(void* mem, uint32_t adr, uint8_t val);
typedef void (*CpuIdleHandler)(void* mem, bool waiting);

typedef struct Cpu Cpu;

struct Cpu {
  // reference to memory handler, pointers to read/write/idle handlers
  void* mem;
  CpuReadHandler read;
  CpuWriteHandler write;
  CpuIdleHandler idle;
  // registers
  uint16_t a;
  uint16_t x;
  uint16_t y;
  uint16_t sp;
  uint16_t pc;
  uint16_t dp; // direct page (D)
  uint8_t k; // program bank (PB)
  uint8_t db; // data bank (B)
  // flags
  bool c;
  bool z;
  bool v;
  bool n;
  bool i;
  bool d;
  bool xf;
  bool mf;
  bool e;
  // power state (WAI/STP)
  bool waiting;
  bool stopped;
  // interrupts
  bool irqWanted;
  bool nmiWanted;
  bool intWanted;
  bool resetWanted;
  // topgear-pc: called before each opcode fetch, for coverage tracing (NULL when unused)
  void (*traceHook)(void* ctx, Cpu* cpu);
  void* traceCtx;
  // topgear-pc: called before each opcode fetch; returning true means the hook ran the
  // code at PC itself (a translated routine) and the opcode is skipped (NULL when unused)
  bool (*callHook)(void* ctx, Cpu* cpu);
  void* callCtx;
  // topgear-pc: counts NMIs and IRQs taken, so the lockstep checker can tell when one
  // interrupted a routine
  uint32_t interrupts;
  // topgear-pc: interrupt handlers running (taken minus RTIs), so writes made by a
  // handler can be told apart from the routine it interrupted
  uint8_t intDepth;
  // topgear-pc: while set, NMIs and IRQs wait (the lockstep checker runs a routine on its
  // own with this)
  bool blockInterrupts;
};

Cpu* cpu_init(void* mem, CpuReadHandler read, CpuWriteHandler write, CpuIdleHandler idle);
void cpu_free(Cpu* cpu);
void cpu_reset(Cpu* cpu, bool hard);
void cpu_handleState(Cpu* cpu, StateHandler* sh);
void cpu_runOpcode(Cpu* cpu);
void cpu_nmi(Cpu* cpu);
void cpu_setIrq(Cpu* cpu, bool state);

#endif
