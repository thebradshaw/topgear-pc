
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
#define ROAD_ROW 31      // the road below this row is drawn f->scale times taller, ending at
                         // the bottom of the screen: 1.3 keeps the horizon low and the road
                         // flat as in the original; 2 (tall road) fills the screen with road
#define HUD_SPLIT 47     // HUD rows from here on (below the race messages) ...
#define HUD_MOVE 116     // ... move down this far
#define HUD_LEFT 64      // widescreen: HUD columns left of this move left ...
#define HUD_RIGHT 160    // ... and from this one on, right

typedef struct PpuFullView {
  bool got[SCREEN_LINES];
  int horizon;     // the first row of road, this frame
  int outHorizon;  // where it is on the screen
  int off;         // the view's first row: 0 for the top view, VIEW_STEP for the bottom one
  float scale;     // how much taller the road is drawn
  float outRoad;   // where ROAD_ROW is on the screen
  uint8_t* lines;  // SCREEN_LINES copies of the line state
  uint8_t* live;
  // VRAM as the top view was drawn: the bottom view's car art goes into the same place
  // afterwards. Swapped in while the top view is redrawn (vramLive keeps the rest).
  uint16_t vramTop[0x8000];
  uint16_t vramLive[0x8000];
  bool haveVramTop, vramIsTop;
  // High frame rate: the line states of the last two race frames, and the in-between ones.
  uint8_t* cur;
  uint8_t* prev;
  uint8_t* blend;
  bool haveCur, havePrev;
  uint8_t curFlags;  // how the last race frame was composed
  int curMargin;
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
  if(row == VIEW_LINES - 1) {
    memcpy(f->vramTop, ppu->vram, sizeof(f->vramTop));
    f->haveVramTop = true;
  }
}

static int bg3Row(PpuFullView* f, int row) {
  return (asPpu(lineState(f, row))->bgLayer[2].vScroll + row + 1) & 0x3ff;
}

static int wrap10(int d) { return ((d + 512) & 0x3ff) - 512; }

// Where a row of the top view goes on the screen.
static float mapRow(PpuFullView* f, float row) {
  if(row >= f->horizon) return f->outRoad + (row - ROAD_ROW) * f->scale;
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
  if(road) src = ROAD_ROW + (y - f->outRoad) / f->scale;
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
static void setView(Ppu* ppu, PpuFullView* f, int off) {
  f->off = off;
  f->scale = ppu->roadScale > 0 ? ppu->roadScale : 1.3f;
  f->outRoad = SCREEN_LINES - 1 - (VIEW_LINES - 1 - ROAD_ROW) * f->scale;
  f->horizon = VIEW_LINES - 1;
  for(int row = VIEW_TOP + 2; row < VIEW_LINES; row++) {
    if(bg3Row(f, off + row) != bg3Row(f, off + row - 1)) {
      f->horizon = row;
      break;
    }
  }
  f->outHorizon = (int) floorf(f->outRoad + (f->horizon - ROAD_ROW) * f->scale + 0.5f);
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

static void composeFrame(Ppu* ppu, PpuFullView* f, uint8_t flags, int m);

// High frame rate: keeps this race frame's line states, and the last one's.
static void keepFrame(PpuFullView* f, uint8_t flags, int m) {
  size_t size = SCREEN_LINES * stateSize();
  if(f->cur == NULL) {
    f->cur = malloc(size);
    f->prev = malloc(size);
    f->blend = malloc(size);
    if(f->cur == NULL || f->prev == NULL || f->blend == NULL) {
      free(f->cur);
      free(f->prev);
      free(f->blend);
      f->cur = f->prev = f->blend = NULL;
      return;
    }
  }
  uint8_t* t = f->prev;
  f->prev = f->cur;
  f->cur = t;
  memcpy(f->cur, f->lines, size);
  f->havePrev = f->haveCur && f->curFlags == flags && f->curMargin == m;
  f->haveCur = true;
  f->curFlags = flags;
  f->curMargin = m;
}

static Ppu* blendRow(PpuFullView* f, int row) { return (Ppu*) (f->blend + (size_t) row * stateSize() - stateStart()); }
static const Ppu* rowOf(const uint8_t* lines, int row) { return asPpu(lines + (size_t) row * stateSize()); }

static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static uint16_t lerpScroll(int a, int b, float t) { return (uint16_t) ((a + (int) floorf(wrap10(b - a) * t + 0.5f)) & 0x3ff); }

// In the rows first..last of a frame, the fractional row where the road is depth d ahead
// (depth grows up the screen), or -1 when no road row is that far. Clamped to the nearest
// road row below.
static float rowAtDepth(const uint8_t* lines, int first, int last, float d) {
  int below = -1;  // the lowest road row
  for(int y = last; y >= first; y--) {
    int dy = rowOf(lines, y)->lineDepth;
    if(dy == PPU_NO_DEPTH) continue;
    if(below < 0) {
      below = y;
      if(d <= dy) return (float) y;
      continue;
    }
    int dn = rowOf(lines, y + 1)->lineDepth;
    if(dn != PPU_NO_DEPTH && d >= dn && d <= dy) return dy == dn ? (float) y : y + 1 - (d - dn) / (float) (dy - dn);
  }
  return -1;
}

// The rows of one view for t of a frame after the last: each road row shows the row of
// the last frame that will be at its depth by then, its sideways scroll between the two
// frames; the sky and scenery scroll between the two frames.
static void blendView(PpuFullView* f, int first, int last, float t) {
  // How far the camera moved (track units).
  int moved = 0;
  for(int y = last; y >= first; y--) {
    const Ppu* c = rowOf(f->cur, y);
    const Ppu* p = rowOf(f->prev, y);
    if(c->lineDepth == PPU_NO_DEPTH || p->lineDepth == PPU_NO_DEPTH) continue;
    moved = (int16_t) (c->lineCam - p->lineCam);
    break;
  }
  if(moved < -0x80 || moved > 0x80) moved = 0;  // over the line, or a restart
  for(int y = first; y <= last; y++) {
    Ppu* b = blendRow(f, y);
    const Ppu* c = rowOf(f->cur, y);
    const Ppu* p = rowOf(f->prev, y);
    if(c->lineDepth == PPU_NO_DEPTH) {
      b->bgLayer[1].hScroll = lerpScroll(p->bgLayer[1].hScroll, c->bgLayer[1].hScroll, t);
      b->bgLayer[1].vScroll = lerpScroll(p->bgLayer[1].vScroll, c->bgLayer[1].vScroll, t);
      continue;
    }
    float d = c->lineDepth;
    float sc = rowAtDepth(f->cur, first, last, d - (1 - t) * moved * 8);
    float sp = rowAtDepth(f->prev, first, last, d + t * moved * 8);
    if(sc < 0) continue;
    int a = (int) floorf(sc);
    float u = sc - a;
    if(a >= last) {
      a = last;
      u = 0;
    }
    int use = u >= 0.5f ? a + 1 : a;
    memcpy(f->blend + (size_t) y * stateSize(), f->cur + (size_t) use * stateSize(), stateSize());
    b = blendRow(f, y);
    // The road's row of art and sideways scroll, between the rows either side.
    const Ppu* ca = rowOf(f->cur, a);
    const Ppu* cb = rowOf(f->cur, u > 0 ? a + 1 : a);
    int ra = (ca->bgLayer[2].vScroll + a + 1) & 0x3ff, rb = (cb->bgLayer[2].vScroll + (u > 0 ? a + 2 : a + 1)) & 0x3ff;
    int dr = wrap10(rb - ra);
    int row = dr >= -16 && dr <= 16 ? ra + (int) floorf(dr * u + 0.5f) : (use == a ? ra : rb);
    b->bgLayer[2].vScroll = (uint16_t) ((row - (y + 1)) & 0x3ff);
    float hc = ca->bgLayer[2].hScroll + wrap10(cb->bgLayer[2].hScroll - ca->bgLayer[2].hScroll) * u;
    float hp = hc;
    if(sp >= 0) {
      int pa = (int) floorf(sp);
      float pu = sp - pa;
      if(pa >= last) {
        pa = last;
        pu = 0;
      }
      const Ppu* qa = rowOf(f->prev, pa);
      const Ppu* qb = rowOf(f->prev, pu > 0 ? pa + 1 : pa);
      hp = qa->bgLayer[2].hScroll + wrap10(qb->bgLayer[2].hScroll - qa->bgLayer[2].hScroll) * pu;
      hp = hc + wrap10((int) floorf(hp - hc + 0.5f));
    }
    b->bgLayer[2].hScroll = (uint16_t) ((int) floorf(lerpf(hp, hc, t) + 0.5f) & 0x3ff);
    // Keep the HUD and the scenery of the row itself.
    b->bgLayer[0] = c->bgLayer[0];
    b->bgLayer[1].hScroll = lerpScroll(p->bgLayer[1].hScroll, c->bgLayer[1].hScroll, t);
    b->bgLayer[1].vScroll = c->bgLayer[1].vScroll;
  }
}

// Moves each object back toward where it was in the last frame: (1 - t) of the way.
static void blendObjects(PpuFullView* f, float t, int16_t dx[128], int16_t dy[128]) {
  const Ppu* c = rowOf(f->cur, 0);
  const Ppu* p = rowOf(f->prev, 0);
  for(int i = 0; i < 128; i++) {
    dx[i] = dy[i] = 0;
    uint16_t id = c->objIdent[i];
    if(id == 0) continue;
    // The object's first sprite in each frame.
    int k = 0, j = 0;
    while(c->objIdent[k] != id) k++;
    while(j < 128 && p->objIdent[j] != id) j++;
    if(j == 128) continue;
    int mx = c->objAnchorX[k] - p->objAnchorX[j], my = c->objAnchorY[k] - p->objAnchorY[j];
    if(mx < -64 || mx > 64 || my < -64 || my > 64) continue;
    dx[i] = (int16_t) -floorf((1 - t) * mx + 0.5f);
    dy[i] = (int16_t) -floorf((1 - t) * my + 0.5f);
  }
}

static void moveObjects(Ppu* b, const int16_t dx[128], const int16_t dy[128]) {
  for(int i = 0; i < 128; i++) {
    if(dx[i] == 0 && dy[i] == 0) continue;
    int x = (b->oam[i * 2] & 0xff) | ((b->highOam[i >> 2] >> ((i & 3) * 2)) & 1) << 8;
    int y = b->oam[i * 2] >> 8;
    if(y >= 224 + 16 && y < 256) continue;  // hidden
    x = (x + dx[i]) & 0x1ff;
    y = (y + dy[i]) & 0xff;
    b->oam[i * 2] = (uint16_t) ((x & 0xff) | y << 8);
    uint8_t bit = (uint8_t) (1 << ((i & 3) * 2));
    b->highOam[i >> 2] = (uint8_t) ((b->highOam[i >> 2] & ~bit) | (x & 0x100 ? bit : 0));
    if(b->objGround[i] != PPU_NO_GROUND) b->objGround[i] = (int16_t) (b->objGround[i] + dy[i]);
  }
}

bool ppu_interpCompose(Ppu* ppu, float t) {
  PpuFullView* f = ppu->full;
  if(f == NULL || !f->haveCur || !f->havePrev || f->blend == NULL) return false;
  if(t < 0) t = 0;
  if(t > 1) t = 1;
  memcpy(f->blend, f->cur, SCREEN_LINES * stateSize());
  blendView(f, VIEW_TOP, VIEW_LINES - 1, t);
  blendView(f, VIEW_STEP + VIEW_TOP, VIEW_STEP + VIEW_LINES - 1, t);
  int16_t dx[128], dy[128];
  blendObjects(f, t, dx, dy);
  for(int y = 0; y < SCREEN_LINES; y++) moveObjects(blendRow(f, y), dx, dy);
  memcpy(ppu->objInterpX, dx, sizeof(dx));
  memcpy(ppu->objInterpY, dy, sizeof(dy));
  uint8_t* lines = f->lines;
  f->lines = f->blend;
  composeFrame(ppu, f, f->curFlags, f->curMargin);
  f->lines = lines;
  memset(ppu->objInterpX, 0, sizeof(ppu->objInterpX));
  memset(ppu->objInterpY, 0, sizeof(ppu->objInterpY));
  return true;
}

// Draws from the VRAM of the top view (top) or of the end of the frame.
static void useVram(Ppu* ppu, PpuFullView* f, bool top) {
  if(!f->haveVramTop || top == f->vramIsTop) return;
  if(top) {
    memcpy(f->vramLive, ppu->vram, sizeof(f->vramLive));
    memcpy(ppu->vram, f->vramTop, sizeof(f->vramTop));
  } else {
    memcpy(ppu->vram, f->vramLive, sizeof(f->vramLive));
  }
  f->vramIsTop = top;
}

// Redraws the screen from the line states in f->lines.
static void composeFrame(Ppu* ppu, PpuFullView* f, uint8_t flags, int m) {
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
      setView(ppu, f, v * VIEW_STEP);
      useVram(ppu, f, v == 0);
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
    useVram(ppu, f, false);
    ppu->fullComposing = false;
    ppu->lineLeft = 0;
    ppu->lineRight = 256;
    ppu->hudMargin = 0;
    ppu->hudClamp = false;
    ppu->lineOut = NULL;
    loadState(ppu, f->live);
    return;
  }
  if(full) setView(ppu, f, 0);
  ppu->lineLeft = (int16_t) -m;
  ppu->lineRight = (int16_t) (256 + m);
  ppu->hudMargin = (int16_t) m;
  for(int y = 0; y < SCREEN_LINES; y++) {
    ppu->lineOut = m > 0 ? ppu->wideBuffer + (size_t) y * (256 + 2 * m) : NULL;
    useVram(ppu, f, full || y < VIEW_STEP);
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
  useVram(ppu, f, false);
  loadState(ppu, f->live);
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
    if(f != NULL) f->haveCur = f->havePrev = false;
    if(m > 0) pillarbox(ppu);
    return;
  }
  if(ppu->interp) keepFrame(f, flags, m);
  composeFrame(ppu, f, flags, m);
}
