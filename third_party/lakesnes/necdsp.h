// NEC uPD77C25 digital signal processor (the DSP-1 to DSP-4 cartridge chips), run from
// its own firmware. Not part of LakeSnes upstream; added for Top Gear 3000's DSP-4.
#ifndef NECDSP_H
#define NECDSP_H

#include <stdbool.h>
#include <stdint.h>

typedef struct NecDspFlags {
  bool ov0, ov1, z, c, s0, s1;
} NecDspFlags;

typedef struct NecDsp {
  uint32_t programRom[2048];  // 24-bit words
  uint16_t dataRom[1024];
  uint16_t dataRam[256];
  uint16_t pc;   // 11 bits
  uint16_t rp;   // 10 bits
  uint8_t dp;
  uint16_t stack[4];
  uint8_t sp;
  uint16_t a, b, tr, trb, dr, sr, si, so, k, l, m, n;
  NecDspFlags fa, fb;
  uint64_t cycles;  // SNES master cycles the chip has caught up to
} NecDsp;

// Firmware: 2048 program words of 3 bytes then 1024 data words of 2 bytes, little-endian
// (8192 bytes). False if the size is wrong.
bool necdsp_load(NecDsp* dsp, const uint8_t* data, int size);
void necdsp_reset(NecDsp* dsp);
// Runs the chip up to the given SNES master cycle count.
void necdsp_catchUp(NecDsp* dsp, uint64_t masterCycles);
// For code translated to C, which takes no time: catches up, then runs the chip on until
// it is ready for the next transfer (its RQM status bit).
void necdsp_runUntilReady(NecDsp* dsp, uint64_t masterCycles);
uint8_t necdsp_readDr(NecDsp* dsp);
uint8_t necdsp_readSr(NecDsp* dsp);
void necdsp_writeDr(NecDsp* dsp, uint8_t val);

#endif
