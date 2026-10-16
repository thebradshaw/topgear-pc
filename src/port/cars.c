// Moving the cars along the track and keeping the car list in track order (bank $00).
//
// The car list is records of 18 bytes at $0200 (RaceCars), addressed as offsets x from
// $0200 (the original runs these routines with the direct page there), so field f of the
// record after x is at x + $12 + f. Record 0's first word is the track length; RaceP1Car
// and RaceP2Car hold the players' offsets. Fields, as far as these routines show:
//   $00      car id in bits 0-4
//   $02      side-to-side position
//   $06-$08  24-bit track position (fraction byte, then the word compared)
//   $09      laps completed
//   $0A      speed, $0C speed change per update, $0E updates left for that change
//   $10      cleared each update

#include "port.h"
#include "variables.h"

#define CAR(x, f) ((uint16_t) (RaceCars + (x) + (f)))

// The ROM table at $00:A3C6, indexed by x, gives the next record to visit (x - $12).
static uint16_t next_car(uint16_t x) { return rom16(0x00A3C6 + x); }

// 16 x 8 signed multiply on the PPU, as the original does it; returns the 24-bit product.
static uint32_t ppu_multiply(uint16_t a, uint8_t b) {
  tg_bus_write8(0x211b, (uint8_t) a);
  tg_bus_write8(0x211b, (uint8_t) (a >> 8));
  tg_bus_write8(0x211c, b);
  return (uint32_t) ((int16_t) a * (int8_t) b) & 0xffffff;
}

// $00:A30C / $00:A372. A player has run into the back of car x: the car takes half the
// player's speed (rounded to $40) as a push spread over $40 updates, the player gets the
// car's old speed doubled, and the car's position snaps to the player's.
static void bump_car(uint16_t x, uint16_t speed_adr, uint16_t track_pos_adr) {
  uint16_t speed = ram16(speed_adr);
  if(speed & 0x8000) return;
  uint16_t old_speed = ram16(CAR(x, 0x0A));
  ram16_set(speed_adr, speed >> 1);
  uint16_t a = ram16(CAR(x, 0x0C));
  if(a != 0) a = (uint16_t) ppu_multiply(a, g_ram[CAR(x, 0x0E)]);  // never happens in the recordings
  a = (uint16_t) (a + ram16(CAR(x, 0x0A)));
  ram16_set(CAR(x, 0x0A), a);
  uint16_t push = (uint16_t) ((ram16(speed_adr) - a) & 0xFFC0);
  ram16_set(CAR(x, 0x0A), (uint16_t) (push + ram16(CAR(x, 0x0A))));
  uint16_t step = push >> 6;
  if(step >= 0x200) step |= 0xFE00;
  ram16_set(CAR(x, 0x0C), (uint16_t) -step);
  ram16_set(CAR(x, 0x0E), 0x40);
  if(ram16(CAR(x, 0x0A)) == 0) {  // never happens in the recordings
    ram16_set(CAR(x, 0x0A), ram16(CAR(x, 0x0C)));
    ram16_set(CAR(x, 0x0E), (uint16_t) (ram16(CAR(x, 0x0E)) - 1));
  }
  ram16_set(speed_adr, (uint16_t) (old_speed << 1));
  ram16_set(CAR(x, 0x07), ram16(track_pos_adr));
}

// $00:AA79. Race position of car x, counting from 1: one plus the cars on fewer laps, or
// on the same lap and later in the list.
static uint16_t race_position(uint16_t x) {
  uint8_t lap = g_ram[CAR(x, 0x1B)];
  uint16_t y = 0x15;
  for(uint16_t k = 0; k < 20 * RaceCarSize; k += RaceCarSize) {
    uint8_t other = g_ram[CAR(k, 0x1B)];
    if(lap > other || (lap == other && x >= k)) y--;
  }
  return y;
}

static void swap_words(uint16_t a, uint16_t b) {
  uint16_t t = ram16(a);
  ram16_set(a, ram16(b));
  ram16_set(b, t);
}

// $00:A1BD. Wraps cars past the finish line onto the next lap (recording finishing
// times), bubbles the list back into track order, and works out both players' race
// positions. Entered with 16-bit A and index; leaves A = 0, X = player 2's car, Y =
// player 2's position; N/V/Z/C are don't-care.
void RaceSortCars(TgRegs* r) {
  uint16_t length = ram16(RaceCars);
  uint16_t x = 0x156;
  for(;;) {
    uint16_t a = ram16(CAR(x, 0x19));
    if(a >= length) {
      if(a >= 0xFC00) {
        a = (uint16_t) (a + length);  // never happens in the recordings
        ram16_set(CAR(x, 0x19), a);
      } else {
        a = (uint16_t) (a - length);
        ram16_set(CAR(x, 0x19), a);
        if(ram16(CAR(x, 0x1C)) == 0) {
          a = 0;
        } else {
          uint8_t old_lap = g_ram[CAR(x, 0x1B)]++;
          if(old_lap == g_ram[RaceLaps]) {
            // Finished: note the order and the time.
            uint8_t id = g_ram[CAR(x, 0x12)] & 0x1F;
            uint16_t y = ram16(RaceFinishCount);
            g_ram[RaceFinishOrder + y] = id;
            g_ram[RaceFinishCount]++;
            uint16_t slot = (uint16_t) ((id << 1) & 0xFF);
            if(y != 0) {
              uint16_t prev = (uint16_t) ((ram16(RaceFinishOrder - 1 + y) & 0xFF) * 2);
              if(ram16(RaceFinishTimes + prev) >= ram16(RaceTime))  // never happens in the recordings
                ram16_set(RaceTime, (uint16_t) (ram16(RaceFinishTimes + prev) + 1));
            }
            ram16_set(RaceFinishTimes + slot, ram16(RaceTime));
          }
          a = ram16(CAR(x, 0x19));
        }
      }
    }
    if(a < ram16(CAR(x, 0x07))) {
      // The car after x is behind it now: swap them, and follow the players' cars.
      for(int i = 0; i < 2; i++) {
        uint16_t p = i ? RaceP2Car : RaceP1Car;
        if(x == ram16(p)) ram16_set(p, (uint16_t) (x - RaceCarSize));
        else if((uint16_t) (x - RaceCarSize) == ram16(p)) ram16_set(p, x);
      }
      static const uint8_t kFields[] = { 0x00, 0x02, 0x04, 0x06, 0x0A, 0x08, 0x0C, 0x0E };
      for(int i = 0; i < 8; i++) swap_words(CAR(x, kFields[i]), CAR(x, kFields[i] + RaceCarSize));
      if(x != 0x156) x += RaceCarSize;
      continue;
    }
    x = next_car(x);
    if(x == 0) break;
  }
  ram16_set(RaceP1Position, (uint16_t) (race_position(ram16(RaceP1Car)) - 1));
  x = ram16(RaceP2Car);
  uint16_t y = (uint16_t) (race_position(x) - 1);
  ram16_set(RaceP2Position, y);
  r->a = 0;
  r->x = x;
  r->y = y;
  r->d = 0;
}

// $00:A175. Once per race update: moves every car by its speed times the frames elapsed,
// applies speed changes, and bumps cars a player has caught up with; then RaceSortCars.
void RaceMoveCars(TgRegs* r) {
  uint16_t x = 0x168;
  do {
    uint16_t speed = ram16(CAR(x, 0x0A));
    if(speed != 0) {
      speed = (uint16_t) (speed + ram16(CAR(x, 0x0C)));
      ram16_set(CAR(x, 0x0A), speed);
      uint16_t left = (uint16_t) (ram16(CAR(x, 0x0E)) - 1);
      ram16_set(CAR(x, 0x0E), left);
      if(left == 0) ram16_set(CAR(x, 0x0C), 0);
      uint32_t product = ppu_multiply(speed, g_ram[RaceFramesElapsed]);
      uint32_t pos = (uint32_t) (g_ram[CAR(x, 0x06)] | ram16(CAR(x, 0x07)) << 8);
      ram16_set(0x001A, ram16(CAR(x, 0x07)));
      pos = (pos + product) & 0xffffff;
      g_ram[CAR(x, 0x06)] = (uint8_t) pos;
      ram16_set(CAR(x, 0x07), (uint16_t) (pos >> 8));
      if((uint16_t) (pos >> 8) >= ram16(CAR(x, 0x19))) {
        // Caught up with the car ahead: a player's car alongside it bumps it.
        uint16_t side = (uint16_t) (ram16(CAR(x, 0x02)) - ram16(CAR(x, 0x14)));
        bool close = (side & 0x8000) ? side >= 0xF800 : side < 0x0800;
        if(close && ram16(CAR(x, 0x1C)) == 0) {
          if(x == ram16(RaceP1Car)) {
            if(ram16(0x0040) == 0) {
              bump_car(x, RaceP1Speed, RaceP1TrackPos);
              g_ram[RaceP1Bumped] |= 0x80;
            }
          } else if(x == ram16(RaceP2Car)) {
            if(ram16(0x0053) == 0) {
              bump_car(x, RaceP2Speed, RaceP2TrackPos);
              g_ram[RaceP2Bumped] |= 0x80;
            }
          }
        }
      }
    }
    ram16_set(CAR(x, 0x10), 0);
    x = next_car(x);
  } while(x != 0);
  RaceSortCars(r);
}

// $00:AB7A. Once both players have finished: runs the other cars on at 8x speed, 8
// frames of race clock per step, until ten cars have finished, recording their order
// and times. Leaves A = 0, X = 0; Y and N/V/Z/C are don't-care.
void RaceFinishOthers(TgRegs* r) {
  ram16_set(0x001A, ram16(RaceTime));
  uint16_t x = 0x168;
  do {
    ram16_set(CAR(x, 0x0A), (uint16_t) (ram16(CAR(x, 0x0A)) << 3));
    x = next_car(x);
  } while(x != 0);
  ram16_set(0x000C, 0);
  do {
    x = 0x168;
    do {
      uint16_t speed = ram16(CAR(x, 0x0A));
      if(speed != 0) {
        uint32_t sum = (uint32_t) speed + ram16(CAR(x, 0x06));
        ram16_set(CAR(x, 0x06), (uint16_t) sum);
        if(sum > 0xFFFF) g_ram[CAR(x, 0x08)]++;
        uint16_t pos = ram16(CAR(x, 0x07));
        if(pos >= ram16(RaceCars)) {
          ram16_set(CAR(x, 0x07), (uint16_t) (pos - ram16(RaceCars)));
          uint8_t old_lap = g_ram[CAR(x, 0x09)]++;
          if(old_lap == g_ram[RaceLaps]) {
            uint8_t id = g_ram[CAR(x, 0x00)] & 0x1F;
            uint16_t y = ram16(RaceFinishCount);
            g_ram[RaceFinishOrder + y] = id;
            ram16_set(RaceFinishCount, (uint16_t) (y + 1));
            ram16_set((uint16_t) (RaceFinishTimes + ((id << 1) & 0xFF)), ram16(0x001A));
          }
        }
      }
      x = next_car(x);
    } while(x != 0);
    ram16_set(0x001A, (uint16_t) (ram16(0x001A) + 8));
  } while(ram16(RaceFinishCount) < 10);
  r->a = 0;
  r->x = 0;
  r->d = 0;
}
