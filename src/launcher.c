// Launcher: choose a game, import its files, set the optional enhancements, then start it.
//
// Each game (Top Gear, Top Gear 2, Top Gear 3000) has its own page: the files it needs
// (your own cartridge dumps, plus the DSP-4 chip's firmware for Top Gear 3000), whether
// they are ready, and the options that game supports. Any file chosen or dropped on the
// window is recognised and goes to the page of the game it belongs to.
//
// Works with a mouse, the keyboard or a gamepad. The options are the ones topgear.exe keeps
// in settings.txt (in the settings folder), so the game starts with them and its own
// options menu (Tab) still changes them. The chosen files are remembered in launcher.txt.

#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "launcher_start.h"
#include "launcher_logo.h"
#include "launcher_logo2.h"
#include "launcher_logo3.h"
#include "rom.h"
#include "sha256.h"

#define VIEW_W 1280
#define VIEW_H 720
#define TEXT 16  // 8x8 debug font drawn at twice its size

#ifdef _WIN32
#define GAME_EXE "topgear.exe"
#else
#define GAME_EXE "topgear"
#endif

// Top Gear 3000's DSP-4 chip firmware (8 KB, the usual dsp4.bin / dsp4.rom dump).
#define DSP4_SIZE 8192
#define DSP4_SHA256 "752d03b2d74441e430b7f713001fa241f8bbcfc1a0d890ed4143f174dbe031da"

#define NGAMES TG_GAME_COUNT
static const char* const kTabNames[NGAMES] = {"TOP GEAR", "TOP GEAR 2", "TOP GEAR 3000"};

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
   "Races fill a wide screen: the road and scenery carry on past the original edges.", "F10"},
  {NULL, "2P side by side", "side_by_side", OPT_BOOL, {"Off", "On"}, 2,
   "With widescreen on, the two players' views sit side by side, each the full height of the screen.", "F12"},
  {"Full-screen view", "1P full view", "full_view", OPT_FULLVIEW, {"Off", "On", "Tall road"}, 3,
   "In 1-player races your view fills the whole screen instead of sharing it with the computer's. Tall road makes the road fill more of it.", "F9"},
  {NULL, "Map", "map_top", OPT_CHOICE, {"Bottom", "Top"}, 2,
   "Where the course map sits in full view: bottom left, or top left.", "Tab menu"},
  {NULL, "Speed and time", "speed_bottom", OPT_CHOICE, {"Top", "Bottom"}, 2,
   "Where the speedometer and timer sit in full view: top right, or the bottom right corner.", "Tab menu"},
  {"Motion", "Smooth motion", "smooth", OPT_BOOL, {"Off", "On"}, 2,
   "The race moves every frame instead of every third (in Top Gear 3000, every second frame), so the road and cars glide instead of stepping.", "F6"},
  {NULL, "High frame rate", "high_fps", OPT_BOOL, {"Off", "On"}, 2,
   "On a 120 Hz or faster display, extra pictures are drawn between frames. Turns on smooth motion too.", "Shift+F6"},
  {"Extras", "Ghost car", "ghost", OPT_BOOL, {"Off", "On"}, 2,
   "Race against a see-through replay of your best race on each course.", "F1"},
  {NULL, "Auto-save", "auto_save", OPT_BOOL, {"Off", "On"}, 2,
   "Remember how far you got, so you don't need passwords.", "Shift+F1"},
  {NULL, "Rumble", "rumble", OPT_BOOL, {"Off", "On"}, 2,
   "Gamepad rumble when your car hits something and when nitro fires.", "F8"},
};

// Focus order: the game tabs, the file buttons, every option, then Play.
enum { F_TAB = 0, F_ROM, F_DSP, F_OPTION0, F_PLAY = F_OPTION0 + O_COUNT, F_COUNT };

static struct {
  SDL_Window* window;
  SDL_Renderer* renderer;
  SDL_Gamepad* pads[4];
  int value[O_COUNT];
  char settingsPath[2400];
  char launcherPath[2400];
  char extra[4096];  // settings.txt lines the launcher doesn't know, kept as they were
  int game;  // the page shown
  char romPath[NGAMES][2048];
  bool romOk[NGAMES];
  char dspPath[2048];
  bool dspOk;
  int focus;
  int hover;  // option row under the mouse, or -1
  bool dialogOpen;
  SDL_Mutex* lock;
  char picked[2048];  // set by the file dialog's callback
  bool pickedReady;
  char message[256];  // a passing note at the bottom
  bool messageGood;
  Uint64 messageUntil;
  float scroll;
  Uint64 heldSince;
  int heldDir;
  Uint64 nextRepeat;
  SDL_Texture* logo[TG_GAME_COUNT];  // each game's logo (built in, or a logo.bmp / logo2.bmp / logo3.bmp next to the launcher)
  SDL_Texture* playWord;  // "PLAY", drawn in the logo's style
} L;

static void note(bool good, const char* s) {
  snprintf(L.message, sizeof(L.message), "%s", s);
  L.messageGood = good;
  L.messageUntil = SDL_GetTicks() + 6000;
}

static bool gameReady(int g) {
  return L.romOk[g] && (g != TG_GAME_TOPGEAR3000 || L.dspOk);
}

// Which options each game supports: all of them in Top Gear. Top Gear 2 has no full-screen
// view and Top Gear 3000 only the display options, widescreen, smooth motion, the ghost car
// and rumble; high frame rate is Top Gear's only (Chris: smooth motion and VSync are enough).
static bool optionShown(int i) {
  if(L.game == TG_GAME_TOPGEAR2) return i != O_FULLVIEW && i != O_MAP && i != O_SPEED && i != O_HIGHFPS;
  if(L.game == TG_GAME_TOPGEAR3000) return i <= O_COLOUR || i == O_WIDE || i == O_SMOOTH || i == O_GHOST || i == O_RUMBLE;
  return true;
}

static bool focusable(int f) {
  if(f == F_DSP) return L.game == TG_GAME_TOPGEAR3000;
  if(f >= F_OPTION0 && f < F_PLAY) return optionShown(f - F_OPTION0);
  return true;
}

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

// launcher.txt: the page shown last, and each game's files.
static const char* const kRomKeys[NGAMES] = {"rom", "rom2", "rom3"};

static void saveLauncher(void) {
  FILE* f = L.launcherPath[0] ? fopen(L.launcherPath, "w") : NULL;
  if(f == NULL) return;
  fprintf(f, "game %d\n", L.game);
  for(int g = 0; g < NGAMES; g++) if(L.romOk[g]) fprintf(f, "%s %s\n", kRomKeys[g], L.romPath[g]);
  if(L.dspOk) fprintf(f, "dsp4 %s\n", L.dspPath);
  fclose(f);
}

// --- Files ---------------------------------------------------------------------------

static const char* baseName(const char* path) {
  const char* b = path;
  for(const char* p = path; *p; p++) if(*p == '/' || *p == '\\') b = p + 1;
  return b;
}

typedef enum { FILE_ROM, FILE_DSP, FILE_BAD_DSP, FILE_BAD_ROM, FILE_UNREADABLE } FileKind;

// Recognises a file: one of the three ROMs (game set), the DSP-4 firmware, or neither.
static FileKind identify(const char* path, int* game) {
  TgRom rom;
  TgRomStatus status = tg_rom_load_any(path, &rom);
  if(status == TG_ROM_OK) {
    *game = rom.game;
    tg_rom_free(&rom);
    return FILE_ROM;
  }
  tg_rom_free(&rom);
  if(status == TG_ROM_NOT_FOUND || status == TG_ROM_READ_ERROR) return FILE_UNREADABLE;
  size_t size = 0;
  Uint8* data = SDL_LoadFile(path, &size);
  if(data == NULL) return FILE_UNREADABLE;
  FileKind kind = FILE_BAD_ROM;
  if(size == DSP4_SIZE) {
    Uint8 digest[32];
    char hex[65];
    tg_sha256(data, size, digest);
    for(int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", digest[i]);
    kind = strcmp(hex, DSP4_SHA256) == 0 ? FILE_DSP : FILE_BAD_DSP;
  }
  SDL_free(data);
  return kind;
}

// The next thing to do on the shown page: a missing file, else Play.
static int nextStep(void) {
  if(!L.romOk[L.game]) return F_ROM;
  if(L.game == TG_GAME_TOPGEAR3000 && !L.dspOk) return F_DSP;
  return F_PLAY;
}

// A file chosen or dropped: it goes to the game it belongs to, whichever page is shown.
static void importFile(const char* path) {
  int g = 0;
  FileKind kind = identify(path, &g);
  char msg[256];
  if(kind == FILE_ROM) {
    snprintf(L.romPath[g], sizeof(L.romPath[g]), "%s", path);
    L.romOk[g] = true;
    if(g == TG_GAME_TOPGEAR3000 && !L.dspOk) snprintf(msg, sizeof(msg), "%s ROM added. It needs the DSP-4 firmware too.", tg_games[g].name);
    else snprintf(msg, sizeof(msg), "%s ROM added: ready to play.", tg_games[g].name);
    L.game = g;
    note(true, msg);
  } else if(kind == FILE_DSP) {
    snprintf(L.dspPath, sizeof(L.dspPath), "%s", path);
    L.dspOk = true;
    L.game = TG_GAME_TOPGEAR3000;
    note(true, L.romOk[L.game] ? "DSP-4 firmware added: Top Gear 3000 is ready." : "DSP-4 firmware added. Now the Top Gear 3000 ROM.");
  } else {
    note(false, kind == FILE_UNREADABLE ? "That file couldn't be read."
              : kind == FILE_BAD_DSP ? "That 8 KB file isn't the DSP-4 firmware Top Gear 3000 needs."
              : "That isn't an unmodified Top Gear, Top Gear 2 or Top Gear 3000 (USA) file.");
    return;
  }
  saveLauncher();
  L.focus = nextStep();
}

// Quietly picks up a file if it is one the launcher still needs (when starting).
static void consider(const char* path) {
  int g = 0;
  FileKind kind = identify(path, &g);
  if(kind == FILE_ROM && !L.romOk[g]) {
    snprintf(L.romPath[g], sizeof(L.romPath[g]), "%s", path);
    L.romOk[g] = true;
  } else if(kind == FILE_DSP && !L.dspOk) {
    snprintf(L.dspPath, sizeof(L.dspPath), "%s", path);
    L.dspOk = true;
  }
}

static bool allFound(void) {
  for(int g = 0; g < NGAMES; g++) if(!L.romOk[g]) return false;
  return L.dspOk;
}

// The remembered files, then any ROMs or firmware sitting next to the launcher.
static void findFiles(void) {
  L.game = TG_GAME_TOPGEAR;
  char* text = L.launcherPath[0] ? SDL_LoadFile(L.launcherPath, NULL) : NULL;
  if(text) {
    for(char* line = text; line && *line; ) {
      char* next = strpbrk(line, "\r\n");
      if(next) { *next++ = 0; while(*next == '\r' || *next == '\n') next++; }
      char* space = strchr(line, ' ');
      if(space && space[1]) {
        *space = 0;
        const char* value = space + 1;
        if(strcmp(line, "game") == 0) {
          int g = SDL_atoi(value);
          if(g >= 0 && g < NGAMES) L.game = g;
        } else if(strcmp(line, "dsp4") == 0 || strcmp(line, "rom") == 0 || strcmp(line, "rom2") == 0 || strcmp(line, "rom3") == 0) {
          consider(value);
        }
      }
      line = next;
    }
    SDL_free(text);
  }
  const char* base = SDL_GetBasePath();
  if(base && !allFound()) {
    int count = 0;
    char** names = SDL_GlobDirectory(base, "*", SDL_GLOB_CASEINSENSITIVE, &count);
    for(int i = 0; names && i < count && !allFound(); i++) {
      char path[2400];
      snprintf(path, sizeof(path), "%s%s", base, names[i]);
      SDL_PathInfo info;
      if(!SDL_GetPathInfo(path, &info) || info.type != SDL_PATHTYPE_FILE) continue;
      if(info.size != DSP4_SIZE && (info.size < 0x80000 || info.size > 0x100200)) continue;
      consider(path);
    }
    SDL_free(names);
  }
  if(!L.romOk[L.game]) {
    // Show a game that is ready, if any is.
    for(int g = 0; g < NGAMES; g++) if(gameReady(g)) { L.game = g; break; }
  }
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

static void chooseFile(bool dsp) {
  if(L.dialogOpen) return;
  static const SDL_DialogFileFilter romFilters[] = {{"SNES ROM", "sfc;smc"}, {"All files", "*"}};
  static const SDL_DialogFileFilter dspFilters[] = {{"DSP-4 firmware", "bin;rom"}, {"All files", "*"}};
  const char* start = dsp ? (L.dspOk ? L.dspPath : NULL) : (L.romOk[L.game] ? L.romPath[L.game] : NULL);
  L.dialogOpen = true;
  SDL_ShowOpenFileDialog(onPicked, NULL, L.window, dsp ? dspFilters : romFilters, 2, start, false);
}

// --- Launching -----------------------------------------------------------------------

static bool launch(void) {
  if(!gameReady(L.game)) {
    note(false, !L.romOk[L.game] ? "Choose this game's ROM first." : "Choose the DSP-4 firmware first.");
    L.focus = nextStep();
    return false;
  }
  saveSettings();
  saveLauncher();
  char exe[2400];
  const char* base = SDL_GetBasePath();
  snprintf(exe, sizeof(exe), "%s%s", base ? base : "", GAME_EXE);
  const char* args[5] = {exe, L.romPath[L.game], NULL, NULL, NULL};
  if(L.game == TG_GAME_TOPGEAR3000) {
    args[2] = "--dsp";
    args[3] = L.dspPath;
  }
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
    char msg[256];
    snprintf(msg, sizeof(msg), "Couldn't start %s (%s).", GAME_EXE, SDL_GetError());
    note(false, msg);
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

static void setGame(int g) {
  g = (g + NGAMES) % NGAMES;
  if(g == L.game) return;
  L.game = g;
  L.scroll = 0;
  L.hover = -1;
  if(!focusable(L.focus)) L.focus = F_TAB;
  saveLauncher();
}

static void activate(int dir) {
  if(L.focus == F_TAB) setGame(L.game + dir);
  else if(L.focus == F_ROM) chooseFile(false);
  else if(L.focus == F_DSP) chooseFile(true);
  else if(L.focus == F_PLAY) { if(launch()) { SDL_Event q = {.type = SDL_EVENT_QUIT}; SDL_PushEvent(&q); } }
  else change(L.focus - F_OPTION0, dir);
}

static void moveFocus(int dir) {
  do L.focus = (L.focus + dir + F_COUNT) % F_COUNT;
  while(!focusable(L.focus));
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

// Rows of the option list: a heading row before each group that has a shown option.
static bool groupHeading(int i) {
  if(!optionShown(i)) return false;
  for(int k = i; k >= 0; k--) {
    if(k < i && optionShown(k)) return false;
    if(kOptions[k].group) return true;
  }
  return false;
}

static float optionY(int i) {
  float y = 0;
  for(int k = 0; k <= i; k++) {
    if(!optionShown(k)) continue;
    if(groupHeading(k)) y += 34;
    if(k < i) y += 28;
  }
  return y;
}

static const char* groupOf(int i) {
  for(int k = i; k >= 0; k--) if(kOptions[k].group) return kOptions[k].group;
  return "";
}

#define TABS_Y 100.0f
#define TABS_H 34.0f
#define LIST_X 530.0f
#define LIST_Y 172.0f
#define LIST_W 710.0f
#define LIST_H 500.0f
#define LEFT_X 40.0f
#define LEFT_W 440.0f
#define CARD_Y 150.0f
#define PLAY_Y 592.0f

static float tabX(int g) { return LEFT_X + g * ((VIEW_W - 2 * LEFT_X + 12) / NGAMES); }
static float tabW(void) { return (VIEW_W - 2 * LEFT_X + 12) / NGAMES - 12; }
static float cardH(void) { return L.game == TG_GAME_TOPGEAR3000 ? 236.0f : 172.0f; }
// The file buttons: one across the card, or ROM and DSP-4 side by side for Top Gear 3000.
static SDL_FRect fileButton(int f) {
  float y = CARD_Y + cardH() - 46;
  if(L.game != TG_GAME_TOPGEAR3000) return (SDL_FRect) {LEFT_X + 20, y, LEFT_W - 40, 32};
  float w = (LEFT_W - 50) / 2;
  return (SDL_FRect) {LEFT_X + 20 + (f == F_DSP ? w + 10 : 0), y, w, 32};
}

// The logos (assets/logo.bmp, logo2.bmp and logo3.bmp, built into the launcher; magenta is
// see-through), one for each game: the header shows the selected game's. A file of the same
// name next to the launcher replaces one.
static void loadLogo(void) {
  static const struct { const char* name; const unsigned char* data; size_t size; } kLogos[TG_GAME_COUNT] = {
    { "logo.bmp", kLogoBmp, sizeof(kLogoBmp) },
    { "logo2.bmp", kLogo2Bmp, sizeof(kLogo2Bmp) },
    { "logo3.bmp", kLogo3Bmp, sizeof(kLogo3Bmp) },
  };
  const char* base = SDL_GetBasePath();
  for(int g = 0; g < TG_GAME_COUNT; g++) {
    char path[2400];
    snprintf(path, sizeof(path), "%s%s", base ? base : "", kLogos[g].name);
    SDL_Surface* s = SDL_LoadBMP(path);
    if(s == NULL) s = SDL_LoadBMP_IO(SDL_IOFromConstMem(kLogos[g].data, kLogos[g].size), true);
    if(s == NULL) continue;
    SDL_SetSurfaceColorKey(s, true, SDL_MapSurfaceRGB(s, 255, 0, 255));
    L.logo[g] = SDL_CreateTextureFromSurface(L.renderer, s);
    SDL_DestroySurface(s);
    if(L.logo[g]) SDL_SetTextureScaleMode(L.logo[g], SDL_SCALEMODE_LINEAR);
  }
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

static void fitName(char* out, size_t size, const char* path, int chars) {
  const char* shown = path[0] ? baseName(path) : "None chosen";
  if((int) strlen(shown) > chars) snprintf(out, size, "%.*s~", chars - 1, shown);
  else snprintf(out, size, "%s", shown);
}

static void button(SDL_FRect r, bool focused, const char* label) {
  panel(r.x, r.y, r.w, r.h, focused ? kAccent : kPanelHi, 255);
  text(r.x + (r.w - textWidth(label, 2)) / 2, r.y + 8, 2, kText, label);
}

// One file's lines on the card: what it is, its name, and whether it's ready.
static void fileLines(float y, const char* what, const char* path, bool ok, const char* okText, const char* missing) {
  text(LEFT_X + 20, y, 2, kDim, what);
  char name[40];
  fitName(name, sizeof(name), ok ? path : "", 24);
  text(LEFT_X + 20, y + 24, 2, ok ? kText : kFaint, name);
  text(LEFT_X + 20, y + 48, 2, ok ? kGood : kBad, ok ? okText : missing);
}

static void draw(void) {
  Uint64 t = SDL_GetTicks();
  color(kBg, 255);
  SDL_RenderClear(L.renderer);
  // Header
  for(int y = 0; y < 86; y++) fill(0, (float) y, VIEW_W, 1, (Rgb) {(Uint8) (20 + y / 4), (Uint8) (22 + y / 6), (Uint8) (36 + y / 3)}, 255);
  SDL_Texture* logo = L.logo[L.game] ? L.logo[L.game] : L.logo[TG_GAME_TOPGEAR];
  if(logo) {
    // At most 78 high and 440 wide (the sequels' logos are long), centred in the header.
    float lw, lh;
    SDL_GetTextureSize(logo, &lw, &lh);
    float h = 78, w = h * lw / lh;
    if(w > LEFT_W) { w = LEFT_W; h = w * lh / lw; }
    SDL_FRect r = {LEFT_X - 8, 5 + (78 - h) / 2, w, h};
    SDL_RenderTexture(L.renderer, logo, NULL, &r);
  } else {
    text(LEFT_X, 22, 5, kAccent, "TOP GEAR");
  }
  drawRoad(t);
  fill(0, 86, VIEW_W, 2, kAccent, 255);

  // The games, one tab each, with a light that shows which are ready to play.
  for(int g = 0; g < NGAMES; g++) {
    float x = tabX(g), w = tabW();
    bool shown = g == L.game, f = shown && L.focus == F_TAB;
    panel(x, TABS_Y, w, TABS_H, f ? kAccent : shown ? kPanelHi : kPanel, 255);
    if(shown && !f) fill(x + 3, TABS_Y + TABS_H - 3, w - 6, 3, kAccent, 255);
    text(x + 16, TABS_Y + 9, 2, shown ? kText : kDim, kTabNames[g]);
    const char* state = gameReady(g) ? "READY" : "SET UP";
    text(x + w - 16 - textWidth(state, 1.5f), TABS_Y + 12, 1.5f, gameReady(g) ? kGood : f ? kText : kFaint, state);
  }

  // The files this game needs
  float cy = CARD_Y, ch = cardH();
  panel(LEFT_X, cy, LEFT_W, ch, kPanel, 255);
  fileLines(cy + 14, "GAME ROM", L.romPath[L.game], L.romOk[L.game], "Verified: USA cartridge", "Needed: your own dump");
  if(L.game == TG_GAME_TOPGEAR3000) {
    fileLines(cy + 98, "DSP-4 FIRMWARE", L.dspPath, L.dspOk, "Verified: DSP-4 program", "Needed: 8 KB chip dump");
    button(fileButton(F_ROM), L.focus == F_ROM, L.dialogOpen && L.focus == F_ROM ? "Choosing..." : "ROM file");
    button(fileButton(F_DSP), L.focus == F_DSP, L.dialogOpen && L.focus == F_DSP ? "Choosing..." : "DSP-4 file");
  } else {
    button(fileButton(F_ROM), L.focus == F_ROM, L.dialogOpen ? "Choosing..." : L.romOk[L.game] ? "Change ROM" : "Choose ROM");
  }

  // Help for the focused item
  float hy = cy + ch + 16, hh = PLAY_Y - 16 - hy;
  panel(LEFT_X, hy, LEFT_W, hh, kPanel, 255);
  float tx = LEFT_X + 20, ty = hy + 18, bw = LEFT_W - 40;
  if(L.focus >= F_OPTION0 && L.focus < F_PLAY) {
    const Option* o = &kOptions[L.focus - F_OPTION0];
    text(tx, ty, 2, kAccent2, o->label);
    float h = wrapped(tx, ty + 34, bw, 2, kText, o->help);
    char hk[64];
    snprintf(hk, sizeof(hk), "In game: %s", o->hotkey);
    text(tx, ty + 34 + h + 10, 2, kDim, hk);
  } else if(L.focus == F_TAB) {
    text(tx, ty, 2, kAccent2, "Choose a game");
    for(int g = 0; g < NGAMES; g++) {
      char line[80];
      snprintf(line, sizeof(line), "%-14s %s", tg_games[g].name, gameReady(g) ? "ready" : "not set up");
      text(tx, ty + 34 + g * 24, 2, gameReady(g) ? kGood : kDim, line);
    }
    wrapped(tx, ty + 34 + NGAMES * 24 + 12, bw, 2, kText, "Left and right switch games.");
  } else if(L.focus == F_ROM) {
    text(tx, ty, 2, kAccent2, "Step 1: the ROM");
    wrapped(tx, ty + 34, bw, 2, kText,
      "Your own unmodified dump of the USA cartridge (.sfc or .smc). You can also drop any game's files on this window.");
  } else if(L.focus == F_DSP) {
    text(tx, ty, 2, kAccent2, "Step 2: DSP-4 firmware");
    wrapped(tx, ty + 34, bw, 2, kText,
      "The cartridge's DSP-4 chip has its own program. Its 8192-byte dump is usually named dsp4.bin.");
  } else if(gameReady(L.game)) {
    text(tx, ty, 2, kAccent2, "Ready");
    wrapped(tx, ty + 34, bw, 2, kText,
      "Starts the game with these options. Tab (or the gamepad's guide button) opens the options menu while you play.");
  } else {
    text(tx, ty, 2, kAccent2, "Not ready yet");
    wrapped(tx, ty + 34, bw, 2, kText, !L.romOk[L.game] ? "Choose this game's ROM first." : "Choose the DSP-4 firmware first.");
  }

  // Play: a dark button with a thin orange edge, the word in the logo's style
  {
    bool f = L.focus == F_PLAY, ready = gameReady(L.game);
    float pulse = f ? 0.5f + 0.5f * SDL_sinf((float) t / 260.0f) : 0;
    panel(LEFT_X, PLAY_Y, LEFT_W, 80, f ? kPanelHi : kPanel, 255);
    Rgb edge = !ready ? kFaint : f ? (Rgb) {255, (Uint8) (120 + 76 * pulse), 64} : kAccent;
    color(edge, 255);
    for(int k = 0; k < (f ? 3 : 2); k++) {
      SDL_FRect r = {LEFT_X + k, PLAY_Y + k, LEFT_W - 2 * k, 80 - 2.0f * k};
      SDL_RenderRect(L.renderer, &r);
    }
    if(L.playWord) {
      // Greyed out until the game has what it needs.
      Uint8 v = ready ? 255 : 110;
      SDL_SetTextureColorMod(L.playWord, v, v, v);
      SDL_FRect r = {LEFT_X + (LEFT_W - START_ART_W) / 2, PLAY_Y + (80 - START_ART_H) / 2.0f + 1, START_ART_W, START_ART_H};
      SDL_RenderTexture(L.renderer, L.playWord, NULL, &r);
    }
  }

  // Options list (scrolls to keep the focused row in view)
  int last = O_COUNT - 1;
  while(last > 0 && !optionShown(last)) last--;
  float total = optionY(last) + 30;
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
    if(!optionShown(i)) continue;
    const Option* o = &kOptions[i];
    float y = LIST_Y + optionY(i) - L.scroll;
    if(y < LIST_Y - 2 || y + 26 > LIST_Y + LIST_H) continue;  // only whole rows
    if(groupHeading(i) && y - 24 >= LIST_Y - 30) {
      const char* g = groupOf(i);
      text(LIST_X, y - 24, 2, kAccent, g);
      fill(LIST_X + textWidth(g, 2) + 10, y - 19, LIST_W - textWidth(g, 2) - 10, 1, kFaint, 255);
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
    text(LEFT_X, VIEW_H - 24, 2, L.messageGood ? kGood : kBad, L.message);
  } else {
    bool pad = L.pads[0] || L.pads[1] || L.pads[2] || L.pads[3];
    text(LEFT_X, VIEW_H - 24, 2, kFaint, pad ? "D-pad: choose   A: change   L/R: game   Start: play   B: quit"
                                              : "Arrows: choose   Enter: change   PgUp/PgDn: game   Space: play   Esc: quit");
  }
}

// --- Input ---------------------------------------------------------------------------

static bool inside(SDL_FRect r, float x, float y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

// What is under the mouse: a focus target; *tab gets the game of a tab.
static int hitTest(float x, float y, int* tab) {
  for(int g = 0; g < NGAMES; g++) {
    if(inside((SDL_FRect) {tabX(g), TABS_Y, tabW(), TABS_H}, x, y)) {
      if(tab) *tab = g;
      return F_TAB;
    }
  }
  if(inside(fileButton(F_ROM), x, y)) return F_ROM;
  if(L.game == TG_GAME_TOPGEAR3000 && inside(fileButton(F_DSP), x, y)) return F_DSP;
  if(x >= LEFT_X && x < LEFT_X + LEFT_W && y >= PLAY_Y && y < PLAY_Y + 80) return F_PLAY;
  if(x >= LIST_X - 4 && x < LIST_X + LIST_W + 4 && y >= LIST_Y - 4 && y < LIST_Y + LIST_H + 4) {
    for(int i = 0; i < O_COUNT; i++) {
      if(!optionShown(i)) continue;
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
  else if(L.focus == F_TAB) setGame(L.game + (dir == 3 ? -1 : 1));
  else if(L.focus == F_DSP && dir == 3) L.focus = F_ROM;
  else if(L.focus == F_ROM && dir == 4 && focusable(F_DSP)) L.focus = F_DSP;
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
  findFiles();
  L.focus = nextStep();

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
  // TG_LAUNCHER_SHOT=out.bmp [TG_LAUNCHER_FOCUS=n] [TG_LAUNCHER_GAME=g] [TG_LAUNCHER_IMPORT=file]
  // saves a picture and quits.
  const char* shot = SDL_getenv("TG_LAUNCHER_SHOT");
  if(SDL_getenv("TG_LAUNCHER_GAME")) L.game = SDL_atoi(SDL_getenv("TG_LAUNCHER_GAME")) % NGAMES;
  if(SDL_getenv("TG_LAUNCHER_FOCUS")) L.focus = SDL_atoi(SDL_getenv("TG_LAUNCHER_FOCUS"));
  if(SDL_getenv("TG_LAUNCHER_IMPORT")) importFile(SDL_getenv("TG_LAUNCHER_IMPORT"));  // as if dropped
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
            case SDL_GAMEPAD_BUTTON_WEST: L.focus = nextStep() == F_DSP ? F_DSP : F_ROM; activate(1); break;
            case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: setGame(L.game - 1); break;
            case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: setGame(L.game + 1); break;
          }
          break;
        case SDL_EVENT_KEY_DOWN:
          if(L.dialogOpen) break;
          switch(e.key.key) {
            case SDLK_UP: moveFocus(-1); break;
            case SDLK_DOWN: case SDLK_TAB: moveFocus(1); break;
            case SDLK_LEFT: direction(3); break;
            case SDLK_RIGHT: direction(4); break;
            case SDLK_PAGEUP: setGame(L.game - 1); break;
            case SDLK_PAGEDOWN: setGame(L.game + 1); break;
            case SDLK_1: case SDLK_2: case SDLK_3: setGame((int) (e.key.key - SDLK_1)); break;
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
          int h = hitTest(e.motion.x, e.motion.y, NULL);
          L.hover = h >= F_OPTION0 && h < F_PLAY ? h - F_OPTION0 : -1;
          if(h >= 0 && h != F_TAB) L.focus = h;
          break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
          if(L.dialogOpen) break;
          int tab = -1;
          int h = hitTest(e.button.x, e.button.y, &tab);
          if(h == F_TAB) {
            setGame(tab);
            L.focus = F_TAB;
          } else if(h >= 0) {
            L.focus = h;
            activate(e.button.button == SDL_BUTTON_RIGHT ? -1 : 1);
          }
          break;
        }
        case SDL_EVENT_MOUSE_WHEEL:
          L.scroll -= e.wheel.y * 30;
          break;
        case SDL_EVENT_DROP_FILE:
          if(e.drop.data) importFile(e.drop.data);
          break;
      }
    }
    SDL_LockMutex(L.lock);
    if(L.pickedReady) {
      L.pickedReady = false;
      char path[2048];
      snprintf(path, sizeof(path), "%s", L.picked);
      SDL_UnlockMutex(L.lock);
      importFile(path);
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
