// The menus' flow (bank $0F): GameInit and the screens that wait for the NMI. GameInit
// waits throughout, so it runs as a fiber (TG_WAITS): each spin on a variable the NMI
// changes ($08, $0A, $90) becomes a loop around tg_port_wait. The NMI handler ($0F:8000)
// stays the original. docs/notes/menus.md maps the bank.

#include "port.h"
#include "variables.h"

void ResetNamesAndRecords(TgRegs* r);
void MenuSetFadeRowsIntro(TgRegs* r);
void MenuUploadHudMap(TgRegs* r);
void MenuDefaultPlayerName(TgRegs* r);
void MenuDrawPlayerPlace(TgRegs* r);
void MenuRecordPlayerPlaces(TgRegs* r);
void MenuLoadPaletteRow15(TgRegs* r);
void MenuCheckQualified(TgRegs* r);
void MenuAdvanceCourse(TgRegs* r);
void MenuMarkNameEnds(TgRegs* r);
void MenuUnmarkNameEnds(TgRegs* r);
void MenuDrawRaceResults(TgRegs* r);
void MenuDrawStandings(TgRegs* r);
void MenuDrawLapRecord(TgRegs* r);
void MenuDrawCourseLength(TgRegs* r);
void MenuLoadPaletteRow9(TgRegs* r);
void MenuLoadPaletteRow15b(TgRegs* r);
void MenuClearFadeRows(TgRegs* r);
void MenuTitleLogoSprites(TgRegs* r);
void MenuOamHighFill(TgRegs* r);
void MenuTitleTextSprites(TgRegs* r);
void MenuLoadPaletteRow1(TgRegs* r);
void MenuStartDemo(TgRegs* r);
void MenuDrawCarPortraits(TgRegs* r);
void MenuClearScreen(TgRegs* r);
void MenuClearTilemapBuffer(TgRegs* r);
void MenuFadePrepare(TgRegs* r);
void MenuFadeStep(TgRegs* r);
void MenuQueueOamAndMap(TgRegs* r);
void MenuBlackOutRows(TgRegs* r);
void MenuDrawTextSprites(TgRegs* r);
void MenuDrawTextSpritesAt(TgRegs* r);
void MenuDrawScreenText(TgRegs* r);
void MenuDrawText(TgRegs* r);
void MenuDrawBigText(TgRegs* r);
void MenuDrawBoxes(TgRegs* r);
void MenuClearPalette(TgRegs* r);
void MenuSetPaletteDma(TgRegs* r);
void MenuLoadBasePalette(TgRegs* r);
void MenuWriteMode7Regs(TgRegs* r);
void MenuMode7Matrix(TgRegs* r);
void MenuTilesToMode7(TgRegs* r);
void MenuInitVideo(TgRegs* r);
void MenuClearVideo(TgRegs* r);
void MenuClearOam(TgRegs* r);
void MenuDecompressToVramPal(TgRegs* r);
void MenuDecompressToVram(TgRegs* r);
void MenuDecompressToRam(TgRegs* r);
void MenuDrawScreen(TgRegs* r);
void MenuDrawCursorBox(TgRegs* r);
void MenuDrawScreenBigText(TgRegs* r);
void MenuDrawFlagSprites(TgRegs* r);
void MenuCheckPasswords(TgRegs* r);
void MenuNameEntryStart(TgRegs* r);
void MenuPlaceEntryCursor(TgRegs* r);
void MenuStorePlayerName(TgRegs* r);
void MenuDrawNameHeading(TgRegs* r);
void MenuDrawControlPad(TgRegs* r);
void MenuDrawCarStats(TgRegs* r);
void MenuDrawStringAndQueue(TgRegs* r);

static bool pads_held(void) { return (ram16(0x0A) | ram16(0x0C)) != 0; }

static void load(void (*fn)(TgRegs*), TgRegs* r, uint16_t a, uint16_t x, uint16_t y) {
  r->a = a;
  r->x = x;
  r->y = y;
  fn(r);
}

// $0F:A24D: waits until the NMI has counted A frames ($08).
static void MenuWaitFrames(TgRegs* r, uint16_t frames) {
  r->a = frames;
  g_ram[0x08] = 0;
  while(g_ram[0x08] != (uint8_t) frames) tg_port_wait();
  r->z = true;
}

// $0F:9E88 / $0F:9EAC: waits up to the given frames for a button on either pad, then
// for all buttons up.
static void MenuWaitButton(TgRegs* r, uint16_t frames) {
  r->x = frames;
  do {
    MenuWaitFrames(r, 1);
    if(pads_held()) break;
  } while(--r->x);
  do MenuWaitFrames(r, 1); while(pads_held());
  MenuWaitFrames(r, 1);
}

// $0F:9CF2: 32 steps of the fade set up in $066C, one a frame: each marked palette row's
// colour range ($0D:C5F8) is stepped, then the NMI uploads the palette ($90).
static void MenuFadeRun(TgRegs* r) {
  ram16_set(0x0426, 0x20);
  do {
    ram16_set(0x042A, 0x20);
    for(;;) {
      uint16_t x = (uint16_t) (ram16(0x042A) - 2);
      ram16_set(0x042A, x);
      if(x & 0x8000) break;
      r->x = x;
      r->a = ram16((uint16_t) (0x042C + x));
      if(r->a == 0) continue;
      r->y = rom16(0x0FB4D4 + x);
      ram16_set(0x0428, rom16(0x0DC5FA + x));
      r->a = r->x = rom16(0x0DC5F8 + x);
      MenuFadeStep(r);
    }
    r->a = 0x004C;
    ram16_set(0x90, r->a);
    while(ram16(0x90) != 0) tg_port_wait();
    ram16_set(0x0426, (uint16_t) (ram16(0x0426) - 1));
  } while(ram16(0x0426) != 0);
  r->a = 0;
}

static void fade(TgRegs* r, bool in) {
  tg_bus_write8(0x2131, 0x20);
  if(in) ram16_set(0x42, 0x000F);
  r->a = in ? 0 : 1;
  ram16_set(0x0422, r->a);
  MenuFadePrepare(r);
  MenuBlackOutRows(r);
  MenuFadeRun(r);
}

// $0F:9DB2: fades the marked palette rows ($042C) in from black, screen on.
static void MenuFadeIn(TgRegs* r) { fade(r, true); }

// $0F:9DC5: fades them out to black.
static void MenuFadeOut(TgRegs* r) { fade(r, false); }

// $0F:9E1C: clears the tilemap at VRAM 0 (a DMA from a zero byte at $0F:9E2B, next NMI)
// and the tilemap buffer.
static void MenuClearBg(TgRegs* r) {
  tg_bus_write8(0x2115, 0x80);
  tg_bus_write8(0x4314, 0x0F);
  tg_bus_write16(0x2116, 0x0000);
  tg_bus_write16(0x4312, 0x9E2B);
  tg_bus_write16(0x4315, 0x1000);
  tg_bus_write16(0x4310, 0x1809);
  for(int x = 0; x < 0x800; x += 2) ram16_set((uint16_t) (0x0C6C + x), 0);
  r->x = 0;
  ram16_set(0x0424, 0x0002);
  MenuWaitFrames(r, 1);
}

// $0F:A462: the mode 7 tiles at $7E:2000 to VRAM's high bytes, $1000 bytes in each of
// four frames.
static void MenuUploadMode7Tiles(TgRegs* r) {
  MenuWaitFrames(r, 2);
  tg_bus_write16(0x2116, 0x0000);
  tg_bus_write8(0x2115, 0x80);
  tg_bus_write8(0x420B, 0x00);
  tg_bus_write8(0x4314, 0x7E);
  tg_bus_write16(0x4312, 0x2000);
  tg_bus_write16(0x4310, 0x1900);
  for(int i = 0; i < 4; i++) {
    tg_bus_write16(0x4315, 0x1000);
    ram16_set(0x0424, 0x0002);
    MenuWaitFrames(r, 1);
  }
}

// $0F:A4D2: a mode 7 map from $7E:6000 + X (every other byte, 32 per row) to VRAM's low
// bytes, row after row ($80 words apart), straight after a frame.
static void MenuWriteMode7Map(TgRegs* r) {
  MenuWaitFrames(r, 2);
  uint16_t adr = 0;
  tg_bus_write16(0x2116, adr);
  tg_bus_write8(0x2115, 0x00);
  uint16_t x = r->x;
  for(;;) {
    for(int y = 0; y < 0x20; y++, x += 2) tg_bus_write8(0x2118, g_ram[0x6000 + x]);
    if(x == 0x01C0 || x >= 0x0380) break;
    adr = (uint16_t) (adr + 0x80);
    tg_bus_write16(0x2116, adr);
  }
  tg_bus_write8(0x2115, 0x80);
  r->a = adr;
  r->x = x;
  r->y = 0x20;
}

// $0F:8133: uploads the sound driver with song A, as the race program does.
static void MenuStartMusic(TgRegs* r) {
  uint16_t song = r->a;
  tg_bus_write8(0x2141, 0x18);
  tg_call_jsl(r, 0x078000, 0x8143, TG_MX_16);  // UploadSoundDriver
  tg_bus_write8(0x2140, 0);
  tg_call_jmp(r, 0x814A, 0x8150, TG_MX_16);  // the delay: the time is what matters
  r->a = (uint16_t) (song + 1);
  tg_bus_write8(0x2140, (uint8_t) r->a);
}

// $0F:80BD: HDMA channels 3 and 4 write a colour gradient to CGRAM ($0D:C7BC, $0D:C7F3).
static void MenuSetupGradientHdma(TgRegs* r) {
  tg_bus_write8(0x420C, 0);
  tg_bus_write16(0x4330, 0x2140);
  tg_bus_write16(0x4340, 0x2242);
  tg_bus_write16(0x4332, 0xC7BC);
  tg_bus_write16(0x4342, 0xC7F3);
  tg_bus_write8(0x4334, 0x0D);
  tg_bus_write8(0x4344, 0x0D);
  tg_bus_write8(0x4337, 0x0D);
  tg_bus_write8(0x4347, 0x0D);
  tg_bus_write8(0x420C, 0x18);
  r->a = 0x0018;
}

// $0F:821D: the course map: the country's tiles ($0D:8000 for the first four countries,
// $0D:911F for the rest) decompressed to $7E:2000, the course's 16 x 64 slice copied to
// $7E:7000 as 32-byte rows and uploaded to VRAM $2200 in the next frame, then the map's
// 8 x 8 tilemap placed in the buffer ($0E:FD86 per course).
static void MenuLoadCourseMap(TgRegs* r) {
  uint16_t course = ram16(CarTypeIndex);
  uint16_t slice = course < 0x10 ? course : (course & 0x000F);
  load(MenuDecompressToRam, r, 0x800D, course < 0x10 ? 0x8000 : 0x911F, 0);
  uint16_t y = (uint16_t) (slice << 10);
  for(uint16_t x = 0; x < 0x800; x += 0x20) {
    for(int i = 0; i < 0x10; i += 2, y += 2) ram16_set(0x7000 + (uint32_t) (x + i), ram16(0x2000 + (uint32_t) y));
    for(int i = 0x10; i < 0x20; i += 2) ram16_set(0x7000 + (uint32_t) (x + i), 0);
  }
  MenuWaitFrames(r, 1);
  tg_bus_write16(0x2116, 0x2200);
  tg_bus_write8(0x2115, 0x80);
  tg_bus_write8(0x420B, 0x00);
  tg_bus_write8(0x4314, 0x7E);
  tg_bus_write16(0x4312, 0x7000);
  tg_bus_write16(0x4310, 0x1801);
  tg_bus_write16(0x4315, 0x0800);
  ram16_set(0x0424, 0x0002);
  MenuWaitFrames(r, 1);
  uint16_t x = rom16(0x0EFD86 + (uint32_t) (ram16(CarTypeIndex) << 1));
  uint16_t tile = 0x1D20;
  do {
    for(int i = 0; i < 8; i++, x += 2) ram16_set((uint16_t) (0x0C6C + x), tile++);
    x = (uint16_t) (x + 0x30);
  } while(tile < 0x1D60);
  r->a = tile;
  r->x = x;
  r->y = 0xFFFF;
}

// $0F:904B: the menu screens' graphics: screen off, VRAM and palette cleared, then the
// fonts, frames and pictures decompressed to VRAM.
static void MenuLoadMenuScreen(TgRegs* r) {
  ram16_set(0x42, 0x008F);
  MenuWaitFrames(r, 1);
  MenuClearScreen(r);
  MenuClearVideo(r);
  MenuClearPalette(r);
  MenuSetPaletteDma(r);
  ram16_set(0x0424, 0x0002);
  MenuWaitFrames(r, 1);
  tg_bus_write8(0x212C, 0x11);
  tg_bus_write8(0x2105, 0x01);
  tg_bus_write8(0x210D, 0x00);
  tg_bus_write8(0x210D, 0x00);
  tg_bus_write8(0x210E, 0x00);
  tg_bus_write8(0x210E, 0x00);
  tg_bus_write8(0x2101, 0x68);
  load(MenuDecompressToVram, r, 0x800D, 0xA44D, 0x1800);
  load(MenuDecompressToVram, r, 0x800F, 0xBBA7, 0x1000);
  load(MenuDecompressToVram, r, 0x800E, 0x8038, 0x4000);
  ram16_set(0x44, 0x0002);
  load(MenuDecompressToVramPal, r, 0x800E, 0xF45E, 0x0800);
  load(MenuDecompressToVram, r, 0x800D, 0xA27C, 0x0C00);
  MenuLoadBasePalette(r);
  MenuLoadPaletteRow1(r);
  MenuSetFadeRowsIntro(r);
}

// Sprites at OAM $0204 + Y onwards (count entries): moved down $60 and given the high
// table's size bit.
static void lower_sprites(TgRegs* r, uint16_t y, int count) {
  for(int i = 0; i < count; i++, y += 4) {
    uint16_t o = (uint16_t) (0x0204 + y);
    ram16_set(o, (uint16_t) ((ram16(o) + 0x60) | 0x0100));
  }
  r->y = y;
  r->x = 0xFFFF;
}

// One frame of the title's zoom: the matrix for angle $12 and scale $14, written after
// the next NMI.
static void zoom_frame(TgRegs* r) {
  MenuMode7Matrix(r);
  MenuWaitFrames(r, 1);
  MenuWriteMode7Regs(r);
}

// $0F:8E14 / $0F:8E98: spins the logo in ($12 += 8 a frame) while it shrinks to scale
// $100 ($14 -= $10 a frame).
static void zoom_in(TgRegs* r) {
  for(;;) {
    zoom_frame(r);
    ram16_set(0x12, (uint16_t) ((ram16(0x12) + 8) & 0x03FF));
    uint16_t scale = ram16(0x14);
    if(scale <= 0x0100) break;
    ram16_set(0x14, (uint16_t) (scale - 0x10));
  }
  ram16_set(0x14, 0x0100);
  zoom_frame(r);
}

static void MenuMain(TgRegs* r);

// $0F:8C95: the title: "KEMCO PRESENTS" (not after the demo), the mode 7 logo spinning
// in, then the attract wait: 1200 frames for a button (from the 200th on), else the
// demo race ($1FF6 = $FFFF). A button leads to the main menu. $1FF6 = $8000 goes
// straight to the main menu.
static void MenuTitle(TgRegs* r) {
  MenuClearFadeRows(r);
  ram16_set(0x66, 0x0708);
  ram16_set(0x1F84, 0x0107);
  ram16_set(0x1FA1, 0x0107);
  tg_bus_write8(0x212C, 0x10);
  tg_bus_write8(0x212D, 0x00);
  tg_bus_write8(0x4200, 0x81);
  tg_bus_write8(0x2101, 0x03);
  ram16_set(0x42, 0x008F);
  MenuWaitFrames(r, 1);
  if(ram16(WarmBootFlag) != 0x8000) {
    load(MenuDecompressToVram, r, 0x800F, 0xB4FE, 0x7000);
    load(MenuDecompressToVram, r, 0x800E, 0xD81B, 0x6000);
    load(MenuDecompressToRam, r, 0x800E, 0xEFD2, 0x5F00);
    load(MenuDecompressToRam, r, 0x800E, 0xD81B, 0x4000);
    r->y = 0;
    MenuTilesToMode7(r);
    MenuUploadMode7Tiles(r);
    load(MenuDecompressToRam, r, 0x800E, 0xEE41, 0x4000);
    r->x = 0;
    MenuWriteMode7Map(r);
    load(MenuDecompressToRam, r, 0x800E, 0xE12E, 0x4000);
    r->y = 0;
    MenuTilesToMode7(r);
    MenuClearOam(r);
    MenuOamHighFill(r);
    if(!(ram16(WarmBootFlag) & 0x8000)) {
      ram16_set(0x42, 0x000F);
      MenuClearPalette(r);
      MenuLoadPaletteRow9(r);
      MenuTitleLogoSprites(r);
      r->y = 0x0100;
      r->x = 0xB022;  // "KEMCO PRESENTS"
      MenuDrawTextSpritesAt(r);
      lower_sprites(r, 0x0100, 11);
      MenuQueueOamAndMap(r);
      ram16_set(0x0424, 0x0080);
      MenuWaitFrames(r, 1);
      MenuClearFadeRows(r);
      ram16_set(0x043E, r->a);
      MenuFadeIn(r);
      MenuClearFadeRows(r);
      MenuLoadPaletteRow15b(r);
      ram16_set(0x044A, 0x2020);
      MenuFadeIn(r);
      MenuWaitButton(r, 0x0200);
      MenuLoadPaletteRow9(r);
      ram16_set(0x043E, 0x2020);
      MenuFadeOut(r);
    }
    MenuClearOam(r);
    MenuOamHighFill(r);
    MenuQueueOamAndMap(r);
    MenuLoadBasePalette(r);
    MenuSetPaletteDma(r);
    ram16_set(0x0424, 0x0082);
    MenuWaitFrames(r, 1);
    for(int x = 0x22; x >= 0; x -= 2) ram16_set((uint16_t) (0x12 + x), rom16(0x0E8000 + (uint32_t) x));
    tg_bus_write8(0x212C, 0x11);
    tg_bus_write8(0x2105, 0x07);
    MenuWaitFrames(r, 1);
    MenuLoadBasePalette(r);
    for(int x = 0x0E; x >= 0; x -= 2) ram16_set((uint16_t) (0x064C + x), 0);
    MenuClearFadeRows(r);
    ram16_set(0x043C, 0x2020);
    ram16_set(0x042E, 0x2020);
    ram16_set(0x42, 0);
    MenuWaitFrames(r, 1);
    MenuSetupGradientHdma(r);
    for(uint16_t x = 0; x != 0x0F; x++) {
      MenuWaitFrames(r, 2);
      ram16_set(0x42, x);
    }
    zoom_in(r);
    MenuTitleTextSprites(r);
    MenuQueueOamAndMap(r);
    ram16_set(0x0424, 0x0080);
    MenuWaitFrames(r, 1);
    ram16_set(0x14, 0x7F00);
    ram16_set(0x28, 0x0060);
    ram16_set(0x2C, 0xFFA2);
    ram16_set(0x12, 0x0038);
    zoom_frame(r);
    MenuUploadMode7Tiles(r);
    load(MenuDecompressToRam, r, 0x800E, 0xECFC, 0x4000);
    r->x = 0;
    MenuWriteMode7Map(r);
    ram16_set(0x14, 0x0880);
    zoom_in(r);
    r->y = 0x00E0;
    r->x = 0xAFC6;  // the copyright lines
    MenuDrawTextSpritesAt(r);
    lower_sprites(r, 0x00E0, 0x46);
    MenuQueueOamAndMap(r);
    ram16_set(0x0424, 0x0080);
    MenuWaitFrames(r, 1);
    MenuClearFadeRows(r);
    ram16_set(0x044A, 0x2020);
    MenuLoadPaletteRow15b(r);
    MenuFadeIn(r);
    ram16_set(WarmBootFlag, 0x04B0);
    for(;;) {
      ram16_set(WarmBootFlag, (uint16_t) (ram16(WarmBootFlag) - 1));
      if(ram16(WarmBootFlag) & 0x8000) {
        MenuStartDemo(r);
        break;
      }
      MenuWaitFrames(r, 1);
      if(pads_held() && ram16(WarmBootFlag) < 0x03E8) {
        ram16_set(WarmBootFlag, 0);
        break;
      }
    }
    uint16_t x = 0x0F;
    for(; !(x & 0x8000); x--) {
      MenuWaitFrames(r, 2);
      ram16_set(0x42, x);
    }
    MenuWaitFrames(r, 2);
    ram16_set(0x42, x);
    r->x = x;
    tg_bus_write8(0x420C, 0);
    tg_bus_write8(0x2121, 0);
    tg_bus_write8(0x2122, 0);
    tg_bus_write8(0x2123, 0);
  }
  tg_bus_write8(0x212D, 0x02);
  uint16_t warm = ram16(WarmBootFlag);
  if(warm == 0 || warm == 0x8000) {
    if(warm != 0) ram16_set(WarmBootFlag, 0);
    MenuLoadMenuScreen(r);
    MenuMain(r);
  }
  ram16_set(0x42, 0x008F);
}

// The tilemap and sprites to VRAM and OAM in the next NMI.
static void queue_screen(TgRegs* r) {
  MenuQueueOamAndMap(r);
  ram16_set(0x0424, 0x0084);
}

// The highlight box at cell $48, size $4A; X is the first OAM entry it may clear.
static void cursor_box(TgRegs* r, uint16_t cell, uint16_t size, uint16_t oam) {
  ram16_set(0x48, cell);
  r->a = size;
  ram16_set(0x4A, size);
  r->x = oam;
  MenuDrawCursorBox(r);
}

static void draw_screen(TgRegs* r, uint16_t screen) {
  r->a = screen;
  MenuDrawScreen(r);
}

// The pad of the player being set up ($81).
static uint16_t player_pad(TgRegs* r) {
  r->y = (uint16_t) (ram16(0x81) << 1);
  r->a = ram16((uint16_t) (0x0A + r->y));
  return r->a;
}

// Waits for a button in mask on that pad; the original spins on the variable the NMI
// sets.
static uint16_t wait_player_press(TgRegs* r, uint16_t mask) {
  for(;; tg_port_wait()) {
    uint16_t a = player_pad(r) & mask;
    if(a) return a;
  }
}

static void wait_player_release(TgRegs* r) {
  while(player_pad(r) != 0) tg_port_wait();
}

static void fade_out_and_clear(TgRegs* r) {
  MenuFadeOut(r);
  MenuClearBg(r);
}

// A menu choice moved up or down, wrapping within count items.
static uint16_t step_choice(uint16_t pressed, uint16_t current, uint16_t count) {
  if(pressed & 0x0800) return current == 0 ? (uint16_t) (count - 1) : (uint16_t) (current - 1);
  return current + 1 >= count ? 0 : (uint16_t) (current + 1);
}

// $0F:943D: the on-screen keyboard for the name or password at $68 (cursor $5E, up to 8
// letters), on the pad of player $81. The cursor ($48: row, column) moves over rows
// $0B-$13; row $13 holds space ($1310), back ($130C) and, from column $14, END ($62 keeps
// the column the cursor came from). B, Y or Select also go back a letter.
static void MenuTextEntry(TgRegs* r) {
  for(;;) {
    uint16_t pressed = wait_player_press(r, 0xFFF0);
    bool back = (pressed & 0x6040) != 0;
    uint16_t cell = ram16(0x48);
    if(!back) {
      uint16_t a = player_pad(r) & 0xDCF0;
      if(a != 0 && !(a & 0x0C00)) {
        // A letter, or the special keys.
        if(cell >= 0x1314) {
          while(player_pad(r) == 0) tg_port_wait();
          return;
        }
        if(cell == 0x130C) {
          back = true;
        } else {
          if(ram16(0x5E) != 8) {
            uint16_t letter = 0x0020;
            if(cell != 0x1310) {
              uint16_t t = (uint16_t) (cell - 0x0B04);
              ram16_set(0x60, (uint16_t) ((t & 0x00FF) >> 2));
              r->y = t >> 8;
              letter = (uint16_t) (rom16(0x0F95A2 + r->y) + ram16(0x60) + 0x41);
              ram16_set(0x60, letter);
            }
            r->y = ram16(0x5E);
            g_ram[(uint16_t) (0x68 + r->y)] = (uint8_t) letter;
            r->a = cell;
            MenuDrawStringAndQueue(r);
            ram16_set(0x5E, (uint16_t) (ram16(0x5E) + 1));
            MenuPlaceEntryCursor(r);
            queue_screen(r);
          }
          ram16_set(0x48, cell);
          wait_player_release(r);
          continue;
        }
      }
      if(!back) {
        if(a == 0) {
          // Left and right, wrapping between columns 4 and $14.
          uint16_t swapped = (uint16_t) (cell << 8 | cell >> 8);
          if(player_pad(r) & 0x0200) {
            swapped = (uint16_t) (swapped - 0x0400);
            if(swapped < 0x0400) swapped = (uint16_t) ((swapped & 0x00FF) | 0x1800);
          } else {
            swapped = (uint16_t) (swapped + 0x0400);
            if(swapped >= 0x1813) swapped = (uint16_t) ((swapped & 0x00FF) | 0x0400);
          }
          cell = (uint16_t) (swapped << 8 | swapped >> 8);
        } else if(a & 0x0800) {
          cell = (uint16_t) (cell - 0x0200);
          if(cell == 0x1114) cell = (uint16_t) ((ram16(0x62) & 0x00FF) | 0x1100);
          else if(cell < 0x0B00) cell = (uint16_t) (((cell < 0x0B14 ? cell : ram16(0x62)) & 0x00FF) | 0x1300);
        } else {
          cell = (uint16_t) (cell + 0x0200);
          if(cell >= 0x1500) cell = (uint16_t) (((cell < 0x1514 ? cell : ram16(0x62)) & 0x00FF) | 0x0B00);
        }
        ram16_set(0x48, cell);
        if(cell >= 0x1314) {
          ram16_set(0x62, cell);
          cursor_box(r, 0x1314, 3, 8);
        } else {
          cursor_box(r, cell, 2, 8);
        }
        queue_screen(r);
        ram16_set(0x4A, 1);
        wait_player_release(r);
        continue;
      }
    }
    // Back a letter.
    uint16_t at = ram16(0x5E);
    if(at != 0) at--;
    at &= 7;
    ram16_set(0x5E, at);
    r->y = at;
    ram16_set((uint16_t) (0x68 + at), 0x2020);
    r->a = cell;
    MenuDrawStringAndQueue(r);
    MenuPlaceEntryCursor(r);
    queue_screen(r);
    ram16_set(0x48, cell);
    wait_player_release(r);
  }
}

// $0F:95B6: in a two-player game where the players are named as the programmers
// ($0D:C41B, $0D:C423), their credits screen.
static void MenuSecretCredits(TgRegs* r) {
  if(ram16(0x1F04) == 0) return;
  for(int x = 6; x >= 0; x -= 2) {
    if(ram16((uint16_t) (0x1F86 + x)) != rom16(0x0DC41B + (uint32_t) x)) return;
    if(ram16((uint16_t) (0x1FA3 + x)) != rom16(0x0DC423 + (uint32_t) x)) return;
  }
  load(MenuDecompressToRam, r, 0x800F, 0xB4FE, 0x0000);
  tg_bus_write16(0x2116, 0x0C00);
  for(uint16_t x = 0; x <= 0x0800; x += 2) tg_bus_write16(0x2118, ram16(0x2400 + (uint32_t) x));
  MenuClearOam(r);
  MenuClearFadeRows(r);
  MenuLoadBasePalette(r);
  MenuLoadPaletteRow1(r);
  tg_call_jsr(r, 0x8C77, 0x9608, TG_MX_16);  // palette row 10
  tg_call_jsr(r, 0x8FC3, 0x960B, TG_MX_16);  // the credits' sprites
  ram16_set(0x022E, 0xF0F0);
  ram16_set(0x0440, 0x2020);
  ram16_set(0x042C, 0x2020);
  ram16_set(0x042E, 0x2020);
  r->x = 0xB031;  // "PROGRAMMED BY"...
  MenuDrawText(r);
  queue_screen(r);
  MenuWaitFrames(r, 1);
  MenuFadeIn(r);
  MenuWaitButton(r, 0x0200);
  MenuFadeOut(r);
  ram16_set(0x42, 0x008F);
  MenuWaitFrames(r, 1);
  load(MenuDecompressToVram, r, 0x800D, 0xA27C, 0x0C00);
}

// Screen off for the next screen's loading.
static void blank(TgRegs* r) {
  ram16_set(0x42, 0x008F);
  MenuWaitFrames(r, 1);
}

// $0F:9781: player $81's setup: name, gears ($87: 1 automatic), control method (the
// buttons at $1F76 / $1F93 from $0D:C656) and car ($1F8F / $1FAC).
static void MenuPlayerSetup(TgRegs* r) {
  uint16_t player = ram16(0x81);
  draw_screen(r, 3);
  if(ram16(0x1F04) != 0) {
    r->x = player == 0 ? 0xB4BA : 0xB4C7;  // "PLAYER 1" / "PLAYER 2"
    MenuDrawBigText(r);
  }
  MenuNameEntryStart(r);
  r->x = 0x0066;
  MenuDrawBigText(r);
  cursor_box(r, rom16(0x0FAA33), 2, 8);
  MenuPlaceEntryCursor(r);
  queue_screen(r);
  MenuFadeIn(r);
  MenuTextEntry(r);
  MenuStorePlayerName(r);
  fade_out_and_clear(r);
  blank(r);
  MenuSecretCredits(r);

  // Gears.
  ram16_set(0x44, 0x0007);
  load(MenuDecompressToVram, r, 0x800F, 0xF0F5, 0x2200);
  MenuClearPalette(r);
  MenuClearBg(r);
  MenuSetPaletteDma(r);
  ram16_set(0x0424, 0x0002);
  MenuWaitFrames(r, 1);
  MenuLoadBasePalette(r);
  MenuLoadPaletteRow1(r);
  MenuSetFadeRowsIntro(r);
  draw_screen(r, 4);
  MenuDrawNameHeading(r);
  r->x = 0x0066;
  MenuDrawBigText(r);
  uint16_t shift = player == 0 ? (uint16_t) (ram16(0x1F7A) | ram16(0x1F7C)) : (uint16_t) (ram16(0x1F97) | ram16(0x1F99));
  ram16_set(0x5A, shift != 0 ? 0 : 1);  // shift buttons set: manual
  r->y = (uint16_t) (ram16(0x5A) << 1);
  cursor_box(r, rom16(0x0FA85C + r->y), rom16(0x0FA860 + r->y), 4);
  queue_screen(r);
  MenuFadeIn(r);
  for(;;) {
    uint16_t pressed = wait_player_press(r, 0xD3F0);
    if(!(pressed & 0x0300)) break;
    ram16_set(0x5A, ram16(0x5A) ^ 1);
    r->y = (uint16_t) (ram16(0x5A) << 1);
    cursor_box(r, rom16(0x0FA85C + r->y), rom16(0x0FA860 + r->y), 4);
    queue_screen(r);
    wait_player_release(r);
  }
  ram16_set(0x87, ram16(0x5A));
  wait_player_release(r);
  fade_out_and_clear(r);
  blank(r);

  // Control method: the current one found among the four, or the first.
  ram16_set(0x5A, 0);
  uint16_t buttons = player == 0 ? 0x1F76 : 0x1F93;
  for(uint16_t x = 0;; x += 0x10) {
    if(x == 0x40) {
      ram16_set(0x5A, 0);
      break;
    }
    if(rom16(0x0DC656 + x) == ram16(buttons) && rom16(0x0DC658 + x) == ram16((uint16_t) (buttons + 2))) break;
    ram16_set(0x5A, (uint16_t) (ram16(0x5A) + 1));
  }
  ram16_set(0x44, 0x0003);
  load(MenuDecompressToVram, r, 0x800F, 0xDA98, 0x2200);
  load(MenuDecompressToRam, r, 0x800E, 0xF03F, 0x0000);
  MenuDrawControlPad(r);
  draw_screen(r, rom16(0x0DC632 + (uint32_t) (ram16(0x5A) << 1)));
  ram16_set(0x46, 0x0005);
  MenuDrawBoxes(r);
  MenuDrawScreenBigText(r);
  MenuDrawNameHeading(r);
  r->x = 0x0066;
  MenuDrawBigText(r);
  cursor_box(r, rom16(0x0FB112 + (uint32_t) (ram16(0x5A) << 1)), 2, 4);
  queue_screen(r);
  MenuFadeIn(r);
  for(;;) {
    uint16_t pressed = wait_player_press(r, 0xD3F0);
    if(!(pressed & 0x0300)) break;
    uint16_t method = (uint16_t) ((pressed & 0x0200 ? ram16(0x5A) - 1 : ram16(0x5A) + 1) & 3);
    ram16_set(0x5A, method);
    uint16_t y = (uint16_t) (method << 1);
    cursor_box(r, rom16(0x0FB112 + y), 2, 4);
    MenuDrawControlPad(r);
    r->x = 0x0066;
    MenuDrawBigText(r);
    draw_screen(r, rom16(0x0DC632 + y));
    ram16_set(0x46, 0x0005);
    MenuDrawBoxes(r);
    MenuDrawScreenBigText(r);
    queue_screen(r);
    wait_player_release(r);
  }
  wait_player_release(r);
  uint16_t y = player == 0 ? 0 : 0x1D;
  for(uint16_t x = (uint16_t) ((ram16(0x5A) & 3) << 4);;) {
    ram16_set((uint16_t) (0x1F76 + y), rom16(0x0DC656 + x));
    x += 2;
    y += 2;
    if(y == 0x10 || y == 0x2D) break;
  }
  if(ram16(0x87) != 0) {
    // Automatic: no shift buttons.
    ram16_set((uint16_t) (0x1F6A + y), 0);
    ram16_set((uint16_t) (0x1F6C + y), 0);
  }

  // Car.
  ram16_set(0x5A, player == 0 ? ram16(0x1F8F) : ram16(0x1FAC));
  fade_out_and_clear(r);
  blank(r);
  load(MenuDecompressToVram, r, 0x800F, 0xBF90, 0x2200);
  ram16_set(0x44, 0x0008);
  draw_screen(r, 6);
  MenuDrawNameHeading(r);
  r->x = 0x0066;
  MenuDrawBigText(r);
  ram16_set(0x44, 0x0008);
  draw_screen(r, 6);
  ram16_set(0x44, 0x0008);
  draw_screen(r, rom16(0x0DC63A + (uint32_t) (ram16(0x5A) << 1)));
  MenuDrawCarStats(r);
  r->y = (uint16_t) (ram16(0x5A) << 1);
  cursor_box(r, rom16(0x0FA948 + r->y), rom16(0x0FA950 + r->y), 4);
  queue_screen(r);
  MenuFadeIn(r);
  for(;;) {
    uint16_t pressed = wait_player_press(r, 0xDFF0);
    if(!(pressed & 0x0F00)) break;
    ram16_set(0x5A, ram16(0x5A) ^ (pressed & 0x0300 ? 1 : 2));  // a 2 x 2 grid
    r->y = (uint16_t) (ram16(0x5A) << 1);
    cursor_box(r, rom16(0x0FA948 + r->y), rom16(0x0FA950 + r->y), 4);
    MenuQueueOamAndMap(r);
    ram16_set(0x44, 0x0008);
    draw_screen(r, rom16(0x0DC63A + (uint32_t) (ram16(0x5A) << 1)));
    MenuDrawCarStats(r);
    ram16_set(0x0424, 0x0084);
    wait_player_release(r);
  }
  ram16_set(player == 0 ? 0x1F8F : 0x1FAC, ram16(0x5A));
  wait_player_release(r);
  fade_out_and_clear(r);
}

// $0F:96DE: one or both players' setup. With two players a chooser comes first
// ($0F:96F2: player 1, player 2 or back), until back is picked.
static void MenuPlayersSetup(TgRegs* r) {
  ram16_set(0x5A, 0);
  ram16_set(0x81, 0);
  if(ram16(0x1F04) == 0) {
    MenuPlayerSetup(r);
  } else {
    for(;;) {
      ram16_set(0x5A, 0);
      draw_screen(r, 0x10);
      cursor_box(r, rom16(0x0FAC30), rom16(0x0FA721), 4);
      queue_screen(r);
      MenuFadeIn(r);
      for(;;) {
        MenuWaitFrames(r, 1);
        uint16_t pressed = (ram16(0x0A) | ram16(0x0C)) & 0xFCF0;
        if(!pressed) continue;
        if(!(pressed & 0x0C00)) break;
        ram16_set(0x5A, step_choice(pressed, ram16(0x5A), 3));
        r->y = (uint16_t) (ram16(0x5A) << 1);
        cursor_box(r, rom16(0x0FAC30 + r->y), rom16(0x0FAC36 + r->y), 4);
        queue_screen(r);
        r->y = ram16(0x81);
        while(pads_held()) tg_port_wait();
      }
      fade_out_and_clear(r);
      if(ram16(0x5A) == 2) break;
      ram16_set(0x81, ram16(0x5A));
      MenuPlayerSetup(r);
      if(ram16(0x1F04) == 0) break;
    }
  }
  ram16_set(0x81, 0);
  ram16_set(0x5A, 0);
}

// $0F:9652: the skill level ($1F10); the course becomes its highest open country's first.
static void MenuSkillLevel(TgRegs* r) {
  draw_screen(r, 2);
  ram16_set(0x5A, ram16(0x1F10));
  r->y = (uint16_t) (ram16(0x5A) << 1);
  cursor_box(r, rom16(0x0FA76B + r->y), rom16(0x0FA771 + r->y), 4);
  queue_screen(r);
  MenuFadeIn(r);
  for(;;) {
    uint16_t pressed;
    while(!(pressed = ram16(0x0A) & 0xDCF0)) tg_port_wait();
    if(!(pressed & 0x0C00)) break;
    ram16_set(0x5A, step_choice(pressed, ram16(0x5A), 3));
    r->y = (uint16_t) (ram16(0x5A) << 1);
    cursor_box(r, rom16(0x0FA76B + r->y), rom16(0x0FA771 + r->y), 4);
    queue_screen(r);
    while(ram16(0x0A) != 0) tg_port_wait();
  }
  uint16_t level = ram16(0x5A);
  ram16_set(0x1F10, level);
  ram16_set(0x5A, 0);
  ram16_set(CarTypeIndex, (uint16_t) (ram16((uint16_t) (0x1F0A + (level << 1))) << 2));
  fade_out_and_clear(r);
}

// $0F:929E: the country: an open one ($1F0A per skill level) becomes the course; a
// locked one asks for its password.
static void MenuCountrySelect(TgRegs* r) {
  uint16_t open = (uint16_t) (0x1F0A + (ram16(0x1F10) << 1));
  uint16_t country = ram16(CarTypeIndex) >> 2;
  if(country >= ram16(open)) ram16_set(open, country);
  ram16_set(0x5A, ram16(open));
  for(;;) {
    draw_screen(r, 7);
    open = (uint16_t) (0x1F0A + (ram16(0x1F10) << 1));
    r->y = (uint16_t) (ram16(open) << 1);
    cursor_box(r, rom16(0x0FA9B4 + r->y), rom16(0x0FA9C4 + r->y), 0x24);
    MenuDrawFlagSprites(r);
    queue_screen(r);
    MenuFadeIn(r);
    for(;;) {
      uint16_t pressed;
      while(!(pressed = ram16(0x0A) & 0xDCF0)) tg_port_wait();
      if(!(pressed & 0x0C00)) break;
      ram16_set(0x5A, step_choice(pressed, ram16(0x5A), 8));
      r->y = (uint16_t) (ram16(0x5A) << 1);
      cursor_box(r, rom16(0x0FA9B4 + r->y), rom16(0x0FA9C4 + r->y), 0x24);
      queue_screen(r);
      while(ram16(0x0A) != 0) tg_port_wait();
    }
    if(ram16(0x5A) <= ram16((uint16_t) (0x1F0A + (ram16(0x1F10) << 1)))) {
      ram16_set(CarTypeIndex, (uint16_t) (ram16(0x5A) << 2));
      fade_out_and_clear(r);
      return;
    }
    // Locked: the password.
    ram16_set(0x5C, ram16(0x5A));
    fade_out_and_clear(r);
    ram16_set(0x5A, 0);
    draw_screen(r, 8);
    for(int k = 0; k < 8; k += 2) ram16_set((uint16_t) (0x68 + k), 0x2020);
    ram16_set(0x66, 0x0708);
    r->x = 0x0066;
    ram16_set(0x5E, 0);
    MenuDrawBigText(r);
    cursor_box(r, rom16(0x0FAA33), 2, 8);
    MenuPlaceEntryCursor(r);
    queue_screen(r);
    MenuFadeIn(r);
    MenuTextEntry(r);
    fade_out_and_clear(r);
    MenuCheckPasswords(r);
  }
}

// $0F:918B: the main menu: players (1 or 2, with their setup), skill level, country,
// speed units (MPH / KPH) and PLAY GAME. Returns when the game starts; in a one-player
// game the rival's car ($1FAC) is picked from the frame counter, not the player's.
static void MenuMain(TgRegs* r) {
  for(;;) {
    ram16_set(0x1F00, 0);
    ram16_set(0x1F02, 1);
    draw_screen(r, 0);
    draw_screen(r, ram16(0x1F04));
    ram16_set(0x5A, 0);
    r->y = ram16(0x1FB0);
    if(r->y != 0) {
      r->x = 0xA709;  // "KPH"
      MenuDrawBigText(r);
      r->y = 0;
    }
    cursor_box(r, rom16(0x0FA717 + r->y), (uint16_t) (rom16(0x0FA721 + r->y) + ram16(0x1F04)), 4);
    queue_screen(r);
    MenuFadeIn(r);
    for(;;) {
      MenuWaitFrames(r, 1);
      uint16_t pressed = ram16(0x0A) & 0xFFF0;
      if(!pressed) continue;
      uint16_t item;
      if(pressed & 0x0C00) {
        item = step_choice(pressed, ram16(0x5A), 5);
      } else if(ram16(0x0A) & 0x0300) {
        if(ram16(0x5A) == 0) {
          // Left or right on the first item: one or two players.
          ram16_set(0x1F04, ram16(0x1F04) ^ 1);
          ram16_set(0x46, ram16(0x1F04));
          MenuDrawScreenBigText(r);
          item = ram16(0x5A);
        } else {
          item = 0xFFFF;
        }
      } else if(ram16(0x5A) == 3) {
        ram16_set(0x1FB0, ram16(0x1FB0) ^ 0xFFFF);
        item = 3;
      } else {
        break;
      }
      if(item != 0xFFFF) {
        ram16_set(0x5A, item);
        r->y = (uint16_t) (item << 1);
        uint16_t size = rom16(0x0FA721 + r->y);
        if(item == 0) size = (uint16_t) (size + ram16(0x1F04));
        cursor_box(r, rom16(0x0FA717 + r->y), size, 4);
        r->x = ram16(0x1FB0) != 0 ? 0xA709 : 0xA6EB;  // "KPH" / "MPH"
        MenuDrawBigText(r);
        queue_screen(r);
      }
      while(ram16(0x0A) != 0) tg_port_wait();
      ram16_set(WarmBootFlag, 0x04B0);
    }
    fade_out_and_clear(r);
    ram16_set(WarmBootFlag, 0);
    uint16_t item = ram16(0x5A);
    if(item == 0) {
      MenuPlayersSetup(r);
    } else if(item == 1) {
      MenuSkillLevel(r);
    } else if(item == 2) {
      MenuCountrySelect(r);
    } else {
      if(ram16(0x1F04) == 0) {
        uint16_t car;
        while((car = ram16(0x1FFC) & 3) == ram16(0x1F8F)) tg_port_wait();
        ram16_set(0x1FAC, car);
      }
      return;
    }
  }
}

static uint16_t name_words(uint16_t name) {
  return (uint16_t) (ram16(name) | ram16((uint16_t) (name + 2)) | ram16((uint16_t) (name + 4)) | ram16((uint16_t) (name + 6)));
}

// $0F:818E: the course preview ("LOCATION", "STATISTICS", the map, length and lap
// record), held until a button unless in the demo, then the race's video set-up.
static void MenuRaceIntro(TgRegs* r) {
  MenuSetFadeRowsIntro(r);
  MenuClearOam(r);
  ram16_set(0x46, 0x000F);
  MenuDrawBoxes(r);
  MenuDrawScreenText(r);
  MenuLoadCourseMap(r);
  tg_bus_write8(0x2101, 0x00);
  r->x = rom16(0x0DC77C + (uint32_t) (ram16(CarTypeIndex) << 1));  // the course's name
  MenuDrawTextSprites(r);
  MenuDrawCourseLength(r);
  MenuDrawLapRecord(r);
  MenuQueueOamAndMap(r);
  ram16_set(0x0424, 0x0084);
  MenuLoadBasePalette(r);
  MenuLoadPaletteRow1(r);
  ram16_set(0x054E, 0x2108);
  ram16_set(0x0550, 0x0018);
  ram16_set(0x0552, 0x7FFF);
  if(ram16(WarmBootFlag) == 0) {
    MenuFadeIn(r);
    MenuWaitButton(r, 0x0200);
    MenuFadeOut(r);
  }
  MenuClearBg(r);
  g_ram[0x42] = 0x8F;
  tg_bus_write8(0x4200, 0x01);
  tg_bus_write8(0x2100, 0x80);
  tg_bus_write8(0x2101, 0x68);
  MenuClearVideo(r);
  MenuUploadHudMap(r);
}

// $0F:815A: the title and menus, default names for blank ones, then the first race's
// preview.
static void MenuFromTitle(TgRegs* r) {
  MenuInitVideo(r);
  ram16_set(0x81, 0);
  MenuTitle(r);
  ram16_set(0x81, 0);
  r->a = name_words(0x1F86);
  if(r->a == 0x2020) MenuDefaultPlayerName(r);
  ram16_set(0x81, (uint16_t) (ram16(0x81) + 1));
  r->a = name_words(0x1FA3);
  if(r->a == 0x2020) MenuDefaultPlayerName(r);
}

static int MenuEndScreen(TgRegs* r, uint16_t kind);

// $0F:83D7: after a race: the results and the championship standings, then the next
// race's preview, or the end screen when a player failed to qualify or a country is
// done. Returns where to go next: 0 the next race's preview, 1 the title.
static int MenuAfterRace(TgRegs* r) {
  MenuInitVideo(r);
  MenuClearOam(r);
  if(ram16(WarmBootFlag) != 0) {
    ram16_set(CarTypeIndex, ram16(0x1F08));
    return 1;
  }
  r->y = (uint16_t) (ram16(0x1F10) << 1);
  uint16_t country = (uint16_t) ((ram16(CarTypeIndex) & 0x001C) >> 2);
  uint16_t open = (uint16_t) (0x1F0A + r->y);
  if(country >= ram16(open)) ram16_set(open, country);
  r->a = 0;
  ram16_set(0x0424, 0);
  ram16_set(0x44, 0);
  tg_bus_write8(0x2107, 0x02);
  tg_bus_write8(0x4200, 0x81);
  MenuLoadMenuScreen(r);
  r->x = 0xB4A1;  // "RESULTS"
  MenuDrawBigText(r);
  MenuMarkNameEnds(r);
  MenuDrawRaceResults(r);
  MenuQueueOamAndMap(r);
  tg_bus_write16(0x4325, 0x1000);
  ram16_set(0x42, 0x000F);
  ram16_set(0x0424, 0x0084);
  MenuFadeIn(r);
  r->x = 0xB48F;  // "WORLD RANKING"
  MenuDrawBigText(r);
  MenuDrawStandings(r);
  MenuQueueOamAndMap(r);
  ram16_set(0x0424, 0x0084);
  MenuWaitFrames(r, 1);
  MenuWaitButton(r, 0x0200);
  MenuUnmarkNameEnds(r);
  // Scrolls the standings' second half up, 3 lines a frame.
  uint16_t scroll = 0;
  do {
    ram16_set(0x044C, scroll);
    MenuWaitFrames(r, 1);
    tg_bus_write8(0x210E, g_ram[0x044C]);
    tg_bus_write8(0x210E, g_ram[0x044D]);
    scroll = (uint16_t) (ram16(0x044C) + 3);
  } while(scroll < 0x0101);
  MenuWaitButton(r, 0x0200);
  MenuFadeOut(r);
  tg_bus_write8(0x210E, 0x10);
  tg_bus_write8(0x210E, 0x00);
  MenuClearBg(r);
  ram16_set(0x1FF8, (uint16_t) (ram16(0x1F00) + 1));
  ram16_set(0x1FFA, (uint16_t) (ram16(0x1F02) + 1));
  MenuCheckQualified(r);
  uint16_t kind;
  if(r->c) {
    kind = 1;  // failed to qualify: the points go
  } else {
    MenuRecordPlayerPlaces(r);
    MenuAdvanceCourse(r);
    if(r->c) {
      kind = 2;
    } else if(r->z) {
      r->x = ram16(CarTypeIndex);
      return MenuEndScreen(r, r->x != 0 ? r->a : 3);  // a country done, or all of them
    } else {
      tg_bus_write8(0x210E, 0);
      tg_bus_write8(0x210E, 0);
      return 0;
    }
  }
  for(int x = 0x12; x >= 0; x -= 2) ram16_set((uint16_t) (0x1F4E + x), 0);
  ram16_set(0x1F00, 0);
  ram16_set(0x1F02, 1);
  r->x = 0xFFFE;
  return MenuEndScreen(r, kind);
}

// $0F:84F5: the end of a country: kind 0 qualified for the next (with its password),
// 1 failed to qualify, 2 qualified but behind the computer drivers (guess: unrun), 3 the
// end of the game. Returns as MenuAfterRace does; after anything but 0 the title
// follows, through MenuAfterRace again with $1FF6 set.
static int MenuEndScreen(TgRegs* r, uint16_t kind) {
  ram16_set(0x42, 0x008F);
  MenuWaitFrames(r, 1);
  load(MenuDecompressToVram, r, 0x800D, 0xAE51, 0x2200);
  load(MenuDecompressToRam, r, 0x800D, 0xC24B, 0x0000);
  load(MenuDecompressToVram, r, 0x800F, 0xBBA7, 0x1000);
  MenuClearScreen(r);
  MenuClearPalette(r);
  MenuSetPaletteDma(r);
  ram16_set(0x0424, 0x0002);
  MenuWaitFrames(r, 1);
  MenuClearFadeRows(r);
  ram16_set(0x042C, 0x2020);
  ram16_set(0x044A, 0x2020);
  ram16_set(0x0430, 0x2020);
  ram16_set(0x0432, 0x2020);
  r->a = kind;
  MenuDrawCarPortraits(r);
  bool places = true;
  if(kind == 0) {
    r->x = 0xB1C7;
    MenuDrawText(r);
    r->a = 5;
    MenuStartMusic(r);
  } else if(kind < 3) {
    r->x = kind == 1 ? 0xB23B : 0xB202;
    MenuDrawText(r);
  } else {
    r->x = 0xB271;
    MenuDrawText(r);
    r->a = 6;
    MenuStartMusic(r);
    r->a = ram16(0x1F10);
    if(r->a != 2) {
      r->x = r->a == 0 ? 0xB29E : 0xB2CA;
      MenuDrawText(r);
    }
    places = false;
  }
  if(places) {
    tg_bus_write8(0x2101, 0);
    MenuClearOam(r);
    MenuLoadPaletteRow15(r);
    ram16_set(0x75, 0);
    ram16_set(0x77, 0);
    ram16_set(0x64, 0x0511);
    bool done = false;
    if(ram16(0x1F04) != 0) {
      if(ram16(0x1FF8) >= ram16(0x1FFA)) {
        ram16_set(0x64, 0x0611);
        r->a = 0;
        MenuDrawPlayerPlace(r);
        ram16_set(0x64, 0x0411);
        r->a = 1;
        MenuDrawPlayerPlace(r);
        done = true;
      } else {
        ram16_set(0x64, 0x0611);
        r->a = 1;
        MenuDrawPlayerPlace(r);
        ram16_set(0x64, 0x0411);
      }
    }
    if(!done) {
      r->a = 0;
      MenuDrawPlayerPlace(r);
    }
    ram16_set(0x70, 0);
    ram16_set(0x72, 0);
  }
  r->a = ram16(CarTypeIndex) & 0x001C;
  if(r->a != 0) {
    // The password for the country reached.
    uint16_t offset = (uint16_t) ((r->a - 4) << 1);
    offset = (uint16_t) (offset + 0x38 * ram16(0x1F10));
    for(int k = 0; k < 8; k += 2) ram16_set((uint16_t) (0x68 + k), rom16(0x0DC513 + (uint32_t) (offset + k)));
    ram16_set(0x70, 0);
    ram16_set(0x72, 0);
    ram16_set(0x66, 0x1C10);
    r->x = 0x0066;
    MenuDrawText(r);
    r->x = 0xB482;
    MenuDrawText(r);
  }
  MenuQueueOamAndMap(r);
  ram16_set(0x0424, 0x0084);
  MenuWaitFrames(r, 1);
  MenuFadeIn(r);
  MenuWaitButton(r, 0x0600);
  MenuFadeOut(r);
  tg_bus_write8(0x210E, 0);
  tg_bus_write8(0x210E, 0);
  MenuClearTilemapBuffer(r);
  MenuSetFadeRowsIntro(r);
  MenuLoadBasePalette(r);
  MenuLoadPaletteRow1(r);
  if(kind == 0) return 0;
  ram16_set(WarmBootFlag, kind);
  r->a = 0;
  MenuStartMusic(r);
  r->y = (uint16_t) (ram16(0x1F10) << 1);
  uint16_t country = ram16(CarTypeIndex) & 0x001C;
  ram16_set(0x1F08, country);
  country >>= 2;
  uint16_t open = (uint16_t) (0x1F0A + r->y);
  if(country >= ram16(open)) ram16_set(open, country);
  return MenuAfterRace(r);
}

// $0F:806D: the menus, from power-on (A = 0) or after a race (A = 1), until the next
// race is set up. Installs the NMI trampoline (JML $0F:8000) and an RTI for the IRQ.
void GameInit(TgRegs* r) {
  uint16_t entry = r->a;
  r->db = 0x0F;
  tg_bus_write8(0x420B, 0);
  tg_bus_write8(0x420C, 0);
  ram16_set(0x78, 0);
  ram16_set(0x7A, 0);
  ram16_set(0x70, 0);
  ram16_set(0x72, 0);
  if((name_words(0x1F86) | name_words(0x1FA3)) == 0) ResetNamesAndRecords(r);
  ram16_set(0x01, 0x8000);
  g_ram[0x00] = 0x5C;
  g_ram[0x03] = 0x0F;
  g_ram[0x04] = 0x40;
  if(entry == 0 || MenuAfterRace(r) == 1) MenuFromTitle(r);
  MenuRaceIntro(r);
}
