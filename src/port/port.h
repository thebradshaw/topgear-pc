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
// The same for a JSL to bank:address from the routine's bank.
void tg_call_jsl(TgRegs* r, uint32_t target, uint16_t ret, uint8_t mx);
// CLI: lets IRQs in.
void tg_cpu_cli(void);
// Sets the CPU's program bank, returning the old one: C code calling a translated routine
// from another bank directly sets it first, as that routine's tg_call_* are in its bank.
uint8_t tg_port_set_bank(uint8_t bank);
bool tg_port_in_emulated_call(void);
// Lowest stack pointer reached in original code run from C since the last call.
uint16_t tg_port_take_min_sp(void);

enum {
  TG_CALL_JSR = 0,  // returns with RTS
  TG_CALL_JSL = 1,  // returns with RTL
  TG_CALL_NONE = 2, // reached by a jump or a return, and never returns
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
  uint8_t flags;     // TG_WAITS, TG_TIMED
} TgPortFunc;

// The routine waits for interrupts (tg_port_wait). It runs on its own stack, as a fiber,
// and only in native mode: the per-call check can't follow a call across frames.
// TG_TIMED: the original code that calls it relies on how long it takes (the menu fade
// routines: a screen queued for the next NMI goes out before the fade's first step). The C code
// that calls it allows for that; the reference run of tools/shift_compare.py, where the
// callers run emulated, runs it emulated too.
enum { TG_WAITS = 1, TG_TIMED = 2 };

// Inside a routine marked TG_WAITS: lets the machine run until the next interrupt has
// been handled. Use in place of the original's spin loops, checking the condition after
// each return, as the original did between interrupts.
void tg_port_wait(void);

// Where the machine has to be for a waiting routine to carry on: after tg_port_wait, back
// where it waited, with the CPU idling (WAI) until then; after a call to original code
// (tg_call_*, which from a waiting routine runs on the machine itself, so it may take
// frames), at the call's return.
typedef struct TgResumePoint {
  uint16_t pc, sp;
  uint8_t k;
  bool idle;
} TgResumePoint;
const TgResumePoint* tg_port_resume_point(void);

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
// The APU ports, $2140-$2143 (in verify mode, what the original read).
uint8_t tg_apu_read8(uint32_t adr);
void tg_port_set_apu_replay(int (*fn)(void* ctx, uint32_t adr), void* ctx);
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

// Video frames per race update when the race loop runs in C (program.c). The original
// takes 3 when it keeps up and more when it doesn't (slowdown); the C loop always takes
// this many. 2 is the most the loop allows: it's what the original would do with no CPU
// time, which is how the comparison tests run it.
extern int tg_port_race_frames;

// Smooth scaling of race objects (sprites.c), an enhancement: when set, the sprite builder
// tags each object's OAM entries in tg_port_obj_tags with a scale in tg_port_obj_scales,
// which the PPU applies (tg_game_set_smooth_sprites connects them).
extern bool tg_port_smooth_sprites;
// Counts the race's top-of-frame interrupts (RaceIrqTop): changes every frame of a race.
extern uint32_t tg_port_race_irqs;
extern uint16_t tg_port_obj_tags[0x2000];
// Full-screen 1-player view and widescreen (sprites.c, LakeSnes ppu_full.c), enhancements:
// when either is on, the sprite builder records each OAM entry's ground line in
// tg_port_obj_grounds (widescreen: and keeps sprites out to the wider edges), and
// RaceIrqTop sets tg_port_race_frame (PPU_RACE_FRAME, PPU_FULL_VIEW in 1-player races) for
// the PPU to redraw the next frame (tg_game_set_full_view and tg_game_set_wide connect
// them).
extern bool tg_port_full_view;
extern bool tg_port_side_by_side;  // 2 players in widescreen: the views side by side
extern int tg_port_wide_margin;  // extra pixels on each side, 0 when off
extern uint8_t tg_port_race_frame;
extern int16_t tg_port_obj_grounds[0x2000];

// Ghost car (ghost.c), an enhancement: the player's best race on each course, replayed as a
// see-through car in 1-player races. The sprite builder flags the ghost's OAM entries in
// tg_port_obj_flags (PPU_OBJ_GHOST) for the PPU to draw them see-through.
#define TG_GHOST_COURSES 32
#define TG_GHOST_SAMPLES 8192
typedef struct TgGhostSample {
  uint16_t time;  // the race clock
  uint16_t pos, side, kind;  // the car record's track position, side position, kind bits
} TgGhostSample;
typedef struct TgGhostLap {
  uint16_t time;  // the race's finishing time
  int count;      // samples; 0 for no lap yet
  TgGhostSample samples[TG_GHOST_SAMPLES];
} TgGhostLap;
extern bool tg_port_ghost;
extern TgGhostLap tg_port_ghosts[TG_GHOST_COURSES];  // by course
extern bool tg_port_ghosts_changed;  // set when a new best lap is recorded
extern uint16_t tg_port_ghost_key, tg_port_ghost_word;
extern uint8_t tg_port_obj_flags[0x2000];
void tg_port_ghost_update(void);

// High frame rate (an enhancement): in-between frames are drawn from the last two race
// frames (LakeSnes ppu_full.c). The renderer records each road line's depth and camera
// position by the address of its scroll entry, and each object list entry's object
// (tg_port_list_ident, by list: 0 the top view's, and entry): 0x8000 + car for cars,
// else 1 + segment x 2 (+1 for a window object). visible.c keeps which car each entry of
// the visible car lists is (tg_port_view_car); the sprite builder marks each OAM entry
// with its object, plus the view (0x4000 for the bottom one), and the object's position.
extern bool tg_port_interp;
extern int16_t tg_port_line_depth[0x2000];
extern uint16_t tg_port_line_cam[0x2000];
extern uint16_t tg_port_list_ident[2][0x40];
extern uint8_t tg_port_view_car[2][0x14];
extern uint16_t tg_port_obj_idents[0x2000];
extern int16_t tg_port_obj_ax[0x2000], tg_port_obj_ay[0x2000];
#define TG_GHOST_CAR 0x7F  // tg_port_view_car for the ghost

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
