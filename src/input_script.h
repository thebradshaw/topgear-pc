#ifndef TG_INPUT_SCRIPT_H
#define TG_INPUT_SCRIPT_H

#include <stdbool.h>
#include <stdint.h>

// A plain-text input recording. Each non-comment line is
//   <frame> <player> <buttons>
// where buttons is '-' (none) or names joined by '+', e.g. "START" or "B+RIGHT".
// The state holds from that frame until the next line for the same player.
// "<frame> reset" presses the console's reset button before that frame runs.

typedef struct {
  uint32_t frame;
  uint8_t player;  // 0 means reset
  uint16_t buttons;
} TgInputEvent;

typedef struct {
  TgInputEvent* events;
  int count;
  int next;
  uint16_t state[3];
  bool resetPending;
} TgInputScript;

bool tg_input_script_load(const char* path, TgInputScript* script, char* error, int errorSize);
void tg_input_script_free(TgInputScript* script);
// Applies all events up to and including frame; returns the button state for player 1 or 2.
void tg_input_script_advance(TgInputScript* script, uint32_t frame);
bool tg_parse_buttons(const char* text, uint16_t* buttons);
void tg_format_buttons(uint16_t buttons, char* out, int size);

#endif
