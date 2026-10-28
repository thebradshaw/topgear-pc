// Launcher: choose your Top Gear ROM, set the optional enhancements, then start the game.
//
// Works with a mouse, the keyboard or a gamepad. The options are the ones topgear.exe keeps
// in settings.txt (in the settings folder), so the game starts with them and its own
// options menu (Tab) still changes them. The chosen ROM is remembered in launcher.txt there.

#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "launcher_start.h"
#include "launcher_logo.h"
#include "rom.h"

#define VIEW_W 1280
#define VIEW_H 720
#define TEXT 16  // 8x8 debug font drawn at twice its size

#ifdef _WIN32
#define GAME_EXE "topgear.exe"
#else
#define GAME_EXE "topgear"
#endif

// --- Options -------------------------------------------------------------------------

typedef enum { OPT_BOOL, OPT_CHOICE, OPT_FULLVIEW } OptKind;

typedef struct {
  const char* group;  // heading shown above the first option of a group
  const char* label;
  const char* key;  // name in settings.txt
  OptKind kind;
  const char* choices[3];
  int count;
  const char* help;
  const char* hotkey;
} Option;

enum {
  O_FULLSCREEN, O_VSYNC, O_ASPECT, O_FILTER, O_INTEGER, O_COLOUR,
  O_WIDE, O_SIDE, O_FULLVIEW, O_MAP, O_SPEED,
  O_SMOOTH, O_HIGHFPS, O_GHOST, O_AUTOSAVE, O_RUMBLE, O_COUNT
};

static const Option kOptions[O_COUNT] = {
  {"Display", "Fullscreen", "fullscreen", OPT_BOOL, {"Off", "On"}, 2,
   "Start in fullscreen. Esc leaves it while you play.", "F11"},
  {NULL, "VSync", "vsync", OPT_BOOL, {"Off", "On"}, 2,
   "Wait for the display's refresh before showing each picture, so it never tears. The game runs at the same speed either way.", "Tab menu"},
  {NULL, "Aspect", "aspect", OPT_CHOICE, {"4:3 TV", "8:7 pixels"}, 2,
   "4:3 is the shape a TV showed. 8:7 keeps the SNES pixels square.", "F2"},
  {NULL, "Filter", "filter", OPT_CHOICE, {"Sharp", "Smooth", "Scanlines"}, 3,
   "Sharp pixels, sharp-bilinear (smooth edges without uneven pixels), or TV scanlines.", "F4"},
  {NULL, "Integer scaling", "integer_scale", OPT_BOOL, {"Off", "On"}, 2,
   "Scale the picture by whole numbers only, for perfectly even pixels.", "F3"},
  {NULL, "Colours", "tv_colour", OPT_CHOICE, {"Exact", "As on a TV"}, 2,
   "Exact SNES colours, or the deeper, richer colours a CRT TV showed.", "Tab menu"},
  {"Widescreen", "Widescreen", "wide", OPT_BOOL, {"Off", "On"}, 2,
   "Races fill a wide screen: the road and scenery carry on past the original edges and the HUD moves out to the sides.", "F10"},
  {NULL, "2P side by side", "side_by_side", OPT_BOOL, {"Off", "On"}, 2,
   "With widescreen on, the two players' views sit side by side, each the full height of the screen.", "F12"},
  {"Full-screen view", "1P full view", "full_view", OPT_FULLVIEW, {"Off", "On", "Tall road"}, 3,
   "In 1-player races your view fills the whole screen instead of sharing it with the computer's. Tall road makes the road fill more of it.", "F9"},
  {NULL, "Map", "map_top", OPT_CHOICE, {"Bottom", "Top"}, 2,
   "Where the course map sits in full view: bottom left, or top left.", "Tab menu"},
  {NULL, "Speed and time", "speed_bottom", OPT_CHOICE, {"Top", "Bottom"}, 2,
   "Where the speedometer and timer sit in full view: top right, or the bottom right corner.", "Tab menu"},
  {"Motion", "Smooth motion", "smooth", OPT_BOOL, {"Off", "On"}, 2,
   "The race moves every frame instead of every third, so the road and cars glide instead of stepping.", "F6"},
  {NULL, "High frame rate", "high_fps", OPT_BOOL, {"Off", "On"}, 2,
   "On a 120 Hz or faster display, extra pictures are drawn between frames. Turns on smooth motion too.", "Shift+F6"},
  {"Extras", "Ghost car", "ghost", OPT_BOOL, {"Off", "On"}, 2,
   "Race against a see-through replay of your best race on each course.", "F1"},
  {NULL, "Auto-save", "auto_save", OPT_BOOL, {"Off", "On"}, 2,
   "Remember the countries you reach, so you don't need passwords.", "Shift+F1"},
  {NULL, "Rumble", "rumble", OPT_BOOL, {"Off", "On"}, 2,
   "Gamepad rumble when your car hits something and when nitro fires.", "F8"},
};

// Focus order: the ROM button, every option, then Play.
enum { F_ROM = 0, F_OPTION0 = 1, F_PLAY = 1 + O_COUNT, F_COUNT };

static struct {
  SDL_Window* window;
  SDL_Renderer* renderer;
  SDL_Gamepad* pads[4];
  int value[O_COUNT];
  char settingsPath[2400];
  char launcherPath[2400];
  char extra[4096];  // settings.txt lines the launcher doesn't know, kept as they were
  char romPath[2048];
  bool romOk;
  char romStatus[160];
  int focus;
  int hover;  // option row under the mouse, or -1
  bool dialogOpen;
  SDL_Mutex* lock;
  char picked[2048];  // set by the file dialog's callback
  bool pickedReady;
  char message[256];  // a passing note at the bottom
  Uint64 messageUntil;
  float scroll;
  Uint64 heldSince;
  int heldDir;
  Uint64 nextRepeat;
  SDL_Texture* logo;  // logo.bmp next to the launcher, when there is one
  SDL_Texture* playWord;  // "PLAY", drawn in the logo's style
} L;

// --- Settings ------------------------------------------------------------------------

static int optionIndex(const char* key) {
  for(int i = 0; i < O_COUNT; i++) if(strcmp(kOptions[i].key, key) == 0) return i;
  return -1;
}

static void loadSettings(void) {
  L.value[O_FILTER] = 0;
  L.value[O_VSYNC] = 1;  // on unless turned off
  char* text = SDL_LoadFile(L.settingsPath, NULL);
  if(text == NULL) return;
  bool tall = false;
  size_t used = 0;
  for(char* line = text; line && *line; ) {
    char* next = strchr(line, '\n');
    if(next) *next++ = 0;
    char name[32];
    int v;
    if(sscanf(line, "%31s %d", name, &v) == 2) {
      int i = optionIndex(name);
      if(i >= 0) L.value[i] = v < 0 ? 0 : v >= kOptions[i].count ? kOptions[i].count - 1 : v;
      else if(strcmp(name, "tall_road") == 0) tall = v != 0;
      else if(used + strlen(line) + 2 < sizeof(L.extra)) used += (size_t) snprintf(L.extra + used, sizeof(L.extra) - used, "%s\n", line);
    }
    line = next;
  }
  if(L.value[O_FULLVIEW] && tall) L.value[O_FULLVIEW] = 2;
  SDL_free(text);
}

static void saveSettings(void) {
  if(!L.settingsPath[0]) return;
  FILE* f = fopen(L.settingsPath, "w");
  if(f == NULL) return;
  for(int i = 0; i < O_COUNT; i++) {
    if(i == O_FULLVIEW) fprintf(f, "full_view %d\ntall_road %d\n", L.value[i] != 0, L.value[i] == 2);
    else fprintf(f, "%s %d\n", kOptions[i].key, L.value[i]);
  }
  fputs(L.extra, f);
  fclose(f);
}

static void saveLauncher(void) {
  FILE* f = L.launcherPath[0] ? fopen(L.launcherPath, "w") : NULL;
  if(f == NULL) return;
  fprintf(f, "rom %s\n", L.romPath);
  fclose(f);
}

// --- ROM -----------------------------------------------------------------------------

static const char* baseName(const char* path) {
  const char* b = path;
  for(const char* p = path; *p; p++) if(*p == '/' || *p == '\\') b = p + 1;
  return b;
}

static bool checkRom(const char* path, bool remember) {
  TgRom rom;
  TgRomStatus status = tg_rom_load(path, &rom);
  if(status == TG_ROM_OK) {
    tg_rom_free(&rom);
    snprintf(L.romPath, sizeof(L.romPath), "%s", path);
    L.romOk = true;
    snprintf(L.romStatus, sizeof(L.romStatus), "Verified: Top Gear (USA)");
    if(remember) saveLauncher();
    return true;
  }
  if(remember) {
    // Keep showing a good ROM if one was already chosen; say what was wrong with this one.
    const char* why = status == TG_ROM_WRONG_HASH ? "That file isn't an unmodified Top Gear (USA) ROM."
                    : status == TG_ROM_WRONG_SIZE ? "That file is the wrong size for Top Gear (USA)."
                    : "That file couldn't be read.";
    snprintf(L.message, sizeof(L.message), "%s", why);
    L.messageUntil = SDL_GetTicks() + 6000;
    if(!L.romOk) {
      snprintf(L.romPath, sizeof(L.romPath), "%s", path);
      snprintf(L.romStatus, sizeof(L.romStatus), "%s", why);
    }
  }
  tg_rom_free(&rom);
  return false;
}

static void findRom(void) {
  char* text = L.launcherPath[0] ? SDL_LoadFile(L.launcherPath, NULL) : NULL;
  if(text) {
    char* nl = strpbrk(text, "\r\n");
    if(nl) *nl = 0;
    bool found = strncmp(text, "rom ", 4) == 0 && text[4] && checkRom(text + 4, false);
    SDL_free(text);
    if(found) return;
  }
  static const char* names[] = {"Top Gear (USA).sfc", "Top Gear (USA).smc", "topgear.sfc"};
  const char* base = SDL_GetBasePath();
  for(size_t i = 0; i < SDL_arraysize(names); i++) {
    char path[2048];
    snprintf(path, sizeof(path), "%s%s", base ? base : "", names[i]);
    if(checkRom(path, false)) return;
  }
  L.romPath[0] = 0;
  snprintf(L.romStatus, sizeof(L.romStatus), "Choose your Top Gear (USA) ROM to play.");
}

static void SDLCALL onPicked(void* user, const char* const* files, int filter) {
  (void) user;
  (void) filter;
  SDL_LockMutex(L.lock);
  if(files && files[0]) {
    snprintf(L.picked, sizeof(L.picked), "%s", files[0]);
    L.pickedReady = true;
  }
  L.dialogOpen = false;
  SDL_UnlockMutex(L.lock);
}

static void chooseRom(void) {
  if(L.dialogOpen) return;
  static const SDL_DialogFileFilter filters[] = {{"SNES ROM", "sfc;smc"}, {"All files", "*"}};
  L.dialogOpen = true;
  SDL_ShowOpenFileDialog(onPicked, NULL, L.window, filters, 2, L.romOk ? L.romPath : NULL, false);
}

// --- Launching -----------------------------------------------------------------------

static bool launch(void) {
  if(!L.romOk) {
    snprintf(L.message, sizeof(L.message), "Choose your Top Gear (USA) ROM first.");
    L.messageUntil = SDL_GetTicks() + 4000;
    L.focus = F_ROM;
    return false;
  }
  saveSettings();
  char exe[2400];
  const char* base = SDL_GetBasePath();
  snprintf(exe, sizeof(exe), "%s%s", base ? base : "", GAME_EXE);
  const char* args[] = {exe, L.romPath, NULL};
  // No standard input or output: the launcher is a windowed program and has none to pass
  // on (on Windows, asking to inherit them makes starting the game fail).
  SDL_PropertiesID props = SDL_CreateProperties();
  SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void*) args);
  SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
  SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL);
  SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
  SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
  SDL_Process* p = SDL_CreateProcessWithProperties(props);
  SDL_DestroyProperties(props);
  if(p == NULL) {
    snprintf(L.message, sizeof(L.message), "Couldn't start %s (%s).", GAME_EXE, SDL_GetError());
    L.messageUntil = SDL_GetTicks() + 6000;
    return false;
  }
  SDL_DestroyProcess(p);  // the game keeps running on its own
  return true;
}

// --- Changing options ----------------------------------------------------------------

static bool optionEnabled(int i) {
  if(i == O_SIDE) return L.value[O_WIDE] != 0;
  if(i == O_MAP || i == O_SPEED) return L.value[O_FULLVIEW] != 0;
  return true;
}

static void change(int i, int dir) {
  if(!optionEnabled(i)) return;
  int n = kOptions[i].count;
  L.value[i] = (L.value[i] + (dir < 0 ? n - 1 : 1)) % n;
  // High frame rate needs smooth motion.
  if(i == O_HIGHFPS && L.value[i]) L.value[O_SMOOTH] = 1;
  if(i == O_SMOOTH && !L.value[i]) L.value[O_HIGHFPS] = 0;
  saveSettings();
}

static void activate(int dir) {
  if(L.focus == F_ROM) chooseRom();
  else if(L.focus == F_PLAY) { if(launch()) { SDL_Event q = {.type = SDL_EVENT_QUIT}; SDL_PushEvent(&q); } }
  else change(L.focus - F_OPTION0, dir);
}

static void moveFocus(int dir) {
  L.focus = (L.focus + dir + F_COUNT) % F_COUNT;
}

// --- Drawing -------------------------------------------------------------------------

typedef struct { Uint8 r, g, b; } Rgb;
static const Rgb kBg = {14, 16, 24}, kPanel = {26, 29, 42}, kPanelHi = {40, 44, 64};
static const Rgb kText = {236, 238, 244}, kDim = {140, 146, 166}, kFaint = {84, 90, 110};
static const Rgb kAccent = {255, 92, 56}, kAccent2 = {255, 196, 64}, kGood = {96, 214, 140}, kBad = {255, 110, 110};

static void color(Rgb c, Uint8 a) { SDL_SetRenderDrawColor(L.renderer, c.r, c.g, c.b, a); }

static void fill(float x, float y, float w, float h, Rgb c, Uint8 a) {
  color(c, a);
  SDL_FRect r = {x, y, w, h};
  SDL_RenderFillRect(L.renderer, &r);
}

// A panel with softly cut corners.
static void panel(float x, float y, float w, float h, Rgb c, Uint8 a) {
  fill(x + 3, y, w - 6, h, c, a);
  fill(x, y + 3, 3, h - 6, c, a);
  fill(x + w - 3, y + 3, 3, h - 6, c, a);
  fill(x + 1, y + 1, 2, 2, c, a);
  fill(x + w - 3, y + 1, 2, 2, c, a);
  fill(x + 1, y + h - 3, 2, 2, c, a);
  fill(x + w - 3, y + h - 3, 2, 2, c, a);
}

static void text(float x, float y, float scale, Rgb c, const char* s) {
  SDL_SetRenderScale(L.renderer, scale, scale);
  color((Rgb) {0, 0, 0}, 140);
  SDL_RenderDebugText(L.renderer, (x + scale * 0.5f) / scale, (y + scale * 0.5f) / scale, s);
  color(c, 255);
  SDL_RenderDebugText(L.renderer, x / scale, y / scale, s);
  SDL_SetRenderScale(L.renderer, 1, 1);
}

static float textWidth(const char* s, float scale) { return (float) strlen(s) * 8 * scale; }

// Wrapped text within width; returns the height used.
static float wrapped(float x, float y, float w, float scale, Rgb c, const char* s) {
  int perLine = (int) (w / (8 * scale));
  float y0 = y;
  while(*s) {
    int n = (int) strlen(s);
    if(n > perLine) {
      n = perLine;
      while(n > 0 && s[n] != ' ') n--;
      if(n == 0) n = perLine;
    }
    char line[256];
    snprintf(line, sizeof(line), "%.*s", n, s);
    text(x, y, scale, c, line);
    y += 8 * scale + 6;
    s += n;
    while(*s == ' ') s++;
  }
  return y - y0;
}

// Rows of the option list: a heading row before each group.
static float optionY(int i) {
  float y = 0;
  for(int k = 0; k <= i; k++) {
    if(kOptions[k].group) y += 34;
    if(k < i) y += 28;
  }
  return y;
}

#define LIST_X 530.0f
#define LIST_Y 100.0f
#define LIST_W 710.0f
#define LIST_H 592.0f
#define LEFT_X 40.0f
#define LEFT_W 440.0f
#define ROMBTN_Y 236.0f
#define PLAY_Y 592.0f

// The logo (assets/logo.bmp, built into the launcher; magenta is see-through). A logo.bmp
// next to the launcher replaces it.
static void loadLogo(void) {
  char path[2400];
  const char* base = SDL_GetBasePath();
  snprintf(path, sizeof(path), "%slogo.bmp", base ? base : "");
  SDL_Surface* s = SDL_LoadBMP(path);
  if(s == NULL) s = SDL_LoadBMP_IO(SDL_IOFromConstMem(kLogoBmp, sizeof(kLogoBmp)), true);
  if(s == NULL) return;
  SDL_SetSurfaceColorKey(s, true, SDL_MapSurfaceRGB(s, 255, 0, 255));
  L.logo = SDL_CreateTextureFromSurface(L.renderer, s);
  SDL_DestroySurface(s);
  if(L.logo) SDL_SetTextureScaleMode(L.logo, SDL_SCALEMODE_LINEAR);
}

// The Start button's word, drawn in the spirit of the Top Gear logo (launcher_start.h).
static SDL_Texture* startTexture(void) {
  SDL_Surface* s = SDL_CreateSurfaceFrom(START_ART_W, START_ART_H, SDL_PIXELFORMAT_RGBA32, (void*) kStartArt, START_ART_W * 4);
  if(s == NULL) return NULL;
  SDL_Texture* tex = SDL_CreateTextureFromSurface(L.renderer, s);
  SDL_DestroySurface(s);
  if(tex) {
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
  }
  return tex;
}

static void drawRoad(Uint64 t) {
  // A little road running to the horizon in the header, its stripes scrolling like a race.
  const float cx = 700, top = 30, rows = 56;
  for(int y = 0; y < rows; y++) {
    float z = (y + 1) / rows;  // nearer towards the bottom
    float half = 6 + z * z * 260;
    int band = (int) (1.0f / (z + 0.05f) * 3 - (float) t / 90.0f) & 1;
    float edge = 1 + 5 * z;
    fill(cx - half, top + y, half * 2, 1, band ? (Rgb) {58, 62, 80} : (Rgb) {48, 52, 68}, 255);
    fill(cx - half - edge, top + y, edge, 1, band ? kAccent : kText, 255);
    fill(cx + half, top + y, edge, 1, band ? kAccent : kText, 255);
    if(band) fill(cx - edge / 2, top + y, edge, 1, kAccent2, 255);
  }
}

static void draw(void) {
  Uint64 t = SDL_GetTicks();
  color(kBg, 255);
  SDL_RenderClear(L.renderer);
  // Header
  for(int y = 0; y < 86; y++) fill(0, (float) y, VIEW_W, 1, (Rgb) {(Uint8) (20 + y / 4), (Uint8) (22 + y / 6), (Uint8) (36 + y / 3)}, 255);
  if(L.logo) {
    float lw, lh;
    SDL_GetTextureSize(L.logo, &lw, &lh);
    SDL_FRect r = {LEFT_X - 8, 5, 78 * lw / lh, 78};
    SDL_RenderTexture(L.renderer, L.logo, NULL, &r);
  } else {
    text(LEFT_X, 22, 5, kAccent, "TOP GEAR");
  }
  drawRoad(t);
  fill(0, 86, VIEW_W, 2, kAccent, 255);

  // ROM card
  panel(LEFT_X, 110, LEFT_W, 172, kPanel, 255);
  text(LEFT_X + 20, 126, 2, kDim, "GAME ROM");
  char name[40];
  const char* shown = L.romPath[0] ? baseName(L.romPath) : "None chosen";
  if(strlen(shown) > 19) snprintf(name, sizeof(name), "%.18s~", shown);
  else snprintf(name, sizeof(name), "%s", shown);
  text(LEFT_X + 20, 152, 2, kText, name);
  wrapped(LEFT_X + 20, 180, LEFT_W - 40, 2, L.romOk ? kGood : kBad, L.romStatus);
  {
    bool f = L.focus == F_ROM;
    panel(LEFT_X + 20, ROMBTN_Y, LEFT_W - 40, 32, f ? kAccent : kPanelHi, 255);
    const char* label = L.dialogOpen ? "Choosing..." : L.romOk ? "Change ROM" : "Choose ROM";
    text(LEFT_X + 20 + (LEFT_W - 40 - textWidth(label, 2)) / 2, ROMBTN_Y + 8, 2, kText, label);
  }

  // Help for the focused option
  panel(LEFT_X, 300, LEFT_W, 272, kPanel, 255);
  if(L.focus >= F_OPTION0 && L.focus < F_PLAY) {
    const Option* o = &kOptions[L.focus - F_OPTION0];
    text(LEFT_X + 20, 318, 2, kAccent2, o->label);
    float h = wrapped(LEFT_X + 20, 352, LEFT_W - 40, 2, kText, o->help);
    char hk[64];
    snprintf(hk, sizeof(hk), "In game: %s", o->hotkey);
    text(LEFT_X + 20, 352 + h + 10, 2, kDim, hk);
  } else if(L.focus == F_ROM) {
    text(LEFT_X + 20, 318, 2, kAccent2, "Your ROM");
    wrapped(LEFT_X + 20, 352, LEFT_W - 40, 2, kText,
      "The game runs from your own unmodified Top Gear (USA) cartridge dump (.sfc or .smc). You can also drop the file on this window.");
  } else {
    text(LEFT_X + 20, 318, 2, kAccent2, "Ready");
    wrapped(LEFT_X + 20, 352, LEFT_W - 40, 2, kText,
      "Starts the game with these options. Tab (or the gamepad's guide button) opens the options menu while you play.");
  }

  // Play: a dark button with a thin orange edge, the word in the logo's style
  {
    bool f = L.focus == F_PLAY;
    float pulse = f ? 0.5f + 0.5f * SDL_sinf((float) t / 260.0f) : 0;
    panel(LEFT_X, PLAY_Y, LEFT_W, 80, f ? kPanelHi : kPanel, 255);
    Rgb edge = !L.romOk ? kFaint : f ? (Rgb) {255, (Uint8) (120 + 76 * pulse), 64} : kAccent;
    color(edge, 255);
    for(int k = 0; k < (f ? 3 : 2); k++) {
      SDL_FRect r = {LEFT_X + k, PLAY_Y + k, LEFT_W - 2 * k, 80 - 2.0f * k};
      SDL_RenderRect(L.renderer, &r);
    }
    if(L.playWord) {
      // Greyed out until there is a ROM to play.
      Uint8 v = L.romOk ? 255 : 110;
      SDL_SetTextureColorMod(L.playWord, v, v, v);
      SDL_FRect r = {LEFT_X + (LEFT_W - START_ART_W) / 2, PLAY_Y + (80 - START_ART_H) / 2.0f + 1, START_ART_W, START_ART_H};
      SDL_RenderTexture(L.renderer, L.playWord, NULL, &r);
    }
  }

  // Options list (scrolls to keep the focused row in view)
  float total = optionY(O_COUNT - 1) + 30;
  float want = L.scroll;
  if(L.focus >= F_OPTION0 && L.focus < F_PLAY) {
    float fy = optionY(L.focus - F_OPTION0);
    if(fy - 40 < want) want = fy - 40;
    if(fy + 30 > want + LIST_H) want = fy + 30 - LIST_H;
  }
  if(want > total - LIST_H) want = total - LIST_H;
  if(want < 0) want = 0;
  L.scroll += (want - L.scroll) * 0.25f;
  for(int i = 0; i < O_COUNT; i++) {
    const Option* o = &kOptions[i];
    float y = LIST_Y + optionY(i) - L.scroll;
    if(y < LIST_Y - 2 || y + 26 > LIST_Y + LIST_H) continue;  // only whole rows
    if(o->group && y - 24 >= LIST_Y - 30) {
      text(LIST_X, y - 24, 2, kAccent, o->group);
      fill(LIST_X + textWidth(o->group, 2) + 10, y - 19, LIST_W - textWidth(o->group, 2) - 10, 1, kFaint, 255);
    }
    bool f = L.focus == F_OPTION0 + i, on = optionEnabled(i);
    if(f) panel(LIST_X - 4, y - 2, LIST_W + 8, 28, kPanelHi, 255);
    else if(L.hover == i) panel(LIST_X - 4, y - 2, LIST_W + 8, 28, kPanel, 255);
    if(f) fill(LIST_X - 4, y - 2, 3, 28, kAccent, 255);
    text(LIST_X + 8, y + 4, 2, on ? kText : kFaint, o->label);
    // The value as a pill, with arrows when focused.
    const char* v = o->choices[L.value[i]];
    bool lit = o->kind == OPT_BOOL ? L.value[i] != 0 : o->kind == OPT_FULLVIEW ? L.value[i] != 0 : true;
    float vw = textWidth(v, 2) + 24, vx = LIST_X + LIST_W - vw - 24;
    panel(vx, y + 1, vw, 22, !on ? kPanel : lit && o->kind != OPT_CHOICE ? (Rgb) {38, 92, 64} : (Rgb) {52, 56, 80}, 255);
    text(vx + 12, y + 4, 2, !on ? kFaint : lit && o->kind != OPT_CHOICE ? kGood : kText, v);
    if(f && on) {
      text(vx - 18, y + 4, 2, kAccent2, "<");
      text(LIST_X + LIST_W - 16, y + 4, 2, kAccent2, ">");
    }
  }

  // Footer: controls, or a passing note
  if(L.message[0] && t < L.messageUntil) {
    text(LEFT_X, VIEW_H - 24, 2, kBad, L.message);
  } else {
    bool pad = L.pads[0] || L.pads[1] || L.pads[2] || L.pads[3];
    text(LEFT_X, VIEW_H - 24, 2, kFaint, pad ? "D-pad: choose   A: change   Start: play   B: quit"
                                              : "Arrows: choose   Enter: change   Space: start   Esc: quit");
  }
}

// --- Input ---------------------------------------------------------------------------

static int hitTest(float x, float y) {
  if(x >= LEFT_X + 20 && x < LEFT_X + LEFT_W - 20 && y >= ROMBTN_Y && y < ROMBTN_Y + 32) return F_ROM;
  if(x >= LEFT_X && x < LEFT_X + LEFT_W && y >= PLAY_Y && y < PLAY_Y + 80) return F_PLAY;
  if(x >= LIST_X - 4 && x < LIST_X + LIST_W + 4 && y >= LIST_Y - 4 && y < LIST_Y + LIST_H + 4) {
    for(int i = 0; i < O_COUNT; i++) {
      float ry = LIST_Y + optionY(i) - L.scroll;
      if(y >= ry - 2 && y < ry + 26) return F_OPTION0 + i;
    }
  }
  return -1;
}

static void openPad(SDL_JoystickID id) {
  for(int i = 0; i < 4; i++) if(L.pads[i] == NULL) { L.pads[i] = SDL_OpenGamepad(id); return; }
}

static void closePad(SDL_JoystickID id) {
  for(int i = 0; i < 4; i++) if(L.pads[i] && SDL_GetGamepadID(L.pads[i]) == id) { SDL_CloseGamepad(L.pads[i]); L.pads[i] = NULL; }
}

// Held directions (stick or d-pad) repeat.
static int padDirection(void) {
  int dir = 0;
  for(int i = 0; i < 4; i++) {
    if(L.pads[i] == NULL) continue;
    Sint16 y = SDL_GetGamepadAxis(L.pads[i], SDL_GAMEPAD_AXIS_LEFTY);
    Sint16 x = SDL_GetGamepadAxis(L.pads[i], SDL_GAMEPAD_AXIS_LEFTX);
    if(SDL_GetGamepadButton(L.pads[i], SDL_GAMEPAD_BUTTON_DPAD_UP) || y < -16000) dir = 1;
    else if(SDL_GetGamepadButton(L.pads[i], SDL_GAMEPAD_BUTTON_DPAD_DOWN) || y > 16000) dir = 2;
    else if(SDL_GetGamepadButton(L.pads[i], SDL_GAMEPAD_BUTTON_DPAD_LEFT) || x < -16000) dir = 3;
    else if(SDL_GetGamepadButton(L.pads[i], SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || x > 16000) dir = 4;
    if(dir) break;
  }
  return dir;
}

static void direction(int dir) {
  if(dir == 1) moveFocus(-1);
  else if(dir == 2) moveFocus(1);
  else if(L.focus >= F_OPTION0 && L.focus < F_PLAY) change(L.focus - F_OPTION0, dir == 3 ? -1 : 1);
}

static void pollPadRepeat(void) {
  int dir = padDirection();
  Uint64 now = SDL_GetTicks();
  if(dir != L.heldDir) {
    L.heldDir = dir;
    if(dir) { direction(dir); L.nextRepeat = now + 380; }
  } else if(dir && now >= L.nextRepeat && dir <= 2) {
    direction(dir);
    L.nextRepeat = now + 110;
  }
}

int main(int argc, char** argv) {
  (void) argc;
  (void) argv;
  SDL_SetAppMetadata("Top Gear Launcher", "0.1", "topgear-pc");
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "0");
  if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Top Gear", SDL_GetError(), NULL);
    return 1;
  }
  L.lock = SDL_CreateMutex();
  L.hover = -1;
  char* pref = SDL_GetPrefPath("topgear-pc", "Top Gear");
  if(pref) {
    snprintf(L.settingsPath, sizeof(L.settingsPath), "%ssettings.txt", pref);
    snprintf(L.launcherPath, sizeof(L.launcherPath), "%slauncher.txt", pref);
    SDL_free(pref);
  }
  loadSettings();
  findRom();
  L.focus = L.romOk ? F_PLAY : F_ROM;

  if(!SDL_CreateWindowAndRenderer("Top Gear", VIEW_W, VIEW_H, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &L.window, &L.renderer)) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Top Gear", SDL_GetError(), NULL);
    return 1;
  }
  SDL_SetRenderVSync(L.renderer, 1);
  SDL_SetRenderLogicalPresentation(L.renderer, VIEW_W, VIEW_H, SDL_LOGICAL_PRESENTATION_LETTERBOX);
  SDL_SetRenderDrawBlendMode(L.renderer, SDL_BLENDMODE_BLEND);
  loadLogo();
  L.playWord = startTexture();

  // For checking the layout without a display: SDL_VIDEO_DRIVER=offscreen
  // TG_LAUNCHER_SHOT=out.bmp [TG_LAUNCHER_FOCUS=n] saves a picture and quits.
  const char* shot = SDL_getenv("TG_LAUNCHER_SHOT");
  if(SDL_getenv("TG_LAUNCHER_FOCUS")) L.focus = SDL_atoi(SDL_getenv("TG_LAUNCHER_FOCUS"));
  int frames = 0;
  bool running = true;
  while(running) {
    SDL_Event e;
    while(SDL_PollEvent(&e)) {
      SDL_ConvertEventToRenderCoordinates(L.renderer, &e);
      switch(e.type) {
        case SDL_EVENT_QUIT: running = false; break;
        case SDL_EVENT_GAMEPAD_ADDED: openPad(e.gdevice.which); break;
        case SDL_EVENT_GAMEPAD_REMOVED: closePad(e.gdevice.which); break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
          if(L.dialogOpen) break;
          switch(e.gbutton.button) {
            case SDL_GAMEPAD_BUTTON_SOUTH: activate(1); break;
            case SDL_GAMEPAD_BUTTON_START: L.focus = F_PLAY; activate(1); break;
            case SDL_GAMEPAD_BUTTON_EAST: running = false; break;
            case SDL_GAMEPAD_BUTTON_WEST: chooseRom(); break;
            case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: L.focus = F_ROM; break;
            case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: L.focus = F_PLAY; break;
          }
          break;
        case SDL_EVENT_KEY_DOWN:
          if(L.dialogOpen) break;
          switch(e.key.key) {
            case SDLK_UP: moveFocus(-1); break;
            case SDLK_DOWN: case SDLK_TAB: moveFocus(1); break;
            case SDLK_LEFT: direction(3); break;
            case SDLK_RIGHT: direction(4); break;
            case SDLK_RETURN: case SDLK_KP_ENTER: activate(1); break;
            case SDLK_SPACE: L.focus = F_PLAY; activate(1); break;
            case SDLK_ESCAPE: running = false; break;
          }
          break;
        case SDL_EVENT_MOUSE_MOTION: {
          // Only a mouse that is really moving takes the focus (not one resting where
          // the window opened, while a gamepad is in use).
          static bool seen;
          static float lastX, lastY;
          bool moved = seen && (SDL_fabsf(e.motion.x - lastX) + SDL_fabsf(e.motion.y - lastY)) > 2;
          if(!seen || moved) { seen = true; lastX = e.motion.x; lastY = e.motion.y; }
          if(!moved) break;
          int h = hitTest(e.motion.x, e.motion.y);
          L.hover = h >= F_OPTION0 && h < F_PLAY ? h - F_OPTION0 : -1;
          if(h >= 0) L.focus = h;
          break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
          if(L.dialogOpen) break;
          int h = hitTest(e.button.x, e.button.y);
          if(h >= 0) {
            L.focus = h;
            activate(e.button.button == SDL_BUTTON_RIGHT ? -1 : 1);
          }
          break;
        }
        case SDL_EVENT_MOUSE_WHEEL:
          L.scroll -= e.wheel.y * 30;
          break;
        case SDL_EVENT_DROP_FILE:
          if(e.drop.data) checkRom(e.drop.data, true);
          break;
      }
    }
    SDL_LockMutex(L.lock);
    if(L.pickedReady) {
      L.pickedReady = false;
      char path[2048];
      snprintf(path, sizeof(path), "%s", L.picked);
      SDL_UnlockMutex(L.lock);
      if(checkRom(path, true)) L.focus = F_PLAY;
    } else {
      SDL_UnlockMutex(L.lock);
    }
    if(!L.dialogOpen) pollPadRepeat();
    draw();
    if(shot && ++frames == 30) {
      SDL_Surface* pic = SDL_RenderReadPixels(L.renderer, NULL);
      if(pic) { SDL_SaveBMP(pic, shot); SDL_DestroySurface(pic); }
      running = false;
    }
    SDL_RenderPresent(L.renderer);
  }
  for(int i = 0; i < 4; i++) if(L.pads[i]) SDL_CloseGamepad(L.pads[i]);
  SDL_Quit();
  return 0;
}
