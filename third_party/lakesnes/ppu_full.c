
// Top Gear port enhancement (not part of LakeSnes): the 1-player race view drawn over the
// whole screen instead of the top half, with the computer's view below it hidden.
//
// The game draws its two views as usual. While it does, the PPU's state for each line of
// the top view (lines 1-108) is kept, and at the start of vblank the screen is drawn again
// from those lines:
// - The road is stretched from 76 lines to 152 below a fixed line, as if seen from a
//   higher camera. Each output line gets the road row and curve offset between the two
//   lines it falls between, so the road gets new detail rather than doubled lines.
// - The sky and scenery above the horizon keep their size and move down with it; above
//   the original top of the view, the sky's colour carries on.
// - The HUD's top rows stay at the top and the rest moves to the bottom of the screen.
// - Sprites move down with the ground line their object stands on (the game's sprite
//   builder records it), so they keep their size and stay on the road.

#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <math.h>

#include "ppu.h"
#include "snes.h"

#define VIEW_LINES 108   // lines 1-108 (pixel rows 0-107) are the top view
#define VIEW_TOP 4       // its first row; the rows above are a black border
#define ROAD_ROW 31      // the road below this row ...
#define OUT_ROAD_ROW 71  // ... starts on this row of the screen ...
#define ROAD_SCALE 2.0f  // ... and is drawn this much taller
#define HUD_SPLIT 44     // HUD rows from here on ...
#define HUD_MOVE 116     // ... move down this far

typedef struct PpuFullView {
  bool got[VIEW_LINES];
  int horizon;     // the first row of road, this frame
  int outHorizon;  // where it is on the screen
  uint8_t* lines;  // VIEW_LINES copies of the line state
  uint8_t* live;
} PpuFullView;

// The part of the PPU that decides how a line looks, apart from VRAM.
static size_t stateStart(void) { return offsetof(Ppu, cgram); }
static size_t stateSize(void) { return offsetof(Ppu, hCount) - offsetof(Ppu, cgram); }

static void loadState(Ppu* ppu, const uint8_t* src) {
  const uint16_t* tags = ppu->objScaleTags;
  const PpuObjScale* scales = ppu->objScales;
  PpuObjBitmap* bitmaps = ppu->objBitmaps;
  memcpy((uint8_t*) ppu + stateStart(), src, stateSize());
  ppu->objScaleTags = tags;
  ppu->objScales = scales;
  ppu->objBitmaps = bitmaps;
}

static const uint8_t* lineState(PpuFullView* f, int row) {
  if(row < 0) row = 0;
  if(row >= VIEW_LINES) row = VIEW_LINES - 1;
  return f->lines + (size_t) row * stateSize();
}

static const Ppu* asPpu(const uint8_t* state) { return (const Ppu*) (state - stateStart()); }

void ppu_fullCapture(Ppu* ppu, int line) {
  int row = line - 1;
  if(row < 0 || row >= VIEW_LINES) return;
  PpuFullView* f = ppu->full;
  if(f == NULL) {
    f = calloc(1, sizeof(PpuFullView));
    if(f == NULL) return;
    f->lines = malloc(VIEW_LINES * stateSize());
    f->live = malloc(stateSize());
    if(f->lines == NULL || f->live == NULL) {
      free(f->lines);
      free(f->live);
      free(f);
      return;
    }
    ppu->full = f;
  }
  memcpy(f->lines + (size_t) row * stateSize(), (uint8_t*) ppu + stateStart(), stateSize());
  f->got[row] = true;
}

static int bg3Row(PpuFullView* f, int row) {
  return (asPpu(lineState(f, row))->bgLayer[2].vScroll + row + 1) & 0x3ff;
}

static int wrap10(int d) { return ((d + 512) & 0x3ff) - 512; }

// Where a row of the top view goes on the screen.
static float mapRow(PpuFullView* f, float row) {
  if(row >= f->horizon) return OUT_ROAD_ROW + (row - ROAD_ROW) * ROAD_SCALE;
  return row + (f->outHorizon - f->horizon);
}

int ppu_fullMapRow(Ppu* ppu, int row) {
  return (int) floorf(mapRow(ppu->full, (float) row) + 0.5f);
}

static void composeLine(Ppu* ppu, PpuFullView* f, int y) {
  float src;
  bool road = y >= f->outHorizon;
  if(road) src = ROAD_ROW + (y - OUT_ROAD_ROW) / ROAD_SCALE;
  else src = (float) (y - (f->outHorizon - f->horizon));
  int a = (int) floorf(src);
  float t = src - a;
  if(a >= VIEW_LINES - 1) {
    a = VIEW_LINES - 1;
    t = 0;
  }
  int sky = a;  // the row the sky above the view repeats
  if(a < VIEW_TOP) a = VIEW_TOP;
  int use = road && t >= 0.5f ? a + 1 : a;
  loadState(ppu, lineState(f, use));
  // The road: its row of art and its curve, between the two rows.
  BgLayer* bg3 = &ppu->bgLayer[2];
  if(road && t > 0) {
    const Ppu* pa = asPpu(lineState(f, a));
    const Ppu* pb = asPpu(lineState(f, a + 1));
    int ra = bg3Row(f, a), rb = bg3Row(f, a + 1);
    int d = wrap10(rb - ra);
    if(d >= -16 && d <= 16) {
      int row = ra + (int) floorf(d * t + 0.5f);
      int ha = pa->bgLayer[2].hScroll, hb = pb->bgLayer[2].hScroll;
      bg3->hScroll = (uint16_t) ((ha + (int) floorf(wrap10(hb - ha) * t + 0.5f)) & 0x3ff);
      bg3->vScroll = (uint16_t) ((row - (y + 1)) & 0x3ff);
    } else {
      bg3->vScroll = (uint16_t) ((bg3Row(f, use) - (y + 1)) & 0x3ff);
    }
  } else {
    bg3->vScroll = (uint16_t) ((bg3Row(f, use) - (y + 1)) & 0x3ff);
  }
  // The scenery: the source row, or above the view the rows above it.
  BgLayer* bg2 = &ppu->bgLayer[1];
  int row2 = road ? use : sky;
  bg2->vScroll = (uint16_t) ((asPpu(lineState(f, use))->bgLayer[1].vScroll + row2 - y) & 0x3ff);
  if(!road && sky < VIEW_TOP) {
    ppu->layer[1].mainScreenEnabled = false;
    ppu->layer[1].subScreenEnabled = false;
  }
  // The HUD.
  int hud = y < HUD_SPLIT ? y : y >= HUD_SPLIT + HUD_MOVE ? y - HUD_MOVE : -1;
  if(hud >= 0) {
    const Ppu* ph = asPpu(lineState(f, hud));
    ppu->bgLayer[0].hScroll = ph->bgLayer[0].hScroll;
    ppu->bgLayer[0].vScroll = (uint16_t) ((ph->bgLayer[0].vScroll + hud - y) & 0x3ff);
  } else {
    ppu->layer[0].mainScreenEnabled = false;
    ppu->layer[0].subScreenEnabled = false;
  }
  // Sprites move with the ground under their object; the HUD's with the HUD.
  for(int i = 0; i < 128; i++) {
    int oy = ppu->oam[i * 2] >> 8;
    int g = ppu->objGround[i];
    int shift = 0;
    if(g != PPU_NO_GROUND) {
      shift = ppu_fullMapRow(ppu, g) - g;
      if(oy >= 224) oy -= 256;  // the part above the top of the view
    } else if(oy < 224 && oy >= HUD_SPLIT) {
      shift = HUD_MOVE;
    }
    ppu->objShift[i] = (int16_t) shift;
    if(shift && !(ppu->objScaleTags && ppu->objTag[i])) {
      int ny = oy + shift;
      ppu->oam[i * 2] = (uint16_t) ((ppu->oam[i * 2] & 0xff) | ((ny < 224 ? ny & 0xff : 0xf0) << 8));
    }
  }
  ppu_runLine(ppu, y + 1);
}

void ppu_fullCompose(Ppu* ppu) {
  PpuFullView* f = ppu->full;
  bool armed = ppu->fullArmed;
  ppu->fullArmed = *ppu->fullViewFrame;
  *ppu->fullViewFrame = false;
  if(f == NULL) return;
  bool all = true;
  for(int i = 0; i < VIEW_LINES; i++) all = all && f->got[i];
  memset(f->got, 0, sizeof(f->got));
  if(!armed || !all) return;
  // The horizon: the first row where the road's rows start to change.
  f->horizon = VIEW_LINES - 1;
  for(int row = VIEW_TOP + 2; row < VIEW_LINES; row++) {
    if(bg3Row(f, row) != bg3Row(f, row - 1)) {
      f->horizon = row;
      break;
    }
  }
  f->outHorizon = (int) floorf(OUT_ROAD_ROW + (f->horizon - ROAD_ROW) * ROAD_SCALE + 0.5f);
  memcpy(f->live, (uint8_t*) ppu + stateStart(), stateSize());
  ppu->fullComposing = true;
  for(int y = 0; y < 224; y++) composeLine(ppu, f, y);
  ppu->fullComposing = false;
  loadState(ppu, f->live);
}
