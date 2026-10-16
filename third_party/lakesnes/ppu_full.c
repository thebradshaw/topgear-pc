
// Top Gear port enhancements (not part of LakeSnes): race frames redrawn wider than the
// SNES screen (widescreen), or with the 1-player view over the whole screen instead of the
// top half, the computer's view below it hidden.
//
// The game draws the frame as usual. While it does, the PPU's state for each line is kept,
// and at the start of vblank the screen is drawn again from those lines.
//
// Widescreen: each line is drawn wider, from left - margin to 256 + margin. The road and
// scenery are scrolled backgrounds and carry on past the old edges; the game's sprite
// builder keeps objects up to the new edges (sprites.c). The HUD's left and right parts
// move out to the new edges. Other screens are drawn in the middle with black borders.
//
// Full-screen view, from the top view's lines (1-108):
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

#define SCREEN_LINES 224
#define VIEW_LINES 108   // lines 1-108 (pixel rows 0-107) are the top view
#define VIEW_TOP 4       // its first row; the rows above are a black border
#define VIEW_STEP 112    // the bottom view is this many rows lower
#define ROAD_ROW 31      // the road below this row ...
#define OUT_ROAD_ROW 71  // ... starts on this row of the screen ...
#define ROAD_SCALE 2.0f  // ... and is drawn this much taller
#define HUD_SPLIT 44     // HUD rows from here on ...
#define HUD_MOVE 116     // ... move down this far
#define HUD_LEFT 64      // widescreen: HUD columns left of this move left ...
#define HUD_RIGHT 160    // ... and from this one on, right

typedef struct PpuFullView {
  bool got[SCREEN_LINES];
  int horizon;     // the first row of road, this frame
  int outHorizon;  // where it is on the screen
  int off;         // the view's first row: 0 for the top view, VIEW_STEP for the bottom one
  uint8_t* lines;  // SCREEN_LINES copies of the line state
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
  if(row >= SCREEN_LINES) row = SCREEN_LINES - 1;
  return f->lines + (size_t) row * stateSize();
}

static const Ppu* asPpu(const uint8_t* state) { return (const Ppu*) (state - stateStart()); }

void ppu_fullCapture(Ppu* ppu, int line) {
  int row = line - 1;
  if(row < 0 || row >= SCREEN_LINES) return;
  PpuFullView* f = ppu->full;
  if(f == NULL) {
    f = calloc(1, sizeof(PpuFullView));
    if(f == NULL) return;
    f->lines = malloc(SCREEN_LINES * stateSize());
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

// Where a row of the screen as the game drew it goes (in the view being composed).
int ppu_fullMapRow(Ppu* ppu, int row) {
  return (int) floorf(mapRow(ppu->full, (float) (row - ppu->full->off)) + 0.5f);
}

int ppu_fullViewOff(Ppu* ppu) { return ppu->full ? ppu->full->off : 0; }

// Widescreen HUD: the source column shown at x, or PPU_NO_HUD.
int ppu_hudSource(Ppu* ppu, int x) {
  int m = ppu->hudMargin;
  if(ppu->hudClamp) return x < 0 ? 0 : x > 255 ? 255 : x;
  if(x + m >= 0 && x + m < HUD_LEFT) return x + m;
  if(x - m >= HUD_RIGHT && x - m < 256) return x - m;
  if(x >= HUD_LEFT && x < HUD_RIGHT) return x;
  return PPU_NO_HUD;
}

// How far a HUD sprite at source column x moves.
int ppu_hudShift(Ppu* ppu, int x) {
  if(x < HUD_LEFT) return -ppu->hudMargin;
  if(x >= HUD_RIGHT) return ppu->hudMargin;
  return 0;
}

// A HUD line that is solid all the way across (a border) is carried on to the edges
// rather than split.
static void checkHudClamp(Ppu* ppu, int line) {
  ppu->hudClamp = false;
  if(!ppu->hudMargin || !ppu->layer[0].mainScreenEnabled) return;
  for(int x = 0; x < 256; x++) if(!ppu_bgOpaque(ppu, 0, x, line)) return;
  ppu->hudClamp = true;
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
  // Rows of the view from here on.
  int o = f->off;
  a += o;
  sky += o;
  use += o;
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
  if(!road && sky < o + VIEW_TOP) {
    ppu->layer[1].mainScreenEnabled = false;
    ppu->layer[1].subScreenEnabled = false;
  }
  // The HUD.
  int hud = y < HUD_SPLIT ? y : y >= HUD_SPLIT + HUD_MOVE ? y - HUD_MOVE : -1;
  if(hud >= 0) {
    hud += o;
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
    } else if(oy < 224) {
      shift = (oy - o >= HUD_SPLIT ? HUD_MOVE : 0) - o;
    }
    ppu->objShift[i] = (int16_t) shift;
    if(shift && !(ppu->objScaleTags && ppu->objTag[i])) {
      int ny = oy + shift;
      ppu->oam[i * 2] = (uint16_t) ((ppu->oam[i * 2] & 0xff) | ((ny < 224 ? ny & 0xff : 0xf0) << 8));
    }
  }
  checkHudClamp(ppu, y + 1);
  ppu_runLine(ppu, y + 1);
}

// The view composed from here on, and its horizon: the first row where the road's rows
// start to change.
static void setView(PpuFullView* f, int off) {
  f->off = off;
  f->horizon = VIEW_LINES - 1;
  for(int row = VIEW_TOP + 2; row < VIEW_LINES; row++) {
    if(bg3Row(f, off + row) != bg3Row(f, off + row - 1)) {
      f->horizon = row;
      break;
    }
  }
  f->outHorizon = (int) floorf(OUT_ROAD_ROW + (f->horizon - ROAD_ROW) * ROAD_SCALE + 0.5f);
}

// Another screen in widescreen: the picture in the middle, black at the sides.
static void pillarbox(Ppu* ppu) {
  int m = ppu->wideMargin, w = 256 + 2 * m;
  for(int y = 0; y < SCREEN_LINES; y++) {
    uint32_t* out = ppu->wideBuffer + (size_t) y * w;
    const uint8_t* row = &ppu->pixelBuffer[(y + (ppu->evenFrame ? 0 : 239)) * 2048];
    for(int x = 0; x < m; x++) out[x] = out[w - 1 - x] = 0;
    for(int x = 0; x < 256; x++) {
      const uint8_t* p = row + x * 8 + 4 + ppu->pixelOutputFormat;
      out[m + x] = (uint32_t) p[2] << 16 | (uint32_t) p[1] << 8 | p[0];
    }
  }
}

void ppu_fullCompose(Ppu* ppu) {
  PpuFullView* f = ppu->full;
  uint8_t flags = ppu->fullArmed;
  ppu->fullArmed = *ppu->raceFrame;
  *ppu->raceFrame = 0;
  int m = ppu->wideMargin;
  if(m > PPU_WIDE_MAX) m = PPU_WIDE_MAX;
  if(m > 0 && ppu->wideBuffer == NULL) {
    ppu->wideBuffer = calloc((size_t) PPU_LINE_BUF * SCREEN_LINES, sizeof(uint32_t));
    if(ppu->wideBuffer == NULL) return;
  }
  bool all = f != NULL && (flags & PPU_RACE_FRAME);
  int need = (flags & PPU_FULL_VIEW) && m == 0 ? VIEW_LINES : SCREEN_LINES;
  for(int i = 0; all && i < need; i++) all = f->got[i];
  if(f != NULL) memset(f->got, 0, sizeof(f->got));
  if(!all) {
    if(m > 0) pillarbox(ppu);
    return;
  }
  bool side = (flags & PPU_SIDE_BY_SIDE) && m > 0;
  bool full = (flags & PPU_FULL_VIEW) || side;
  memcpy(f->live, (uint8_t*) ppu + stateStart(), stateSize());
  ppu->fullComposing = true;
  if(side) {
    // Each view full height in half the width, showing the middle of its picture, with its
    // HUD's sides moved in to its edges.
    int w = 256 + 2 * m, half = w / 2;
    ppu->lineLeft = (int16_t) (128 - half / 2);
    ppu->lineRight = (int16_t) (ppu->lineLeft + half);
    ppu->hudMargin = (int16_t) (half / 2 - 128);
    for(int v = 0; v < 2; v++) {
      setView(f, v * VIEW_STEP);
      for(int y = 0; y < SCREEN_LINES; y++) {
        ppu->lineOut = ppu->wideBuffer + (size_t) y * w + v * (w - half);
        composeLine(ppu, f, y);
      }
    }
    for(int y = 0; y < SCREEN_LINES; y++) {  // a dark line between them
      uint32_t* out = ppu->wideBuffer + (size_t) y * w;
      out[half - 1] = out[w - half] = 0;
    }
    f->off = 0;
    ppu->fullComposing = false;
    ppu->lineLeft = 0;
    ppu->lineRight = 256;
    ppu->hudMargin = 0;
    ppu->hudClamp = false;
    ppu->lineOut = NULL;
    loadState(ppu, f->live);
    return;
  }
  if(full) setView(f, 0);
  ppu->lineLeft = (int16_t) -m;
  ppu->lineRight = (int16_t) (256 + m);
  ppu->hudMargin = (int16_t) m;
  for(int y = 0; y < SCREEN_LINES; y++) {
    ppu->lineOut = m > 0 ? ppu->wideBuffer + (size_t) y * (256 + 2 * m) : NULL;
    if(full) {
      composeLine(ppu, f, y);
    } else {
      loadState(ppu, lineState(f, y));
      memset(ppu->objShift, 0, sizeof(ppu->objShift));
      checkHudClamp(ppu, y + 1);
      ppu_runLine(ppu, y + 1);
    }
  }
  ppu->fullComposing = false;
  ppu->lineLeft = 0;
  ppu->lineRight = 256;
  ppu->hudMargin = 0;
  ppu->hudClamp = false;
  ppu->lineOut = NULL;
  loadState(ppu, f->live);
}
