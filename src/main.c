// Windows (and desktop) frontend: window, fullscreen, scaling, keyboard, gamepads, audio.
//
// Every session is recorded to recordings/ next to the executable: the inputs (which
// replay exactly, for regression tests) and which parts of the ROM ran (for the
// disassembly). Command line: topgear [rom] [--no-record] [--replay file.txt]

#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "coverage.h"
#include "game.h"
#include "input_script.h"
#include "port/port.h"
#include "rom.h"
#include "sha256.h"

#define SNES_W 256
#define SNES_H 224
// NTSC SNES: 21.477272 MHz master clock / (1364 * 262 clocks per frame).
#define FRAME_NS (1000000000.0 * 1364.0 * 262.0 / 21477272.0)
#define AUDIO_RATE 48000

typedef enum { ASPECT_4_3, ASPECT_8_7, ASPECT_COUNT } AspectMode;
static const char* aspectNames[] = {"4:3 (TV)", "8:7 (square pixels)"};
// Display filters (F4): sharp pixels, sharp-bilinear (pixels scaled up by a whole number,
// then smoothly to the window, so they stay crisp without uneven widths), or scanlines.
typedef enum { FILTER_SHARP, FILTER_SMOOTH, FILTER_SCANLINES, FILTER_COUNT } FilterMode;
static const char* filterNames[] = {"sharp", "sharp-bilinear", "scanlines"};

static struct {
  SDL_Window* window;
  SDL_Renderer* renderer;
  SDL_Texture* texture;
  SDL_AudioStream* audio;
  SDL_Gamepad* pads[2];
  TgGame* game;
  bool fullscreen;
  bool integerScale;
  AspectMode aspect;
  FilterMode filter;
  bool rumble;  // controller rumble in races (F8, --rumble)
  SDL_Texture* upscaled;  // sharp-bilinear's intermediate picture
  int upscaledFactor, upscaledW;
  SDL_Texture* wideTexture;  // the widescreen picture, wideTextureW x 224
  int wideTextureW;
  uint16_t keys[3];  // keyboard state per player
  uint32_t frame;  // frames run since power-on
  // recording
  FILE* record;
  uint16_t recorded[3];
  bool resetRequested;
  TgCoverage* coverage;
  char coveragePath[2400];
  // replay
  TgInputScript replay;
  bool replaying;
  bool original;  // the game's own code throughout (--original)
  bool smooth;  // enhancement: race logic every frame instead of every third (F6, --smooth)
  bool recordedSmooth;
  bool highFps;  // enhancement: in-between frames at the display's rate (Shift+F6, --high-fps)
  bool vsync;
  bool scaling;  // enhancement: race objects grow smoothly (F7, --smooth-scaling)
  bool recordedScaling;
  bool fullView;  // enhancement: 1-player races fill the whole screen (F9, --full-view)
  bool recordedFullView;
  bool tallRoad;  // full view with the road twice as tall, filling the screen (F9 again, --tall-road)
  bool recordedTallRoad;
  bool wide;  // enhancement: races drawn as wide as the window (F10, --wide)
  bool sideBySide;  // enhancement: 2-player widescreen views side by side (F12, --side-by-side)
  bool recordedSideBySide;
  bool ghost;  // enhancement: race against your best race on each course (F1, --ghost)
  bool recordedGhost;
  char ghostPath[2400];  // where the best races are kept; empty when there is nowhere
  bool autoSave;  // enhancement: countries reached are remembered between sessions (Shift+F1, --auto-save)
  uint8_t saved[3];  // the countries open at each skill level, as saved
  char progressPath[2400];
  char settingsPath[2400];  // the options, remembered between sessions
  int recordedWide;
} app;

static const char* defaultRomNames[] = {"Top Gear (USA).sfc", "Top Gear (USA).smc", "topgear.sfc"};

static bool findRom(const char* romArg, TgRom* rom, char* message, size_t messageSize) {
  if(romArg) {
    TgRomStatus status = tg_rom_load(romArg, rom);
    if(status == TG_ROM_OK) return true;
    snprintf(message, messageSize, "%s\n\n%s\n\nSHA-256 of your file:\n%s", romArg, tg_rom_status_message(status), rom->sha256);
    return false;
  }
  const char* base = SDL_GetBasePath();
  for(size_t i = 0; i < SDL_arraysize(defaultRomNames); i++) {
    char path[2048];
    snprintf(path, sizeof(path), "%s%s", base ? base : "", defaultRomNames[i]);
    TgRomStatus status = tg_rom_load(path, rom);
    if(status == TG_ROM_OK) return true;
    if(status != TG_ROM_NOT_FOUND) {
      snprintf(message, messageSize, "%s\n\n%s\n\nSHA-256 of your file:\n%s", path, tg_rom_status_message(status), rom->sha256);
      return false;
    }
  }
  snprintf(message, messageSize,
    "Top Gear needs your own copy of the game.\n\n"
    "Put your Top Gear (USA) ROM next to topgear.exe and name it\n"
    "\"Top Gear (USA).sfc\", or drag the ROM file onto topgear.exe.\n\n"
    "Expected SHA-256:\n" TG_ROM_SHA256);
  return false;
}

// Controls: controls.txt next to topgear.exe maps keys and gamepad buttons to SNES
// buttons (written with the defaults when missing).
static const char* kButtonNames[12] = {"B", "Y", "SELECT", "START", "UP", "DOWN", "LEFT", "RIGHT", "A", "X", "L", "R"};

typedef struct Binding {
  bool pad;
  int code;  // SDL_Keycode, or SDL_GamepadButton
  uint16_t button;
  int player;  // keys: 1 or 2 (gamepads: each pad is its own player)
} Binding;

static Binding bindings[96];
static int bindingCount;

static const char* kDefaultControls =
  "# Top Gear controls: one binding per line, <SNES button> key <key name> or\n"
  "# <SNES button> pad <gamepad button>. A button can have several bindings.\n"
  "# SNES buttons: B Y SELECT START UP DOWN LEFT RIGHT A X L R\n"
  "# Key names as SDL writes them (Z, Return, Right Shift, Keypad Enter, Space, ...).\n"
  "# Gamepad buttons by position: a (bottom) b (right) x (left) y (top) back start\n"
  "# leftshoulder rightshoulder dpup dpdown dpleft dpright. The left stick steers and the\n"
  "# triggers work as L and R. Start a line with P2 for player 2's keys (P2 B key N).\n"
  "# Delete this file to get the defaults back.\n"
  "UP key Up\nDOWN key Down\nLEFT key Left\nRIGHT key Right\n"
  "B key Z\nA key X\nY key A\nX key S\nL key Q\nR key W\n"
  "START key Return\nSTART key Keypad Enter\nSELECT key Right Shift\n"
  "B pad a\nA pad b\nY pad x\nX pad y\nL pad leftshoulder\nR pad rightshoulder\n"
  "START pad start\nSELECT pad back\nUP pad dpup\nDOWN pad dpdown\nLEFT pad dpleft\nRIGHT pad dpright\n";

static void parseControls(const char* text) {
  bindingCount = 0;
  while(*text) {
    const char* end = strchr(text, '\n');
    size_t n = end ? (size_t) (end - text) : strlen(text);
    char line[128];
    if(n >= sizeof(line)) n = sizeof(line) - 1;
    memcpy(line, text, n);
    line[n] = 0;
    text += end ? n + 1 : n;
    while(n > 0 && (line[n - 1] == '\r' || line[n - 1] == ' ')) line[--n] = 0;
    char name[16], device[8];
    int used = 0;
    const char* start = line;
    int player = 1;
    if(SDL_strncasecmp(start, "P2 ", 3) == 0) {
      player = 2;
      start += 3;
    }
    if(line[0] == '#' || sscanf(start, "%15s %7s %n", name, device, &used) < 2 || used == 0) continue;
    const char* what = start + used;
    int b = -1;
    for(int i = 0; i < 12; i++) if(SDL_strcasecmp(name, kButtonNames[i]) == 0) b = i;
    if(b < 0 || bindingCount == SDL_arraysize(bindings)) continue;
    Binding bind = {false, 0, (uint16_t) (1 << b), player};
    if(SDL_strcasecmp(device, "key") == 0) {
      bind.code = (int) SDL_GetKeyFromName(what);
      if(bind.code == SDLK_UNKNOWN) continue;
    } else if(SDL_strcasecmp(device, "pad") == 0) {
      bind.pad = true;
      bind.code = SDL_GetGamepadButtonFromString(what);
      if(bind.code == SDL_GAMEPAD_BUTTON_INVALID) continue;
    } else {
      continue;
    }
    bindings[bindingCount++] = bind;
  }
}

static void loadControls(void) {
  const char* base = SDL_GetBasePath();
  char path[2048];
  snprintf(path, sizeof(path), "%scontrols.txt", base ? base : "");
  size_t size;
  char* text = SDL_LoadFile(path, &size);
  if(text) {
    parseControls(text);
    SDL_free(text);
    if(bindingCount > 0) return;
  } else {
    SDL_SaveFile(path, kDefaultControls, strlen(kDefaultControls));
  }
  parseControls(kDefaultControls);
}

static uint16_t keyToButton(SDL_Keycode key, int player) {
  uint16_t b = 0;
  for(int i = 0; i < bindingCount; i++) if(!bindings[i].pad && bindings[i].player == player && bindings[i].code == (int) key) b |= bindings[i].button;
  return b;
}

static uint16_t padButtons(SDL_Gamepad* pad) {
  if(pad == NULL) return 0;
  uint16_t b = 0;
  for(int i = 0; i < bindingCount; i++) {
    if(bindings[i].pad && SDL_GetGamepadButton(pad, (SDL_GamepadButton) bindings[i].code)) b |= bindings[i].button;
  }
  const Sint16 dead = 16000;
  Sint16 x = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
  Sint16 y = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
  if(x < -dead) b |= TG_BTN_LEFT;
  if(x > dead) b |= TG_BTN_RIGHT;
  if(y < -dead) b |= TG_BTN_UP;
  if(y > dead) b |= TG_BTN_DOWN;
  // Triggers double as shoulder buttons.
  if(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > dead) b |= TG_BTN_L;
  if(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > dead) b |= TG_BTN_R;
  return b;
}

static void openPad(SDL_JoystickID id) {
  for(int i = 0; i < 2; i++) {
    if(app.pads[i] == NULL) {
      app.pads[i] = SDL_OpenGamepad(id);
      return;
    }
  }
}

static void closePad(SDL_JoystickID id) {
  for(int i = 0; i < 2; i++) {
    if(app.pads[i] && SDL_GetGamepadID(app.pads[i]) == id) {
      SDL_CloseGamepad(app.pads[i]);
      app.pads[i] = NULL;
    }
  }
}

// The options are remembered in settings.txt in the settings folder, one "name 0|1" per line.
typedef struct {
  const char* name;
  bool* value;
} Setting;

static const Setting kSettings[] = {
  {"fullscreen", &app.fullscreen}, {"integer_scale", &app.integerScale}, {"rumble", &app.rumble},
  {"smooth", &app.smooth}, {"smooth_scaling", &app.scaling}, {"full_view", &app.fullView},
  {"tall_road", &app.tallRoad}, {"wide", &app.wide}, {"side_by_side", &app.sideBySide},
  {"ghost", &app.ghost}, {"auto_save", &app.autoSave}, {"high_fps", &app.highFps},
};

static void loadSettings(void) {
  char* text = SDL_LoadFile(app.settingsPath, NULL);
  if(text == NULL) return;
  for(char* line = text; line && *line; ) {
    char* next = strchr(line, '\n');
    if(next) *next++ = 0;
    char name[32];
    int value;
    if(sscanf(line, "%31s %d", name, &value) == 2) {
      for(size_t i = 0; i < SDL_arraysize(kSettings); i++) if(strcmp(name, kSettings[i].name) == 0) *kSettings[i].value = value != 0;
      if(strcmp(name, "aspect") == 0 && value >= 0 && value < ASPECT_COUNT) app.aspect = (AspectMode) value;
      if(strcmp(name, "filter") == 0 && value >= 0 && value < FILTER_COUNT) app.filter = (FilterMode) value;
    }
    line = next;
  }
  SDL_free(text);
}

static void saveSettings(void) {
  if(!app.settingsPath[0] || app.replaying) return;
  FILE* f = fopen(app.settingsPath, "w");
  if(f == NULL) return;
  fprintf(f, "aspect %d\nfilter %d\n", (int) app.aspect, (int) app.filter);
  for(size_t i = 0; i < SDL_arraysize(kSettings); i++) fprintf(f, "%s %d\n", kSettings[i].name, *kSettings[i].value ? 1 : 0);
  fclose(f);
}

static void saveProgress(void) {
  FILE* f = app.progressPath[0] ? fopen(app.progressPath, "w") : NULL;
  if(f == NULL) return;
  fprintf(f, "%d %d %d\n", app.saved[0], app.saved[1], app.saved[2]);
  fclose(f);
}

static void loadProgress(void) {
  char* text = app.progressPath[0] ? SDL_LoadFile(app.progressPath, NULL) : NULL;
  if(text == NULL) return;
  int a, b, c;
  if(sscanf(text, "%d %d %d", &a, &b, &c) == 3) {
    app.saved[0] = (uint8_t) (a & 7);
    app.saved[1] = (uint8_t) (b & 7);
    app.saved[2] = (uint8_t) (c & 7);
  }
  SDL_free(text);
}

static void updateTitle(void) {
  saveSettings();
  char title[128];
  snprintf(title, sizeof(title), "Top Gear  |  %s, %s%s%s%s%s%s", aspectNames[app.aspect], filterNames[app.filter],
           app.integerScale ? ", integer scale" : "",
           app.smooth && !app.original ? ", smooth motion" : "", app.scaling ? ", smooth scaling" : "",
           app.fullView && !app.original ? (app.tallRoad ? ", full-screen view (tall road)" : ", full-screen view") : "", app.rumble ? ", rumble" : "");
  if(app.wide && !app.original) SDL_strlcat(title, ", widescreen", sizeof(title));
  if(app.sideBySide && !app.original) SDL_strlcat(title, ", side by side", sizeof(title));
  if(app.ghost && !app.original) SDL_strlcat(title, ", ghost car", sizeof(title));
  if(app.autoSave) SDL_strlcat(title, ", auto-save", sizeof(title));
  if(app.highFps && !app.original) SDL_strlcat(title, ", high frame rate", sizeof(title));
  SDL_SetWindowTitle(app.window, title);
}

static bool menuOpen(void);
static void drawMenu(void);

static void present(void) {
  int outW, outH;
  SDL_GetCurrentRenderOutputSize(app.renderer, &outW, &outH);
  // Picture size in output pixels before scaling: 256x224 (or wider) with the chosen pixel
  // aspect.
  int margin = tg_port_wide_margin;
  bool wide = margin > 0 && app.wideTexture && app.wideTextureW == SNES_W + 2 * margin;
  int srcW = wide ? app.wideTextureW : SNES_W;
  SDL_Texture* tex = wide ? app.wideTexture : app.texture;
  double picW = (app.aspect == ASPECT_4_3 ? SNES_H * 4.0 / 3.0 / SNES_W : 1.0) * srcW;
  double picH = SNES_H;
  double scale = SDL_min(outW / picW, outH / picH);
  if(app.integerScale && scale >= 1.0) scale = SDL_floor(scale);
  SDL_FRect dst;
  dst.w = (float) (picW * scale);
  dst.h = (float) (picH * scale);
  dst.x = (float) SDL_floor((outW - dst.w) / 2);
  dst.y = (float) SDL_floor((outH - dst.h) / 2);
  // The framebuffer is 512x480 with 2x2 pixels; the 224 visible lines start at y=16.
  SDL_FRect src = {0, 16, TG_FB_WIDTH, SNES_H * 2};
  if(wide) src = (SDL_FRect) {0, 0, (float) srcW, SNES_H};
  SDL_SetRenderDrawColor(app.renderer, 0, 0, 0, 255);
  SDL_RenderClear(app.renderer);
  if(app.filter == FILTER_SMOOTH) {
    int k = (int) SDL_ceil(SDL_max(dst.w / srcW, dst.h / SNES_H));
    if(k < 1) k = 1;
    if(k > 8) k = 8;
    if(app.upscaled == NULL || app.upscaledFactor != k || app.upscaledW != srcW) {
      if(app.upscaled) SDL_DestroyTexture(app.upscaled);
      app.upscaled = SDL_CreateTexture(app.renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_TARGET, srcW * k, SNES_H * k);
      if(app.upscaled) SDL_SetTextureScaleMode(app.upscaled, SDL_SCALEMODE_LINEAR);
      app.upscaledFactor = k;
      app.upscaledW = srcW;
    }
    if(app.upscaled) {
      SDL_SetRenderTarget(app.renderer, app.upscaled);
      SDL_RenderTexture(app.renderer, tex, &src, NULL);
      SDL_SetRenderTarget(app.renderer, NULL);
      SDL_RenderTexture(app.renderer, app.upscaled, NULL, &dst);
    } else {
      SDL_RenderTexture(app.renderer, tex, &src, &dst);
    }
  } else {
    SDL_RenderTexture(app.renderer, tex, &src, &dst);
  }
  if(app.filter == FILTER_SCANLINES && dst.h >= SNES_H * 2) {
    // Darken the lower part of every line, as the gaps between a CRT's scanlines.
    float line = dst.h / SNES_H;
    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(app.renderer, 0, 0, 0, 96);
    for(int y = 0; y < SNES_H; y++) {
      SDL_FRect r = {dst.x, dst.y + line * y + line * 0.6f, dst.w, line * 0.4f};
      SDL_RenderFillRect(app.renderer, &r);
    }
    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
  }
  if(menuOpen()) drawMenu();
  SDL_RenderPresent(app.renderer);
}

static void startRecording(void) {
  const char* base = SDL_GetBasePath();
  char dir[2048];
  snprintf(dir, sizeof(dir), "%srecordings", base ? base : "");
  if(!SDL_CreateDirectory(dir)) {
    char* pref = SDL_GetPrefPath("topgear-pc", "Top Gear");
    if(pref == NULL) return;
    snprintf(dir, sizeof(dir), "%srecordings", pref);
    SDL_free(pref);
    if(!SDL_CreateDirectory(dir)) return;
  }
  SDL_Time now;
  SDL_DateTime t;
  SDL_GetCurrentTime(&now);
  SDL_TimeToDateTime(now, &t, true);
  char path[2300];
  snprintf(path, sizeof(path), "%s/session-%04d%02d%02d-%02d%02d%02d.txt", dir, t.year, t.month, t.day, t.hour, t.minute, t.second);
  app.record = fopen(path, "w");
  if(app.record) {
    fprintf(app.record, "# Top Gear input recording from power-on. Replay: tg_headless --input <this file>\n");
    fprintf(app.record, "# ROM SHA-256 %s\n", TG_ROM_SHA256);
    fprintf(app.record, app.original ? "# Played with the original code (--original)\n"
                                     : "# Played with the C race code: replay with tg_headless --port native\n");
  }
  snprintf(app.coveragePath, sizeof(app.coveragePath), "%s/coverage.tgcov", dir);
  app.coverage = SDL_malloc(sizeof(TgCoverage));
  if(app.coverage) {
    tg_coverage_load(app.coverage, app.coveragePath);
    tg_game_set_coverage(app.game, app.coverage);
  }
}

static void stopRecording(void) {
  if(app.record) {
    uint8_t d[32];
    tg_sha256(tg_game_wram(app.game), 0x20000, d);
    fprintf(app.record, "# end frame %u wram ", app.frame);
    for(int i = 0; i < 32; i++) fprintf(app.record, "%02x", d[i]);
    fprintf(app.record, "\n");
    fclose(app.record);
    app.record = NULL;
  }
  if(app.coverage) {
    tg_game_set_coverage(app.game, NULL);
    tg_coverage_save(app.coverage, app.coveragePath);
    SDL_free(app.coverage);
    app.coverage = NULL;
  }
}

// Controller rumble in races: a knock when a car bumps something ($1EF5/$1EF6 bit 7) and
// a long light rumble when its nitro fires ($1E6C/$1E6E bit 15). Pad 1 drives the first
// car, pad 2 the second.
static void rumble(void) {
  static uint32_t lastIrqs;
  static uint8_t lastBump[2], lastNitro[2];
  bool inRace = tg_port_race_irqs != lastIrqs;
  lastIrqs = tg_port_race_irqs;
  const uint8_t* ram = tg_game_wram(app.game);
  for(int p = 0; p < 2; p++) {
    uint8_t bump = inRace ? ram[0x1EF5 + p] & 0x80 : 0;
    uint8_t nitro = inRace ? ram[0x1E6D + 2 * p] & 0x80 : 0;
    if(app.rumble && app.pads[p] && ram[0x1FF6] == 0) {  // not in the attract demo
      if(bump && !lastBump[p]) SDL_RumbleGamepad(app.pads[p], 0xC000, 0xFFFF, 200);
      else if(nitro && !lastNitro[p]) SDL_RumbleGamepad(app.pads[p], 0x3000, 0x8000, 800);
    }
    lastBump[p] = bump;
    lastNitro[p] = nitro;
  }
}

// Widescreen: the margin that makes the picture as wide as the window.
static int wantedMargin(void) {
  if(!app.wide || app.original) return 0;
  int outW, outH;
  if(!SDL_GetCurrentRenderOutputSize(app.renderer, &outW, &outH) || outH <= 0) return 0;
  double pixel = app.aspect == ASPECT_4_3 ? SNES_H * 4.0 / 3.0 / SNES_W : 1.0;
  int m = (int) SDL_floor(((double) outW / outH * SNES_H / pixel - SNES_W) / 2);
  return m < 0 ? 0 : m > 96 ? 96 : m;
}

// The game's picture into the textures.
static void uploadPicture(void) {
  void* pixels;
  int pitch;
  if(SDL_LockTexture(app.texture, NULL, &pixels, &pitch)) {
    tg_game_get_pixels(app.game, pixels);
    SDL_UnlockTexture(app.texture);
  }
  const uint32_t* wide = tg_game_wide_pixels(app.game);
  if(wide) {
    int w = SNES_W + 2 * tg_port_wide_margin;
    if(app.wideTexture == NULL || app.wideTextureW != w) {
      if(app.wideTexture) SDL_DestroyTexture(app.wideTexture);
      app.wideTexture = SDL_CreateTexture(app.renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, w, SNES_H);
      if(app.wideTexture) SDL_SetTextureScaleMode(app.wideTexture, SDL_SCALEMODE_NEAREST);
      app.wideTextureW = w;
    }
    if(app.wideTexture) SDL_UpdateTexture(app.wideTexture, NULL, wide, w * 4);
  }
}

static void runFrame(int samples) {
  uint16_t input[3] = {0, app.keys[1] | padButtons(app.pads[0]), app.keys[2] | padButtons(app.pads[1])};
  bool reset = app.resetRequested;
  app.resetRequested = false;
  if(app.replaying) {
    tg_input_script_advance(&app.replay, app.frame);
    input[1] = app.replay.state[1];
    input[2] = app.replay.state[2];
    reset = app.replay.resetPending;
    app.replay.resetPending = false;
    if(app.replay.smooth != app.smooth) { app.smooth = app.replay.smooth; updateTitle(); }
    if(app.replay.scaling != app.scaling) { app.scaling = app.replay.scaling; updateTitle(); }
    if(app.replay.fullview != app.fullView) { app.fullView = app.replay.fullview; updateTitle(); }
    if(app.replay.tallRoad != app.tallRoad) { app.tallRoad = app.replay.tallRoad; updateTitle(); }
    if((app.replay.wide > 0) != app.wide) { app.wide = app.replay.wide > 0; updateTitle(); }
    if(app.replay.sideBySide != app.sideBySide) { app.sideBySide = app.replay.sideBySide; updateTitle(); }
    if(app.replay.ghost != app.ghost) { app.ghost = app.replay.ghost; updateTitle(); }
  }
  // Smooth motion runs the race logic every frame; the original updates it every third
  // frame (its pace is scaled by the frames elapsed, so the race takes the same time).
  tg_port_race_frames = app.smooth ? 1 : 3;
  if(app.scaling != tg_port_smooth_sprites) tg_game_set_smooth_sprites(app.game, app.scaling);
  if(app.fullView != tg_port_full_view) tg_game_set_full_view(app.game, app.fullView);
  tg_game_set_tall_road(app.game, app.tallRoad);
  int margin = app.replaying ? app.replay.wide : wantedMargin();
  if(margin != tg_port_wide_margin) tg_game_set_wide(app.game, margin);
  if(app.sideBySide != tg_port_side_by_side) tg_game_set_side_by_side(app.game, app.sideBySide);
  if(app.ghost != tg_port_ghost) tg_game_set_ghost(app.game, app.ghost);
  if(app.highFps != tg_port_interp) tg_game_set_interp(app.game, app.highFps);
  if(app.record) {
    if(reset) fprintf(app.record, "%u reset\n", app.frame);
    if(app.smooth != app.recordedSmooth) {
      fprintf(app.record, "%u smooth %s\n", app.frame, app.smooth ? "on" : "off");
      app.recordedSmooth = app.smooth;
    }
    if(app.scaling != app.recordedScaling) {
      fprintf(app.record, "%u scaling %s\n", app.frame, app.scaling ? "on" : "off");
      app.recordedScaling = app.scaling;
    }
    if(app.fullView != app.recordedFullView) {
      fprintf(app.record, "%u fullview %s\n", app.frame, app.fullView ? "on" : "off");
      app.recordedFullView = app.fullView;
    }
    if(app.tallRoad != app.recordedTallRoad) {
      fprintf(app.record, "%u tallroad %s\n", app.frame, app.tallRoad ? "on" : "off");
      app.recordedTallRoad = app.tallRoad;
    }
    if(margin != app.recordedWide) {
      fprintf(app.record, "%u wide %d\n", app.frame, margin);
      app.recordedWide = margin;
    }
    if(app.sideBySide != app.recordedSideBySide) {
      fprintf(app.record, "%u sidebyside %s\n", app.frame, app.sideBySide ? "on" : "off");
      app.recordedSideBySide = app.sideBySide;
    }
    if(app.ghost != app.recordedGhost) {
      fprintf(app.record, "%u ghost %s\n", app.frame, app.ghost ? "on" : "off");
      app.recordedGhost = app.ghost;
    }
    for(int p = 1; p <= 2; p++) {
      if(input[p] != app.recorded[p] || (app.frame == 0 && p == 1)) {
        char names[96];
        tg_format_buttons(input[p], names, sizeof(names));
        fprintf(app.record, "%u %d %s\n", app.frame, p, names);
        app.recorded[p] = input[p];
      }
    }
  }
  if(reset) tg_game_reset(app.game, false);
  if(app.replaying && app.replay.unlockPending) {
    uint16_t u = app.replay.unlock;
    uint8_t open[3] = {u & 15, (u >> 4) & 15, (u >> 8) & 15};
    tg_game_unlock(app.game, open);
    app.replay.unlockPending = false;
  }
  if(app.autoSave && !app.replaying) {
    // Countries reached in earlier sessions are opened again (after power-on clears them).
    uint8_t open[3];
    tg_game_progress(app.game, open);
    if(open[0] < app.saved[0] || open[1] < app.saved[1] || open[2] < app.saved[2]) {
      tg_game_unlock(app.game, app.saved);
      uint8_t now[3];
      tg_game_progress(app.game, now);
      if(app.record && memcmp(now, open, 3) != 0) fprintf(app.record, "%u unlock %d %d %d\n", app.frame, app.saved[0], app.saved[1], app.saved[2]);
    }
  }
  tg_game_set_input(app.game, 1, input[1]);
  tg_game_set_input(app.game, 2, input[2]);
  tg_game_run_frame(app.game);
  app.frame++;
  // A new best race is saved straight away (not while replaying someone's recording).
  if(tg_game_ghosts_changed(app.game) && !app.replaying && app.ghostPath[0]) tg_game_save_ghosts(app.game, app.ghostPath);
  if(app.autoSave && !app.replaying) {
    uint8_t open[3];
    tg_game_progress(app.game, open);
    bool more = false;
    for(int i = 0; i < 3; i++) {
      if(open[i] > app.saved[i]) {
        app.saved[i] = open[i];
        more = true;
      }
    }
    if(more) saveProgress();
  }
  rumble();

  uploadPicture();

  static int16_t buffer[2048 * 2];
  tg_game_get_samples(app.game, buffer, samples);
  // Keep at most ~6 frames queued so latency can't build up.
  if(SDL_GetAudioStreamQueued(app.audio) < 6 * 800 * 4) {
    SDL_PutAudioStreamData(app.audio, buffer, samples * 4);
  }
}


// ---------------------------------------------------------------------------------------
// The options menu (Tab, or a gamepad's guide button): every option, the controls and the
// gamepad order in one place. The game waits while it is open.

typedef enum {
  MI_RESUME, MI_ASPECT, MI_FILTER, MI_INTEGER, MI_FULLSCREEN, MI_WIDE, MI_FULLVIEW, MI_SIDE,
  MI_SMOOTH, MI_HIGHFPS, MI_SCALING, MI_GHOST, MI_AUTOSAVE, MI_RUMBLE, MI_SWAP, MI_KEYS,
  MI_KEYS2, MI_PADS, MI_RESET, MI_QUIT, MI_COUNT
} MenuItem;

static struct {
  bool open;
  int page;    // 0 options, 1 player 1's keys, 2 gamepad controls, 3 player 2's keys
  int sel;
  int binding; // controls page: the SNES button waiting for a key or gamepad button, or -1
  bool quit;
} menu = {false, 0, 0, -1, false};

static void setFullscreen(bool on) {
  app.fullscreen = on;
  SDL_SetWindowFullscreen(app.window, on);
  saveSettings();
}

static void setWide(bool on) {
  app.wide = on;
  if(!app.fullscreen) {
    // A window gets the shape of the picture: 16:9 for widescreen, else 4:3.
    int w, h;
    SDL_GetWindowSize(app.window, &w, &h);
    SDL_SetWindowSize(app.window, on ? h * 16 / 9 : h * 4 / 3, h);
  }
  updateTitle();
}

// Off, then full view with the original's low horizon, then with a tall road.
static void stepFullView(void) {
  if(!app.fullView) app.fullView = true;
  else if(!app.tallRoad) app.tallRoad = true;
  else app.fullView = app.tallRoad = false;
  updateTitle();
}

static void setHighFps(bool on) {
  // In-between frames need the race to move every frame: smooth motion comes on too.
  app.highFps = on;
  if(on && !app.replaying) app.smooth = true;
  updateTitle();
}

static void setSmooth(bool on) {
  app.smooth = on;
  if(!on) app.highFps = false;
  updateTitle();
}

// The names of a SNES button's keyboard (player's) or gamepad bindings.
static void bindingNames(int button, bool pad, int player, char* out, size_t size) {
  out[0] = 0;
  for(int i = 0; i < bindingCount; i++) {
    if(bindings[i].pad != pad || bindings[i].button != (uint16_t) (1 << button)) continue;
    if(!pad && bindings[i].player != player) continue;
    const char* name = pad ? SDL_GetGamepadStringForButton((SDL_GamepadButton) bindings[i].code) : SDL_GetKeyName((SDL_Keycode) bindings[i].code);
    if(out[0]) SDL_strlcat(out, ", ", size);
    SDL_strlcat(out, name ? name : "?", size);
  }
  if(!out[0]) SDL_strlcpy(out, "-", size);
}

static void saveControls(void) {
  const char* base = SDL_GetBasePath();
  char path[2048];
  snprintf(path, sizeof(path), "%scontrols.txt", base ? base : "");
  FILE* f = fopen(path, "w");
  if(f == NULL) return;
  // The comment lines of the defaults, then the bindings.
  for(const char* t = kDefaultControls; *t == '#'; ) {
    const char* end = strchr(t, '\n');
    fwrite(t, 1, (size_t) (end - t + 1), f);
    t = end + 1;
  }
  for(int i = 0; i < bindingCount; i++) {
    int b = 0;
    while(b < 12 && bindings[i].button != (uint16_t) (1 << b)) b++;
    if(bindings[i].pad) fprintf(f, "%s pad %s\n", kButtonNames[b], SDL_GetGamepadStringForButton((SDL_GamepadButton) bindings[i].code));
    else fprintf(f, "%s%s key %s\n", bindings[i].player == 2 ? "P2 " : "", kButtonNames[b], SDL_GetKeyName((SDL_Keycode) bindings[i].code));
  }
  fclose(f);
}

// Replaces a SNES button's keyboard (player's) or gamepad bindings with code.
static void rebind(int button, bool pad, int player, int code) {
  int n = 0;
  for(int i = 0; i < bindingCount; i++) {
    bool same = bindings[i].pad == pad && (pad || bindings[i].player == player);
    if(same && bindings[i].button == (uint16_t) (1 << button)) continue;
    if(bindings[i].pad == pad && bindings[i].code == code) continue;  // the key or button moves here
    bindings[n++] = bindings[i];
  }
  bindingCount = n;
  if(bindingCount < (int) SDL_arraysize(bindings)) bindings[bindingCount++] = (Binding) {pad, code, (uint16_t) (1 << button), player};
  saveControls();
}

static const char* onOff(bool on) { return on ? "on" : "off"; }

static void menuItemText(int i, char* out, size_t size) {
  switch(i) {
    case MI_RESUME: snprintf(out, size, "Back to the game"); break;
    case MI_ASPECT: snprintf(out, size, "Aspect             %s", aspectNames[app.aspect]); break;
    case MI_FILTER: snprintf(out, size, "Display filter     %s", filterNames[app.filter]); break;
    case MI_INTEGER: snprintf(out, size, "Integer scaling    %s", onOff(app.integerScale)); break;
    case MI_FULLSCREEN: snprintf(out, size, "Fullscreen         %s", onOff(app.fullscreen)); break;
    case MI_WIDE: snprintf(out, size, "Widescreen         %s", onOff(app.wide)); break;
    case MI_FULLVIEW: snprintf(out, size, "Full-screen view   %s", !app.fullView ? "off" : app.tallRoad ? "on, tall road" : "on"); break;
    case MI_SIDE: snprintf(out, size, "2P side by side    %s", onOff(app.sideBySide)); break;
    case MI_SMOOTH: snprintf(out, size, "Smooth motion      %s", onOff(app.smooth)); break;
    case MI_HIGHFPS: snprintf(out, size, "High frame rate    %s", onOff(app.highFps)); break;
    case MI_SCALING: snprintf(out, size, "Smooth scaling     %s", onOff(app.scaling)); break;
    case MI_GHOST: snprintf(out, size, "Ghost car          %s", onOff(app.ghost)); break;
    case MI_AUTOSAVE: snprintf(out, size, "Auto-save          %s", onOff(app.autoSave)); break;
    case MI_RUMBLE: snprintf(out, size, "Rumble             %s", onOff(app.rumble)); break;
    case MI_SWAP: snprintf(out, size, "Swap gamepads      P1: %s", app.pads[0] ? SDL_GetGamepadName(app.pads[0]) : "none"); break;
    case MI_KEYS: snprintf(out, size, "Player 1 keys..."); break;
    case MI_KEYS2: snprintf(out, size, "Player 2 keys..."); break;
    case MI_PADS: snprintf(out, size, "Gamepad controls..."); break;
    case MI_RESET: snprintf(out, size, "Reset the game"); break;
    case MI_QUIT: snprintf(out, size, "Quit"); break;
  }
}

static int menuCount(void) { return menu.page == 0 ? MI_COUNT : 13; }

static void menuBack(void);

// Changes the selected item (dir: -1 left, +1 right or select).
static void menuActivate(int dir) {
  if(menu.page != 0) {
    if(menu.sel == 12) {
      menuBack();
    } else {
      menu.binding = menu.sel;
    }
    return;
  }
  bool locked = app.replaying;  // a replay sets the options that change the race
  switch(menu.sel) {
    case MI_RESUME: menu.open = false; break;
    case MI_ASPECT: app.aspect = (app.aspect + 1) % ASPECT_COUNT; updateTitle(); break;
    case MI_FILTER: app.filter = (FilterMode) ((app.filter + (dir < 0 ? FILTER_COUNT - 1 : 1)) % FILTER_COUNT); updateTitle(); break;
    case MI_INTEGER: app.integerScale = !app.integerScale; updateTitle(); break;
    case MI_FULLSCREEN: setFullscreen(!app.fullscreen); break;
    case MI_WIDE: if(!locked) setWide(!app.wide); break;
    case MI_FULLVIEW: if(!locked) stepFullView(); break;
    case MI_SIDE: if(!locked) { app.sideBySide = !app.sideBySide; updateTitle(); } break;
    case MI_SMOOTH: if(!locked) setSmooth(!app.smooth); break;
    case MI_HIGHFPS: setHighFps(!app.highFps); break;
    case MI_SCALING: if(!locked) { app.scaling = !app.scaling; updateTitle(); } break;
    case MI_GHOST: if(!locked) { app.ghost = !app.ghost; updateTitle(); } break;
    case MI_AUTOSAVE: app.autoSave = !app.autoSave; updateTitle(); break;
    case MI_RUMBLE: app.rumble = !app.rumble; updateTitle(); break;
    case MI_SWAP: {
      SDL_Gamepad* t = app.pads[0];
      app.pads[0] = app.pads[1];
      app.pads[1] = t;
      break;
    }
    case MI_KEYS: menu.page = 1; menu.sel = 0; break;
    case MI_KEYS2: menu.page = 3; menu.sel = 0; break;
    case MI_PADS: menu.page = 2; menu.sel = 0; break;
    case MI_RESET: app.resetRequested = true; menu.open = false; break;
    case MI_QUIT: menu.quit = true; break;
  }
}

static void menuBack(void) {
  if(menu.binding >= 0) menu.binding = -1;
  else if(menu.page != 0) {
    menu.sel = menu.page == 1 ? MI_KEYS : menu.page == 3 ? MI_KEYS2 : MI_PADS;
    menu.page = 0;
  } else menu.open = false;
}

static void menuMove(int d) {
  int n = menuCount();
  menu.sel = (menu.sel + d + n) % n;
}

static void openMenu(void) {
  menu.open = true;
  menu.page = 0;
  menu.sel = MI_RESUME;
  menu.binding = -1;
  app.keys[1] = app.keys[2] = 0;
}

// A key while the menu is open.
static void menuKey(SDL_Keycode key) {
  if(menu.binding >= 0) {
    if(key == SDLK_ESCAPE) menu.binding = -1;
    else if(menu.page == 1 || menu.page == 3) {
      rebind(menu.binding, false, menu.page == 3 ? 2 : 1, (int) key);
      menu.binding = -1;
    }
    return;
  }
  switch(key) {
    case SDLK_UP: menuMove(-1); break;
    case SDLK_DOWN: menuMove(1); break;
    case SDLK_LEFT: if(menu.page == 0) menuActivate(-1); break;
    case SDLK_RIGHT: case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: menuActivate(1); break;
    case SDLK_ESCAPE: case SDLK_BACKSPACE: menuBack(); break;
    case SDLK_TAB: menu.open = false; break;
    default: break;
  }
}

static void menuPadButton(SDL_GamepadButton b) {
  if(menu.binding >= 0) {
    if(menu.page == 2 && b != SDL_GAMEPAD_BUTTON_GUIDE) {
      rebind(menu.binding, true, 1, (int) b);
      menu.binding = -1;
    }
    return;
  }
  switch(b) {
    case SDL_GAMEPAD_BUTTON_DPAD_UP: menuMove(-1); break;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: menuMove(1); break;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: if(menu.page == 0) menuActivate(-1); break;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: case SDL_GAMEPAD_BUTTON_SOUTH: menuActivate(1); break;
    case SDL_GAMEPAD_BUTTON_EAST: menuBack(); break;
    case SDL_GAMEPAD_BUTTON_GUIDE: case SDL_GAMEPAD_BUTTON_START: menu.open = false; break;
    default: break;
  }
}

static bool menuOpen(void) { return menu.open; }

static void drawMenu(void) {
  int outW, outH;
  SDL_GetCurrentRenderOutputSize(app.renderer, &outW, &outH);
  int lines = menuCount() + 4;
  float scale = SDL_max(1.0f, SDL_floorf(SDL_min(outH / (lines * 12.0f + 16), outW / 400.0f)));
  SDL_SetRenderScale(app.renderer, scale, scale);
  float w = outW / scale, h = outH / scale;
  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(app.renderer, 0, 0, 0, 200);
  SDL_FRect box = {0, 0, w, h};
  SDL_RenderFillRect(app.renderer, &box);
  float x = SDL_max(8.0f, (w - 50 * 8) / 2), y = SDL_max(8.0f, (h - lines * 12) / 2);
  const char* title = menu.page == 0 ? "OPTIONS" : menu.page == 1 ? "PLAYER 1 KEYS" : menu.page == 3 ? "PLAYER 2 KEYS" : "GAMEPAD CONTROLS (BOTH PADS)";
  SDL_SetRenderDrawColor(app.renderer, 255, 210, 80, 255);
  SDL_RenderDebugText(app.renderer, x, y, title);
  y += 20;
  for(int i = 0; i < menuCount(); i++) {
    char text[160];
    if(menu.page == 0) {
      menuItemText(i, text, sizeof(text));
    } else if(i == 12) {
      snprintf(text, sizeof(text), "Back");
    } else {
      char names[96];
      bindingNames(i, menu.page == 2, menu.page == 3 ? 2 : 1, names, sizeof(names));
      snprintf(text, sizeof(text), "%-7s %s", kButtonNames[i], menu.binding == i ? (menu.page != 2 ? "press a key (Esc: cancel)" : "press a button") : names);
    }
    bool sel = i == menu.sel;
    SDL_SetRenderDrawColor(app.renderer, sel ? 255 : 200, sel ? 255 : 200, sel ? 255 : 200, 255);
    SDL_RenderDebugText(app.renderer, x, y, sel ? ">" : " ");
    SDL_RenderDebugText(app.renderer, x + 16, y, text);
    y += 12;
  }
  y += 8;
  SDL_SetRenderDrawColor(app.renderer, 150, 150, 150, 255);
  SDL_RenderDebugText(app.renderer, x, y, menu.page == 0 ? "Up/Down choose, Enter or Left/Right change, Tab closes" : "Enter: change, Esc: back");
  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
  SDL_SetRenderScale(app.renderer, 1, 1);
}

int main(int argc, char** argv) {
  SDL_SetAppMetadata("Top Gear", "0.1", "topgear-pc");
  if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Top Gear", SDL_GetError(), NULL);
    return 1;
  }

  // Remembered options first; the command line can turn more on.
  app.aspect = ASPECT_4_3;
  char* prefDir = SDL_GetPrefPath("topgear-pc", "Top Gear");
  if(prefDir) {
    snprintf(app.settingsPath, sizeof(app.settingsPath), "%ssettings.txt", prefDir);
    snprintf(app.progressPath, sizeof(app.progressPath), "%sprogress.txt", prefDir);
    SDL_free(prefDir);
    loadSettings();
    loadProgress();
  }
  const char* romArg = NULL;
  const char* replayPath = NULL;
  bool record = true;
  bool original = false;
  for(int i = 1; i < argc; i++) {
    if(strcmp(argv[i], "--no-record") == 0) record = false;
    else if(strcmp(argv[i], "--original") == 0) original = true;
    else if(strcmp(argv[i], "--smooth") == 0) app.smooth = true;
    else if(strcmp(argv[i], "--rumble") == 0) app.rumble = true;
    else if(strcmp(argv[i], "--smooth-scaling") == 0) app.scaling = true;
    else if(strcmp(argv[i], "--full-view") == 0) app.fullView = true;
    else if(strcmp(argv[i], "--tall-road") == 0) app.tallRoad = true;
    else if(strcmp(argv[i], "--wide") == 0) app.wide = true;
    else if(strcmp(argv[i], "--side-by-side") == 0) app.sideBySide = true;
    else if(strcmp(argv[i], "--ghost") == 0) app.ghost = true;
    else if(strcmp(argv[i], "--auto-save") == 0) app.autoSave = true;
    else if(strcmp(argv[i], "--high-fps") == 0) app.highFps = app.smooth = true;
    else if(strcmp(argv[i], "--filter") == 0 && i + 1 < argc) {
      i++;
      for(int f = 0; f < FILTER_COUNT; f++) if(strcmp(argv[i], filterNames[f]) == 0) app.filter = f;
    }
    else if(strcmp(argv[i], "--replay") == 0 && i + 1 < argc) replayPath = argv[++i];
    else romArg = argv[i];
  }

  TgRom rom;
  char message[4096];
  if(!findRom(romArg, &rom, message, sizeof(message))) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Top Gear", message, NULL);
    SDL_Quit();
    return 1;
  }
  app.game = tg_game_create(&rom);
  if(app.game == NULL) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Top Gear", "Failed to start the game.", NULL);
    return 1;
  }
  // The translated C code runs the races; --original runs the game's own code throughout,
  // with its slowdown and loading pauses (recordings made that way can be checked call by
  // call: tg_headless --port verify).
  app.original = original;
  tg_game_set_port_mode(app.game, original ? TG_PORT_OFF : TG_PORT_NATIVE);

  if(replayPath) {
    if(!tg_input_script_load(replayPath, &app.replay, message, sizeof(message))) {
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Top Gear", message, NULL);
      return 1;
    }
    app.replaying = true;
    record = false;
  }
  if(record) startRecording();
  // Ghost cars: the best race on each course, kept with the settings.
  char* pref = SDL_GetPrefPath("topgear-pc", "Top Gear");
  if(pref) {
    snprintf(app.ghostPath, sizeof(app.ghostPath), "%sghosts.bin", pref);
    SDL_free(pref);
    tg_game_load_ghosts(app.game, app.ghostPath);
  }

  loadControls();
  if(!SDL_CreateWindowAndRenderer("Top Gear", app.wide ? 1366 : 1024, 768, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &app.window, &app.renderer)) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Top Gear", SDL_GetError(), NULL);
    return 1;
  }
  SDL_SetWindowMinimumSize(app.window, SNES_W, SNES_H);
  if(app.fullscreen) SDL_SetWindowFullscreen(app.window, true);
  app.texture = SDL_CreateTexture(app.renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, TG_FB_WIDTH, TG_FB_HEIGHT);
  SDL_SetTextureScaleMode(app.texture, SDL_SCALEMODE_NEAREST);
  updateTitle();

  SDL_AudioSpec spec = {SDL_AUDIO_S16, 2, AUDIO_RATE};
  app.audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
  if(app.audio) SDL_ResumeAudioStreamDevice(app.audio);

  Uint64 next = SDL_GetTicksNS();
  double sampleAcc = 0;
  bool running = true;
  while(running) {
    SDL_Event e;
    while(SDL_PollEvent(&e)) {
      switch(e.type) {
        case SDL_EVENT_QUIT: running = false; break;
        case SDL_EVENT_GAMEPAD_ADDED: openPad(e.gdevice.which); break;
        case SDL_EVENT_GAMEPAD_REMOVED: closePad(e.gdevice.which); break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
          if(menu.open) menuPadButton((SDL_GamepadButton) e.gbutton.button);
          else if(e.gbutton.button == SDL_GAMEPAD_BUTTON_GUIDE) openMenu();
          break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
          bool down = e.type == SDL_EVENT_KEY_DOWN;
          if(menu.open) {
            if(down) menuKey(e.key.key);
            break;
          }
          if(down && !e.key.repeat && e.key.key == SDLK_TAB) {
            openMenu();
            break;
          }
          if(down && !e.key.repeat) {
            if(e.key.key == SDLK_F11 || (e.key.key == SDLK_RETURN && (e.key.mod & SDL_KMOD_ALT))) {
              app.fullscreen = !app.fullscreen;
              SDL_SetWindowFullscreen(app.window, app.fullscreen);
              saveSettings();
              break;
            }
            if(e.key.key == SDLK_ESCAPE && app.fullscreen) {
              app.fullscreen = false;
              SDL_SetWindowFullscreen(app.window, false);
              saveSettings();
              break;
            }
            if(e.key.key == SDLK_F2) { app.aspect = (app.aspect + 1) % ASPECT_COUNT; updateTitle(); break; }
            if(e.key.key == SDLK_F3) { app.integerScale = !app.integerScale; updateTitle(); break; }
            if(e.key.key == SDLK_F4) { app.filter = (app.filter + 1) % FILTER_COUNT; updateTitle(); break; }
            if(e.key.key == SDLK_F5) { app.resetRequested = true; break; }
            if(e.key.key == SDLK_F6 && (e.key.mod & SDL_KMOD_SHIFT)) {
              // In-between frames need the race to move every frame: smooth motion comes on too.
              app.highFps = !app.highFps;
              if(app.highFps && !app.replaying) app.smooth = true;
              updateTitle();
              break;
            }
            if(e.key.key == SDLK_F6 && !app.replaying) {
              app.smooth = !app.smooth;
              if(!app.smooth) app.highFps = false;
              updateTitle();
              break;
            }
            if(e.key.key == SDLK_F7 && !app.replaying) { app.scaling = !app.scaling; updateTitle(); break; }
            if(e.key.key == SDLK_F8) { app.rumble = !app.rumble; updateTitle(); break; }
            if(e.key.key == SDLK_F9 && !app.replaying) {
              // Off, then full view with the original's low horizon, then with a tall road.
              if(!app.fullView) app.fullView = true;
              else if(!app.tallRoad) app.tallRoad = true;
              else app.fullView = app.tallRoad = false;
              updateTitle();
              break;
            }
            if(e.key.key == SDLK_F12 && !app.replaying) { app.sideBySide = !app.sideBySide; updateTitle(); break; }
            if(e.key.key == SDLK_F1 && (e.key.mod & SDL_KMOD_SHIFT)) { app.autoSave = !app.autoSave; updateTitle(); break; }
            if(e.key.key == SDLK_F1 && !app.replaying) { app.ghost = !app.ghost; updateTitle(); break; }
            if(e.key.key == SDLK_F10 && !app.replaying) {
              app.wide = !app.wide;
              if(!app.fullscreen) {
                // A window gets the shape of the picture: 16:9 for widescreen, else 4:3.
                int w, h;
                SDL_GetWindowSize(app.window, &w, &h);
                SDL_SetWindowSize(app.window, app.wide ? h * 16 / 9 : h * 4 / 3, h);
              }
              updateTitle();
              break;
            }
          }
          for(int p = 1; p <= 2; p++) {
            uint16_t button = keyToButton(e.key.key, p);
            if(button && !(e.key.mod & SDL_KMOD_ALT)) {
              if(down) app.keys[p] |= button;
              else app.keys[p] &= ~button;
            }
          }
          break;
        }
      }
    }

    if(menu.quit) break;
    Uint64 now = SDL_GetTicksNS();
    if(menu.open) {
      // The game waits; the picture stays, with the menu over it.
      present();
      SDL_DelayPrecise((Uint64) FRAME_NS);
      next = SDL_GetTicksNS();
      continue;
    }
    if(now - next > (Uint64) (FRAME_NS * 8) && now > next) next = now;  // resync after a stall
    bool highFps = app.highFps && !app.original;
    if(highFps != app.vsync) {
      // High frame rate shows a picture every refresh of the display, waiting for it.
      SDL_SetRenderVSync(app.renderer, highFps ? 1 : 0);
      app.vsync = highFps;
    }
    if(highFps) {
      if(now >= next) {
        sampleAcc += AUDIO_RATE * FRAME_NS / 1e9;
        int samples = (int) sampleAcc;
        sampleAcc -= samples;
        runFrame(samples);
        next += (Uint64) FRAME_NS;
        now = SDL_GetTicksNS();
      }
      // The picture between the last two frames, as far on as the time since the last.
      double t = 1.0 - (double) (next > now ? next - now : 0) / FRAME_NS;
      if(tg_game_interp(app.game, (float) t)) uploadPicture();
      present();
    } else if(now >= next) {
      sampleAcc += AUDIO_RATE * FRAME_NS / 1e9;
      int samples = (int) sampleAcc;
      sampleAcc -= samples;
      runFrame(samples);
      next += (Uint64) FRAME_NS;
      present();
    } else {
      SDL_DelayPrecise(next - now);
    }
  }

  stopRecording();
  tg_input_script_free(&app.replay);
  tg_game_destroy(app.game);
  tg_rom_free(&rom);
  for(int i = 0; i < 2; i++) if(app.pads[i]) SDL_CloseGamepad(app.pads[i]);
  if(app.audio) SDL_DestroyAudioStream(app.audio);
  SDL_DestroyTexture(app.texture);
  SDL_DestroyRenderer(app.renderer);
  SDL_DestroyWindow(app.window);
  SDL_Quit();
  return 0;
}
