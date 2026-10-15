#include "coverage.h"

#include <stdio.h>
#include <string.h>

// Instruction lengths with 8-bit registers. Immediates that grow by one byte with
// 16-bit registers are handled in tg_w65816_length; 'x' marks the index ones.
static const char lengths[257] =
  "2222222212113334"  // 00
  "2222222213113334"  // 10
  "3242222212113334"  // 20
  "2222222213113334"  // 30
  "1222322212113334"  // 40
  "2222322213114334"  // 50
  "1232222212113334"  // 60
  "2222222213113334"  // 70
  "2232222212113334"  // 80
  "2222222213113334"  // 90
  "x2x2222212113334"  // A0
  "2222222213113334"  // B0
  "x222222212113334"  // C0
  "2222222213113334"  // D0
  "x222222212113334"  // E0
  "2222322213113334";  // F0

int tg_w65816_length(uint8_t opcode, bool m8, bool x8) {
  switch(opcode) {
    case 0x09: case 0x29: case 0x49: case 0x69: case 0x89: case 0xa9: case 0xc9: case 0xe9:
      return m8 ? 2 : 3;
    case 0xa0: case 0xa2: case 0xc0: case 0xe0:
      return x8 ? 2 : 3;
  }
  char c = lengths[opcode];
  return c - '0';
}

int32_t tg_lorom_offset(uint32_t adr) {
  uint8_t bank = (uint8_t) (adr >> 16);
  if((bank & 0x7e) == 0x7e && bank < 0x80) return -1;  // work RAM
  if((adr & 0x8000) == 0) return -1;
  return (int32_t) ((((uint32_t) (bank & 0x7f) << 15) | (adr & 0x7fff)) % TG_ROM_SIZE);
}

static int32_t ramOffset(uint32_t adr) {
  uint8_t bank = (uint8_t) (adr >> 16);
  if(bank == 0x7e || bank == 0x7f) return (int32_t) (adr & 0x1ffff);
  if((bank & 0x40) == 0 && (adr & 0xffff) < 0x2000) return (int32_t) (adr & 0x1fff);
  return -1;
}

void tg_coverage_init(TgCoverage* cov) {
  memset(cov, 0, sizeof(*cov));
  cov->prevNext = 0xffffffff;
}

static bool isCall(uint8_t op) { return op == 0x20 || op == 0x22 || op == 0xfc; }

void tg_coverage_on_opcode(TgCoverage* cov, uint32_t adr, const uint8_t* bytes, bool m8, bool x8, bool emu, uint8_t db, uint16_t dp) {
  int len = tg_w65816_length(bytes[0], m8, x8);
  uint8_t widths = m8 ? (x8 ? TG_COV_M8X8 : TG_COV_M8X16) : (x8 ? TG_COV_M16X8 : TG_COV_M16X16);
  uint8_t mode = widths | (emu ? TG_COV_EMU : 0);
  bool target = adr != cov->prevNext;
  bool called = target && isCall(cov->prevOpcode);
  cov->prevNext = (adr & 0xff0000) | ((adr + len) & 0xffff);
  cov->prevOpcode = bytes[0];

  int32_t off = tg_lorom_offset(adr);
  if(off < 0) {
    int32_t r = ramOffset(adr);
    if(r >= 0) cov->ramFlags[r] |= TG_COV_OPCODE | mode;
    return;
  }
  uint8_t* f = &cov->flags[off];
  if(*f & TG_COV_OPCODE) {
    if(cov->db[off] != db) cov->entry[off] |= TG_COV_DB_VARIES;
    if(cov->dp[off] != dp) cov->entry[off] |= TG_COV_DP_VARIES;
  } else {
    cov->db[off] = db;
    cov->dp[off] = dp;
  }
  *f |= TG_COV_OPCODE | mode;
  if(target) cov->entry[off] |= TG_COV_FLOW_TARGET;
  if(called) cov->entry[off] |= TG_COV_CALL_TARGET;
  for(int i = 1; i < len; i++) {
    int32_t o = tg_lorom_offset((adr & 0xff0000) | ((adr + i) & 0xffff));
    if(o >= 0) cov->flags[o] |= TG_COV_OPERAND;
  }
}

void tg_coverage_on_read(TgCoverage* cov, uint32_t adr) {
  int32_t off = tg_lorom_offset(adr);
  if(off >= 0) cov->flags[off] |= TG_COV_READ;
}

static const char magic[8] = "TGCOV01";

bool tg_coverage_save(const TgCoverage* cov, const char* path) {
  FILE* f = fopen(path, "wb");
  if(f == NULL) return false;
  bool ok = fwrite(magic, 1, 8, f) == 8 &&
    fwrite(cov->flags, 1, TG_ROM_SIZE, f) == TG_ROM_SIZE &&
    fwrite(cov->entry, 1, TG_ROM_SIZE, f) == TG_ROM_SIZE &&
    fwrite(cov->db, 1, TG_ROM_SIZE, f) == TG_ROM_SIZE &&
    fwrite(cov->dp, 2, TG_ROM_SIZE, f) == TG_ROM_SIZE &&
    fwrite(cov->ramFlags, 1, TG_COV_RAM_SIZE, f) == TG_COV_RAM_SIZE;
  return fclose(f) == 0 && ok;
}

bool tg_coverage_load(TgCoverage* cov, const char* path) {
  tg_coverage_init(cov);
  FILE* f = fopen(path, "rb");
  if(f == NULL) return false;
  char m[8];
  bool ok = fread(m, 1, 8, f) == 8 && memcmp(m, magic, 8) == 0 &&
    fread(cov->flags, 1, TG_ROM_SIZE, f) == TG_ROM_SIZE &&
    fread(cov->entry, 1, TG_ROM_SIZE, f) == TG_ROM_SIZE &&
    fread(cov->db, 1, TG_ROM_SIZE, f) == TG_ROM_SIZE &&
    fread(cov->dp, 2, TG_ROM_SIZE, f) == TG_ROM_SIZE &&
    fread(cov->ramFlags, 1, TG_COV_RAM_SIZE, f) == TG_COV_RAM_SIZE;
  fclose(f);
  if(!ok) tg_coverage_init(cov);
  return ok;
}
