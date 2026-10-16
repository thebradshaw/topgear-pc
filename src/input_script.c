#include "input_script.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"

static const struct {
  const char* name;
  uint16_t bit;
} buttonNames[] = {
  {"B", TG_BTN_B}, {"Y", TG_BTN_Y}, {"SELECT", TG_BTN_SELECT}, {"START", TG_BTN_START},
  {"UP", TG_BTN_UP}, {"DOWN", TG_BTN_DOWN}, {"LEFT", TG_BTN_LEFT}, {"RIGHT", TG_BTN_RIGHT},
  {"A", TG_BTN_A}, {"X", TG_BTN_X}, {"L", TG_BTN_L}, {"R", TG_BTN_R},
};

bool tg_parse_buttons(const char* text, uint16_t* buttons) {
  *buttons = 0;
  if(strcmp(text, "-") == 0) return true;
  char copy[128];
  snprintf(copy, sizeof(copy), "%s", text);
  for(char* tok = strtok(copy, "+"); tok != NULL; tok = strtok(NULL, "+")) {
    for(char* c = tok; *c; c++) *c = (char) toupper((unsigned char) *c);
    bool found = false;
    for(size_t i = 0; i < sizeof(buttonNames) / sizeof(buttonNames[0]); i++) {
      if(strcmp(tok, buttonNames[i].name) == 0) {
        *buttons |= buttonNames[i].bit;
        found = true;
      }
    }
    if(!found) return false;
  }
  return true;
}

void tg_format_buttons(uint16_t buttons, char* out, int size) {
  if(buttons == 0) {
    snprintf(out, size, "-");
    return;
  }
  out[0] = 0;
  for(size_t i = 0; i < sizeof(buttonNames) / sizeof(buttonNames[0]); i++) {
    if(buttons & buttonNames[i].bit) {
      if(out[0]) strncat(out, "+", size - strlen(out) - 1);
      strncat(out, buttonNames[i].name, size - strlen(out) - 1);
    }
  }
}

bool tg_input_script_load(const char* path, TgInputScript* script, char* error, int errorSize) {
  memset(script, 0, sizeof(*script));
  FILE* f = fopen(path, "r");
  if(f == NULL) {
    snprintf(error, errorSize, "cannot open %s", path);
    return false;
  }
  int capacity = 0;
  char line[256];
  int lineNo = 0;
  uint32_t lastFrame = 0;
  while(fgets(line, sizeof(line), f)) {
    lineNo++;
    char* hash = strchr(line, '#');
    if(hash) *hash = 0;
    unsigned frame, player = 0;
    char buttons[128];
    uint16_t mask = 0;
    char word[16];
    int n;
    char value[16];
    if(sscanf(line, "%u %15s", &frame, word) == 2 && strcmp(word, "reset") == 0) {
      n = 3;
    } else if(sscanf(line, "%u %15s %15s", &frame, word, value) == 3 && (strcmp(word, "smooth") == 0 || strcmp(word, "scaling") == 0 || strcmp(word, "fullview") == 0 || strcmp(word, "sidebyside") == 0 || strcmp(word, "ghost") == 0)) {
      player = strcmp(word, "smooth") == 0 ? 3 : strcmp(word, "scaling") == 0 ? 4 : strcmp(word, "fullview") == 0 ? 5 :
               strcmp(word, "sidebyside") == 0 ? 7 : 8;
      mask = strcmp(value, "on") == 0;
      n = strcmp(value, "on") == 0 || strcmp(value, "off") == 0 ? 3 : -1;
    } else if(sscanf(line, "%u %15s %15s", &frame, word, value) == 3 && strcmp(word, "wide") == 0) {
      player = 6;
      mask = (uint16_t) atoi(value);
      n = 3;
    } else {
      n = sscanf(line, "%u %u %127s", &frame, &player, buttons);
      if(n <= 0) continue;
      if(n == 3 && !tg_parse_buttons(buttons, &mask)) n = -1;
      if(player != 1 && player != 2) n = -1;
    }
    if(n != 3 || frame < lastFrame) {
      snprintf(error, errorSize, "%s:%d: expected '<frame> <1|2> <buttons>' in frame order", path, lineNo);
      fclose(f);
      tg_input_script_free(script);
      return false;
    }
    lastFrame = frame;
    if(script->count == capacity) {
      capacity = capacity ? capacity * 2 : 64;
      script->events = realloc(script->events, capacity * sizeof(TgInputEvent));
    }
    script->events[script->count++] = (TgInputEvent){frame, (uint8_t) player, mask};
  }
  fclose(f);
  return true;
}

void tg_input_script_free(TgInputScript* script) {
  free(script->events);
  memset(script, 0, sizeof(*script));
}

void tg_input_script_advance(TgInputScript* script, uint32_t frame) {
  while(script->next < script->count && script->events[script->next].frame <= frame) {
    TgInputEvent* e = &script->events[script->next++];
    if(e->player == 0) script->resetPending = true;
    else if(e->player == 3) script->smooth = e->buttons != 0;
    else if(e->player == 4) script->scaling = e->buttons != 0;
    else if(e->player == 5) script->fullview = e->buttons != 0;
    else if(e->player == 6) script->wide = e->buttons;
    else if(e->player == 7) script->sideBySide = e->buttons != 0;
    else if(e->player == 8) script->ghost = e->buttons != 0;
    else script->state[e->player] = e->buttons;
  }
}
