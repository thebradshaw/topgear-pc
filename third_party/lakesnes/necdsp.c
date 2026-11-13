// NEC uPD77C25, following its published instruction set (OP, RT, JP and LD words).

#include "necdsp.h"

#include <string.h>

enum { SR_RQM = 0x8000, SR_DRS = 0x1000, SR_DRC = 0x0400 };

// The chip runs at 7.6 MHz, one instruction per cycle; the SNES master clock is 21.477 MHz.
#define MASTER_PER_INSN 3

bool necdsp_load(NecDsp* dsp, const uint8_t* data, int size) {
  if(size != 8192) return false;
  memset(dsp, 0, sizeof(*dsp));
  for(int i = 0; i < 2048; i++) dsp->programRom[i] = data[i * 3] | data[i * 3 + 1] << 8 | (uint32_t) data[i * 3 + 2] << 16;
  for(int i = 0; i < 1024; i++) dsp->dataRom[i] = data[6144 + i * 2] | data[6144 + i * 2 + 1] << 8;
  necdsp_reset(dsp);
  return true;
}

void necdsp_reset(NecDsp* dsp) {
  memset(dsp->dataRam, 0, sizeof(dsp->dataRam));
  dsp->pc = 0;
  dsp->drOut = false;
  dsp->rp = 0x3ff;
  dsp->dp = 0;
  dsp->sp = 0;
  memset(dsp->stack, 0, sizeof(dsp->stack));
  dsp->a = dsp->b = dsp->tr = dsp->trb = dsp->dr = dsp->si = dsp->so = 0;
  dsp->k = dsp->l = dsp->m = dsp->n = 0;
  dsp->sr = 0;
  memset(&dsp->fa, 0, sizeof(dsp->fa));
  memset(&dsp->fb, 0, sizeof(dsp->fb));
}

static void ld(NecDsp* d, uint16_t id, int dst) {
  switch(dst) {
    case 0: break;
    case 1: d->a = id; break;
    case 2: d->b = id; break;
    case 3: d->tr = id; break;
    case 4: d->dp = (uint8_t) id; break;
    case 5: d->rp = id & 0x3ff; break;
    case 6: d->dr = id; d->sr |= SR_RQM; d->drOut = true; break;
    case 7: d->sr = (d->sr & 0x907c) | (id & ~0x907c); break;
    case 8: {  // SO, least significant bit first
      uint16_t r = 0;
      for(int i = 0; i < 16; i++) if(id & (1 << i)) r |= 0x8000 >> i;
      d->so = r;
      break;
    }
    case 9: d->so = id; break;
    case 10: d->k = id; break;
    case 11: d->k = id; d->l = d->dataRom[d->rp & 0x3ff]; break;
    case 12: d->l = id; d->k = d->dataRam[(d->dp | 0x40) & 0xff]; break;
    case 13: d->l = id; break;
    case 14: d->trb = id; break;
    case 15: d->dataRam[d->dp] = id; break;
  }
}

static void op(NecDsp* d, uint32_t w) {
  int pselect = (w >> 20) & 3, alu = (w >> 16) & 15, asl = (w >> 15) & 1;
  int dpl = (w >> 13) & 3, dphm = (w >> 9) & 15, rpdcr = (w >> 8) & 1;
  int src = (w >> 4) & 15, dst = w & 15;
  uint16_t idb = 0;
  switch(src) {
    case 0: idb = d->trb; break;
    case 1: idb = d->a; break;
    case 2: idb = d->b; break;
    case 3: idb = d->tr; break;
    case 4: idb = d->dp; break;
    case 5: idb = d->rp; break;
    case 6: idb = d->dataRom[d->rp & 0x3ff]; break;
    case 7: idb = (uint16_t) (0x8000 - d->fa.s1); break;
    case 8: idb = d->dr; d->sr |= SR_RQM; break;
    case 9: idb = d->dr; break;
    case 10: idb = d->sr; break;
    case 11: case 12: idb = d->si; break;
    case 13: idb = d->k; break;
    case 14: idb = d->l; break;
    case 15: idb = d->dataRam[d->dp]; break;
  }
  if(alu) {
    uint16_t p = 0, q, r = 0;
    switch(pselect) {
      case 0: p = d->dataRam[d->dp]; break;
      case 1: p = idb; break;
      case 2: p = d->m; break;
      case 3: p = d->n; break;
    }
    NecDspFlags f = asl ? d->fb : d->fa;
    bool c = asl ? d->fa.c : d->fb.c;
    q = asl ? d->b : d->a;
    switch(alu) {
      case 1: r = q | p; break;
      case 2: r = q & p; break;
      case 3: r = q ^ p; break;
      case 4: r = q - p; break;
      case 5: r = q + p; break;
      case 6: r = q - p - c; break;
      case 7: r = q + p + c; break;
      case 8: r = q - 1; p = 1; break;
      case 9: r = q + 1; p = 1; break;
      case 10: r = ~q; break;
      case 11: r = (q >> 1) | (q & 0x8000); break;
      case 12: r = (q << 1) | c; break;
      case 13: r = (q << 2) | 3; break;
      case 14: r = (q << 4) | 15; break;
      case 15: r = (q << 8) | (q >> 8); break;
    }
    f.s0 = r & 0x8000;
    f.z = r == 0;
    if(!f.ov1) f.s1 = f.s0;
    switch(alu) {
      case 4: case 5: case 6: case 7: case 8: case 9:
        if(alu & 1) {
          f.ov0 = (q ^ r) & (p ^ r) & 0x8000;
          f.c = r < q;
        } else {
          f.ov0 = (q ^ r) & (q ^ p) & 0x8000;
          f.c = r > q;
        }
        f.ov1 = (f.ov0 && f.ov1) ? (f.s1 == f.s0) : (f.ov0 || f.ov1);
        break;
      case 11: f.c = q & 1; f.ov0 = f.ov1 = false; break;
      case 12: f.c = q >> 15; f.ov0 = f.ov1 = false; break;
      default: f.c = false; f.ov0 = f.ov1 = false; break;
    }
    if(asl) { d->b = r; d->fb = f; } else { d->a = r; d->fa = f; }
  }
  ld(d, idb, dst);
  if(dst != 4) {
    switch(dpl) {
      case 1: d->dp = (d->dp & 0xf0) | ((d->dp + 1) & 0x0f); break;
      case 2: d->dp = (d->dp & 0xf0) | ((d->dp - 1) & 0x0f); break;
      case 3: d->dp &= 0xf0; break;
    }
    d->dp ^= dphm << 4;
  }
  if(dst != 5 && rpdcr) d->rp = (d->rp - 1) & 0x3ff;
}

static void jp(NecDsp* d, uint32_t w) {
  int brch = (w >> 13) & 0x1ff;
  uint16_t na = (w >> 2) & 0x7ff;
  bool r = false;
  const NecDspFlags* fa = &d->fa;
  const NecDspFlags* fb = &d->fb;
  switch(brch) {
    case 0x080: r = !fa->c; break;
    case 0x082: r = fa->c; break;
    case 0x084: r = !fb->c; break;
    case 0x086: r = fb->c; break;
    case 0x088: r = !fa->z; break;
    case 0x08a: r = fa->z; break;
    case 0x08c: r = !fb->z; break;
    case 0x08e: r = fb->z; break;
    case 0x090: r = !fa->ov0; break;
    case 0x092: r = fa->ov0; break;
    case 0x094: r = !fb->ov0; break;
    case 0x096: r = fb->ov0; break;
    case 0x098: r = !fa->ov1; break;
    case 0x09a: r = fa->ov1; break;
    case 0x09c: r = !fb->ov1; break;
    case 0x09e: r = fb->ov1; break;
    case 0x0a0: r = !fa->s0; break;
    case 0x0a2: r = fa->s0; break;
    case 0x0a4: r = !fb->s0; break;
    case 0x0a6: r = fb->s0; break;
    case 0x0a8: r = !fa->s1; break;
    case 0x0aa: r = fa->s1; break;
    case 0x0ac: r = !fb->s1; break;
    case 0x0ae: r = fb->s1; break;
    case 0x0b0: r = (d->dp & 0x0f) == 0x00; break;
    case 0x0b1: r = (d->dp & 0x0f) != 0x00; break;
    case 0x0b2: r = (d->dp & 0x0f) == 0x0f; break;
    case 0x0b3: r = (d->dp & 0x0f) != 0x0f; break;
    case 0x0bc: r = !(d->sr & SR_RQM); break;
    case 0x0be: r = d->sr & SR_RQM; break;
    case 0x100: r = true; break;
    case 0x140:
      d->stack[d->sp & 3] = d->pc;
      d->sp = (d->sp + 1) & 3;
      r = true;
      break;
  }
  if(r) d->pc = na;
}

static void step(NecDsp* d) {
  uint32_t w = d->programRom[d->pc & 0x7ff];
  d->pc = (d->pc + 1) & 0x7ff;
  switch(w >> 22) {
    case 0: op(d, w); break;
    case 1:
      op(d, w);
      d->sp = (d->sp - 1) & 3;
      d->pc = d->stack[d->sp] & 0x7ff;
      break;
    case 2: jp(d, w); break;
    case 3: ld(d, (uint16_t) (w >> 6), w & 15); break;
  }
  int32_t product = (int16_t) d->k * (int16_t) d->l;
  d->m = (uint16_t) (product >> 15);
  d->n = (uint16_t) (product << 1);
}

void necdsp_catchUp(NecDsp* dsp, uint64_t masterCycles) {
  // Never more than a frame behind (after a long gap the chip is idle waiting anyway).
  if(masterCycles > dsp->cycles + 400000) dsp->cycles = masterCycles - 400000;
  while(dsp->cycles + MASTER_PER_INSN <= masterCycles) {
    step(dsp);
    dsp->cycles += MASTER_PER_INSN;
  }
}

void necdsp_runUntilReady(NecDsp* dsp, uint64_t masterCycles, bool forRead) {
  necdsp_catchUp(dsp, masterCycles);
  // A routine translated to C takes no time, and neither do these steps: the chip runs on
  // until it asks for the next word, and stays on the machine's clock (were it ahead,
  // original code talking to it next would find it not listening yet).
  for(int n = 0; (!(dsp->sr & SR_RQM) || (forRead && !dsp->drOut)) && n < 1000000; n++) step(dsp);
  // Then on to where it waits for the CPU (a "jump while RQM" to itself): the chip often
  // does a few more steps after asking, such as reading the old data register (DRNF),
  // which a real CPU is always too slow to get in before.
  for(int n = 0; n < 4096; n++) {
    uint32_t w = dsp->programRom[dsp->pc & 0x7ff];
    if(w >> 22 == 2 && ((w >> 13) & 0x1ff) == 0x0be && ((w >> 2) & 0x7ff) == (dsp->pc & 0x7ff)) break;
    step(dsp);
  }
}

uint8_t necdsp_readSr(NecDsp* d) { return (uint8_t) (d->sr >> 8); }

#include <stdio.h>
int necdsp_trace;
uint8_t necdsp_readDr(NecDsp* d) {
  if(necdsp_trace) fprintf(stderr, "R %04x sr=%04x out=%d pc=%03x\n", d->dr, d->sr, d->drOut, d->pc);
  if(!(d->sr & SR_DRC)) {
    if(!(d->sr & SR_DRS)) {
      d->sr |= SR_DRS;
      return (uint8_t) d->dr;
    }
    d->sr &= ~(SR_RQM | SR_DRS);
    d->drOut = false;
    return (uint8_t) (d->dr >> 8);
  }
  d->sr &= ~SR_RQM;
  d->drOut = false;
  return (uint8_t) d->dr;
}

void necdsp_writeDr(NecDsp* d, uint8_t val) {
  if(necdsp_trace) fprintf(stderr, "W %02x sr=%04x pc=%03x\n", val, d->sr, d->pc);
  d->drOut = false;
  if(!(d->sr & SR_DRC)) {
    if(!(d->sr & SR_DRS)) {
      d->sr |= SR_DRS;
      d->dr = (d->dr & 0xff00) | val;
      return;
    }
    d->sr &= ~(SR_RQM | SR_DRS);
    d->dr = (uint16_t) (val << 8) | (d->dr & 0xff);
    return;
  }
  d->sr &= ~SR_RQM;
  d->dr = (d->dr & 0xff00) | val;
}
