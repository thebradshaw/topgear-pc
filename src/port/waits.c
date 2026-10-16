// Routines that wait for interrupts (TG_WAITS): they run as a fiber in native mode, and
// tg_port_wait stands in for each pass of the original's spin loop.

#include <string.h>

#include "port.h"

#include "race_common.h"
#include "variables.h"

static uint16_t dp16(TgRegs* r, uint16_t o) { return ram16((uint16_t) (r->d + o)); }
static void dp16_set(TgRegs* r, uint16_t o, uint16_t v) { ram16_set((uint16_t) (r->d + o), v); }

// $00:8878. While the race is played (not the demo), Start pauses it: $1E58 = $FFFF
// tells the raster IRQ, which keeps reading the pads, and Start again resumes. Player 2's
// Start counts when there is a second player ($1F04).
void RacePauseCheck(TgRegs* r) {
  r->a = 0x2000;
  if(ram16(WarmBootFlag) & 0x8000) return;
  uint16_t p1 = dp16(r, 0x14), p2 = dp16(r, 0x16);
  dp16_set(r, 0x14, p1 & ~0x2000);
  if(!(p1 & 0x2000)) {
    r->x = ram16(0x1F04);
    if(r->x == 0) return;
    dp16_set(r, 0x16, p2 & ~0x2000);
    if(!(p2 & 0x2000)) return;
  }
  r->x = 0xFFFF;
  ram16_set(0x1E58, 0xFFFF);
  for(;; tg_port_wait()) {
    uint16_t v = dp16(r, 0x14);
    dp16_set(r, 0x14, v & ~0x2000);
    if(v & 0x2000) return;
    r->x = ram16(0x1F04);
    if(r->x == 0) continue;
    v = dp16(r, 0x16);
    dp16_set(r, 0x16, v & ~0x2000);
    if(v & 0x2000) return;
  }
}

// $00:EC0E. Sets $0C to ~A, then waits until the raster IRQ has counted it up to zero:
// with A = 0, until the next race update is due.
void RaceSyncWait(TgRegs* r) {
  dp16_set(r, 0x0C, (uint16_t) ~r->a);
  while(dp16(r, 0x0C) != 0) tg_port_wait();
  r->a = 0;
  r->n = false;
  r->z = true;
}

// MVN from bank:src to low RAM, count bytes, forwards as MVN copies.
static void mvn_low(uint16_t dst, uint8_t bank, uint16_t src, int count) {
  for(int i = 0; i < count; i++) g_ram[(uint16_t) (dst + i)] = tg_read8((uint32_t) bank << 16 | (uint16_t) (src + i));
}

// $00:CCDB. The race palettes: clears the palette buffer at $038E, copies the course's
// colours and both players' car colours from bank $08, sets up the sky ($0100-$0104, $013D)
// for the course, then has the NMI upload them ($038C) and waits for it. Afterwards the
// buffer is copied on to $038E and $044E. Registers and flags are don't-care; DB is $00.
static void RaceLoadPalettes(TgRegs* r) {
  memset(g_ram + 0x038E, 0, 0x200);
  for(int i = 0; i < 8; i++) ram16_set(0x1B9C + 2 * i, (uint16_t) (i * 0x200));
  ram16_set(0x1C56, 0);
  ram16_set(0x1DA4, 0);
  mvn_low(0x03EE, 0x08, 0x899A, 0xA0);
  mvn_low(0x050E, 0x08, 0x925A, 0x80);
  ram16_set(0x1C08, 0);
  ram16_set(0x1D56, 0);
  ram16_set(0x1C0C, 0);
  ram16_set(0x1D5A, 0);
  ram16_set(0x1C0A, 0x80);
  ram16_set(0x1D58, 0x80);
  ram16_set(0x1C0E, 0x80);
  ram16_set(0x1D5C, 0x80);
  mvn_low(0x048E, 0x08, (uint16_t) ((ram16(0x1F8F) << 5) + 0x92DA), 0x20);
  if(ram16(0x1FAC) == ram16(0x1F8F)) {
    ram16_set(0x1C0A, 0);
    ram16_set(0x1D58, 0);
  }
  mvn_low(0x04AE, 0x08, (uint16_t) ((ram16(0x1FAC) << 5) + 0x92DA), 0x20);
  uint16_t rival = ram16(0x1FF8);
  mvn_low(0x04CE, 0x08, (uint16_t) ((rival << 5) + 0x935A), 0x20);
  mvn_low(0x04EE, 0x08, (uint16_t) ((rival << 5) + 0x93DA), 0x20);
  ram16_set(0x1FF8, (rival + 1) & 3);

  uint16_t y = (uint16_t) (ram16(CarTypeIndex) << 1);
  ram16_set(0x013D, 0x44);
  uint16_t sky = rom16(0x00E0A4 + y), base;
  if(!(sky & 0x8000)) {
    ram16_set(0x0398, sky);
    ram16_set(0x039A, sky);
    ram16_set(0x039C, sky);
    for(int x = 0x20; x != 0; x -= 2) ram16_set(0x03CC + x, sky);
    ram16_set(0x0100, sky);
    ram16_set(0x0102, 0xFFFF);
    ram16_set(0x0104, rom16(0x00E024 + y));
    base = 0x8A5A;
  } else {
    ram16_set(0x0102, (uint16_t) ((sky & 0x3FFF) - 1));
    ram16_set(0x013D, 0x58);
    if(sky & 0x4000) {
      ram16_set(0x0100, (sky & 0xC000) | 0xDF);
      tg_bus_write8(0x2132, 0xED);
      ram16_set(0x0104, rom16(0x00E064 + y));
      dp16_set(r, 0x66, 0xB494);
      base = 0x8E5A;
    } else {
      ram16_set(0x0100, sky & 0xC000);
      tg_bus_write8(0x2132, 0xE0);
      ram16_set(0x0104, rom16(0x00E024 + y));
      dp16_set(r, 0x66, 0xB6B6);
      base = 0x8A5A;
    }
  }
  mvn_low(0x03AE, 0x08, (uint16_t) (base + rom16(0x00DFE4 + y)), 0x20);
  ram16_set(0x038C, 0xFFFF);
  while(ram16(0x038C) & 0x8000) tg_port_wait();
  memmove(g_ram + 0x038E, g_ram + 0x03CE, 0xC0);
  memmove(g_ram + 0x044E, g_ram + 0x038E, 0xC0);
  r->db = 0;
}

// $00:B639. Seven HDMA channels: CGRAM colours (0-4), BG3 scroll (5), window edges (6).
static void RaceSetupHdma(void) {
  tg_bus_write8(0x420C, 0);
  tg_bus_write16(0x4300, 0x2142);
  for(int c = 1; c <= 4; c++) tg_bus_write16(0x4300 + 0x10 * c, 0x2242);
  tg_bus_write16(0x4350, 0x1143);
  uint16_t window = 0x2604;
  if(ram16(0x0100) & 0x8000) {
    tg_bus_write16(0x2128, 0x00FF);
    window = 0x2841;
  }
  tg_bus_write16(0x4360, window);
  for(int c = 0; c <= 4; c++) tg_bus_write16(0x4302 + 0x10 * c, 0x0C12);
  tg_bus_write16(0x4352, 0xBEFA);
  tg_bus_write16(0x4362, 0x0E54);
  for(int c = 0; c <= 6; c++) tg_bus_write8(0x4304 + 0x10 * c, 0);
  tg_bus_write8(0x4357, 0);
  tg_bus_write8(0x4367, 0);
  tg_bus_write8(0x4357, 0x08);
  tg_bus_write8(0x4367, 0x08);
  for(int c = 0; c <= 4; c++) tg_bus_write8(0x4307 + 0x10 * c, (uint8_t) (0x08 + c));
  tg_bus_write8(0x420C, 0x7F);
}

// $00:BF14. Everything before a race: course, skyline, roadside, cars, grid, the computer
// steering's gap table, tile caches, palettes, HUD, names, then HDMA from the next race
// update on, and $1E56 for the course. Registers and flags are don't-care; DB is $00.
void RaceLoadAll(TgRegs* r) {
  RaceLoadCourse(r);
  RaceLoadSkyline(r);
  RaceLoadRoadside(r);
  RaceLoadCars(r);
  RaceSetupGrid(r);
  tg_call_jsr(r, 0xA8E1, 0xBF26, TG_MX_16);  // RaceBuildGapTable: reads junk past its table
  RacePreloadTileCache(r);
  RaceLoadPalettes(r);
  RaceDrawHudFrame(r);
  RaceSetupNames(r);
  RaceClearHdmaTables(r);
  r->a = 0;
  RaceSyncWait(r);
  RaceSetupHdma();
  ram16_set(0x1E56, rom16(0x00DEA4 + (uint16_t) (ram16(CarTypeIndex) << 1)));
  r->db = 0;
}
