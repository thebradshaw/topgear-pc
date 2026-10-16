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
  int upscaledFactor;
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
  bool scaling;  // enhancement: race objects grow smoothly (F7, --smooth-scaling)
  bool recordedScaling;
  bool fullView;  // enhancement: 1-player races fill the whole screen (F9, --full-view)
  bool recordedFullView;
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
  "# triggers work as L and R. Delete this file to get the defaults back.\n"
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
    if(line[0] == '#' || sscanf(line, "%15s %7s %n", name, device, &used) < 2 || used == 0) continue;
    const char* what = line + used;
    int b = -1;
    for(int i = 0; i < 12; i++) if(SDL_strcasecmp(name, kButtonNames[i]) == 0) b = i;
    if(b < 0 || bindingCount == SDL_arraysize(bindings)) continue;
    Binding bind = {false, 0, (uint16_t) (1 << b)};
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

static uint16_t keyToButton(SDL_Keycode key) {
  uint16_t b = 0;
  for(int i = 0; i < bindingCount; i++) if(!bindings[i].pad && bindings[i].code == (int) key) b |= bindings[i].button;
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

static void updateTitle(void) {
  char title[128];
  snprintf(title, sizeof(title), "Top Gear  |  %s, %s%s%s%s%s%s", aspectNames[app.aspect], filterNames[app.filter],
           app.integerScale ? ", integer scale" : "",
           app.smooth && !app.original ? ", smooth motion" : "", app.scaling ? ", smooth scaling" : "",
           app.fullView && !app.original ? ", full-screen view" : "", app.rumble ? ", rumble" : "");
  SDL_SetWindowTitle(app.window, title);
}

static void present(void) {
  int outW, outH;
  SDL_GetCurrentRenderOutputSize(app.renderer, &outW, &outH);
  // Picture size in output pixels before scaling: 256x224 with the chosen pixel aspect.
  double picW = app.aspect == ASPECT_4_3 ? SNES_H * 4.0 / 3.0 : SNES_W;
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
  SDL_SetRenderDrawColor(app.renderer, 0, 0, 0, 255);
  SDL_RenderClear(app.renderer);
  if(app.filter == FILTER_SMOOTH) {
    int k = (int) SDL_ceil(SDL_max(dst.w / SNES_W, dst.h / SNES_H));
    if(k < 1) k = 1;
    if(k > 8) k = 8;
    if(app.upscaled == NULL || app.upscaledFactor != k) {
      if(app.upscaled) SDL_DestroyTexture(app.upscaled);
      app.upscaled = SDL_CreateTexture(app.renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_TARGET, SNES_W * k, SNES_H * k);
      if(app.upscaled) SDL_SetTextureScaleMode(app.upscaled, SDL_SCALEMODE_LINEAR);
      app.upscaledFactor = k;
    }
    if(app.upscaled) {
      SDL_SetRenderTarget(app.renderer, app.upscaled);
      SDL_RenderTexture(app.renderer, app.texture, &src, NULL);
      SDL_SetRenderTarget(app.renderer, NULL);
      SDL_RenderTexture(app.renderer, app.upscaled, NULL, &dst);
    } else {
      SDL_RenderTexture(app.renderer, app.texture, &src, &dst);
    }
  } else {
    SDL_RenderTexture(app.renderer, app.texture, &src, &dst);
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
  }
  // Smooth motion runs the race logic every frame; the original updates it every third
  // frame (its pace is scaled by the frames elapsed, so the race takes the same time).
  tg_port_race_frames = app.smooth ? 1 : 3;
  if(app.scaling != tg_port_smooth_sprites) tg_game_set_smooth_sprites(app.game, app.scaling);
  if(app.fullView != tg_port_full_view) tg_game_set_full_view(app.game, app.fullView);
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
  tg_game_set_input(app.game, 1, input[1]);
  tg_game_set_input(app.game, 2, input[2]);
  tg_game_run_frame(app.game);
  app.frame++;
  rumble();

  void* pixels;
  int pitch;
  if(SDL_LockTexture(app.texture, NULL, &pixels, &pitch)) {
    tg_game_get_pixels(app.game, pixels);
    SDL_UnlockTexture(app.texture);
  }

  static int16_t buffer[2048 * 2];
  tg_game_get_samples(app.game, buffer, samples);
  // Keep at most ~6 frames queued so latency can't build up.
  if(SDL_GetAudioStreamQueued(app.audio) < 6 * 800 * 4) {
    SDL_PutAudioStreamData(app.audio, buffer, samples * 4);
  }
}

int main(int argc, char** argv) {
  SDL_SetAppMetadata("Top Gear", "0.1", "topgear-pc");
  if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Top Gear", SDL_GetError(), NULL);
    return 1;
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

  app.aspect = ASPECT_4_3;
  loadControls();
  if(!SDL_CreateWindowAndRenderer("Top Gear", 1024, 768, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &app.window, &app.renderer)) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Top Gear", SDL_GetError(), NULL);
    return 1;
  }
  SDL_SetWindowMinimumSize(app.window, SNES_W, SNES_H);
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
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
          bool down = e.type == SDL_EVENT_KEY_DOWN;
          if(down && !e.key.repeat) {
            if(e.key.key == SDLK_F11 || (e.key.key == SDLK_RETURN && (e.key.mod & SDL_KMOD_ALT))) {
              app.fullscreen = !app.fullscreen;
              SDL_SetWindowFullscreen(app.window, app.fullscreen);
              break;
            }
            if(e.key.key == SDLK_ESCAPE && app.fullscreen) {
              app.fullscreen = false;
              SDL_SetWindowFullscreen(app.window, false);
              break;
            }
            if(e.key.key == SDLK_F2) { app.aspect = (app.aspect + 1) % ASPECT_COUNT; updateTitle(); break; }
            if(e.key.key == SDLK_F3) { app.integerScale = !app.integerScale; updateTitle(); break; }
            if(e.key.key == SDLK_F4) { app.filter = (app.filter + 1) % FILTER_COUNT; updateTitle(); break; }
            if(e.key.key == SDLK_F5) { app.resetRequested = true; break; }
            if(e.key.key == SDLK_F6 && !app.replaying) { app.smooth = !app.smooth; updateTitle(); break; }
            if(e.key.key == SDLK_F7 && !app.replaying) { app.scaling = !app.scaling; updateTitle(); break; }
            if(e.key.key == SDLK_F8) { app.rumble = !app.rumble; updateTitle(); break; }
            if(e.key.key == SDLK_F9 && !app.replaying) { app.fullView = !app.fullView; updateTitle(); break; }
          }
          uint16_t button = keyToButton(e.key.key);
          if(button && !(e.key.mod & SDL_KMOD_ALT)) {
            if(down) app.keys[1] |= button;
            else app.keys[1] &= ~button;
          }
          break;
        }
      }
    }

    Uint64 now = SDL_GetTicksNS();
    if(now - next > (Uint64) (FRAME_NS * 8) && now > next) next = now;  // resync after a stall
    if(now >= next) {
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
