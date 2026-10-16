// The race program: from the end of the menus to the end of a race, then back to the
// menus. It waits for interrupts throughout, so it runs as a fiber (TG_WAITS).

#include <string.h>

#include "port.h"

#include "race_common.h"
#include "variables.h"

void RaceRevEngines(TgRegs* r);
void RaceBuildSprites(TgRegs* r);
void RaceDrawHud(TgRegs* r);
void RaceBuildWindowTable(TgRegs* r);
void RaceUpdateSegmentValues(TgRegs* r);
void RaceUpdateMessages(TgRegs* r);
void RaceMoveCars(TgRegs* r);
void RaceSortCars(TgRegs* r);
void RaceListVisibleCars(TgRegs* r);
void RaceListGridCars(TgRegs* r);
void RaceFinishOthers(TgRegs* r);
void RaceBuildSplitWindowTable(TgRegs* r);
void RaceDrivePlayers(TgRegs* r);
void RaceRenderViews(TgRegs* r);
void RaceCallOvertakes(TgRegs* r);
void RaceUpdatePitStops(TgRegs* r);
void RacePauseCheck(TgRegs* r);
void RaceLoadAll(TgRegs* r);

static uint16_t dp16(TgRegs* r, uint16_t o) { return ram16((uint16_t) (r->d + o)); }
static void dp16_set(TgRegs* r, uint16_t o, uint16_t v) { ram16_set((uint16_t) (r->d + o), v); }
static void dp8_set(TgRegs* r, uint16_t o, uint8_t v) { g_ram[(uint16_t) (r->d + o)] = v; }

int tg_port_race_frames = 3;

// Routines in other banks, whose calls to original code are in their bank.
static void in_bank(uint8_t bank, void (*fn)(TgRegs*), TgRegs* r) {
  uint8_t old = tg_port_set_bank(bank);
  fn(r);
  tg_port_set_bank(old);
}

// Translated routines that can decline a call (see tg_port_decline) run emulated then.
static void render_views(TgRegs* r, uint16_t ret) {
  in_bank(0x7E, RaceRenderViews, r);
  if(tg_port_take_declined()) tg_call_jsl(r, 0x7E2000, ret, TG_MX_16);
}

static void build_split_window_table(TgRegs* r, uint16_t ret) {
  RaceBuildSplitWindowTable(r);
  if(tg_port_take_declined()) tg_call_jsr(r, 0xB18C, ret, TG_MX_16);  // hangs, as the original does
}

static void sync_wait(TgRegs* r) {
  r->a = 0;
  RaceSyncWait(r);
}

// The demo race ends at the end of the demo ($51) or on a button press, which also
// returns to the title ($E1, WarmBootFlag). True when it's over.
static bool demo_over(TgRegs* r) {
  if(!(ram16(WarmBootFlag) & 0x8000)) return false;
  if(dp16(r, 0x51) & 0x8000) return true;
  if((dp16(r, 0x10) | dp16(r, 0x12)) & 0xFFF0) {
    dp16_set(r, 0xE1, 0xFFFF);
    ram16_set(WarmBootFlag, 0x8000);
  }
  return false;
}

// The views' scroll rate for the start-line sky, from the speed and the course's curve
// table at $7E:4C00, through the PPU multiplier.
static uint16_t sky_scroll(TgRegs* r, uint16_t pos_dp, uint16_t speed) {
  uint8_t curve = g_ram[0x4C00 + ((dp16(r, pos_dp) >> 4) & 0xFFFE)];
  uint16_t a = (uint16_t) (ram16(speed) << 1);
  tg_bus_write8(0x211B, 0);
  tg_bus_write8(0x211B, (uint8_t) a);
  tg_bus_write8(0x211C, curve);
  uint16_t product = (uint16_t) (tg_bus_read8(0x2135) | tg_bus_read8(0x2136) << 8);
  return (uint16_t) -product;
}

// $0106 set (negative): the next sky colour goes to $0104.
static void take_sky_colour(void) {
  uint16_t v = ram16(0x0106);
  if(v & 0x8000) {
    ram16_set(0x0104, v);
    ram16_set(0x0106, 0);
  }
}

static void dec_e3(TgRegs* r) { dp16_set(r, 0xE3, (uint16_t) (dp16(r, 0xE3) - 1)); }

// $00:8175: the race music for the course, uploading the sound driver.
static void start_race_music(TgRegs* r) {
  tg_bus_write8(0x2141, 0x18);
  r->a = (uint16_t) (((ram16(CarTypeIndex) & 3) ^ 3) + 1);
  tg_call_jsl(r, 0x078000, 0x818A, TG_MX_16);  // UploadSoundDriver
  tg_bus_write8(0x2140, 0);
  tg_call_jmp(r, 0x8191, 0x8197, TG_MX_A8_I16);  // delay loops: the time is what matters
  tg_bus_write8(0x2140, (uint8_t) (((g_ram[CarTypeIndex] & 3) ^ 3) + 2));
  tg_call_jmp(r, 0x81A3, 0x81A9, TG_MX_A8_I16);
  tg_bus_write8(0x2141, 0x02);
  tg_call_jmp(r, 0x81AE, 0x81B1, TG_MX_A8_I16);
  tg_bus_write8(0x2141, 0x0D);
}

// $00:81B8-$00:848F: interrupt trampolines, HDMA table pointers, race variables and the
// PPU, then NMI and the raster IRQ on.
static void race_setup(TgRegs* r) {
  static const uint16_t kDp[][2] = {
    { 0x00, 0x005C }, { 0x04, 0x005C }, { 0x01, 0xEC1A }, { 0x05, 0xEC5B }, { 0x08, 0xEC7E },
    { 0x0A, 0xEC7E }, { 0x18, 0x0202 }, { 0xA0, 0x0D33 }, { 0xA2, 0xB462 }, { 0xA4, 0x0020 },
    { 0xA8, 0x0000 }, { 0xAA, 0x0363 }, { 0xAC, 0xB462 }, { 0xAE, 0x0020 }, { 0xB0, 0x0000 },
    { 0xB2, 0x0363 }, { 0xB4, 0x0D33 }, { 0xB6, 0xB57B }, { 0xB8, 0x0090 }, { 0xBC, 0x0000 },
    { 0xBE, 0x02F3 }, { 0xC0, 0xB57B }, { 0xC2, 0x0090 }, { 0xC4, 0x0000 }, { 0xC6, 0x02F3 },
    { 0xC8, 0x0080 }, { 0xCE, 0x0080 }, { 0xD4, 0x0080 }, { 0xDA, 0x0080 }, { 0xCC, 0x134A },
    { 0xD2, 0x0F0A }, { 0xD8, 0x156A }, { 0xDE, 0x112A }, { 0xE7, 0x08D2 }, { 0xE9, 0x0D33 },
  };
  for(size_t i = 0; i < sizeof(kDp) / sizeof(kDp[0]); i++) dp16_set(r, kDp[i][0], kDp[i][1]);
  // Both window tables start as one $7F-line run of $945A, then the end.
  static const uint16_t kWindows[] = { 0x0E54, 0x0EAF };
  for(int i = 0; i < 2; i++) {
    dp16_set(r, 0xEB, kWindows[i]);
    ram16_set(kWindows[i], 0x007F);
    ram16_set(kWindows[i] + 1, 0x945A);
    ram16_set(kWindows[i] + 3, 0x0000);
  }
  static const uint16_t kRam[][2] = {
    { 0x178A, 0x0090 }, { 0x17DC, 0x0090 }, { 0x178C, 0x0400 }, { 0x17DE, 0x0400 },
    { 0x178E, 0x0200 }, { 0x17E0, 0x0200 }, { 0x1790, 0x0400 }, { 0x17E2, 0x0400 },
    { 0x1792, 0x7FFF }, { 0x17E4, 0x7FFF },
  };
  for(size_t i = 0; i < sizeof(kRam) / sizeof(kRam[0]); i++) ram16_set(kRam[i][0], kRam[i][1]);
  ram16_set(0x1F8F, ram16(0x1F8F) & 3);
  ram16_set(0x1FAC, ram16(0x1FAC) & 3);
  ram16_set(0x1FF8, ram16(0x1FF8) & 3);
  dp16_set(r, 0x66, 0xA6A6);
  ram16_set(0x0106, 0x0002);
  ram16_set(0x1E6C, 0x0003);
  ram16_set(0x1E6E, 0x0003);
  ram16_set(0x0112, 0x201A);
  ram16_set(0x0114, 0x201A);
  ram16_set(0x0116, 0x3022);
  ram16_set(0x1EB4, 0x0003);
  ram16_set(0x0381, 0xFFFF);
  ram16_set(0x1E76, 0xFFFF);
  ram16_set(0x1E78, 0xFFFF);
  static const uint8_t kDp8[][2] = {
    { 0x52, 0x80 }, { 0x65, 0x80 }, { 0x3A, 0x00 }, { 0x3B, 0x80 }, { 0x3C, 0x07 },
    { 0x3E, 0x00 }, { 0x3F, 0x00 }, { 0x3D, 0xFF },
  };
  for(size_t i = 0; i < sizeof(kDp8) / sizeof(kDp8[0]); i++) dp8_set(r, kDp8[i][0], kDp8[i][1]);
  // PPU registers, in the original's order. The scroll and mode 7 registers are written
  // twice (low, then high byte).
  static const uint16_t kPpu[][2] = {
    { 0x2100, 0x8F }, { 0x2101, 0x01 }, { 0x2102, 0x00 }, { 0x2103, 0x80 }, { 0x2105, 0x61 },
    { 0x2106, 0x00 }, { 0x2107, 0x0C }, { 0x2108, 0x13 }, { 0x2109, 0x13 }, { 0x210A, 0x00 },
    { 0x210B, 0x50 }, { 0x210C, 0x05 },
    { 0x210D, 0x00 }, { 0x210D, 0x00 }, { 0x210E, 0xFF }, { 0x210E, 0x00 },
    { 0x210F, 0x00 }, { 0x210F, 0x00 }, { 0x2110, 0x00 }, { 0x2110, 0x00 },
    { 0x2111, 0x00 }, { 0x2111, 0x00 }, { 0x2112, 0x00 }, { 0x2112, 0x00 },
    { 0x2113, 0x00 }, { 0x2113, 0x00 }, { 0x2114, 0x00 }, { 0x2114, 0x00 },
    { 0x2115, 0x80 }, { 0x2116, 0x00 }, { 0x2117, 0x00 }, { 0x211A, 0x00 },
    { 0x211B, 0x00 }, { 0x211B, 0x00 }, { 0x211C, 0x00 }, { 0x211C, 0x00 },
    { 0x211D, 0x00 }, { 0x211D, 0x00 }, { 0x211E, 0x00 }, { 0x211E, 0x00 },
    { 0x211F, 0x00 }, { 0x211F, 0x00 }, { 0x2120, 0x00 }, { 0x2120, 0x00 },
    { 0x2121, 0x00 }, { 0x2123, 0x00 }, { 0x2124, 0x00 }, { 0x2125, 0xB0 }, { 0x2126, 0x00 },
    { 0x2127, 0xFF }, { 0x2128, 0x00 }, { 0x2129, 0xFF }, { 0x212A, 0x00 }, { 0x212B, 0x00 },
    { 0x212C, 0x17 }, { 0x212D, 0x00 }, { 0x212E, 0x06 }, { 0x212F, 0x00 }, { 0x2130, 0x20 },
    { 0x2131, 0xA6 }, { 0x2132, 0xFF }, { 0x2133, 0x00 }, { 0x213E, 0x00 }, { 0x213F, 0x10 },
    { 0x4200, 0xB1 }, { 0x4207, 0x7C }, { 0x4208, 0x00 }, { 0x4209, 0xE0 }, { 0x420A, 0x00 },
    { 0x420D, 0x00 },
  };
  for(size_t i = 0; i < sizeof(kPpu) / sizeof(kPpu[0]); i++) tg_bus_write8(kPpu[i][0], (uint8_t) kPpu[i][1]);
  tg_cpu_cli();
}

// $00:8510-$00:861F: the cameras pan onto the cars on the grid, one step per frame.
// False if the demo ended.
static bool race_intro(TgRegs* r) {
  do {
    uint16_t frames = (uint16_t) (dp16(r, 0x0C) + 1);
    ram16_set(0x1E90, frames);
    static const uint16_t kViews[2][3] = { { 0x1ED7, 0x46, 0x1E5A }, { 0x1EDA, 0x59, 0x1E5C } };
    do {
      for(int v = 0; v < 2; v++) {
        uint16_t d = (uint16_t) (ram16(kViews[v][0]) - dp16(r, kViews[v][1]));
        if(d == 0) continue;
        d = (uint16_t) ((d >> 4) + 1);
        if(d >= 0x0C) d = 0x0C;
        ram16_set(kViews[v][2], d);
        dp16_set(r, kViews[v][1], (uint16_t) (d + dp16(r, kViews[v][1])));
      }
      ram16_set(0x1E90, (uint16_t) (ram16(0x1E90) - 1));
    } while(ram16(0x1E90) != 0);
    ram16_set(0x1E90, frames);
    if(demo_over(r)) return false;
    RacePauseCheck(r);
    ram16_set(0x1E58, 1);
    sync_wait(r);
    RaceUpdateMessages(r);
    RaceRevEngines(r);
    ram16_set(0x1E9A, sky_scroll(r, 0x46, 0x1E5A));
    ram16_set(0x1E9C, sky_scroll(r, 0x59, 0x1E5C));
    RaceDrawHud(r);
    take_sky_colour();
    render_views(r, 0x85FA);
    RaceBuildWindowTable(r);
    in_bank(0x01, RaceBuildSprites, r);
    RaceUpdateSegmentValues(r);
    if(!(ram16(0x0100) & 0x8000)) build_split_window_table(r, 0x860C);
    dec_e3(r);
    RaceListGridCars(r);
  } while(ram16(0x1ED7) != dp16(r, 0x46) || ram16(0x1EDA) != dp16(r, 0x59));
  return true;
}

// $00:8622-$00:8698: the start lights, four steps of 60 frames (the first of one).
// False if the demo ended.
static bool race_countdown(TgRegs* r) {
  ram16_set(0x1E9A, 0);
  ram16_set(0x1E9C, 0);
  ram16_set(0x1E58, 1);
  dp16_set(r, 0x0C, 0);
  uint16_t step = 0xFFFF, frames = 1;
  for(;;) {
    ram16_set(0x1E90, (uint16_t) (dp16(r, 0x0C) + 1));
    sync_wait(r);
    if(demo_over(r)) return false;
    RaceUpdateMessages(r);
    RaceDrawHud(r);
    if(ram16(0x0100) & 0x8000) RaceBuildWindowTable(r);
    RaceRevEngines(r);
    RacePauseCheck(r);
    ram16_set(0x1E58, 1);
    if(--frames != 0) continue;
    step++;
    ram16_set(0x0128, 0);
    ram16_set(0x013A, 0);
    ram16_set(0x0118, step);
    ram16_set(0x012A, step);
    frames = 0x3C;
    if(step == 4) return true;
  }
}

// $00:8790-$00:87D7: a player who has finished ($1F00/$1F02 negative) gets their final
// position and the finish message.
static void player_finished(uint16_t place, uint16_t msg_clear, uint16_t time,
                            uint16_t time_to, uint16_t final_place, uint16_t place_to, uint16_t laps, uint16_t msg) {
  if(!(ram16(place) & 0x8000)) return;
  ram16_set(msg_clear, 0);
  ram16_set(time_to, ram16(time));
  ram16_set(place, ram16(final_place));
  ram16_set(place_to, ram16(final_place));
  if(!ram16(laps)) ram16_set(laps, (uint16_t) (ram16(laps) + 1));
  ram16_set(msg, 5);
}

// $00:86C3-$00:8831: the race, one pass per race update. Returns at the end of the race
// with $1E58 = $FFFF, or false straight away if the demo ended.
static bool race_main(TgRegs* r) {
  for(;;) {
    dp16_set(r, 0x0E, (uint16_t) (dp16(r, 0x0E) + 1));
    // The original goes on as soon as $0C (frames since the last update, less one) isn't
    // 0, after taking about two frames for an update. Its pace, without slowdown:
    while(dp16(r, 0x0C) == 0 || dp16(r, 0x0C) < tg_port_race_frames - 1) tg_port_wait();
    ram16_set(0x1E90, (uint16_t) (dp16(r, 0x0C) + 1));
    if(demo_over(r)) return false;
    RacePauseCheck(r);
    ram16_set(0x1E58, 0);
    sync_wait(r);
    RaceUpdateMessages(r);
    RaceDrawHud(r);
    RaceUpdatePitStops(r);
    if(ram16(0x1EE0) == 1) dp16_set(r, 0xE1, 0xFFFF);
    in_bank(0x05, RaceDrivePlayers, r);
    if(!(ram16(0x1EDB) & 0x8000)) {
      ram16_set(0x1EDE, ram16(0x1EBE) & 0x00FF);
      dp16_set(r, 0x46, ram16(0x1ED7));
      ram16_set(0x1EE4, ram16(0x1E8A));
    }
    if(!(ram16(0x1EDC) & 0x8000)) {
      ram16_set(0x1EDE, ram16(0x1EDE) & 0x00FF);
      ram16_set(0x1EDE, (ram16(0x1EBE) & 0xFF00) | ram16(0x1EDE));
      dp16_set(r, 0x59, ram16(0x1EDA));
      ram16_set(0x1EE6, ram16(0x1E8C));
    }
    RaceMoveCars(r);
    RaceListVisibleCars(r);
    take_sky_colour();
    render_views(r, 0x8766);
    RaceBuildWindowTable(r);
    in_bank(0x01, RaceBuildSprites, r);
    RaceUpdateSegmentValues(r);
    RaceCallOvertakes(r);
    if(!(ram16(0x0100) & 0x8000)) build_split_window_table(r, 0x877B);
    dec_e3(r);

    uint16_t countdown;
    if(ram16(0x1EB0) < 0x0A) {
      if(ram16(0x1EB4) != 0) {
        ram16_set(0x1EE0, 0);
        continue;
      }
      countdown = ram16(0x1EE0);
    } else {
      player_finished(0x1F00, 0x0128, 0x1E76, 0x011E, 0x1E7A, 0x011A, 0x1E62, 0x0118);
      player_finished(0x1F02, 0x013A, 0x1E78, 0x0130, 0x1E7C, 0x012C, 0x1E64, 0x012A);
      countdown = ram16(0x1EE0);
    }
    if(countdown == 0) {
      ram16_set(0x1EE0, 0x0078);
      continue;
    }
    if(countdown == 1) {
      if(dp16(r, 0xE1) != 0) continue;
    } else {
      ram16_set(0x1EE0, (uint16_t) (countdown - 1));
      if(countdown - 1 != 0) continue;
    }
    if(ram16(0x1EB0) < 0x0A) {
      RaceFinishOthers(r);
      if((ram16(0x1F00) | ram16(0x1F02)) & 0x8000) {
        RaceSortCars(r);
        if(ram16(0x1F00) & 0x8000) ram16_set(0x1F00, ram16(0x1E7A));
        if(ram16(0x1F02) & 0x8000) ram16_set(0x1F02, ram16(0x1E7C));
      }
    }
    ram16_set(0x1E58, 0xFFFF);
    return true;
  }
}

// $00:869A-$00:86C0: the race is on.
static void race_start(void) {
  ram16_set(0x011A, ram16(0x1E7A));
  ram16_set(0x1E5A, 0);
  ram16_set(0x1E5C, 0);
  ram16_set(0x1EDC, 0);
  ram16_set(0x1EAE, 0);
  ram16_set(0x1F26, 0xFFFF);
  ram16_set(0x1F28, 0xFFFF);
  ram16_set(0x1E58, 0);
  ram16_set(0x1EF5, 0x0010);
  ram16_set(0x1EF6, 0x0010);
}

// $00:8834-$00:8875: after the race, back to the menus. After the demo, the title
// screen's cars come back from $013C.
static void race_end(TgRegs* r) {
  if(ram16(WarmBootFlag) & 0x8000) {
    ram16_set(0x1F00, 0);
    ram16_set(0x1F02, 1);
    g_ram[0x1F90] = 0;
    g_ram[0x1FAD] = 0;
    g_ram[0x1F8F] = g_ram[0x013C] & 3;
    g_ram[0x1FAC] = (g_ram[0x013C] >> 2) & 3;
  } else {
    tg_bus_write8(0x2141, 0x01);
  }
  tg_bus_write8(0x4200, 0x00);
  r->a = 1;
  tg_call_jsl(r, 0x0F806D, 0x8873, TG_MX_16);  // GameInit: the menus, until the next race
}

// $00:812A, reached when the menus (GameInit) return: one race, then the menus again.
// Never returns.
void RaceProgram(TgRegs* r) {
  for(;;) {
    r->db = 0;
    for(uint16_t x = 0; x != 0x1F00; x += 2) dp16_set(r, x, 0);
    if(ram16(WarmBootFlag) != 0) {
      // The demo race: the title screen's cars kept in $013C, random ones for the race.
      g_ram[0x013C] = (uint8_t) ((g_ram[0x1F8F] & 3) | (g_ram[0x1FAC] & 3) << 2);
      ram16_set(0x1F8F, race_random() & 3);
      ram16_set(0x1FAC, race_random() & 3);
    } else {
      start_race_music(r);
    }
    race_setup(r);
    dp16_set(r, 0xAC, 0xEC7E);
    dp16_set(r, 0x0A, 0xB6D1);
    RaceLoadAll(r);
    ram16_set(0x1EB6, 0x0004);
    ram16_set(0x1EB8, 0x0024);
    if(!ram16(0x1FF2)) ram16_set(0x1FF2, 1);
    ram16_set(0x1EDC, 0xFFFF);
    dp16_set(r, 0x08, 0xB75A);
    RaceListVisibleCars(r);
    for(int i = 0; i < 2; i++) {
      render_views(r, i == 0 ? 0x84BB : 0x84D1);
      RaceUpdateSegmentValues(r);
      in_bank(0x01, RaceBuildSprites, r);
      RaceUpdateSegmentValues(r);
      dec_e3(r);
      sync_wait(r);
    }
    dp8_set(r, 0xE1, 1);
    dp8_set(r, 0xE2, 1);
    ram16_set(0x1E58, 1);
    dp16_set(r, 0x0C, 1);
    uint8_t warm = g_ram[WarmBootFlag];
    g_ram[0x1EF3] |= warm;
    g_ram[0x1EF4] |= warm;
    while(dp16(r, 0x51) & 0x8000) tg_port_wait();
    dp16_set(r, 0x0C, 0);
    ram16_set(0x1F26, 0);
    ram16_set(0x1F28, 0);
    if(race_intro(r) && race_countdown(r)) {
      race_start();
      race_main(r);
    }
    race_end(r);
  }
}
