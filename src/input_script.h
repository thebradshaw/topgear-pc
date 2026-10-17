#ifndef TG_INPUT_SCRIPT_H
#define TG_INPUT_SCRIPT_H

#include <stdbool.h>
#include <stdint.h>

// A plain-text input recording. Each non-comment line is
//   <frame> <player> <buttons>
// where buttons is '-' (none) or names joined by '+', e.g. "START" or "B+RIGHT".
// The state holds from that frame until the next line for the same player.
// "<frame> reset" presses the console's reset button before that frame runs.
// "<frame> smooth on|off" switches the smooth motion option (one race update a frame), and
// "<frame> fullview on|off"
// the full-screen 1-player view; "<frame> wide <margin>" sets widescreen's extra pixels on
// each side (0 for off), "<frame> sidebyside on|off" the side-by-side 2-player views, and "<frame> ghost on|off"
// the ghost car, and "<frame> tallroad on|off" the full view's road drawn twice as tall;
// "<frame> maptop on|off" keeps the full view's map at the top, "<frame> speedbottom on|off"
// puts its speedometer and timer at the bottom right;
// "<frame> unlock A B C" opens countries up to A, B and C at the three skill levels (auto-save).

typedef struct {
  uint32_t frame;
  uint8_t player;  // 0 means reset, 3 smooth motion, 4 smooth scaling (removed, ignored), 5 full view, 6 widescreen (buttons: the margin), 7 side by side, 8 ghost, 9 tall road (buttons 1 on, 0 off), 10 unlock (buttons: the countries open at each skill level, 4 bits each), 11 map at the top, 12 speedometer at the bottom
  uint16_t buttons;
} TgInputEvent;

typedef struct {
  TgInputEvent* events;
  int count;
  int next;
  uint16_t state[3];
  bool resetPending;
  bool smooth;  // the smooth motion option, as the events so far set it
  bool fullview;  // the full-screen 1-player view option
  int wide;  // the widescreen margin, 0 when off
  bool sideBySide;  // the side-by-side 2-player option
  bool ghost;  // the ghost car option
  bool tallRoad;  // full view: the road drawn twice as tall
  bool mapTop;  // full view: the map stays at the top left
  bool speedBottom;  // full view: the speedometer and timer go to the bottom right
  bool unlockPending;  // auto-save: countries to open before the next frame
  uint16_t unlock;
} TgInputScript;

bool tg_input_script_load(const char* path, TgInputScript* script, char* error, int errorSize);
void tg_input_script_free(TgInputScript* script);
// Applies all events up to and including frame; returns the button state for player 1 or 2.
void tg_input_script_advance(TgInputScript* script, uint32_t frame);
bool tg_parse_buttons(const char* text, uint16_t* buttons);
void tg_format_buttons(uint16_t buttons, char* out, int size);

#endif
