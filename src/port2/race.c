// Top Gear 2: the race's main loop, for one player ($01:826D-$83BD) and two ($8522-$86A8).
//
// The loop runs as often as it can: each pass steps the game by the frames gone since the
// last one ($F8, from the frame counter at $1A that the NMI keeps), builds the next picture
// into the back buffers and flags it ready ($0122) for the NMI to show. The original takes
// three or four frames a pass.

#include <stddef.h>

#include "../port/port.h"

void Tg2DriveCar1(TgRegs* r);
void Tg2Cars(TgRegs* r);
void Tg2Stream1(TgRegs* r);
void Tg2BuildRoad(TgRegs* r);
void Tg2DrawObjects1P(TgRegs* r);
void Tg2DriveCar2(TgRegs* r);
void Tg2Stream2(TgRegs* r);
void Tg2BuildRoad2P(TgRegs* r);
void Tg2DrawObjects2P(TgRegs* r);
void Tg2RaceOptions(TgRegs* r);
void Tg2StartingGrid(TgRegs* r);
void Tg2RacePlayers(TgRegs* r);
void Tg2RaceSetup(TgRegs* r);
void Tg2DashboardMap1P(TgRegs* r);
void Tg2DashboardMap2P(TgRegs* r);
void Tg2VramDma7E(TgRegs* r);

static uint16_t w(uint16_t adr) { return ram16(adr); }
static void ws(uint16_t adr, uint16_t v) { ram16_set(adr, v); }
static bool neg(uint16_t v) { return (v & 0x8000) != 0; }

static void in_bank(uint8_t bank, void (*fn)(TgRegs*), TgRegs* r) {
  uint8_t old = tg_port_set_bank(bank);
  r->d = 0;
  r->db = 0x82;
  fn(r);
  tg_port_set_bank(old);
}

// Waits for the NMI to read the joypads ($0020 changes every frame).
static void next_frame(void) {
  uint16_t a = w(0x0020);
  while(w(0x0020) == a) tg_port_wait();
}

// True if `bits` were set in a pad's newly pressed buttons ($0030, $0032), which it clears.
static bool pressed(uint16_t pad, uint16_t bits) {
  uint16_t a = w(pad);
  ws(pad, a & (uint16_t) ~bits);
  return (a & bits) != 0;
}

// $83BF / $86AA: Start pauses the race (once the screen is at full brightness) until it
// is pressed again. True if the race was left instead, with L and R held.
static bool start_pressed(bool two) { return pressed(0x0030, 0x1000) || (two && pressed(0x0032, 0x1000)); }
static bool l_and_r(uint16_t pad) { return (w(pad) & 0x0030) == 0x0030; }

static bool paused(bool two) {
  if(!start_pressed(two) || (w(0x0059) & 0x000F) < 7) return false;
  ws(0x0100, 0xFFFF);
  g_ram[0x00FC] = 0xF8;
  do {
    next_frame();
    if(l_and_r(0x0028) || (two && l_and_r(0x002A))) return true;
  } while(!start_pressed(two));
  if(two) ws(0x005B, 0x0040);
  ws(0x00FC, 0x0008);
  ws(0x0100, 0);
  return false;
}

// $E25F: a random number, stirred with the PPU's beam position outside the demo.
static uint16_t rotl_swapped(uint16_t v) {
  v = (uint16_t) (v << 8 | v >> 8);
  return (uint16_t) (v << 1 | v >> 15);
}
static uint16_t random_regs(uint16_t a, uint16_t x, uint16_t y, bool carry, TgRegs* out) {
  uint32_t t = (uint32_t) x + a + carry;
  t = (t & 0xFFFF) + y + (t >> 16);
  uint16_t d0 = (uint16_t) t;
  x = d0;
  if(w(0xFF1A) == 0) {
    tg_bus_read8(0x2137);
    tg_bus_read8(0x213F);
    uint8_t lo = tg_bus_read8(0x213C);
    x = (uint16_t) (lo | tg_bus_read8(0x213C) << 8);
  }
  ws(0x00CC, (uint16_t) (x + w(0x00CC)));
  uint16_t d2 = rotl_swapped(w(0x00C7)) ^ d0;
  // The subtraction comes with the carry clear, so it takes one more.
  uint16_t cb = rotl_swapped(w(0x00CB));
  bool c = (uint32_t) cb >= (uint32_t) d0 + 1;
  d0 = (uint16_t) (cb - d0 - 1);
  ws(0x00D0, d0);
  ws(0x00D2, d2);
  uint16_t ce = w(0x00CE);
  ws(0x00CE, w(0x00C2));
  ws(0x00C2, w(0x00C4));
  ws(0x00C4, w(0x00C6));
  ws(0x00C6, w(0x00C8));
  ws(0x00C8, w(0x00CA));
  ws(0x00CA, w(0x00CC) ^ d0);
  ws(0x00CC, ce ^ d2);
  t = (uint32_t) w(0x00C2) + w(0x00C4) + c;
  uint32_t u = (t & 0xFFFF) + (uint16_t) ~w(0x00CA) + (t >> 16);
  uint16_t v = (uint16_t) u;
  c = (v & 1) != 0;
  v >>= 1;
  if(c) v |= 0x8000;
  v++;
  t = (uint32_t) v + d0 + c;
  u = (t & 0xFFFF) + (uint16_t) ~d2 + (t >> 16);
  ws(0x00C2, (uint16_t) u);
  if(out) {
    // The registers and flags it leaves: X the old $CE, and the last SBC's flags.
    uint16_t m = (uint16_t) t, res = (uint16_t) u;
    out->a = res;
    out->x = ce;
    out->c = (u >> 16) != 0;
    out->v = ((m ^ d2) & (m ^ res) & 0x8000) != 0;
    out->n = (res & 0x8000) != 0;
    out->z = res == 0;
  }
  return (uint16_t) u;
}

uint16_t tg2_random16(uint16_t a, uint16_t x, uint16_t y, bool carry) {
  return random_regs(a, x, y, carry, NULL);
}

// $01:E25F with the registers: A, X and the flags as it leaves them (Y is kept).
void tg2_random(TgRegs* r) { random_regs(r->a, r->x, r->y, r->c, r); }

// Leaves the race for the code the menus named at $7E:FF16.
static void leave(void) {
  ws(0x0076, w(0xFF16));
  ws(0x0078, w(0xFF18));
  ws(0x0596, 0);
  tg_cpu_sei();
  tg_port_leave((uint32_t) g_ram[0x0078] << 16 | w(0x0076));
}

// The rain's on/off switch for a view: latched while on, and turned over when asked.
static void rain_before(uint16_t state, uint16_t shown, uint16_t car) {
  if(neg(w(state))) ws(shown, w(state));
  if(pressed(car, 0x0020)) ws(state, (uint16_t) ~w(state));
}
static void rain_after(uint16_t state, uint16_t shown) {
  if(!neg(w(state))) ws(shown, w(state));
}

// A race, from its first frame until it is over.
static void race(TgRegs* r, bool two) {
  ws(0x00F6, (uint16_t) (w(0x001A) - 1));
  for(;;) {
    while(neg(w(0x0122))) tg_port_wait();
    ws(0x001E, (uint16_t) (w(0x001E) + 1));
    uint16_t now = w(0x001A);
    ws(0x00F8, (uint16_t) (now - w(0x00F6)));
    ws(0x00F6, now);
    if(paused(two)) {
      // Left from the pause: the screen fades out first.
      ws(0xFEC6, 0);
      for(;;) {
        next_frame();
        uint16_t a = w(0x0058) & 0x0FFF;
        if(a < 0x0080) break;
        ws(0x0058, (uint16_t) (a - 0x0080));
      }
      return leave();
    }
    in_bank(0x80, Tg2DriveCar1, r);
    if(two) in_bank(0x80, Tg2DriveCar2, r);
    in_bank(0x80, Tg2Cars, r);
    in_bank(0x88, Tg2Stream1, r);
    if(two) in_bank(0x88, Tg2Stream2, r);
    in_bank(0x88, two ? Tg2BuildRoad2P : Tg2BuildRoad, r);
    in_bank(0x8B, two ? Tg2DrawObjects2P : Tg2DrawObjects1P, r);
    if(w(0x0514) == 0) ws(0x0122, 0xFFFF);
    // Lightning: when the last flash is over, the next comes at random. (The original
    // stirs in whatever X, Y and the carry the drawing left; here they are zero.)
    if(neg(w(0x0500)) && w(0x0502) == 0) {
      uint16_t a = tg2_random16(0, 0, 0, false);
      if(a < 0x0080) ws(0x0502, (uint16_t) ((a >> 2) + 4));
    }
    if(two) {
      if(neg(w(0x0504))) ws(0x0506, w(0x0504));
      if(neg(w(0x050C))) ws(0x050E, w(0x050C));
      if(pressed(0x0634, 0x0020)) ws(0x0504, (uint16_t) ~w(0x0504));
      if(pressed(0x0636, 0x0020)) ws(0x050C, (uint16_t) ~w(0x050C));
    } else {
      rain_before(0x0504, 0x0506, 0x0634);
    }
    // The race is over when the time is up ($0024, $0026), or a while after the finish
    // ($06F2 counts from it).
    bool over = w(0x0024) != 0 && (w(0x0674) != 0 || w(0x0026) >= 0x0600);
    if(!over) {
      bool finished = w(0x06F0) == (two ? 2 : 1);
      if(!finished && w(0x06EE) >= 0x0014) {
        if(!two || w(0x0A16) == 0) ws(0x0A16, 0x0015);
        if(two && w(0x0A32) == 0) ws(0x0A32, 0x0015);
        finished = true;
      }
      if(finished) {
        uint16_t a = w(0x06F2);
        if(a == 0) ws(0x06F2, 0x7ED4);
        else if(neg(a)) over = true;
        else ws(0x06F2, (uint16_t) (a + w(0x00F8)));
      }
    }
    if(over) {
      if((w(0x0059) & 0x000F) == 0) break; // faded out
      if(w(0x005B) == 0) {
        ws(0x005B, 0xFF80);
        if(g_ram[0x0598] != 0) g_ram[0x00FC] = 0xF7;
      }
    }
    rain_after(0x0504, 0x0506);
    if(two) rain_after(0x050C, 0x050E);
    if(w(0x0514) != 0) ws(0x0122, 0xFFFF);
  }
  // Outside the demo, the other cars run on until twenty have finished.
  if(w(0xFF1A) == 0 && w(0x06EE) < 0x0014) {
    ws(0x00F2, 0xFFFF);
    do {
      ws(0x00F8, 0x0011);
      ws(0x001A, (uint16_t) (w(0x001A) + 0x0011));
      in_bank(0x80, Tg2Cars, r);
    } while(w(0x06EE) < 0x0014);
  }
  leave();
}

// $01:826D and $01:8522.
void Tg2RaceLoop1P(TgRegs* r) { race(r, false); }
void Tg2RaceLoop2P(TgRegs* r) { race(r, true); }

// $E2DA: waits for A + 1 NMIs ($1C counts up to zero), then times the frame as the race
// loop does.
static void wait_nmis(uint16_t a) {
  ws(0x001C, (uint16_t) ~a);
  while(w(0x001C) != 0) tg_port_wait();
  ws(0x001E, (uint16_t) (w(0x001E) + 1));
  uint16_t now = w(0x001A);
  ws(0x00F8, (uint16_t) (now - w(0x00F6)));
  ws(0x00F6, now);
}

// The race's HDMA channels 0-6: the road's scroll and window tables (bank $7E, indirect
// from banks $8D-$8F) and the backdrop's colours (bank $87).
static void race_hdma(void) {
  static const uint8_t kSource[7] = { 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x87, 0x87 };
  static const uint8_t kIndirect[7] = { 0x7E, 0x7E, 0x8D, 0x8E, 0x8F, 0x87, 0x87 };
  static const uint16_t kMode[7] = { 0x0F43, 0x2143, 0x2143, 0x2143, 0x2143, 0x2644, 0x1143 };
  for(int i = 0; i < 7; i++) tg_bus_write8((uint16_t) (0x4304 + 0x10 * i), kSource[i]);
  for(int i = 0; i < 7; i++) tg_bus_write8((uint16_t) (0x4307 + 0x10 * i), kIndirect[i]);
  for(int i = 0; i < 7; i++) tg_bus_write16((uint16_t) (0x4300 + 0x10 * i), kMode[i]);
  tg_bus_write16(0x4302, w(0x028E));
  for(int i = 1; i < 5; i++) tg_bus_write16((uint16_t) (0x4302 + 0x10 * i), w(0x028C));
  tg_bus_write16(0x4352, 0xF5FA);
  tg_bus_write16(0x4362, 0xF5FA);
}

// $01:8145: the game's flow from the menus into a race: sets the race up from the menus'
// choices, draws its first picture, starts the screen's interrupts and runs the race.
void Tg2GameLoop(TgRegs* r) {
  in_bank(0x80, Tg2RaceOptions, r);
  bool two = neg(w(0x0102));
  in_bank(0x81, Tg2RaceSetup, r);
  in_bank(0x80, Tg2RacePlayers, r);
  in_bank(0x81, two ? Tg2DashboardMap2P : Tg2DashboardMap1P, r);
  r->a = 0x0400;
  r->x = 0xC910;
  r->y = 0x7800;
  in_bank(0x81, Tg2VramDma7E, r);
  in_bank(0x80, Tg2StartingGrid, r);
  in_bank(0x88, two ? Tg2BuildRoad2P : Tg2BuildRoad, r);
  in_bank(0x8B, two ? Tg2DrawObjects2P : Tg2DrawObjects1P, r);
  ws(0x001A, 0);
  if(w(0xFF1A) != 0) {  // the demo: no time limit
    ws(0x0026, 0);
    ws(0x0024, 0xFFFF);
  }
  ws(0x0122, 0xFFFF);
  tg_bus_write8(0x420C, 0x00);
  tg_bus_write8(0x210E, two ? 0x03 : 0xFF);
  tg_bus_write8(0x210E, two ? 0x00 : 0x03);
  race_hdma();
  wait_nmis(0);
  tg_bus_write8(0x420C, 0x7F);
  // The race's NMI and IRQ handlers.
  ws(0x0011, two ? 0xB345 : 0xAC79);
  ws(0x0016, 0xE47F);
  g_ram[0x0013] = 0x81;
  g_ram[0x0018] = 0x81;
  ws(0x001A, 0);
  ws(0x001C, 0);
  ws(0x001E, 0);
  ws(0x0062, 0);
  ws(0x0064, 0);
  wait_nmis(0);
  tg_cpu_cli();
  ws(0x005B, 0x0040);
  ws(0x06F4, 0x01E0);
  ws(0x0634, w(0x0634) | 0x8000);
  ws(0x0636, w(0x0636) | 0x8000);
  ws(0x01AC, 0);
  ws(0x01C6, 0);
  race(r, two);
}
