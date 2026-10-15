// Windows (and desktop) frontend: window, fullscreen, scaling, keyboard, gamepads, audio.

#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "game.h"
#include "rom.h"

#define SNES_W 256
#define SNES_H 224
// NTSC SNES: 21.477272 MHz master clock / (1364 * 262 clocks per frame).
#define FRAME_NS (1000000000.0 * 1364.0 * 262.0 / 21477272.0)
#define AUDIO_RATE 48000

typedef enum { ASPECT_4_3, ASPECT_8_7, ASPECT_COUNT } AspectMode;
static const char* aspectNames[] = {"4:3 (TV)", "8:7 (square pixels)"};

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
  uint16_t keys[3];  // keyboard state per player
} app;

static const char* defaultRomNames[] = {"Top Gear (USA).sfc", "Top Gear (USA).smc", "topgear.sfc"};

static bool findRom(int argc, char** argv, TgRom* rom, char* message, size_t messageSize) {
  if(argc > 1) {
    TgRomStatus status = tg_rom_load(argv[1], rom);
    if(status == TG_ROM_OK) return true;
    snprintf(message, messageSize, "%s\n\n%s\n\nSHA-256 of your file:\n%s", argv[1], tg_rom_status_message(status), rom->sha256);
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

static uint16_t keyToButton(SDL_Keycode key) {
  switch(key) {
    case SDLK_UP: return TG_BTN_UP;
    case SDLK_DOWN: return TG_BTN_DOWN;
    case SDLK_LEFT: return TG_BTN_LEFT;
    case SDLK_RIGHT: return TG_BTN_RIGHT;
    case SDLK_Z: return TG_BTN_B;
    case SDLK_X: return TG_BTN_A;
    case SDLK_A: return TG_BTN_Y;
    case SDLK_S: return TG_BTN_X;
    case SDLK_Q: return TG_BTN_L;
    case SDLK_W: return TG_BTN_R;
    case SDLK_RETURN: return TG_BTN_START;
    case SDLK_RSHIFT: return TG_BTN_SELECT;
  }
  return 0;
}

static uint16_t padButtons(SDL_Gamepad* pad) {
  if(pad == NULL) return 0;
  uint16_t b = 0;
  // Positional mapping: the SNES layout matches an Xbox/PlayStation pad by position.
  if(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH)) b |= TG_BTN_B;
  if(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_EAST)) b |= TG_BTN_A;
  if(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_WEST)) b |= TG_BTN_Y;
  if(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_NORTH)) b |= TG_BTN_X;
  if(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) b |= TG_BTN_L;
  if(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) b |= TG_BTN_R;
  if(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_START)) b |= TG_BTN_START;
  if(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_BACK)) b |= TG_BTN_SELECT;
  if(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP)) b |= TG_BTN_UP;
  if(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) b |= TG_BTN_DOWN;
  if(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) b |= TG_BTN_LEFT;
  if(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) b |= TG_BTN_RIGHT;
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
  snprintf(title, sizeof(title), "Top Gear  |  %s%s", aspectNames[app.aspect], app.integerScale ? ", integer scale" : "");
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
  SDL_RenderTexture(app.renderer, app.texture, &src, &dst);
  SDL_RenderPresent(app.renderer);
}

static void runFrame(int samples) {
  tg_game_set_input(app.game, 1, app.keys[1] | padButtons(app.pads[0]));
  tg_game_set_input(app.game, 2, app.keys[2] | padButtons(app.pads[1]));
  tg_game_run_frame(app.game);

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

  TgRom rom;
  char message[4096];
  if(!findRom(argc, argv, &rom, message, sizeof(message))) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Top Gear", message, NULL);
    SDL_Quit();
    return 1;
  }
  app.game = tg_game_create(&rom);
  if(app.game == NULL) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Top Gear", "Failed to start the game.", NULL);
    return 1;
  }

  app.aspect = ASPECT_4_3;
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
            if(e.key.key == SDLK_F5) { tg_game_reset(app.game, false); break; }
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
