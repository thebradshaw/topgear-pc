#include "lockstep.h"

#include <stdlib.h>
#include <string.h>

#include "port/port.h"
#include "snes.h"

enum { kMaxLog = 4096, kMaxDetails = 3, kMaxSteps = 2000000 };

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
  IoWrite hdma[kMaxLog];  // B-bus writes made by HDMA while the routine ran
  int hdmaCount;
} Result;

typedef struct FuncStats {
  uint32_t calls, verified, interrupted, crossedFrame, wrongMode, mismatches;
} FuncStats;

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
  char* details;   // text of the first mismatches
  size_t detailsLen;
};

TgLockstep* tg_lockstep_create(Snes* snes, const uint8_t* rom, TgPortMode mode) {
  TgLockstep* ls = calloc(1, sizeof(TgLockstep));
  ls->snes = snes;
  ls->mode = mode;
  ls->stats = calloc(tg_port_func_count, sizeof(FuncStats));
  ls->skipOnce = 0xffffffff;
  for(int i = 0; i < tg_port_func_count; i++) {
    uint16_t lo = (uint16_t) tg_port_funcs[i].addr;
    ls->entryBits[lo >> 3] |= (uint8_t) (1 << (lo & 7));
  }
  if(mode == TG_PORT_VERIFY) {
    ls->c = malloc(sizeof(Result));
    ls->orig = malloc(sizeof(Result));
    ls->state = malloc(snes_saveState(snes, NULL) + 0x10000);
    ls->after = malloc(snes_saveState(snes, NULL) + 0x10000);
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

void tg_lockstep_on_write(TgLockstep* ls, uint32_t adr, uint8_t val) {
  Result* r = ls->logTarget;
  if(r == NULL || !isLoggedIo(adr)) return;
  if(r->logCount < kMaxLog) r->log[r->logCount] = (IoWrite) { (uint16_t) adr, val };
  r->logCount++;
}

void tg_lockstep_on_hdma_write(TgLockstep* ls, uint8_t adr, uint8_t val) {
  Result* r = ls->logTarget;
  if(r == NULL) return;
  if(r->hdmaCount < kMaxLog) r->hdma[r->hdmaCount] = (IoWrite) { adr, val };
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
  bool regs = c->a == o->a && c->x == o->x && c->y == o->y && c->sp == o->sp && c->pc == o->pc &&
    c->dp == o->dp && c->k == o->k && c->db == o->db && c->flags == o->flags;
  bool ram = memcmp(c->ram, o->ram, sizeof(c->ram)) == 0;
  bool vram = memcmp(c->vram, o->vram, sizeof(c->vram)) == 0;
  bool other = memcmp(c->cgram, o->cgram, sizeof(c->cgram)) == 0 && memcmp(c->oam, o->oam, sizeof(c->oam)) == 0 &&
    memcmp(c->highOam, o->highOam, sizeof(c->highOam)) == 0;
  bool log = c->logCount == o->logCount &&
    memcmp(c->log, o->log, sizeof(IoWrite) * (c->logCount < kMaxLog ? c->logCount : kMaxLog)) == 0;
  if(regs && ram && vram && other && log) return true;
  if(!detailed) return false;
  addDetail(ls, "%s ($%02X:%04X) mismatch at frame %u:\n", f->name, f->addr >> 16, f->addr & 0xffff,
    ls->snes->frames);
  if(!regs) {
    addDetail(ls, "  original: A=%04X X=%04X Y=%04X S=%04X D=%04X DB=%02X P=%02X PC=%02X:%04X\n", o->a, o->x, o->y,
      o->sp, o->dp, o->db, o->flags, o->k, o->pc);
    addDetail(ls, "  C:        A=%04X X=%04X Y=%04X S=%04X D=%04X DB=%02X P=%02X PC=%02X:%04X\n", c->a, c->x, c->y,
      c->sp, c->dp, c->db, c->flags, c->k, c->pc);
  }
  for(int i = 0, shown = 0; i < 0x20000 && shown < 8; i++) {
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
  if(!other) addDetail(ls, "  CGRAM or OAM differs\n");
  if(!log) {
    int n = c->logCount < o->logCount ? c->logCount : o->logCount;
    int i = 0;
    while(i < n && i < kMaxLog && c->log[i].adr == o->log[i].adr && c->log[i].val == o->log[i].val) i++;
    addDetail(ls, "  register writes: original %d, C %d, first difference at write %d\n", o->logCount,
      c->logCount, i);
  }
  return false;
}

static bool mxMatches(const TgPortFunc* f, const Cpu* cpu) {
  switch(f->mx) {
    case TG_MX_16: return !cpu->mf && !cpu->xf;
    case TG_MX_8: return cpu->mf && cpu->xf;
    case TG_MX_A8_I16: return cpu->mf && !cpu->xf;
    default: return true;
  }
}

// Runs the C version on the live machine, including the return to the caller.
static void runC(TgLockstep* ls, const TgPortFunc* f, Cpu* cpu) {
  TgRegs r = { cpu->a, cpu->x, cpu->y, cpu->dp, cpu->db, cpu->n, cpu->v, cpu->z, cpu->c };
  ls->inC = true;
  f->fn(&r);
  ls->inC = false;
  cpu->a = cpu->mf ? (uint16_t) ((cpu->a & 0xff00) | (r.a & 0xff)) : r.a;
  cpu->x = cpu->xf ? (r.x & 0xff) : r.x;
  cpu->y = cpu->xf ? (r.y & 0xff) : r.y;
  cpu->dp = r.d;
  cpu->db = r.db;
  cpu->n = r.n; cpu->v = r.v; cpu->z = r.z; cpu->c = r.c;
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
}

bool tg_lockstep_on_opcode(TgLockstep* ls, Cpu* cpu) {
  if(ls->mode == TG_PORT_OFF || ls->inOriginal) return false;
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
    runC(ls, f, cpu);
    return true;
  }

  // The original first, from a saved state, until it returns to the same caller.
  Snes* snes = ls->snes;
  ls->stateSize = snes_saveState(snes, ls->state);
  uint8_t* ram = snes->ram;
  uint16_t retSp = (uint16_t) (cpu->sp + (f->call == TG_CALL_JSL ? 3 : 2));
  uint16_t retPc = (uint16_t) ((ram[(uint16_t) (cpu->sp + 1) & 0x1fff] | ram[(uint16_t) (cpu->sp + 2) & 0x1fff] << 8) + 1);
  uint8_t retK = f->call == TG_CALL_JSL ? ram[(uint16_t) (cpu->sp + 3) & 0x1fff] : cpu->k;
  if(cpu->e) retSp = (uint16_t) (0x100 | (retSp & 0xff));
  uint32_t ints = cpu->interrupts, frames = snes->frames;
  bool vblank = snes->inVblank;
  Result* o = ls->orig;
  o->logCount = o->hdmaCount = 0;
  ls->logTarget = o;
  ls->inOriginal = true;
  int steps = 0;
  while(!(cpu->sp == retSp && cpu->pc == retPc && cpu->k == retK)) {
    cpu_runOpcode(cpu);
    if(snes->frames != frames || snes->inVblank != vblank || ++steps > kMaxSteps) break;
  }
  ls->inOriginal = false;
  ls->logTarget = NULL;
  if(!(cpu->sp == retSp && cpu->pc == retPc && cpu->k == retK)) {
    // It ran into the next part of the frame: run it again normally instead.
    snes_loadState(snes, ls->state, ls->stateSize);
    st->crossedFrame++;
    st->calls--;  // counted again when it runs
    ls->skipOnce = adr;
    return false;
  }
  if(cpu->interrupts != ints) {
    st->interrupted++;
    return true;
  }
  capture(ls, o);
  ls->afterSize = snes_saveState(snes, ls->after);

  // Then the C version from the same state, plus the HDMA writes that happened meanwhile.
  snes_loadState(snes, ls->state, ls->stateSize);
  Result* c = ls->c;
  c->logCount = c->hdmaCount = 0;
  ls->logTarget = c;
  runC(ls, f, cpu);
  ls->logTarget = NULL;
  for(int i = 0; i < o->hdmaCount && i < kMaxLog; i++) snes_writeBBus(snes, (uint8_t) o->hdma[i].adr, o->hdma[i].val);
  capture(ls, c);

  // The game continues from the original's result.
  snes_loadState(snes, ls->after, ls->afterSize);
  if(compare(ls, f, false)) {
    st->verified++;
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
      fprintf(f, "  %-24s calls %u, matched %u, mismatched %u, skipped (interrupted %u, crossed frame %u, "
        "wrong width %u)\n", tg_port_funcs[i].name, s->calls, s->verified, s->mismatches, s->interrupted,
        s->crossedFrame, s->wrongMode);
    } else {
      fprintf(f, "  %-24s calls %u, wrong width %u\n", tg_port_funcs[i].name, s->calls, s->wrongMode);
    }
    mismatches += (int) s->mismatches;
  }
  if(ls->details) fputs(ls->details, f);
  return mismatches;
}
