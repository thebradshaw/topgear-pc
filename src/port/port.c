#include <stdio.h>
#include <stdlib.h>
#include "port.h"

#include "../coverage.h"
#include "../fiber.h"
#include "cpu.h"
#include "dma.h"
#include "snes.h"

uint8_t* g_ram;
static bool g_declined;

void tg_port_decline(void) { g_declined = true; }


bool tg_port_take_declined(void) {
  bool d = g_declined;
  g_declined = false;
  return d;
}
static Snes* g_snes;
static const uint8_t* g_rom;

void tg_port_bind(Snes* snes, const uint8_t* rom) {
  g_snes = snes;
  g_ram = snes->ram;
  g_rom = rom;
}

uint8_t tg_bus_read8(uint32_t adr) {
  return snes_read(g_snes, adr);
}

void tg_bus_write8(uint32_t adr, uint8_t val) {
  snes_write(g_snes, adr, val);
  // On the SNES a DMA starts right after the write that enables it, and the CPU waits for
  // it; translated code runs without CPU cycles, so the transfer happens here.
  if((adr & 0x40ffff) == 0x420b && val != 0) dma_runPending(g_snes->dma);
}

const uint8_t* tg_rom(uint32_t adr) {
  return g_rom + tg_lorom_offset(adr);
}

uint8_t tg_read8(uint32_t adr) {
  int32_t off = tg_lorom_offset(adr);
  if(off >= 0) return g_rom[off];
  uint8_t bank = (uint8_t) (adr >> 16);
  if(bank == 0x7e || bank == 0x7f) return g_ram[adr & 0x1ffff];
  if((bank & 0x40) == 0 && (adr & 0xffff) < 0x2000) return g_ram[adr & 0x1fff];
  return snes_read(g_snes, adr);
}

static int g_emulatedDepth;
static uint16_t g_minSp = 0xffff;

uint16_t tg_port_take_min_sp(void) {
  uint16_t v = g_minSp;
  g_minSp = 0xffff;
  return v;
}

bool tg_port_in_emulated_call(void) { return g_emulatedDepth > 0; }

static TgResumePoint g_resume;

const TgResumePoint* tg_port_resume_point(void) { return &g_resume; }

void tg_port_wait(void) {
  Cpu* cpu = g_snes->cpu;
  g_resume = (TgResumePoint) { cpu->pc, cpu->sp, cpu->k, true };
  tg_fiber_yield();
}

static void run_until(Cpu* cpu, uint16_t target, uint16_t stop, uint8_t k, uint16_t sp) {
  g_emulatedDepth++;
  for(long steps = 0; !(cpu->pc == stop && cpu->k == k && cpu->sp == sp); steps++) {
    if(steps == 10000000) {
      fprintf(stderr, "original code called at $%02X:%04X never got back to $%04X (now at $%02X:%04X, S=%04X, wanted %04X)\n",
        k, target, stop, cpu->k, cpu->pc, cpu->sp, sp);
      abort();
    }
    cpu_runOpcode(cpu);
    if(cpu->sp < g_minSp) g_minSp = cpu->sp;
  }
  g_emulatedDepth--;
}

enum { kJmp, kJsr, kJsl };

// Runs emulated code from target (in the current bank unless it's a JSL) until it gets
// to stop, in the current bank, with the stack pointer back where it was.
static void run_emulated(TgRegs* r, uint32_t target, uint16_t stop, uint8_t mx, int how) {
  Cpu* cpu = g_snes->cpu;
  uint16_t pc = cpu->pc;
  uint8_t k = cpu->k;
  bool mf = cpu->mf, xf = cpu->xf;
  cpu->mf = mx == TG_MX_8 || mx == TG_MX_A8_I16;
  cpu->xf = mx == TG_MX_8;
  cpu->a = r->a;
  cpu->x = cpu->xf ? (r->x & 0xff) : r->x;
  cpu->y = cpu->xf ? (r->y & 0xff) : r->y;
  cpu->dp = r->d;
  cpu->db = r->db;
  cpu->n = r->n; cpu->v = r->v; cpu->z = r->z; cpu->c = r->c;
  uint16_t sp = cpu->sp;
  if(how != kJmp) {
    // JSR: the return address minus one, high byte first, on the stack in bank 0; JSL
    // pushes the bank first.
    uint16_t s = sp;
    if(how == kJsl) snes_write(g_snes, s--, k);
    uint16_t pushed = (uint16_t) (stop - 1);
    snes_write(g_snes, s, (uint8_t) (pushed >> 8));
    snes_write(g_snes, (uint16_t) (s - 1), (uint8_t) pushed);
    cpu->sp = (uint16_t) (s - 2);
  }
  if(how == kJsl) cpu->k = (uint8_t) (target >> 16);
  cpu->pc = (uint16_t) target;
  if(tg_fiber_active()) {
    // From a routine that waits: the machine runs the call itself.
    g_resume = (TgResumePoint) { stop, sp, k, false };
    tg_fiber_yield();
  } else {
    run_until(cpu, (uint16_t) target, stop, k, sp);
  }
  r->a = cpu->a; r->x = cpu->x; r->y = cpu->y;
  r->d = cpu->dp; r->db = cpu->db;
  r->n = cpu->n; r->v = cpu->v; r->z = cpu->z; r->c = cpu->c;
  cpu->pc = pc;
  cpu->k = k;
  cpu->mf = mf;
  cpu->xf = xf;
}

void tg_call_jsr(TgRegs* r, uint16_t target, uint16_t ret, uint8_t mx) { run_emulated(r, target, ret, mx, kJsr); }
void tg_call_jsl(TgRegs* r, uint32_t target, uint16_t ret, uint8_t mx) { run_emulated(r, target, ret, mx, kJsl); }
void tg_call_jmp(TgRegs* r, uint16_t target, uint16_t stop, uint8_t mx) { run_emulated(r, target, stop, mx, kJmp); }

void tg_cpu_cli(void) { g_snes->cpu->i = false; }
