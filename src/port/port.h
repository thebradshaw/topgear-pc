#ifndef TG_PORT_H
#define TG_PORT_H

#include <stdbool.h>
#include <stdint.h>

// Interface for routines translated from the ROM to C.
//
// While the port is incomplete, translated routines are called from emulated code: when
// the emulated CPU reaches a routine's entry point, the C version runs instead (or, when
// checking, alongside it; see lockstep.h). So a routine sees the CPU registers it was
// called with and must leave them as the original would. As callers are translated too,
// these register-level signatures turn into ordinary C parameters and return values.

// CPU registers as a translated routine sees them. Flags other than N/V/Z/C are fixed by
// the routine's entry requirements (TgPortFunc.mx) and must be left unchanged.
typedef struct TgRegs {
  uint16_t a, x, y;
  uint16_t d;   // direct page
  uint8_t db;   // data bank
  bool n, v, z, c;
} TgRegs;

typedef void TgPortFn(TgRegs* r);

// Called by a translated routine, before it changes anything, when the original would
// never return (a deliberate hang): the call then runs emulated instead.
void tg_port_decline(void);
bool tg_port_take_declined(void);

// Runs original code from a translated routine: a JSR to target in the routine's bank,
// returning to ret (the address after the original's JSR), with the registers in r and
// the given M/X widths (TG_MX_*). r holds the registers it returns with. Translated
// routines reached inside run emulated too.
void tg_call_jsr(TgRegs* r, uint16_t target, uint16_t ret, uint8_t mx);
// The same for original code reached by a JMP that comes back by jumping to stop.
void tg_call_jmp(TgRegs* r, uint16_t target, uint16_t stop, uint8_t mx);
bool tg_port_in_emulated_call(void);
// Lowest stack pointer reached in original code run from C since the last call.
uint16_t tg_port_take_min_sp(void);

enum {
  TG_CALL_JSR = 0,  // returns with RTS
  TG_CALL_JSL = 1,  // returns with RTL
};

// Entry width requirements: the routine only runs translated when M/X match.
enum {
  TG_MX_ANY = 0,
  TG_MX_16 = 1,     // M=0, X=0 (16-bit A and index)
  TG_MX_8 = 2,      // M=1, X=1
  TG_MX_A8_I16 = 3, // M=1, X=0
  TG_MX_I16 = 4,    // X=0, M either
};

// Registers the callers never read after the routine returns. They aren't compared, and in
// verify mode they're overwritten with junk after each call, so the recording tests prove
// nothing reads them.
enum {
  TG_DEAD_A = 1,
  TG_DEAD_X = 2,
  TG_DEAD_Y = 4,
  TG_DEAD_FLAGS = 8,  // N, V, Z and C
  TG_DEAD_AXY_FLAGS = 15,
  TG_DEAD_NVZ = 16,   // N, V and Z only: callers test the carry
};

typedef struct TgPortFunc {
  uint32_t addr;     // entry point, bank:address
  const char* name;  // as in disasm/symbols.txt
  TgPortFn* fn;
  uint8_t call;      // TG_CALL_*
  uint8_t mx;        // TG_MX_*: widths required on entry
  uint8_t mx_out;    // TG_MX_*: widths on return (TG_MX_ANY: as on entry)
  uint8_t dead;      // TG_DEAD_*
  uint16_t bus_noise; // a low-RAM byte the original fills from open bus, where HDMA can leave
                      // a byte mid-instruction; not compared (0: none)
} TgPortFunc;

// All translated routines (registry.c).
extern const TgPortFunc tg_port_funcs[];
extern const int tg_port_func_count;
const TgPortFunc* tg_port_find(uint32_t addr);

// 128 KiB of work RAM, $7E:0000-$7F:FFFF. Low RAM ($0000-$1FFF in banks $00-$3F) is its
// first 8 KiB.
extern uint8_t* g_ram;

static inline uint16_t ram16(uint32_t adr) { return (uint16_t) (g_ram[adr] | g_ram[adr + 1] << 8); }
static inline void ram16_set(uint32_t adr, uint16_t v) { g_ram[adr] = (uint8_t) v; g_ram[adr + 1] = (uint8_t) (v >> 8); }

// Any address on the CPU bus: ROM, RAM, PPU and CPU registers. Use for hardware
// registers; ROM tables are cheaper through tg_rom.
uint8_t tg_bus_read8(uint32_t adr);
void tg_bus_write8(uint32_t adr, uint8_t val);
static inline void tg_bus_write16(uint32_t adr, uint16_t v) { tg_bus_write8(adr, (uint8_t) v); tg_bus_write8(adr + 1, (uint8_t) (v >> 8)); }

// Reads ROM and RAM directly and anything else through the bus.
uint8_t tg_read8(uint32_t adr);
static inline uint16_t tg_read16(uint32_t adr) {
  return (uint16_t) (tg_read8(adr) | tg_read8((adr & 0xff0000) | (uint16_t) (adr + 1)) << 8);
}

// ROM bytes at a LoROM bank:address.
const uint8_t* tg_rom(uint32_t adr);
static inline uint16_t rom16(uint32_t adr) { const uint8_t* p = tg_rom(adr); return (uint16_t) (p[0] | p[1] << 8); }

// Connects the helpers above to the machine (port.c).
typedef struct Snes Snes;
void tg_port_bind(Snes* snes, const uint8_t* rom);

// Flag helpers for routines whose callers look at N/Z/C/V after they return.
static inline void flags_nz16(TgRegs* r, uint16_t v) { r->n = v >> 15; r->z = v == 0; }
// A - b - !C in 16-bit binary mode, as SBC sets the flags.
static inline uint16_t sbc16(TgRegs* r, uint16_t a, uint16_t b) {
  uint32_t res = (uint32_t) a - b - (r->c ? 0 : 1);
  uint16_t out = (uint16_t) res;
  r->v = ((a ^ b) & (a ^ out) & 0x8000) != 0;
  r->c = res < 0x10000;
  flags_nz16(r, out);
  return out;
}
static inline uint16_t adc16(TgRegs* r, uint16_t a, uint16_t b) {
  uint32_t res = (uint32_t) a + b + (r->c ? 1 : 0);
  uint16_t out = (uint16_t) res;
  r->v = (~(a ^ b) & (a ^ out) & 0x8000) != 0;
  r->c = res > 0xffff;
  flags_nz16(r, out);
  return out;
}
static inline void cmp16(TgRegs* r, uint16_t a, uint16_t b) { r->c = a >= b; flags_nz16(r, (uint16_t) (a - b)); }

#endif
