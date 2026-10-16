#include "lockstep.h"

#include <stdlib.h>
#include <string.h>

#include "port/port.h"
#include "snes.h"

enum { kMaxLog = 4096, kMaxHdma = 1 << 15, kMaxDetails = 3, kMaxSteps = 2000000 };

typedef struct IoWrite {
  uint16_t adr;
  uint8_t val;
} IoWrite;

typedef struct Result {
  uint16_t a, x, y, sp, pc, dp;
  uint8_t k, db, flags;
  uint8_t ram[0x20000];
  uint16_t vram[0x8000];
  uint16_t cgram[0x100];
  uint16_t oam[0x100];
  uint8_t highOam[0x20];
  IoWrite log[kMaxLog];
  int logCount;
  IoWrite hdma[kMaxHdma];  // B-bus writes made by HDMA while the routine ran (a few frames' worth)
  int hdmaCount;
} Result;

typedef struct FuncStats {
  uint32_t calls, verified, verifiedInterrupted, verifiedIsolated, interrupted, wrongMode, mismatches, declined;
} FuncStats;

enum { kMaxHandlerWrites = 1024 };

struct TgLockstep {
  Snes* snes;
  TgPortMode mode;
  uint8_t entryBits[0x2000];  // low 16 bits of every entry point, as a bitmap
  FuncStats* stats;
  bool inC, inOriginal;
  uint32_t skipOnce;  // entry to emulate once, after a call was abandoned
  Result* logTarget;
  Result* c;       // the C version's result
  Result* orig;    // the original's result
  uint8_t* state;  // machine state at the call
  int stateSize;
  uint8_t* after;  // machine state after the original returned
  int afterSize;
  uint8_t* pixels; // the picture, kept while a call is checked on its own
  // Work RAM written by interrupt handlers while the original ran; not compared.
  uint8_t baseDepth;
  uint32_t handlerWrites[kMaxHandlerWrites];
  int handlerWriteCount;
  bool handlerTouchedPpu;  // a handler wrote PPU memory or started DMA: can't compare
  char* details;   // text of the first mismatches
  size_t detailsLen;
  // Registers poisoned after the last call, and their real values: an interrupt handler
  // saves registers on the stack whether or not they're dead, so it gets the real ones.
  bool poisoned;
  uint32_t poisonInts;
  uint16_t realA, realX, realY, junkA, junkX, junkY;
  uint16_t origMinSp;  // lowest stack pointer while the original ran
  // The CPU's interrupt count isn't in saved states: these keep it to the timeline the
  // machine carries on in.
  uint32_t stateInts, afterInts;
};

TgLockstep* tg_lockstep_create(Snes* snes, const uint8_t* rom, TgPortMode mode) {
  TgLockstep* ls = calloc(1, sizeof(TgLockstep));
  ls->snes = snes;
  ls->mode = mode;
  ls->stats = calloc(tg_port_func_count, sizeof(FuncStats));
  ls->skipOnce = 0xffffffff;
  for(int i = 1; i < tg_port_func_count; i++) {
    if(tg_port_funcs[i].addr <= tg_port_funcs[i - 1].addr) {
      fprintf(stderr, "src/port/registry.c: %s is out of address order\n", tg_port_funcs[i].name);
      abort();
    }
  }
  for(int i = 0; i < tg_port_func_count; i++) {
    uint16_t lo = (uint16_t) tg_port_funcs[i].addr;
    ls->entryBits[lo >> 3] |= (uint8_t) (1 << (lo & 7));
  }
  if(mode == TG_PORT_VERIFY) {
    ls->c = malloc(sizeof(Result));
    ls->orig = malloc(sizeof(Result));
    ls->state = malloc(snes_saveState(snes, NULL) + 0x10000);
    ls->after = malloc(snes_saveState(snes, NULL) + 0x10000);
    ls->pixels = malloc(sizeof(snes->ppu->pixelBuffer));
  }
  tg_port_bind(snes, rom);
  return ls;
}

void tg_lockstep_destroy(TgLockstep* ls) {
  if(ls == NULL) return;
  free(ls->stats);
  free(ls->c);
  free(ls->orig);
  free(ls->state);
  free(ls->after);
  free(ls->pixels);
  free(ls->details);
  free(ls);
}

TgPortMode tg_lockstep_mode(const TgLockstep* ls) { return ls->mode; }
bool tg_lockstep_in_c(const TgLockstep* ls) { return ls->inC; }

static bool isLoggedIo(uint32_t adr) {
  uint8_t bank = (uint8_t) (adr >> 16);
  if(bank >= 0x40 && bank < 0x80) return false;
  if(bank >= 0xc0) return false;
  uint16_t a = (uint16_t) adr;
  // The multiply/divide inputs are scratch: C does its arithmetic directly.
  if(a == 0x211b || a == 0x211c || (a >= 0x4202 && a <= 0x4206)) return false;
  return (a >= 0x2100 && a < 0x2200) || (a >= 0x4200 && a < 0x4400);
}

static int32_t wramIndex(uint32_t adr) {
  uint8_t bank = (uint8_t) (adr >> 16);
  if(bank == 0x7e || bank == 0x7f) return (int32_t) (adr & 0x1ffff);
  if((bank < 0x40 || (bank >= 0x80 && bank < 0xc0)) && (adr & 0xffff) < 0x2000) return (int32_t) (adr & 0x1fff);
  return -1;
}

void tg_lockstep_on_write(TgLockstep* ls, uint32_t adr, uint8_t val) {
  Result* r = ls->logTarget;
  if(r == NULL) return;
  if(r == ls->orig && ls->snes->cpu->intDepth > ls->baseDepth) {
    // An interrupt handler, not the routine.
    int32_t i = wramIndex(adr);
    if(i >= 0) {
      if(ls->handlerWriteCount < kMaxHandlerWrites) ls->handlerWrites[ls->handlerWriteCount] = (uint32_t) i;
      ls->handlerWriteCount++;
    }
    uint16_t a = (uint16_t) adr;
    if(a == 0x2104 || a == 0x2118 || a == 0x2119 || a == 0x2122 || a == 0x420b) ls->handlerTouchedPpu = true;
    return;
  }
  if(!isLoggedIo(adr)) return;
  if(r->logCount < kMaxLog) r->log[r->logCount] = (IoWrite) { (uint16_t) adr, val };
  r->logCount++;
}

void tg_lockstep_on_hdma_write(TgLockstep* ls, uint8_t adr, uint8_t val) {
  Result* r = ls->logTarget;
  if(r == NULL) return;
  if(r->hdmaCount < kMaxHdma) r->hdma[r->hdmaCount] = (IoWrite) { adr, val };
  r->hdmaCount++;
}

static uint8_t cpuFlags(const Cpu* cpu) {
  return (uint8_t) (cpu->n << 7 | cpu->v << 6 | cpu->mf << 5 | cpu->xf << 4 | cpu->d << 3 | cpu->i << 2 |
    cpu->z << 1 | cpu->c);
}

static void capture(TgLockstep* ls, Result* r) {
  Cpu* cpu = ls->snes->cpu;
  Ppu* ppu = ls->snes->ppu;
  r->a = cpu->a; r->x = cpu->x; r->y = cpu->y; r->sp = cpu->sp; r->pc = cpu->pc; r->dp = cpu->dp;
  r->k = cpu->k; r->db = cpu->db; r->flags = cpuFlags(cpu);
  memcpy(r->ram, ls->snes->ram, sizeof(r->ram));
  memcpy(r->vram, ppu->vram, sizeof(r->vram));
  memcpy(r->cgram, ppu->cgram, sizeof(r->cgram));
  memcpy(r->oam, ppu->oam, sizeof(r->oam));
  memcpy(r->highOam, ppu->highOam, sizeof(r->highOam));
}

static void addDetail(TgLockstep* ls, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
#include <stdarg.h>
static void addDetail(TgLockstep* ls, const char* fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if(n < 0) return;
  if(n >= (int) sizeof(buf)) n = sizeof(buf) - 1;
  ls->details = realloc(ls->details, ls->detailsLen + n + 1);
  memcpy(ls->details + ls->detailsLen, buf, n + 1);
  ls->detailsLen += n;
}

// Compares the two results; describes differences while detailed is set.
static bool compare(TgLockstep* ls, const TgPortFunc* f, bool detailed) {
  const Result* c = ls->c;
  const Result* o = ls->orig;
  uint8_t flagMask = (f->dead & TG_DEAD_FLAGS) ? 0x3c : (f->dead & TG_DEAD_NVZ) ? 0x3d : 0xff;  // keep M, X, D, I (C)
  bool regs = ((f->dead & TG_DEAD_A) || c->a == o->a) && ((f->dead & TG_DEAD_X) || c->x == o->x) &&
    ((f->dead & TG_DEAD_Y) || c->y == o->y) && c->sp == o->sp && c->pc == o->pc &&
    c->dp == o->dp && c->k == o->k && c->db == o->db && (c->flags & flagMask) == (o->flags & flagMask);
  // The C version takes the byte that was on the bus without HDMA; keep the original's
  // (the machine carries on from the C version's state).
  if(f->bus_noise) ls->c->ram[f->bus_noise] = ls->snes->ram[f->bus_noise] = o->ram[f->bus_noise];
  bool ram = memcmp(c->ram, o->ram, sizeof(c->ram)) == 0;
  bool vram = memcmp(c->vram, o->vram, sizeof(c->vram)) == 0;
  bool other = memcmp(c->cgram, o->cgram, sizeof(c->cgram)) == 0 && memcmp(c->oam, o->oam, sizeof(c->oam)) == 0 &&
    memcmp(c->highOam, o->highOam, sizeof(c->highOam)) == 0;
  bool log = c->logCount == o->logCount &&
    memcmp(c->log, o->log, sizeof(IoWrite) * (c->logCount < kMaxLog ? c->logCount : kMaxLog)) == 0;
  if(regs && ram && vram && other && log) return true;
  if(!detailed) return false;
  addDetail(ls, "%s ($%02X:%04X) mismatch at frame %u (%d HDMA writes while the original ran):\n", f->name,
    f->addr >> 16, f->addr & 0xffff, ls->snes->frames, o->hdmaCount);
  if(!regs) {
    addDetail(ls, "  original: A=%04X X=%04X Y=%04X S=%04X D=%04X DB=%02X P=%02X PC=%02X:%04X\n", o->a, o->x, o->y,
      o->sp, o->dp, o->db, o->flags, o->k, o->pc);
    addDetail(ls, "  C:        A=%04X X=%04X Y=%04X S=%04X D=%04X DB=%02X P=%02X PC=%02X:%04X\n", c->a, c->x, c->y,
      c->sp, c->dp, c->db, c->flags, c->k, c->pc);
  }
  for(int i = 0, shown = 0; i < 0x20000 && shown < (getenv("TG_LOCKSTEP_ALLDIFF") ? 100000 : 8); i++) {
    if(c->ram[i] != o->ram[i]) {
      addDetail(ls, "  RAM $%05X: original %02X, C %02X\n", i, o->ram[i], c->ram[i]);
      shown++;
    }
  }
  for(int i = 0, shown = 0; i < 0x8000 && shown < 8; i++) {
    if(c->vram[i] != o->vram[i]) {
      addDetail(ls, "  VRAM word $%04X: original %04X, C %04X\n", i, o->vram[i], c->vram[i]);
      shown++;
    }
  }
  for(int i = 0, shown = 0; i < 0x100 && shown < 4; i++) {
    if(c->cgram[i] != o->cgram[i]) {
      addDetail(ls, "  CGRAM $%02X: original %04X, C %04X\n", i, o->cgram[i], c->cgram[i]);
      shown++;
    }
  }
  for(int i = 0, shown = 0; i < (int) (sizeof(c->oam) / sizeof(c->oam[0])) && shown < 4; i++) {
    if(c->oam[i] != o->oam[i]) {
      addDetail(ls, "  OAM word $%03X: original %04X, C %04X\n", i, o->oam[i], c->oam[i]);
      shown++;
    }
  }
  for(int i = 0, shown = 0; i < 0x20 && shown < 4; i++) {
    if(c->highOam[i] != o->highOam[i]) {
      addDetail(ls, "  high OAM $%02X: original %02X, C %02X\n", i, o->highOam[i], c->highOam[i]);
      shown++;
    }
  }
  if(!log) {
    int n = c->logCount < o->logCount ? c->logCount : o->logCount;
    int i = 0;
    while(i < n && i < kMaxLog && c->log[i].adr == o->log[i].adr && c->log[i].val == o->log[i].val) i++;
    addDetail(ls, "  register writes: original %d, C %d, first difference at write %d\n", o->logCount,
      c->logCount, i);
    for(int k = i; k < i + 4; k++) {
      if(k < o->logCount && k < kMaxLog) addDetail(ls, "    original: $%04X <- %02X\n", o->log[k].adr, o->log[k].val);
      if(k < c->logCount && k < kMaxLog) addDetail(ls, "    C:        $%04X <- %02X\n", c->log[k].adr, c->log[k].val);
    }
  }
  return false;
}

static bool mxMatches(const TgPortFunc* f, const Cpu* cpu) {
  switch(f->mx) {
    case TG_MX_16: return !cpu->mf && !cpu->xf;
    case TG_MX_8: return cpu->mf && cpu->xf;
    case TG_MX_A8_I16: return cpu->mf && !cpu->xf;
    case TG_MX_I16: return !cpu->xf;
    default: return true;
  }
}

// Runs the C version on the live machine, including the return to the caller. False if
// the routine declined the call (see tg_port_decline), leaving the machine as it was.
static bool runC(TgLockstep* ls, const TgPortFunc* f, Cpu* cpu) {
  TgRegs r = { cpu->a, cpu->x, cpu->y, cpu->dp, cpu->db, cpu->n, cpu->v, cpu->z, cpu->c };
  ls->inC = true;
  f->fn(&r);
  ls->inC = false;
  if(tg_port_take_declined()) return false;
  // A is always all 16 bits: a routine entered with 8-bit A may still change the high byte
  // (REP #$20 inside), and one that doesn't leaves r.a's high byte as it found it.
  cpu->a = r.a;
  cpu->x = r.x;
  cpu->y = r.y;
  cpu->dp = r.d;
  cpu->db = r.db;
  cpu->n = r.n; cpu->v = r.v; cpu->z = r.z; cpu->c = r.c;
  if(f->mx_out != TG_MX_ANY) {
    cpu->mf = f->mx_out == TG_MX_8 || f->mx_out == TG_MX_A8_I16;
    cpu->xf = f->mx_out == TG_MX_8;
  }
  if(cpu->xf) {
    cpu->x &= 0xff;
    cpu->y &= 0xff;
  }
  // RTS / RTL: the stack is in bank 0 low RAM
  uint8_t* ram = ls->snes->ram;
  uint16_t sp = cpu->sp;
  uint16_t ret = (uint16_t) (ram[(uint16_t) (sp + 1) & 0x1fff] | ram[(uint16_t) (sp + 2) & 0x1fff] << 8);
  if(f->call == TG_CALL_JSL) {
    cpu->k = ram[(uint16_t) (sp + 3) & 0x1fff];
    sp += 3;
  } else {
    sp += 2;
  }
  cpu->sp = cpu->e ? (uint16_t) (0x100 | (sp & 0xff)) : sp;
  cpu->pc = (uint16_t) (ret + 1);
  return true;
}

// The stack below SP is free space: the original's pushes and pulls leave bytes there
// that C has no reason to write, and original code C calls leaves its own. Only the part
// either of them used is free; variables further down are compared.
static void fixStack(TgLockstep* ls, const Cpu* cpu) {
  if(cpu->e) return;
  uint16_t lowest = ls->origMinSp;
  uint16_t cLowest = tg_port_take_min_sp();
  if(cLowest < lowest) lowest = cLowest;
  for(uint16_t a = ls->orig->sp; a > lowest && a < 0x2000; a--) ls->c->ram[a] = ls->orig->ram[a];
}

static void poisonDead(TgLockstep* ls, const TgPortFunc* f, Cpu* cpu) {
  // Registers still holding the last call's junk keep their real values from then.
  bool again = ls->poisoned;
  ls->realA = again && cpu->a == ls->junkA ? ls->realA : cpu->a;
  ls->realX = again && cpu->x == ls->junkX ? ls->realX : cpu->x;
  ls->realY = again && cpu->y == ls->junkY ? ls->realY : cpu->y;
  uint16_t mask = cpu->xf ? 0xff : 0xffff;
  if(f->dead & TG_DEAD_A) cpu->a = ls->realA ^ 0xa5c3;
  if(f->dead & TG_DEAD_X) cpu->x = (ls->realX ^ 0x5a3c) & mask;
  if(f->dead & TG_DEAD_Y) cpu->y = (ls->realY ^ 0x3c5a) & mask;
  ls->junkA = cpu->a; ls->junkX = cpu->x; ls->junkY = cpu->y;
  ls->poisoned = (f->dead & (TG_DEAD_A | TG_DEAD_X | TG_DEAD_Y)) != 0;
  ls->poisonInts = cpu->interrupts;
  if(f->dead & TG_DEAD_FLAGS) {
    cpu->n = !cpu->n; cpu->v = !cpu->v; cpu->z = !cpu->z; cpu->c = !cpu->c;
  } else if(f->dead & TG_DEAD_NVZ) {
    cpu->n = !cpu->n; cpu->v = !cpu->v; cpu->z = !cpu->z;
  }
}

// Checks the call saved in ls->state on its own: the original with interrupts held off,
// then C, each from that state. Used when an interrupt or the start of the next frame got
// in the way of the normal check. Leaves the machine in no particular state; the caller
// loads the one to continue from. Returns 1 if they matched, 0 if not, -1 if it couldn't
// tell (the original didn't return, or C declined).
static int isolatedCheck(TgLockstep* ls, const TgPortFunc* f, Cpu* cpu, uint16_t retSp, uint16_t retPc, uint8_t retK) {
  Snes* snes = ls->snes;
  // Running on past vblank draws over the top of the finished picture.
  memcpy(ls->pixels, snes->ppu->pixelBuffer, sizeof(snes->ppu->pixelBuffer));
  snes_loadState(snes, ls->state, ls->stateSize);
    cpu->interrupts = ls->stateInts;
  cpu->intDepth = ls->baseDepth;
  Result* o = ls->orig;
  o->logCount = o->hdmaCount = 0;
  ls->handlerWriteCount = 0;
  ls->handlerTouchedPpu = false;
  ls->logTarget = o;
  ls->inOriginal = true;
  cpu->blockInterrupts = true;
  ls->origMinSp = cpu->sp;
  for(int steps = 0; !(cpu->sp == retSp && cpu->pc == retPc && cpu->k == retK) && steps < kMaxSteps; steps++) {
    cpu_runOpcode(cpu);
    if(cpu->sp < ls->origMinSp) ls->origMinSp = cpu->sp;
  }
  // Too many HDMA writes to replay counts as unchecked.
  bool returned = cpu->sp == retSp && cpu->pc == retPc && cpu->k == retK && o->hdmaCount <= kMaxHdma;
  cpu->blockInterrupts = false;
  ls->inOriginal = false;
  ls->logTarget = NULL;
  int result = -1;
  if(returned) {
    capture(ls, o);
    snes_loadState(snes, ls->state, ls->stateSize);
    cpu->interrupts = ls->stateInts;
    cpu->intDepth = ls->baseDepth;
    Result* c = ls->c;
    c->logCount = c->hdmaCount = 0;
    ls->logTarget = c;
    cpu->blockInterrupts = true;  // original code that C calls runs without them too
    tg_port_take_min_sp();
    bool ran = runC(ls, f, cpu);
    cpu->blockInterrupts = false;
    ls->logTarget = NULL;
    if(ran) {
      for(int i = 0; i < o->hdmaCount && i < kMaxHdma; i++) snes_writeBBus(snes, (uint8_t) o->hdma[i].adr, o->hdma[i].val);
      capture(ls, c);
      fixStack(ls, cpu);
      result = compare(ls, f, false) ? 1 : 0;
    }
  }
  memcpy(snes->ppu->pixelBuffer, ls->pixels, sizeof(snes->ppu->pixelBuffer));
  return result;
}

static void countIsolated(TgLockstep* ls, const TgPortFunc* f, FuncStats* st, int result) {
  if(result == 1) {
    st->verifiedIsolated++;
  } else if(result == 0) {
    if(st->mismatches < kMaxDetails) {
      addDetail(ls, "(checked with interrupts held off)\n");
      compare(ls, f, true);
    }
    st->mismatches++;
  } else {
    st->interrupted++;
  }
}

bool tg_lockstep_on_opcode(TgLockstep* ls, Cpu* cpu) {
  if(ls->mode == TG_PORT_OFF || ls->inOriginal || tg_port_in_emulated_call()) return false;
  if(ls->poisoned && cpu->interrupts != ls->poisonInts) {
    // An interrupt came before the caller replaced the junk: the handler is about to save
    // the registers on the stack, so give it the real values.
    ls->poisoned = false;
    if(cpu->a == ls->junkA) cpu->a = ls->realA;
    uint16_t mask = cpu->xf ? 0xff : 0xffff;
    if(cpu->x == ls->junkX) cpu->x = ls->realX & mask;
    if(cpu->y == ls->junkY) cpu->y = ls->realY & mask;
  }
  uint16_t lo = cpu->pc;
  if(!(ls->entryBits[lo >> 3] & (1 << (lo & 7)))) return false;
  uint32_t adr = (uint32_t) cpu->k << 16 | lo;
  if(adr == ls->skipOnce) {
    ls->skipOnce = 0xffffffff;
    return false;
  }
  const TgPortFunc* f = tg_port_find(adr);
  if(f == NULL) return false;
  FuncStats* st = &ls->stats[f - tg_port_funcs];
  st->calls++;
  if(!mxMatches(f, cpu) || cpu->e) {
    st->wrongMode++;
    return false;
  }
  if(ls->mode == TG_PORT_NATIVE) {
    if(runC(ls, f, cpu)) return true;
    st->declined++;
    ls->skipOnce = adr;
    return false;
  }

  // The original first, from a saved state, until it returns to the same caller.
  Snes* snes = ls->snes;
  ls->stateSize = snes_saveState(snes, ls->state);
  ls->stateInts = cpu->interrupts;
  uint8_t* ram = snes->ram;
  uint16_t retSp = (uint16_t) (cpu->sp + (f->call == TG_CALL_JSL ? 3 : 2));
  uint16_t retPc = (uint16_t) ((ram[(uint16_t) (cpu->sp + 1) & 0x1fff] | ram[(uint16_t) (cpu->sp + 2) & 0x1fff] << 8) + 1);
  uint8_t retK = f->call == TG_CALL_JSL ? ram[(uint16_t) (cpu->sp + 3) & 0x1fff] : cpu->k;
  if(cpu->e) retSp = (uint16_t) (0x100 | (retSp & 0xff));
  uint32_t ints = cpu->interrupts, entryFrame = snes->frames;
  bool vblank = snes->inVblank;
  unsigned entryLine = snes->vPos;
  Result* o = ls->orig;
  o->logCount = o->hdmaCount = 0;
  ls->baseDepth = cpu->intDepth;
  ls->handlerWriteCount = 0;
  ls->handlerTouchedPpu = false;
  ls->logTarget = o;
  ls->inOriginal = true;
  // Running on past the end of vblank is fine. Reaching the start of the next vblank is
  // not: the frame would end inside the call, out of step with the frontend.
  int steps = 0;
  ls->origMinSp = cpu->sp;
  bool sawActive = !vblank, reachedVblank = false;
  while(!(cpu->sp == retSp && cpu->pc == retPc && cpu->k == retK)) {
    cpu_runOpcode(cpu);
    if(cpu->sp < ls->origMinSp) ls->origMinSp = cpu->sp;
    if(!snes->inVblank) sawActive = true;
    else if(sawActive) reachedVblank = true;
    if(reachedVblank || ++steps > kMaxSteps) break;
  }
  ls->inOriginal = false;
  ls->logTarget = NULL;
  if(!(cpu->sp == retSp && cpu->pc == retPc && cpu->k == retK)) {
    if(getenv("TG_LOCKSTEP_DEBUG")) fprintf(stderr, "%s: abandoned at frame %u line %u -> frame %u line %u, %d steps\n", f->name, entryFrame, entryLine, snes->frames, snes->vPos, steps);
    // It ran into the next frame: check it on its own, then run it again normally.
    countIsolated(ls, f, st, isolatedCheck(ls, f, cpu, retSp, retPc, retK));
    snes_loadState(snes, ls->state, ls->stateSize);
    cpu->interrupts = ls->stateInts;
    cpu->intDepth = ls->baseDepth;  // not part of the saved state
    ls->skipOnce = adr;
    return false;
  }
  bool interrupted = cpu->interrupts != ints;
  if(interrupted && (ls->handlerTouchedPpu || ls->handlerWriteCount > kMaxHandlerWrites)) {
    // Not comparable as it ran: check it on its own, then carry on from the original.
    ls->afterSize = snes_saveState(snes, ls->after);
    ls->afterInts = cpu->interrupts;
    uint8_t depth = cpu->intDepth;
    countIsolated(ls, f, st, isolatedCheck(ls, f, cpu, retSp, retPc, retK));
    snes_loadState(snes, ls->after, ls->afterSize);
    cpu->interrupts = ls->afterInts;
    cpu->intDepth = depth;
    return true;
  }
  uint8_t depthAfter = cpu->intDepth;
  capture(ls, o);
  ls->afterSize = snes_saveState(snes, ls->after);
    ls->afterInts = cpu->interrupts;

  // Then the C version from the same state, plus the HDMA writes that happened meanwhile.
  snes_loadState(snes, ls->state, ls->stateSize);
    cpu->interrupts = ls->stateInts;
  cpu->intDepth = ls->baseDepth;
  Result* c = ls->c;
  c->logCount = c->hdmaCount = 0;
  ls->logTarget = c;
  tg_port_take_min_sp();
  bool ran = runC(ls, f, cpu);
  ls->logTarget = NULL;
  if(!ran) {
    // Calls the C version doesn't handle (yet) run emulated.
    st->declined++;
    snes_loadState(snes, ls->after, ls->afterSize);
    cpu->interrupts = ls->afterInts;
    cpu->intDepth = depthAfter;
    return true;
  }
  for(int i = 0; i < o->hdmaCount && i < kMaxHdma; i++) snes_writeBBus(snes, (uint8_t) o->hdma[i].adr, o->hdma[i].val);
  capture(ls, c);
  fixStack(ls, cpu);

  // RAM that interrupt handlers wrote isn't the routine's doing.
  for(int i = 0; i < ls->handlerWriteCount; i++) c->ram[ls->handlerWrites[i]] = o->ram[ls->handlerWrites[i]];

  // The game continues from the original's result, with junk in the registers no caller
  // should read, so the recording tests catch a wrong TG_DEAD_* declaration.
  snes_loadState(snes, ls->after, ls->afterSize);
    cpu->interrupts = ls->afterInts;
  cpu->intDepth = depthAfter;
  poisonDead(ls, f, cpu);
  if(compare(ls, f, false)) {
    if(interrupted) st->verifiedInterrupted++; else st->verified++;
  } else if(interrupted) {
    // The handler may have changed something the routine read: check it on its own.
    ls->afterSize = snes_saveState(snes, ls->after);
    ls->afterInts = cpu->interrupts;
    uint8_t depth = cpu->intDepth;
    countIsolated(ls, f, st, isolatedCheck(ls, f, cpu, retSp, retPc, retK));
    snes_loadState(snes, ls->after, ls->afterSize);
    cpu->interrupts = ls->afterInts;
    cpu->intDepth = depth;
  } else {
    if(st->mismatches < kMaxDetails) compare(ls, f, true);
    st->mismatches++;
  }
  return true;
}

int tg_lockstep_report(const TgLockstep* ls, FILE* f) {
  int mismatches = 0;
  fprintf(f, "port: %s, %d translated routines\n",
    ls->mode == TG_PORT_VERIFY ? "verify" : ls->mode == TG_PORT_NATIVE ? "native" : "off", tg_port_func_count);
  for(int i = 0; i < tg_port_func_count; i++) {
    const FuncStats* s = &ls->stats[i];
    if(ls->mode == TG_PORT_VERIFY) {
      fprintf(f, "  %-24s calls %u, matched %u (%u interrupted, %u on their own), mismatched %u, unchecked %u, "
        "declined %u, wrong width %u\n", tg_port_funcs[i].name, s->calls, s->verified + s->verifiedInterrupted + s->verifiedIsolated,
        s->verifiedInterrupted, s->verifiedIsolated, s->mismatches, s->interrupted, s->declined, s->wrongMode);
    } else {
      fprintf(f, "  %-24s calls %u, wrong width %u\n", tg_port_funcs[i].name, s->calls, s->wrongMode);
    }
    mismatches += (int) s->mismatches;
  }
  if(ls->details) fputs(ls->details, f);
  return mismatches;
}
