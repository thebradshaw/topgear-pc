// Driving the players' cars (bank $05): gears, engine speed, acceleration, fuel, moving
// along the track and across it, steering, track objects, and bumping into other cars.
//
// RaceDrivePlayers is one long routine in the original, with a copy of the per-player part
// for each player. The copies differ only in addresses, apart from a few slips kept here
// as quirks. It runs with 16-bit A and index registers; absolute addresses are in the
// data bank (bank $05's tables at $8000 and up, low RAM below $2000).
//
// Per-player state flags (RaceP1Bumped's byte, $1EF5 / $1EF6):
//   $01  pushed sideways past the steering limit (skids, with tyre smoke)
//   $04  finished the race
//   $10  engine revved on the start line, settling
//   $20  out of fuel
//   $80  bumped something

#include "port.h"
#include "variables.h"

typedef struct Driver {
  uint16_t view_off;     // $0040 / $0053: nonzero when the view is off (no player)
  uint16_t ai;           // bit 15: driven by the computer (word read; a byte is set at finish)
  uint16_t state;        // flags above
  uint16_t speed, rpm, gear, gear_tab, fuel, smoke, nitro;
  uint16_t accel, brake, shift_up, shift_down, nitro_button, steer_minus, steer_plus;  // button masks
  uint8_t held, pressed;            // direct page joypad words
  uint8_t revving_held;             // the joypad word the settling revs test (P2: P1's)
  uint16_t messages;                // sound / message request word
  uint16_t pos;                     // 24-bit track position, low byte first
  uint16_t x;                       // side-to-side position
  uint16_t sign_hit;                // bit 15 set once a sign marking a special zone is passed
  uint16_t fuel_rate;               // fuel use byte
  uint16_t running_bit;             // bit in $1EB4, cleared when the fuel runs out
  uint16_t skid;                    // sideways push, counts down
  uint16_t steer, drift;            // steering words: angle, and the turn it keeps
  uint16_t edge_mask;               // byte of $1EBE: the track edges stop the car
  uint16_t finished;                // bit 15: the player has finished (byte set at the line)
  uint16_t sway, view_x;            // $1E9A / $1E9C, and the view's sideways scroll word (dp)
  uint16_t angle_base;              // base sprite angle
  uint16_t car;                     // RaceP1Car / RaceP2Car
  uint16_t car_pos;                 // sprite angle word for RaceBuildSprites
  uint16_t brake_bit;               // bit in $0108 while braking
  uint16_t object_handlers;         // table of track object routines
  uint16_t object_ret_right, object_ret_left;  // where the original's JSRs return
  uint16_t solid_handler, side_handler;        // two of the routines in it
  uint16_t lane;                    // $1EC9 / $1EC4: the gap the computer aims for (2 words, flag byte at +3)
  bool brake_tables;     // player 1 loads the braking grip limits; player 2 keeps the others
  bool skid_doubles;     // player 2 doubles the sideways speed while skidding
} Driver;

static const Driver kDrivers[2] = {
  { 0x0040, 0x1EF2, 0x1EF5, 0x1E5A, 0x1E5E, 0x1E72, 0x1EE8, 0x1E62, 0x1E66, 0x1E6C,
    0x1F76, 0x1F78, 0x1F7A, 0x1F7C, 0x1F7E, 0x1F80, 0x1F82, 0x10, 0x14, 0x10, 0x0118,
    0x1ED6, 0x1E8A, 0x1E86, 0x1EFD, 0x0001, 0x1EEC, 0x1E7E, 0x1E82, 0x00FF, 0x1EDB,
    0x1E9A, 0x48, 0x1EB6, 0x0204, 0x182E, 0x8000, 0xF5C3, 0xD7A0, 0xD7E4, 0xF6B3, 0xF6F5,
    0x1EC9, true, false },
  { 0x0053, 0x1EF3, 0x1EF6, 0x1E5C, 0x1E60, 0x1E74, 0x1EEA, 0x1E64, 0x1E68, 0x1E6E,
    0x1F93, 0x1F95, 0x1F97, 0x1F99, 0x1F9B, 0x1F9D, 0x1F9F, 0x12, 0x16, 0x10, 0x012A,
    0x1ED9, 0x1E8C, 0x1E88, 0x1EFE, 0x0002, 0x1EEE, 0x1E80, 0x1E84, 0xFF00, 0x1EDC,
    0x1E9C, 0x5B, 0x1EB8, 0x0206, 0x1830, 0x4000, 0xF63B, 0xE006, 0xE04A, 0xF729, 0xF76B,
    0x1EC4, false, true },
};

static uint8_t g_db;
static uint16_t g_d;

static uint16_t tab16(uint16_t adr) { return tg_read16((uint32_t) g_db << 16 | adr); }
static uint8_t tab8(uint16_t adr) { return tg_read8((uint32_t) g_db << 16 | adr); }
static uint16_t dp16(uint8_t o) { return ram16((uint16_t) (g_d + o)); }
static void dp16_set(uint8_t o, uint16_t v) { ram16_set((uint16_t) (g_d + o), v); }
static uint16_t neg(uint16_t v) { return (uint16_t) -v; }
static uint16_t swap(uint16_t v) { return (uint16_t) (v << 8 | v >> 8); }
static uint16_t asr(uint16_t v) { return (uint16_t) ((v >> 1) | (v & 0x8000)); }
static void tsb16(uint16_t adr, uint16_t m) { ram16_set(adr, ram16(adr) | m); }
// TRB: returns whether any of the bits were set.
static bool trb16(uint16_t adr, uint16_t m) {
  uint16_t v = ram16(adr);
  ram16_set(adr, v & (uint16_t) ~m);
  return (v & m) != 0;
}
static bool trb_dp(uint8_t o, uint16_t m) { return trb16((uint16_t) (g_d + o), m); }

// The PPU's signed 16 x 8 multiply, written as the original writes it: A low then high,
// then each byte it writes to B (the last one counts).
static void m7a(uint16_t a) {
  tg_bus_write8(0x211B, (uint8_t) a);
  tg_bus_write8(0x211B, (uint8_t) (a >> 8));
}
static void m7b(uint8_t b) { tg_bus_write8(0x211C, b); }
static uint16_t mpy_lm(void) { return (uint16_t) (tg_bus_read8(0x2134) | tg_bus_read8(0x2135) << 8); }
static uint16_t mpy_mh(void) { return (uint16_t) (tg_bus_read8(0x2135) | tg_bus_read8(0x2136) << 8); }
static uint16_t mpy_with(uint16_t a, uint8_t b) {
  m7a(a);
  m7b(0);
  m7b(b);
  return mpy_mh();
}

// Grip limits for this many frames since the last update: table + ($1E90 - 1) * 2 + gear table.
static uint16_t frames_index(const Driver* d) {
  return (uint16_t) (((uint16_t) (ram16(RaceFramesElapsed) - 1) << 1) + ram16(d->gear_tab));
}
static void set_grip(const Driver* d, uint16_t hi, uint16_t lo) {
  uint16_t t = frames_index(d);
  ram16_set(0x1E94, tab16((uint16_t) (hi + t)));
  ram16_set(0x1E92, tab16((uint16_t) (lo + t)));
}

static uint16_t rpm_from_speed(const Driver* d, uint16_t y) {
  return (uint16_t) (mpy_with(ram16(d->speed), tab8((uint16_t) (0x9767 + y))) << 1);
}

// Runs an original routine with the registers as the original has them at the JSR.
static TgRegs call(uint16_t target, uint16_t ret, uint16_t a, uint16_t x, uint16_t y, bool c) {
  TgRegs r = { .a = a, .x = x, .y = y, .d = g_d, .db = g_db, .c = c };
  flags_nz16(&r, a);
  tg_call_jsr(&r, target, ret, TG_MX_16);
  return r;
}

// $00:A89E with A = 0: the next random byte (also left in $1EF7).
static uint16_t race_random(void) {
  uint16_t t = (uint16_t) (ram16(0x1FF4) + 7);
  ram16_set(0x1EF7, t);
  ram16_set(0x1EF9, (uint16_t) (t << 2));
  uint16_t v = (uint16_t) ((t << 3) + ram16(0x1EF9) + ram16(0x1EF7));
  ram16_set(0x1FF4, v);
  v = (uint16_t) ((swap(v) ^ v) & 0x00FF);
  ram16_set(0x1EF7, v);
  return v;
}

// $05:E99F: a random bump sound, 0-15.
static uint16_t bump_sound(void) { return (uint16_t) ((race_random() & 0x00FF) >> 4); }

// The N flag after CMP: the original compares positions with BPL/BMI.
static bool minus(uint16_t a, uint16_t b) { return ((uint16_t) (a - b) & 0x8000) != 0; }

// Computer steering, shared by $05:E9AF / $05:EB58 (a player's car, in attract mode or
// after finishing) and the computer-driven second car ($05:ED92). The car aims for a gap
// between two side bounds, moves the gap aside for the next car ahead, and steers toward
// its middle.

// The car's side position ($1E) and the next car ahead ($20 its side, $22 a lap's length
// when that's the first record); returns that car's record offset.
static uint16_t gap_setup(const Driver* d) {
  dp16_set(0x1E, asr(ram16(d->x)));
  dp16_set(0x22, 0);
  uint16_t y = (uint16_t) (ram16(d->car) + 0x12);
  if(y == 0x0168) {
    y = 0;
    dp16_set(0x22, ram16(RaceCars));
  }
  ram16_set(0x0020, asr(ram16((uint16_t) (0x0214 + y))));
  return y;
}

// The gap's bounds from a table word: high byte the upper side, low byte the lower.
static void gap_bounds(uint16_t w) {
  dp16_set(0x1A, asr(w & 0xFF00));
  dp16_set(0x1C, asr((uint16_t) (w << 8)));
}

// Passes the car ahead (record y) on one side if it's inside the gap, limits how far the
// gap moves ($258 one way, $500 the other per update), and returns the steering word: the
// gap's middle, less the speed times $7E:4C08[x] times 3, relative to the car. The gap's
// second word and the flag word overlap at lane + 3, so they're read back.
static uint16_t gap_steer(const Driver* d, uint16_t x, uint16_t y) {
  uint16_t lane = d->lane, lane2 = (uint16_t) (lane + 2), flag = (uint16_t) (lane + 3);
  uint16_t ahead = (uint16_t) (ram16((uint16_t) (0x0219 + y)) + dp16(0x22));
  uint16_t pos = ram16((uint16_t) (d->pos + 1));
  if((ahead < pos || (uint16_t) (ahead - pos) < 0x0200) && !minus(dp16(0x20), dp16(0x1C)) &&
     minus(dp16(0x20), dp16(0x1A))) {
    bool upper;  // pass on the upper side (raise the lower bound)
    uint16_t c = dp16(0x20);
    if(!(ram16(flag) & 0x8000)) {
      if((uint16_t) (dp16(0x1A) - c) < 0x0F00) {
        upper = false;
      } else if((ram16((uint16_t) (0x0216 + y)) & 0x8000) || (uint16_t) (c - dp16(0x1C)) < 0x0A00) {
        upper = true;
      } else {
        uint16_t t = (uint16_t) (c - dp16(0x1E));
        upper = (t & 0x8000) || t < 0x0800;
      }
    } else {
      if((uint16_t) (c - dp16(0x1C)) < 0x0F00) {
        upper = true;
      } else if(!(ram16((uint16_t) (0x0216 + y)) & 0x8000) || (uint16_t) (dp16(0x1A) - c) < 0x0A00) {
        upper = false;
      } else {
        uint16_t t = (uint16_t) (dp16(0x1E) - c);
        upper = t != 0 && !(t & 0x8000) && t >= 0x0800;
      }
    }
    if(upper) {
      ram16_set(flag, ram16(flag) & 0x00FF);
      uint16_t a = (uint16_t) (dp16(0x20) + 0x0A00);
      if(!minus(a, dp16(0x1A))) a = dp16(0x1C);
      dp16_set(0x1C, a);
    } else {
      ram16_set(flag, ram16(flag) | 0xFF00);
      uint16_t a = (uint16_t) (dp16(0x20) - 0x0A00);
      if(minus(a, dp16(0x1C))) a = dp16(0x1A);
      dp16_set(0x1A, a);
    }
  }
  uint16_t a = (uint16_t) (dp16(0x1A) - ram16(lane));
  if(a & 0x8000) {
    if(a < 0xFAFF) dp16_set(0x1A, (uint16_t) (ram16(lane) - 0x0500));
  } else if(a >= 0x0259) {
    dp16_set(0x1A, (uint16_t) (ram16(lane) + 0x0258));
  }
  a = (uint16_t) (ram16(lane2) - dp16(0x1C));
  if(a & 0x8000) {
    if(a < 0xFAFF) dp16_set(0x1C, (uint16_t) (ram16(lane2) + 0x0500));
  } else if(a >= 0x0259) {
    dp16_set(0x1C, (uint16_t) (ram16(lane2) - 0x0258));
  }
  ram16_set(lane, ram16(0x001A));
  ram16_set(lane2, ram16(0x001C));
  m7a(ram16(d->speed));
  m7b(g_ram[0x4C08 + x]);
  uint16_t mid = (uint16_t) (asr((uint16_t) (ram16(0x001C) - ram16(0x001A))) + ram16(0x001A));
  uint16_t p = mpy_mh();
  p = (uint16_t) ((p << 2) - mpy_mh());
  a = (uint16_t) (mid - p - dp16(0x1E));
  if(a & 0x8000) {
    if(a < 0xF000) a = 0xF000;
  } else if(a >= 0x2000) {
    a = 0x1FFF;
  }
  return (uint16_t) (a << 2);
}

// $05:E9AF / $05:EB58: the gap is the road's ($7E:7002 for this stretch). Returns the car
// record offset it looked at (Y) and the steering's high byte (X).
static uint16_t computer_steer(const Driver* d, uint16_t* x_out) {
  uint16_t x = (ram16((uint16_t) (d->pos + 1)) >> 4) & 0xFFFE;
  gap_bounds(ram16((uint16_t) (0x7002 + x)));
  uint16_t y = gap_setup(d);
  uint16_t a = gap_steer(d, x, y);
  ram16_set(d->steer, a);
  ram16_set(d->drift, a);
  ram16_set(0x001A, a);
  *x_out = swap(a);
  return y;
}

// Engine and speed: gears, revs on the start line, nitro, acceleration, braking, fuel.
// Returns y, the gear's table offset that later steps use.
static uint16_t drive_engine(const Driver* d, bool* c_out) {
  bool c = *c_out;
  // $D30F: revved engine settling on the start line.
  if(ram16(d->state) & 0x0010) {
    uint16_t rpm = ram16(d->rpm);
    rpm = rpm >= 0x0030 ? (uint16_t) (rpm - 0x0030) : 0;
    ram16_set(d->rpm, rpm);
    if(rpm != 0) {
      ram16_set(d->smoke, (uint16_t) ~ram16(d->smoke));
      if(!(ram16(d->accel) & dp16(d->revving_held))) {
        rpm = ram16(d->rpm);
        ram16_set(d->rpm, rpm >= 0x0100 ? (uint16_t) (rpm - 0x0100) : 0);
      }
    } else {
      trb16(d->state, 0x0010);
      ram16_set(d->smoke, 0);
    }
  } else {
    ram16_set(d->smoke, 0);
  }
  // $D353: nitro.
  uint16_t nitro = ram16(d->nitro);
  if(!(ram16(d->ai) & 0x8000) && nitro != 0 && !(nitro & 0x8000) && trb_dp(d->pressed, ram16(d->nitro_button))) {
    tsb16(d->nitro, 0x8000);
    uint16_t speed = ram16(d->speed);
    if(!(speed & 0x8000) && speed >= 0x1900) ram16_set(d->messages, 0x000F);
  }
  // $D37C: gears, automatic unless a shift-up button is set.
  uint16_t x = ram16(d->gear), y;
  if(!(ram16(d->ai) & 0x8000) && ram16(d->shift_up) != 0) {
    if(x != 0 && trb_dp(d->pressed, ram16(d->shift_down))) {
      x--;
    } else if(x != 4 && trb_dp(d->pressed, ram16(d->shift_up))) {
      x++;
    }
    ram16_set(d->gear, x);
    y = (uint16_t) ((x << 1) + ram16(d->gear_tab));
  } else if(ram16(d->speed) & 0x8000) {
    y = (uint16_t) ((x << 1) + ram16(d->gear_tab));
  } else {
    uint16_t g = ram16(d->gear);
    y = (uint16_t) ((uint16_t) (g << 1) + ram16(d->gear_tab) + (g >> 15));
    uint16_t speed = ram16(d->speed);
    if(speed < tab16((uint16_t) (0x99DB + y))) ram16_set(d->gear, (uint16_t) (g - 1));
    else if(speed >= tab16((uint16_t) (0x9A15 + y))) ram16_set(d->gear, (uint16_t) (g + 1));
  }
  // $D3D6: top speed for the fuel load, and the hill's pull.
  if(ram16(d->nitro) & 0x8000) y = (uint16_t) (y + 0x003A);
  uint16_t top = (uint16_t) ((uint16_t) (0xA7FF - ram16(d->fuel)) >> 6);
  top = (uint16_t) (top + tab16((uint16_t) (0x9597 + y)));
  ram16_set(0x1EE2, top);
  uint16_t pos = ram16((uint16_t) (d->pos + 1));
  uint16_t a = neg(ram16((uint16_t) (0x4000 + ((pos >> 4) & 0xFFFE))));
  if(a & 0x8000) a = (uint16_t) ((a >> 2) | 0xC000);
  m7a(a);
  m7b(0);
  m7b(tab8((uint16_t) (0x96F3 + y)));
  x = mpy_mh();
  a = (uint16_t) (a >> 4);
  if(a >= 0x0800) a |= 0xF000;
  uint16_t speed = (uint16_t) (a + ram16(d->speed));
  ram16_set(d->speed, speed);
  if((speed & 0x8000) && neg(speed) >= ram16(0x1EE2)) {  // never happens in the recordings
    ram16_set(d->speed, neg(ram16(0x1EE2)));
  }
  // $D453: drag towards the gear's speed.
  speed = ram16(d->speed);
  if(speed & 0x8000) {
    a = (uint16_t) (speed + tab16((uint16_t) (0x960B + y)));
    a = (uint16_t) ((swap(a) & 0x00FF) + ram16(d->speed));
    if(!(a & 0x8000)) a = 0;
  } else {
    a = (uint16_t) (tab16((uint16_t) (0x960B + y)) - speed);
    if(a & 0x8000) a = neg(a);
    a = (uint16_t) (neg(swap(a) & 0x00FF) + ram16(d->speed));
    if(a & 0x8000) a = 0;
  }
  ram16_set(d->speed, a);
  set_grip(d, 0x97DF, 0x981F);
  // $D4A1: accelerating, or coasting.
  bool accelerating = (ram16(d->ai) & 0x8000) || (ram16(d->accel) & dp16(d->held));
  if(accelerating && ram16(0x1EB0) < 0x000A && ram16(d->fuel) != 0) {
    trb16(d->state, 0x0020);
    set_grip(d, 0x985F, 0x989F);
    a = (uint16_t) (ram16(0x1EE2) - ram16(d->speed));
    if(a & 0x8000) {
      // Over the top speed: slow by the excess's high byte.
      uint32_t sum = (uint32_t) (uint16_t) ~(swap(neg(a)) & 0x00FF) + ram16(d->speed) + 1;
      c = sum > 0xFFFF;
      ram16_set(d->speed, (uint16_t) sum);
    } else {
      m7a(a);
      m7b(0);
      m7b(tab8((uint16_t) (0x967F + y)));
      a = (uint16_t) (mpy_mh() >> 2);
      if(ram16(d->state) & 0x0010) a >>= 1;
      uint32_t sum = (uint32_t) a + ram16(d->speed);
      c = sum > 0xFFFF;
      ram16_set(d->speed, (uint16_t) sum);
    }
  } else {
    uint16_t s2 = (uint16_t) (ram16(d->speed) << 1);
    uint16_t top2 = ram16(0x1EE2);
    a = (uint16_t) (s2 - top2);
    c = s2 >= top2;
    if(!(a & 0x8000)) {
      a = swap(a);
      a = (uint16_t) ((a << 1) | (a >> 15));
      a &= 0x01FF;
      uint32_t sum = (uint32_t) (uint16_t) ~a + ram16(d->speed) + 1;
      c = sum > 0xFFFF;
      ram16_set(d->speed, (uint16_t) sum);
    }
  }
  // $D545: braking.
  uint16_t brake = ram16(0x0108) & (uint16_t) ~d->brake_bit;
  if(!(ram16(d->ai) & 0x8000) && (ram16(d->brake) & dp16(d->held))) {
    brake |= d->brake_bit;
    if(d->brake_tables) set_grip(d, 0x98DF, 0x991F);
    speed = ram16(d->speed);
    if(speed & 0x8000) {  // never happens in the recordings
      a = (uint16_t) (speed + ram16(0x1EE2));
      if(a & 0x8000) a = (uint16_t) (~a + 0x0800);
      m7a(a);
      m7b(0);
      m7b(0x0B);
      uint32_t sum = (uint32_t) mpy_mh() + ram16(d->speed);
      c = sum > 0xFFFF;
      a = (uint16_t) sum;
      ram16_set(d->speed, (a & 0x8000) ? a : 0);
    } else {
      a = (uint16_t) (neg(speed) + ram16(0x1EE2));
      if(a & 0x8000) a = (uint16_t) (~a + 0x0800);  // never happens in the recordings
      m7a(a);
      m7b(0);
      m7b(0x0B);
      uint32_t sum = (uint32_t) neg(mpy_mh()) + ram16(d->speed);
      c = sum > 0xFFFF;
      a = (uint16_t) sum;
      ram16_set(d->speed, (a & 0x8000) ? 0 : a);
    }
  }
  ram16_set(0x0108, brake);
  // $D5EB: revs from speed, and fuel used.
  if(!(ram16(d->state) & 0x0020)) {
    m7a(ram16(d->speed));
    m7b(0);
    m7b(tab8((uint16_t) (0x9767 + y)));
    uint16_t rpm;
    bool set = true;
    if(ram16(d->state) & 0x0010) {
      uint16_t half = (uint16_t) ((ram16(d->rpm) >> 1) | (c ? 0x8000 : 0));
      if(half > mpy_mh()) {
        rpm = ram16(d->rpm);
        set = false;
      } else {
        trb16(d->state, 0x0010);
      }
    }
    if(set) {
      rpm = (uint16_t) (mpy_mh() << 1);
      ram16_set(d->rpm, rpm);
    }
    if(!(rpm & 0x8000)) {
      if(rpm == 0) {
        tsb16(d->state, 0x0020);
      } else if(ram16(d->fuel) != 0) {
        m7a(rpm);
        m7b(g_ram[d->fuel_rate]);
        a = swap(mpy_mh());
        a = (uint16_t) ((a << 1) | (a >> 15));
        a = (uint16_t) ((a << 1) | (a >> 15));
        a = (uint16_t) ((a & 0x03FF) + 1);
        uint16_t fuel = ram16(d->fuel);
        if(fuel <= a) {
          trb16(0x1EB4, d->running_bit);
          tsb16(d->state, 0x0020);
          ram16_set(d->rpm, 0);
          ram16_set(d->fuel, 0);
        } else {
          ram16_set(d->fuel, (uint16_t) (fuel - a));
        }
      }
    }
  }
  *c_out = c;
  return y;
}

// $D686: distance moved this update, speed / 2 x frames, saturated to 16 bits.
static void drive_distance(const Driver* d) {
  m7a(asr(ram16(d->speed)));
  m7b(g_ram[RaceFramesElapsed]);
  uint16_t x = mpy_lm();
  uint8_t mid = tg_bus_read8(0x2135);
  uint8_t hi = (uint8_t) (tg_bus_read8(0x2136) << 1 | mid >> 7);
  if(hi != 0) {
    if(!(hi & 0x80)) x = 0x7FFF;  // never happens in the recordings
    else if(hi != 0xFF) x = 0x8000;  // never happens in the recordings
  }
  ram16_set(0x1E8E, x);
}

// Bounces the car back off something solid: half its speed, reversed (at most $640 back),
// and no distance this update.
static void bounce(const Driver* d) {
  uint16_t a = neg(asr(ram16(d->speed)));
  if(a < 0xF9C0) a = 0xF9C0;
  ram16_set(d->speed, a);
  ram16_set(0x1E8E, 0);
}

// $05:ED3A / $05:ED01: an object with bit 15 set. $C004 and $C006 turn the edges' hold on
// the car on and off; others are solid, with a list of x ranges at $A167 (6-byte entries:
// offset, width to the right, width to the left; 0 ends it). True if the car hits one.
static bool hit_object(const Driver* d, uint16_t v) {
  if(v >= 0xC000) {
    if(v == 0xC004) ram16_set(0x1EBE, ram16(0x1EBE) | d->edge_mask);
    else if(v == 0xC006) ram16_set(0x1EBE, ram16(0x1EBE) & (uint16_t) ~d->edge_mask);
    return false;
  }
  uint16_t x = tab16((uint16_t) (0xA15F + (v & 0x3FFE)));
  ram16_set(0x001A, asr(ram16(d->x)));
  do {
    uint16_t a = (uint16_t) (ram16(0x001A) - tab16((uint16_t) (0xA167 + x)));
    if(a & 0x8000) {
      if(a >= tab16((uint16_t) (0xA16B + x))) return true;
    } else if(a < tab16((uint16_t) (0xA169 + x))) {
      return true;
    }
    x = (uint16_t) (x + 6);
  } while(tab16((uint16_t) (0xA167 + x)) != 0);
  return false;
}

// $05:F6B3 / $05:F729: a solid roadside object, with a crunch at speed.
static void hit_solid(const Driver* d) {
  tsb16(d->state, 0x0080);
  if(ram16(d->speed) >= 0x1770) ram16_set(d->messages, tab8((uint16_t) (0xE1DE + bump_sound())));
  bounce(d);
}

// $05:F6FA / $05:F6FF (and $F770 / $F775): thrown sideways, steering to angle; half speed.
static void throw_aside(const Driver* d, uint16_t angle) {
  ram16_set(d->steer, angle);
  ram16_set(d->drift, angle);
  ram16_set(d->skid, 0x0600);
  tsb16(d->state, 0x0080);
  ram16_set(d->speed, asr(ram16(d->speed)));
  ram16_set(0x1E8E, asr(ram16(0x1E8E)));
}

// The routine in a player's object table for kind k (JSR (table,X) reads the program
// bank). Player 1's last entry is the first of player 2's table, so it acts on player 2.
static void run_object_handler(const Driver* d, uint16_t k, uint16_t diff, bool left, bool rival) {
  uint16_t h = rom16(0x050000 | (uint16_t) (d->object_handlers + k));
  for(int i = 0; i < 2; i++) {
    const Driver* p = &kDrivers[i];
    if(h == p->solid_handler) {
      hit_solid(p);
      return;
    }
    if(h == p->side_handler) {  // $F6F5: away from the side of the road the car is on
      throw_aside(p, (ram16(p->x) & 0x8000) ? 0x5000 : 0xB000);
      return;
    }
    if(h == (uint16_t) (p->side_handler - 4)) {  // $F6F1: away from the object
      throw_aside(p, left ? 0xB000 : 0x5000);
      return;
    }
  }
  // Player 2's table ends in code bytes; no track has those kinds.
  uint16_t ret = rival ? (left ? 0xF168 : 0xF11C) : (left ? d->object_ret_left : d->object_ret_right);
  call(h, ret, diff, k, diff, left);
}

// A track object at the segment the car passed: x is its offset in the $7E:6400 table, v
// its word (bits 0-7 distance from the middle, bit 8 on the left, bits 9-14 the kind; bit
// 15 for the kinds hit_object handles). The computer-driven second car (rival) gets a unit
// of fuel back at the sign when it has run out.
static void track_object(const Driver* d, uint16_t v, bool rival) {
  if(v & 0x8000) {
    if(hit_object(d, v)) {
      tsb16(d->state, 0x0080);
      bounce(d);
    }
    return;
  }
  uint16_t ox = (uint16_t) ((v & 0x00FF) << 7);
  if(v & 0x0100) ox = neg(ox);
  ox = asr(ox);
  uint16_t diff = (uint16_t) (asr(ram16(d->x)) - ox);
  bool right = !(diff & 0x8000);
  // Passing a sign of type $3A marks the special zone.
  if((v & 0xFE00) == 0x7400 && (right ? !(v & 0x0100) : (v & 0x0100)) && !(ram16(d->sign_hit) & 0x8000)) {
    ram16_set(d->sign_hit, 0xFFFF);
    if(ram16(d->fuel) == 0) {  // never happens in the recordings
      if(!rival) {
        if(ram16(0x1EE0) != 1) tsb16(0x1EB4, d->running_bit);
      } else {
        ram16_set(d->fuel, 1);
        if((ram16(0x1F02) & 0x8000) && ram16(0x1EE0) != 1) tsb16(0x1EB4, d->running_bit);
      }
    }
  }
  uint16_t k = swap(v) & 0x007E;
  if(right ? diff < tab16((uint16_t) (0xA05F + k)) : diff >= tab16((uint16_t) (0xA0DF + k))) {
    run_object_handler(d, k, diff, !right, rival);
  }
}

// $D6BD: moves the car along, then checks the track objects of each segment passed.
static void drive_along(const Driver* d, bool rival) {
  uint16_t start = ram16((uint16_t) (d->pos + 1)) & 0xFFE0;
  uint16_t dist = ram16(0x1E8E);
  uint32_t lo = (uint32_t) dist + ram16(d->pos);
  ram16_set(d->pos, (uint16_t) lo);
  g_ram[d->pos + 2] = (uint8_t) ((dist & 0x8000 ? 0xFF : 0) + g_ram[d->pos + 2] + (lo > 0xFFFF));
  uint16_t a = (uint16_t) ((ram16((uint16_t) (d->pos + 1)) & 0xFFE0) - start);
  if(a == 0 || (a & 0x8000) || a >= ram16(RaceCars)) return;
  do {
    uint16_t p = (uint16_t) (0x0040 - a + ram16((uint16_t) (d->pos + 1)));
    ram16_set(0x001C, a);
    uint16_t x = (p >> 4) & 0xFFFE;
    uint16_t v = ram16((uint16_t) (0x6400 + x));
    if(v != 0) track_object(d, v, rival);
    a = (uint16_t) (ram16(0x001C) - 0x0020);
  } while(a != 0);
}

// Clamps a steering word towards zero by the grip (limit first: $51FF / $AE00).
static uint16_t settle(uint16_t a, uint16_t grip) {
  if(a == 0) return 0;
  if(a & 0x8000) {
    if(a < 0xB000) a = 0xAE00;
    uint32_t sum = (uint32_t) a + grip;
    return sum > 0xFFFF ? 0 : (uint16_t) sum;
  }
  if(a >= 0x5000) a = 0x51FF;
  return a >= grip ? (uint16_t) (a - grip) : 0;
}

// Adds a sideways move to the position, saturating at $7FFF / $8001 when the signs agree.
static uint16_t add_sideways(uint16_t pos, uint16_t a) {
  uint16_t sum = (uint16_t) (a + pos);
  if(!(pos & 0x8000) && !(a & 0x8000) && (sum & 0x8000)) return 0x7FFF;  // never happens in the recordings
  if((pos & 0x8000) && (a & 0x8000) && !(sum & 0x8000)) return 0x8001;  // never happens in the recordings
  return sum;
}

// $D7F0 to $DB51: track edges, steering, sliding on curves, sway and the car's sprite angle.
// y is the gear table offset from drive_engine.
static void drive_steer(const Driver* d, uint16_t y) {
  uint16_t pos = ram16((uint16_t) (d->pos + 1));
  uint16_t x = ram16((uint16_t) (0x5800 + ((pos >> 4) & 0xFFFE))) & 0x7FFE;
  y = tab16((uint16_t) (0x9B3F + x));
  // Off the edges of the road: slowed down (or stopped at the edge where $1EBE says so).
  uint16_t side = ram16(d->x);
  bool off;
  if(side & 0x8000) {
    off = side < ram16((uint16_t) (0x017B + y));
    if(off && (ram16(0x1EBE) & d->edge_mask)) {  // never happens in the recordings
      ram16_set(d->x, ram16((uint16_t) (0x017B + y)));
      throw_aside(d, 0x5000);
    }
  } else {
    off = side >= ram16((uint16_t) (0x017D + y));
    if(off && (ram16(0x1EBE) & d->edge_mask)) {  // never happens in the recordings
      ram16_set(d->x, ram16((uint16_t) (0x017D + y)));
      throw_aside(d, 0xB000);
    }
  }
  if(off) {
    m7a(ram16(d->speed));
    m7b(0x06);
    m7b(0x06);
    ram16_set(d->speed, (uint16_t) (ram16(d->speed) - mpy_mh()));
  }
  // $D85B: steering, or the slide after being pushed sideways.
  uint16_t skid = ram16(d->skid);
  if(skid != 0) {
    ram16_set(d->smoke, 0xFFFF);
    uint16_t a = ram16(d->steer);
    if(a != 0) {
      if(a & 0x8000) {
        uint32_t sum = (uint32_t) a + skid;
        a = sum > 0xFFFF ? 0 : (uint16_t) sum;
      } else {
        a = a >= skid ? (uint16_t) (a - skid) : 0;  // the clamp never happens in the recordings
      }
      ram16_set(d->steer, a);
    }
    x = swap(a);
    if(x == 0) ram16_set(d->skid, 0);
  } else if(ram16(d->ai) & 0x8000) {
    y = computer_steer(d, &x);
  } else {
    ram16_set(0x001A, ram16(d->steer));
    if(ram16(d->speed) < 0x0280) {
      ram16_set(0x1E94, (uint16_t) (ram16(0x1E94) << 1));
      ram16_set(0x1E92, (uint16_t) (ram16(0x1E92) << 1));
    }
    uint16_t f = (uint16_t) (ram16(RaceFramesElapsed) << 1);
    y = (uint16_t) (f + ram16(d->gear_tab));
    x = (uint16_t) ((x & 0xFFF0) | f);
    uint16_t turn = tab16((uint16_t) (0x9AC3 + y));
    if(ram16(d->steer_plus) & dp16(d->held)) {
      if(ram16(d->speed) < 0x0280) {
        uint16_t a = (uint16_t) ((turn << 1) + ram16(d->steer));
        ram16_set(d->steer, a);
        ram16_set(d->drift, a);
      } else {
        ram16_set(d->steer, (uint16_t) (ram16(d->steer) - tab16((uint16_t) (0x9DDF + x)) + turn));
        ram16_set(d->drift, (uint16_t) (ram16(d->drift) + turn));
      }
    }
    if(ram16(d->steer_minus) & dp16(d->held)) {
      if(ram16(d->speed) < 0x0F00) {
        uint16_t a = (uint16_t) (ram16(d->steer) - (uint16_t) (turn << 1));
        ram16_set(d->steer, a);
        ram16_set(d->drift, a);
      } else {
        ram16_set(d->steer, (uint16_t) (ram16(d->steer) + tab16((uint16_t) (0x9DDF + x)) - turn));
        ram16_set(d->drift, (uint16_t) (ram16(d->drift) - turn));
      }
    }
    uint16_t grip = tab16((uint16_t) (0x9B01 + y));
    ram16_set(d->drift, settle(ram16(d->drift), grip));
    uint16_t a = settle(ram16(d->steer), grip);
    ram16_set(d->steer, a);
    x = swap(a);
    if(x == 0) ram16_set(d->skid, 0);
  }
  // $D995: sideways move = distance x steering, limited by grip; past it, the car skids.
  m7a(ram16(0x1E8E));
  m7b((uint8_t) x);
  uint16_t a = mpy_mh();
  if(!(a & 0x8000)) {
    if(a >= ram16(0x1E94)) {
      tsb16(d->state, 0x0001);
      uint16_t s = (uint16_t) (ram16(0x001A) - tab16((uint16_t) (0x9B01 + y)));
      ram16_set(d->steer, s);
      ram16_set(d->drift, s);
      a = (uint16_t) (ram16(0x1E94) - 1);
    }
  } else if(a < ram16(0x1E92)) {
    tsb16(d->state, 0x0001);
    uint16_t s = (uint16_t) (ram16(0x001A) + tab16((uint16_t) (0x9B01 + y)));
    ram16_set(d->steer, s);
    ram16_set(d->drift, s);
    a = ram16(0x1E92);
  }
  // Player 2's copy doesn't keep the move in $001A; $001A is set again below either way.
  ram16_set(d->x, add_sideways(ram16(d->x), a));
  // $DA0F: the curve throws the car outwards.
  pos = ram16((uint16_t) (d->pos + 1));
  uint16_t curve = ram16((uint16_t) (0x4C00 + ((pos >> 4) & 0xFFFE)));
  if(curve != 0 && ram16(d->skid) == 0) {
    if(trb16(d->state, 0x0001)) ram16_set(d->smoke, 0xFFFF);
    m7a(ram16(0x1E8E));
    m7b((uint8_t) curve);
    m7b((uint8_t) curve);
    a = add_sideways(ram16(d->x), mpy_mh());
    if(a == 0x8000) a++;  // never happens in the recordings
    ram16_set(d->x, a);
  }
  x = curve;
  // $DA7E: with the other player on the track, the view's sway and scroll.
  if(!(ram16(d->finished) & 0x8000)) {
    m7a(ram16(d->speed));
    m7b((uint8_t) x);
    ram16_set(d->sway, neg(mpy_mh()));
    uint16_t side2 = ram16(d->x);
    uint16_t s = (uint16_t) ((side2 << 1) & 0xFF00);
    if(side2 & 0x8000) s |= 0x00FF;
    m7a(swap(s));
    m7b(0x19);
    m7b(0x19);
    uint16_t v = (uint16_t) (mpy_lm() >> 4);
    if(v >= 0x0800) v |= 0xF000;
    dp16_set(d->view_x, v);
  }
  // $DAD0: the car's sprite angle from how fast the steering turns it.
  x = ram16(d->angle_base);
  m7a(ram16(d->speed));
  m7b(g_ram[d->steer + 1]);
  dp16_set(0x1A, mpy_mh());
  m7b(g_ram[d->drift + 1]);
  a = (uint16_t) (mpy_mh() - dp16(0x1A));
  if(a & 0x8000) a = neg(a);
  if(a >= 0x0100) ram16_set(d->smoke, 0xFFFF);
  a = mpy_mh();
  if(ram16(d->skid) != 0) {
    a = neg(a);
    if(d->skid_doubles) a = (uint16_t) (a << 1);
  }
  if(ram16(d->speed) & 0x8000) a = neg(a);  // never happens in the recordings
  if(a & 0x8000) {
    if(a < 0xFF60) {
      x = (uint16_t) (x - 2);
      if(a < 0xFEB0) x = (uint16_t) (x - 2);
    }
  } else if(a >= 0x00A0) {
    x = (uint16_t) (x + 2);
    if(a >= 0x0150) x = (uint16_t) (x + 2);
  }
  a = swap(x);
  ram16_set((uint16_t) (0x0212 + ram16(d->car)), a);
  ram16_set(d->car_pos, a);
}

static void drive_player(const Driver* d) {
  bool c = false;
  uint16_t y = drive_engine(d, &c);
  drive_distance(d);
  drive_along(d, false);
  drive_steer(d, y);
}

// ---------------------------------------------------------------------------------------
// The computer-driven second car (the rival), when player 2 isn't in the race.

// $05:F79F: whether k times the distance left to the finish (in half track units, high
// part only: laps to go $1E56 - $1E78 times the lap length, less the position) is at least
// the rival's fuel. Halves the position word for the sum and doubles it back, which loses
// its bit 0.
static bool rival_fuel_short(uint8_t k) {
  uint16_t half = ram16(RaceCars) >> 1;
  ram16_set(0x1EDA, ram16(0x1EDA) >> 1);
  m7a(half);
  m7b(0);
  m7b((uint8_t) (g_ram[0x1E56] - g_ram[0x1E78]));
  uint8_t lo = tg_bus_read8(0x2134);
  int borrow = lo < g_ram[0x1EDA];
  uint8_t m = tg_bus_read8(0x2135);
  int mid = m - g_ram[0x1EDB] - borrow;
  borrow = mid < 0;
  uint8_t hi = (uint8_t) (tg_bus_read8(0x2136) - borrow);
  m7a((uint16_t) (hi << 8 | (uint8_t) mid));
  m7b(k);
  ram16_set(0x1EDA, (uint16_t) (ram16(0x1EDA) << 1));
  return (uint16_t) (mpy_lm() << 1) >= ram16(0x1E64);
}

// $EE05 to $EE93: the rival's top speed in this gear, less a little as its tank is fuller;
// in top gear, cut further the further it is ahead of player 1's view ($0046 / $0059), by
// half as much on the hardest skill level ($1F10 = 3: none).
static void rival_top_speed(uint16_t y) {
  ram16_set(0x1EE2, (uint16_t) (((uint16_t) (0xA7FF - ram16(0x1E64)) >> 6) + tab16((uint16_t) (0x9597 + y))));
  if(ram16(0x1F10) == 3 || ram16(0x1E74) != 4) return;
  uint16_t laps = (uint16_t) (ram16(0x1E78) - ram16(0x1E76));
  if(laps & 0x8000) return;
  m7a(ram16(RaceCars) >> 1);
  m7b((uint8_t) laps);
  uint16_t lm = mpy_lm();
  dp16_set(0x1A, (uint16_t) (lm << 1));
  // A 16-bit read of $2136: the high byte comes from $2137 (latching the counters).
  uint8_t h = tg_bus_read8(0x2136);
  tg_bus_read8(0x2137);
  dp16_set(0x1C, (uint8_t) (h << 1 | lm >> 15));
  uint16_t v2 = ram16(0x0059), v1 = ram16(0x0046);
  uint16_t carry_hi = v2 < v1 ? 0x00FF : 0;
  uint32_t sum = (uint32_t) (uint16_t) (v2 - v1) + dp16(0x1A);
  dp16_set(0x1A, (uint16_t) sum);
  uint16_t hi = (uint16_t) ((carry_hi + dp16(0x1C) + (sum > 0xFFFF)) & 0x00FF);
  uint16_t x = 0x0400;
  if(hi != 0) {
    if(hi & 0x0080) return;  // player 1 is ahead
  } else if(ram16(0x001A) < 0x0400) {
    x = ram16(0x001A);
  }
  uint16_t cut = (uint16_t) ~x;
  if(ram16(0x1F10) == 0) cut = (uint16_t) (cut << 1);
  ram16_set(0x1EE2, (uint16_t) (ram16(0x1EE2) + cut + 1));
}

// $05:ED92, jumped to from $05:DB5C: drives the rival for one update, then the code at
// $E3B9 carries on.
static void drive_rival(void) {
  const Driver* d = &kDrivers[1];
  trb16(0x1EF6, 0x0010);
  if(ram16(d->view_off) != 0) {
    ram16_set(d->rpm, rpm_from_speed(d, (uint16_t) (ram16(d->gear) << 1)));
    if(ram16(0x1E88) == 0xFFFA && !rival_fuel_short(0x50)) ram16_set(0x1E88, 0xFFF9);
    return;
  }
  ram16_set(d->smoke, 0);
  // Gears change by speed alone.
  uint16_t y = (uint16_t) (ram16(d->gear) << 1);
  uint16_t speed = ram16(d->speed);
  if(!(speed & 0x8000)) {
    if(speed < tab16((uint16_t) (0x9A4F + y))) ram16_set(d->gear, (uint16_t) (ram16(d->gear) - 1));
    else if(speed >= tab16((uint16_t) (0x9A89 + y))) ram16_set(d->gear, (uint16_t) (ram16(d->gear) + 1));
  }
  rival_top_speed(y);
  // Hills.
  uint16_t pos = ram16((uint16_t) (d->pos + 1));
  uint16_t hill = neg(ram16((uint16_t) (0x4000 + ((pos >> 4) & 0xFFFE))));
  if(hill & 0x8000) hill = (uint16_t) (0xC000 | hill >> 2);
  m7a(hill);
  m7b(0);
  m7b(tab8((uint16_t) (0x96F3 + y)));
  uint16_t pull = mpy_mh();
  uint16_t a = (uint16_t) (hill >> 4);
  if(a >= 0x0800) a |= 0xF000;
  speed = (uint16_t) (a + ram16(d->speed));
  ram16_set(d->speed, speed);
  uint16_t top = ram16(0x1EE2);
  if(speed & 0x8000) {
    if(neg(speed) >= top) ram16_set(d->speed, neg(top));  // never happens in the recordings
  } else if(speed >= top) {
    ram16_set(d->speed, top);
  }
  // Drag.
  speed = ram16(d->speed);
  if(speed & 0x8000) {
    a = (uint16_t) (((uint16_t) (speed + tab16((uint16_t) (0x960B + y))) >> 8) + speed);
    if(!(a & 0x8000)) a = 0;
  } else {
    a = (uint16_t) (neg((uint16_t) (((uint16_t) (neg(speed) + tab16((uint16_t) (0x960B + y))) >> 8))) + speed);
    if(a & 0x8000) a = 0;
  }
  ram16_set(d->speed, a);
  // Accelerate towards the top speed while there's fuel (and $1EB0 < 10); else ease off
  // when over half of it.
  if(ram16(d->fuel) != 0 && ram16(0x1EB0) < 0x000A) {
    m7a((uint16_t) (ram16(0x1EE2) - ram16(d->speed)));
    m7b(0);
    m7b(tab8((uint16_t) (0x967F + y)));
    dp16_set(0x1A, (uint16_t) (mpy_mh() >> 2));
    a = (uint16_t) (pull + dp16(0x1A));
    if(!(a & 0x8000)) ram16_set(d->speed, (uint16_t) (a + ram16(d->speed)));
  } else {
    a = (uint16_t) ((ram16(d->speed) << 1) - ram16(0x1EE2));
    if(!(a & 0x8000)) {
      a = swap(a);
      a = (uint16_t) ((a << 1 | a >> 15) & 0x01FF);
      ram16_set(d->speed, (uint16_t) (ram16(d->speed) - a));
    }
  }
  // Revs, and fuel used by them.
  m7a(ram16(d->speed));
  m7b(0);
  m7b(tab8((uint16_t) (0x9767 + y)));
  uint8_t lo = tg_bus_read8(0x2134);
  uint16_t rpm = (uint16_t) (mpy_mh() << 1 | lo >> 7);
  ram16_set(d->rpm, rpm);
  if(rpm != 0 && !(rpm & 0x8000) && ram16(d->fuel) != 0) {
    m7a(rpm);
    m7b(g_ram[d->fuel_rate]);
    a = swap(mpy_mh());
    a = (uint16_t) (a << 2 | a >> 14);
    a = (uint16_t) ((a & 0x03FF) + 1);
    uint16_t fuel = ram16(d->fuel);
    if(fuel <= a) {
      trb16(0x1EB4, d->running_bit);
      tsb16(d->state, 0x0020);
      ram16_set(d->rpm, 0);
      ram16_set(d->fuel, 0);
    } else {
      ram16_set(d->fuel, (uint16_t) (fuel - a));
    }
  }
  // Distance this update.
  m7a(asr(ram16(d->speed)));
  m7b(g_ram[RaceFramesElapsed]);
  uint16_t dist = mpy_lm();
  if((mpy_mh() ^ ram16(d->speed)) & 0x8000) dist = (ram16(d->speed) & 0x8000) ? 0x8000 : 0x7FFF;  // never happens in the recordings
  ram16_set(0x1E8E, dist);
  drive_along(d, true);
  // Steering: the gap is the road's, or near the start of a section marked in $1EF2 (and
  // with fuel short for the rest of the race), one of two fixed courses, mirrored by bit 1.
  uint16_t x;
  if(ram16(d->skid) == 0) {
    y = gap_setup(d);
    uint16_t flags = ram16(0x1EF2);  // the course flags
    bool course = (flags & 0x0001) != 0;
    if(!course && ram16(d->fuel) < 0x3000) {
      course = rival_fuel_short(0x3D);
      y &= 0x00FF;  // the routine leaves 8-bit index registers, which drops Y's high byte
    }
    bool fixed = false;
    if(course) {
      a = (uint16_t) (ram16((uint16_t) (d->pos + 1)) - ram16(0x1EF0));
      if(ram16((uint16_t) (d->pos + 1)) >= ram16(0x1EF0) && a < 0x04A0) {
        if(a >= 0x0300) a = 0x0300;
        x = (a >> 4) & 0xFFFE;
        gap_bounds(tab16((uint16_t) (((flags & 0x0004) ? 0xE198 : 0xE166) + x)));
        if(flags & 0x0002) {
          dp16_set(0x1A, neg(dp16(0x1A)));
          dp16_set(0x1C, neg(dp16(0x1C)));
        }
        fixed = true;
      } else {
        trb16(0x1EF2, 0x0001);
      }
    }
    if(!fixed) {
      x = (ram16((uint16_t) (d->pos + 1)) >> 4) & 0xFFFE;
      gap_bounds(ram16((uint16_t) (0x7002 + x)));
    }
    ram16_set(d->steer, gap_steer(d, x, y));
    x = ram16((uint16_t) (d->steer + 1));
  } else {
    // Skidding: the steering straightens by $280 plus the skid each update.
    ram16_set(d->smoke, 0xFFFF);
    a = ram16(d->steer);
    uint16_t skid = ram16(d->skid);
    if(a != 0) {
      if(a & 0x8000) {
        uint32_t sum = (uint32_t) a + 0x0280;
        if(sum <= 0xFFFF) sum = (uint16_t) sum + (uint32_t) skid;
        a = sum > 0xFFFF ? 0 : (uint16_t) sum;
      } else if(a < 0x0280) {
        a = 0;  // never happens in the recordings
      } else {
        a = (uint16_t) (a - 0x0280);
        a = a >= skid ? (uint16_t) (a - skid) : 0;
      }
    }
    ram16_set(d->steer, a);
    x = swap(a);
    if(x == 0) ram16_set(d->skid, 0);
  }
  // $F3D1: sideways move = distance x steering, limited by the grip for this many frames.
  ram16_set(0x001A, x);
  m7a(ram16(0x1E8E));
  m7b((uint8_t) x);
  uint16_t t = (uint16_t) ((ram16(RaceFramesElapsed) - 1) << 1);
  a = mpy_mh();
  if(a & 0x8000) {
    if(a < tab16((uint16_t) (0x981F + t))) tsb16(d->state, 0x0001);
    if(a < tab16((uint16_t) (0x999D + t))) a = tab16((uint16_t) (0x999D + t));
  } else {
    if(a >= tab16((uint16_t) (0x985F + t))) tsb16(d->state, 0x0001);
    if(a >= tab16((uint16_t) (0x995F + t))) a = (uint16_t) (tab16((uint16_t) (0x995F + t)) - 1);
  }
  ram16_set(d->x, add_sideways(ram16(d->x), a));
  // The curve throws the car outwards (even while skidding, unlike a player's car).
  pos = ram16((uint16_t) (d->pos + 1));
  uint16_t curve = ram16((uint16_t) (0x4C00 + ((pos >> 4) & 0xFFFE)));
  if(curve != 0) {
    if(trb16(d->state, 0x0001)) ram16_set(d->smoke, 0xFFFF);
    m7a(ram16(0x1E8E));
    m7b((uint8_t) curve);
    m7b((uint8_t) curve);
    a = add_sideways(ram16(d->x), mpy_mh());
    if(a == 0x8000) a++;  // never happens in the recordings
    ram16_set(d->x, a);
  }
  // The view's sway and scroll, as for a player.
  if(!(ram16(d->finished) & 0x8000)) {
    m7a(ram16(d->speed));
    m7b((uint8_t) curve);
    ram16_set(d->sway, neg(mpy_mh()));
    uint16_t side = ram16(d->x);
    uint16_t sw = (uint16_t) ((side << 1) & 0xFF00);
    if(side & 0x8000) sw |= 0x00FF;
    m7a(swap(sw));
    m7b(0x19);
    m7b(0x19);
    uint16_t v = (uint16_t) (mpy_lm() >> 4);
    if(v >= 0x0800) v |= 0xF000;
    dp16_set(d->view_x, v);
  }
  // The sprite angle from the sideways speed.
  x = ram16(d->angle_base);
  m7a(ram16(d->speed));
  m7b(g_ram[0x001A]);
  a = mpy_mh();
  if(ram16(d->skid) != 0) a = neg(a);
  if(ram16(d->speed) & 0x8000) a = neg(a);  // never happens in the recordings
  if(a & 0x8000) {
    if(a < 0xFF60) {
      x = (uint16_t) (x - 2);
      if(a < 0xFE80) x = (uint16_t) (x - 2);
    }
  } else if(a >= 0x00A0) {
    x = (uint16_t) (x + 2);
    if(a >= 0x0180) x = (uint16_t) (x + 2);
  }
  // Off the road: slowed by the distance (or held at the edge where $1EBE says so).
  pos = ram16((uint16_t) (d->pos + 1));
  uint16_t e = tab16((uint16_t) (0x9B3F + (ram16((uint16_t) (0x5800 + ((pos >> 4) & 0xFFFE))) & 0x7FFF)));
  uint16_t side = ram16(d->x);
  bool off;
  if(side & 0x8000) {
    off = side < ram16((uint16_t) (0x017B + e));
    if(off && (ram16(0x1EBE) & 0xFF00)) ram16_set(d->x, ram16((uint16_t) (0x017B + e)));  // never happens in the recordings
  } else {
    off = side >= ram16((uint16_t) (0x017D + e));
    if(off && (ram16(0x1EBE) & 0xFF00)) ram16_set(d->x, ram16((uint16_t) (0x017D + e)));  // never happens in the recordings
  }
  if(off) {
    m7a(ram16(0x1E8E));
    m7b(0x04);
    m7b(0x04);
    ram16_set(d->speed, (uint16_t) (ram16(d->speed) - mpy_mh()));
  }
  a = swap(x);
  ram16_set((uint16_t) (0x0212 + ram16(d->car)), a);
  ram16_set(d->car_pos, a);
}

// ---------------------------------------------------------------------------------------
// Bumping into the other cars, and laps.

// $05:E8E9 / $05:E944: the player has run into the back of computer car x (record after
// x). The car takes the player's halved speed, less its own push still to come, as a push
// spread over $40 updates; the player gets the car's old speed doubled.
static void push_car(uint16_t x, uint16_t speed_adr) {
  uint16_t old = ram16((uint16_t) (0x021C + x));
  ram16_set(speed_adr, ram16(speed_adr) >> 1);
  uint16_t a = ram16((uint16_t) (0x021E + x));
  if(a != 0) {
    m7a(a);
    m7b(g_ram[0x0220 + x]);
    a = mpy_lm();
  }
  a = (uint16_t) (a + ram16((uint16_t) (0x021C + x)));
  ram16_set((uint16_t) (0x021C + x), a);
  uint16_t push = (uint16_t) ((ram16(speed_adr) - a) & 0xFFC0);
  ram16_set((uint16_t) (0x021C + x), (uint16_t) (push + ram16((uint16_t) (0x021C + x))));
  uint16_t step = (uint16_t) (neg(push) >> 6);
  if(step >= 0x0200) step |= 0xFE00;
  ram16_set((uint16_t) (0x021E + x), step);
  ram16_set((uint16_t) (0x0220 + x), 0x0040);
  ram16_set(speed_adr, (uint16_t) (old << 1));
}

// Each player's copy of the bumping code.
typedef struct Bumper {
  uint16_t car, other_car;           // RaceP1Car / RaceP2Car
  uint16_t speed, other_speed;
  uint16_t pos, other_pos;           // track position words
  uint16_t x;                        // side-to-side position
  uint16_t other_view_off;           // $0053 / $0040
  uint16_t messages;
  uint16_t bump_count;               // byte, for the engine sound
  uint16_t state;
  uint16_t other_steer;              // the other player's steering word, set when bumped
  bool earlier_reads_mpy;            // player 2's copy uses $0858 for the high word of the
                                     // projected position of cars earlier in the list
  uint16_t later_mask;               // $FF80 for player 1, $FF00 for player 2
} Bumper;

static const Bumper kBumpers[2] = {
  { 0x0204, 0x0206, 0x1E5A, 0x1E5C, 0x1ED7, 0x1EDA, 0x1E8A, 0x0053, 0x0118, 0x1EFB, 0x1EF5, 0x1E80,
    true, 0xFF80 },
  { 0x0206, 0x0204, 0x1E5C, 0x1E5A, 0x1EDA, 0x1ED7, 0x1E8C, 0x0040, 0x012A, 0x1EFC, 0x1EF6, 0x1E7E,
    false, 0xFF00 },
};

// A car's track position after this update, from its speed (a) x frames, into $001A-$001C
// (24 bits). Saturates where the original does (never in the recordings).
static void car_projected(uint16_t x, uint16_t a, uint16_t mask, bool reads_mpy) {
  m7a(a);
  m7b(g_ram[RaceFramesElapsed]);
  a = mpy_mh() & mask;
  uint16_t lo;
  if((a & 0x8000) ? a != mask : a != 0) {  // never happens in the recordings
    ram16_set(0x001C, (a & 0x8000) ? 0x00FF : 0);
    lo = (a & 0x8000) ? 0x8000 : 0;
  } else {
    // A 16-bit read of $2136: the high byte comes from $2137, which latches the counters
    // and returns open bus, normally $2136's byte again (HDMA can leave another there).
    uint16_t hi = 0x0858;
    if(reads_mpy) hi = (uint16_t) (tg_bus_read8(0x2136) | tg_bus_read8(0x2137) << 8);
    ram16_set(0x001C, hi);
    lo = mpy_lm();
  }
  uint32_t sum = (uint32_t) lo + ram16((uint16_t) (0x0218 + x));
  ram16_set(0x001A, (uint16_t) sum);
  ram16_set(0x001C, (uint16_t) (ram16((uint16_t) (0x021A + x)) + ram16(0x001C) + (sum > 0xFFFF)));
}

static uint16_t car_speed2(const Bumper* b, uint16_t x) {
  uint16_t v = (uint16_t) (ram16((uint16_t) (0x021C + x)) << 1);
  return v != 0 ? v : ram16(b->other_speed);
}

// $E3D3 / $E5B2: the player's car against each car record, starting after its own and
// wrapping at the end of the list, until one is out of reach.
static void bump_cars(const Bumper* b) {
  uint16_t a = ram16(b->car);
  for(;; a = ram16(0x0020)) {
    a = (uint16_t) (a + 0x0012);
    uint16_t x = a == 0x0168 ? 0 : a;
    dp16_set(0x20, x);
    uint16_t mine = ram16(b->car);
    if(x < mine) {
      // Earlier in the list, so a lap on: add the track length.
      car_projected(x, ram16((uint16_t) (0x021C + x)), 0xFF00, b->earlier_reads_mpy);
      uint32_t sum = (uint32_t) ram16(0x001B) + ram16(RaceCars);
      ram16_set(0x001B, (uint16_t) sum);
      if(sum > 0xFFFF) return;
    } else {
      if(x == mine) return;
      uint16_t v = ram16((uint16_t) (0x021C + x));
      if(v == 0 && ram16(b->other_pos) >= 0xFC00) return;
      car_projected(x, v, b->later_mask, true);
    }
    uint16_t pos = ram16(b->pos);
    if(pos >= 0xFC00) return;
    if(pos < ram16(0x001B) && (uint16_t) (pos - ram16(0x001B)) < 0xFFF9) return;
    // Only cars going slower than the player.
    if(!((uint16_t) (car_speed2(b, x) - ram16(b->speed)) & 0x8000)) continue;
    ram16_set(0x001E, asr(ram16((uint16_t) (0x0214 + x))));
    uint16_t side = (uint16_t) (asr(ram16(b->x)) - ram16(0x001E));
    bool near = (side & 0x8000) ? side >= 0xF800 : side < 0x0800;
    bool loud = (uint16_t) (car_speed2(b, x) - ram16(b->speed)) < 0xE200;
    if(!near) {
      if(loud) ram16_set(b->messages, tab8((uint16_t) (0xE1CA + bump_sound())));
      continue;
    }
    if(loud) ram16_set(b->messages, tab8((uint16_t) (0xE1CE + bump_sound())));
    g_ram[b->bump_count] = (uint8_t) (g_ram[b->bump_count] + 0x30);
    if(x == ram16(b->other_car)) {
      // The other player: swap speeds and drop in behind.
      if(ram16(b->other_view_off) != 0) continue;
      ram16_set(b->other_steer, (uint16_t) (neg(side) << 3));
      tsb16(b->state, 0x0080);
      ram16_set(0x001A, (uint16_t) (ram16(b->speed) - ram16(b->other_speed)));
      ram16_set(b->other_speed, (uint16_t) (ram16(0x001A) + ram16(b->other_speed)));
      ram16_set(b->speed, (uint16_t) (ram16(b->speed) - ram16(0x001A)));
      uint16_t p = (uint16_t) (ram16(b->other_pos) - 0x0008);
      ram16_set(b->pos, p);
      ram16_set((uint16_t) (0x0219 + ram16(b->car)), p);
    } else {
      push_car(x, b->speed);
      tsb16(b->state, 0x0080);
      uint16_t p = (uint16_t) (ram16(0x001B) - 0x0008);
      ram16_set(b->pos, p);
      ram16_set((uint16_t) (0x0219 + ram16(b->car)), p);
    }
  }
}

// Laps: each player's copy of $E789 / $E826.
typedef struct Lapper {
  uint16_t car, pos, lap, shown, fuel, finish_sound, running_bit, state;
  uint8_t finish_value;   // written to the finishing order list at $1F12
  uint16_t ai, finished, view_pos, sway, best, x;
} Lapper;

static const Lapper kLappers[2] = {
  { 0x0204, 0x1ED7, 0x1E76, 0x1F00, 0x1E62, 0x0128, 0x01, 0x1EF5, 0, 0x1EF3, 0x1EDB, 0x0046,
    0x1E9A, 0x1F26, 0x1E8A },
  { 0x0206, 0x1EDA, 0x1E78, 0x1F02, 0x1E64, 0x013A, 0x02, 0x1EF6, 1, 0x1EF4, 0x1EDC, 0x0059,
    0x1E9C, 0x1F28, 0x1E8C },
};

static void count_lap(const Lapper* l) {
  uint16_t y = ram16(l->car);
  uint16_t a = ram16(l->pos);
  if(a >= 0xFC00) {  // backwards over the line; never happens in the recordings
    ram16_set(l->pos, (uint16_t) (a + ram16(RaceCars)));
    g_ram[0x021B + y]--;
    ram16_set(l->lap, (uint16_t) (ram16(l->lap) - 1));
  } else if(a >= ram16(RaceCars)) {
    ram16_set(l->pos, (uint16_t) (a - ram16(RaceCars)));
    uint8_t laps = g_ram[0x021B + y]++;
    if(laps == g_ram[0x1E56]) {
      // Finished.
      if((g_ram[l->shown] & 0x80) && g_ram[l->fuel] != 0) g_ram[l->finish_sound] = 0;
      g_ram[0x1EB4] &= (uint8_t) ~l->running_bit;
      g_ram[l->state] |= 0x04;
      uint16_t x = ram16(0x1EB0);
      ram16_set(l->shown, x);
      g_ram[0x1F12 + x] = l->finish_value;
      x++;
      ram16_set(0x1EB0, x);
      g_ram[l->ai] = 0xFF;
      if(!(ram16(l->finished) & 0x8000)) {
        ram16_set(l->view_pos, 0);
        tsb16((uint16_t) (l->finished + 1), 0x00FF);
      }
      ram16_set(l->sway, 0);
      x--;
      if(x != 0) {
        uint16_t k = (uint16_t) ((ram16((uint16_t) (0x1F11 + x)) & 0x00FF) << 1);
        uint16_t t = ram16((uint16_t) (0x1F26 + k));
        if(t >= ram16(0x1EAE)) ram16_set(0x1EAE, (uint16_t) (t + 1));  // never happens in the recordings
      }
      ram16_set(l->best, ram16(0x1EAE));
    }
    ram16_set(l->lap, (uint16_t) (ram16(l->lap) + 1));
  }
  ram16_set((uint16_t) (0x0214 + y), ram16(l->x));
}

// $05:D2E0, JSL. Drives both players' cars for one race update. A view that is off (no
// player) only has its revs worked out. Leaves 16-bit A and index; registers and
// flags are don't-care.
void RaceDrivePlayers(TgRegs* r) {
  g_db = r->db;
  g_d = r->d;
  const Driver* p1 = &kDrivers[0];
  const Driver* p2 = &kDrivers[1];
  if(ram16(p1->view_off) != 0) {
    ram16_set(p1->rpm, rpm_from_speed(p1, (uint16_t) (ram16(p1->gear) << 1)));
  } else {
    drive_player(p1);
  }
  if(!(ram16(p2->ai) & 0x8000) && ram16(0x1F04) == 0) {
    drive_rival();
  } else if(ram16(p2->view_off) != 0) {  // never happens in the recordings
    ram16_set(p2->rpm, rpm_from_speed(p2, (uint16_t) (ram16(p2->gear) << 1)));
  } else {
    drive_player(p2);
  }
  // $E3B9: the car records follow the players.
  ram16_set((uint16_t) (0x0219 + ram16(RaceP1Car)), ram16(RaceP1TrackPos));
  ram16_set((uint16_t) (0x0219 + ram16(RaceP2Car)), ram16(RaceP2TrackPos));
  if(ram16(p1->view_off) == 0) bump_cars(&kBumpers[0]);
  if(ram16(p2->view_off) == 0) bump_cars(&kBumpers[1]);
  count_lap(&kLappers[0]);
  count_lap(&kLappers[1]);
  if(ram16(RaceP1Speed) < 0x0300) ram16_set(p1->skid, 0);
  if(ram16(RaceP2Speed) < 0x0300) ram16_set(p2->skid, 0);
}
