
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
#define SKY_ROWS 40      // the most rows of scenery carried on above the view
#define HUD_SPLIT 47     // full view: parts of the HUD centred below this row ...
#define HUD_MOVE 116     // ... move down this far, and so do the race messages
#define HUD_LEFT 64      // widescreen: parts of the HUD centred left of this move left ...
#define HUD_RIGHT 160    // ... and from this one on, right
#define HUD_GAP 6        // HUD pixels this close together are one part (a word, the map)
#define HUD_PARTS 4096

typedef struct PpuFullView {
  bool got[SCREEN_LINES];
  int horizon;     // the first row of road, this frame
  int outHorizon;  // where it is on the screen
  int off;         // the view's first row: 0 for the top view, VIEW_STEP for the bottom one
  float scale;     // how much taller the road is drawn
  float outRoad;   // where ROAD_ROW is on the screen
  int skyRows;     // how many rows of scenery carry on above the top of the view
  // Each view's top row from the last frame that had sky there, for when a hill brings
  // the road up to the top of the view (the full view shows more above it).
  uint8_t* skyState[2];
  int skyBg3Row[2];
  // The HUD's parts, moved: for each screen pixel, the game's pixel (ppu.h hudLine), each
  // row's BG1 scroll, and how far each HUD sprite moves (by view).
  int32_t hudSrc[SCREEN_LINES * PPU_LINE_BUF];
  uint16_t hudH[SCREEN_LINES], hudV[SCREEN_LINES];
  int16_t objDx[2][128], objDy[2][128];
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
  memcpy((uint8_t*) ppu + stateStart(), src, stateSize());
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

// The HUD's parts. BG1 pixels close together (HUD_GAP) are one part, and so is a HUD
// sprite with what it touches, so a word, the map with its dots, or the pit-stop car stays
// whole. Widescreen moves each part out to the side its middle is on; the full view moves
// the parts centred low in the view (and the race messages in the middle) to the bottom
// of the screen, and the map to just above the gear.
typedef struct HudBox { int16_t x0, x1, y0, y1; } HudBox;

static int hudRoot(int* p, int i) {
  while(p[i] != i) i = p[i] = p[p[i]];
  return i;
}

static void hudJoin(int* p, int a, int b) {
  a = hudRoot(p, a);
  b = hudRoot(p, b);
  if(a != b) p[a] = b;
}

static bool boxesNear(const HudBox* a, const HudBox* b, int gx, int gy) {
  return a->x0 <= b->x1 + gx && b->x0 <= a->x1 + gx && a->y0 <= b->y1 + gy && b->y0 <= a->y1 + gy;
}

// A sprite's 9-bit x from 256 up is either off the left edge or, on a wide line, on the
// right. The race's objects say which (PPU_OBJ_LEFT); for others (the car in the pits),
// a sprite goes on the side nearer the sprites next to it in OAM, else on the left as on
// the SNES. Marks the left ones in the line states of rows first..last (ppu: the loaded
// middle row).
static void leftOrRight(Ppu* ppu, PpuFullView* f, int first, int last) {
  for(int i = 0; i < 128; i++) {
    if(ppu->objGround[i] != PPU_NO_GROUND || (ppu->objFlag[i] & PPU_OBJ_LEFT)) continue;
    if(!((ppu->highOam[i >> 2] >> ((i & 3) * 2)) & 1)) continue;
    int low = ppu->oam[i * 2] & 0xff, oy = ppu->oam[i * 2] >> 8;
    bool left = true;
    for(int d = 1; d <= 2; d++) {
      int k = -1;
      for(int s = -1; s <= 1 && k < 0; s += 2) {
        int j = i + s * d;
        if(j < 0 || j >= 128 || ((ppu->highOam[j >> 2] >> ((j & 3) * 2)) & 1)) continue;
        int jy = ppu->oam[j * 2] >> 8;
        if(jy >= 224 || (jy - oy > 64 || oy - jy > 64)) continue;
        k = j;
      }
      if(k < 0) continue;
      int nx = ppu->oam[k * 2] & 0xff;
      left = abs((low - 256) - nx) <= abs((low + 256) - nx);
      break;
    }
    if(!left) continue;
    ppu->objFlag[i] |= PPU_OBJ_LEFT;
    for(int r = first; r <= last; r++) ((Ppu*) (f->lines + (size_t) r * stateSize() - stateStart()))->objFlag[i] |= PPU_OBJ_LEFT;
  }
}

// Lays out the HUD of the rows first..last (one view): fills f->hudSrc for the screen rows
// they go to, f->hudH/V for them, and the view's sprite moves. m: how far the sides move
// out; full: the full view (the rows go to the whole screen).
static void layoutHud(Ppu* ppu, PpuFullView* f, int first, int last, int m, bool full, int view) {
  static HudBox box[HUD_PARTS + 128];
  static int parent[HUD_PARTS + 128];
  static int rowStart[SCREEN_LINES + 1];
  static uint8_t opaque[SCREEN_LINES][256];
  static int16_t dx[HUD_PARTS + 128], dy[HUD_PARTS + 128];
  int n = 0, rows = last - first + 1;
  int outFirst = full ? 0 : first, outLast = full ? SCREEN_LINES - 1 : last;
  for(int y = outFirst; y <= outLast; y++) {
    int32_t* out = f->hudSrc + (size_t) y * PPU_LINE_BUF;
    for(int x = 0; x < PPU_LINE_BUF; x++) out[x] = -1;
  }
  for(int k = 0; k < rows; k++) {
    int r = first + k;
    rowStart[k] = n;
    loadState(ppu, lineState(f, r));
    f->hudH[r] = ppu->bgLayer[0].hScroll;
    f->hudV[r] = ppu->bgLayer[0].vScroll;
    bool on = ppu->layer[0].mainScreenEnabled, all = on;
    for(int x = 0; x < 256; x++) {
      opaque[k][x] = on && ppu_bgOpaque(ppu, 0, x, r + 1);
      all = all && opaque[k][x];
    }
    if(all && m != 0 && !full) {
      // Solid all the way across (a border): carried on to the edges.
      int32_t* out = f->hudSrc + (size_t) r * PPU_LINE_BUF;
      for(int x = -m; x < 256 + m; x++) out[PPU_OBJX(x)] = r << 9 | (x < 0 ? 0 : x > 255 ? 255 : x);
      memset(opaque[k], 0, 256);
      continue;
    }
    for(int x = 0; x < 256;) {
      if(!opaque[k][x]) { x++; continue; }
      int x0 = x, x1 = x;
      while(x < 256 && (opaque[k][x] || x - x1 <= HUD_GAP)) {
        if(opaque[k][x]) x1 = x;
        x++;
      }
      if(n == HUD_PARTS) break;
      box[n] = (HudBox) {(int16_t) x0, (int16_t) x1, (int16_t) r, (int16_t) r};
      parent[n] = n;
      for(int j = k >= 2 ? rowStart[k - 2] : 0; j < rowStart[k]; j++) if(boxesNear(&box[j], &box[n], HUD_GAP, 2)) hudJoin(parent, j, n);
      n++;
    }
  }
  rowStart[rows] = n;
  // HUD sprites, as the view's middle row has them.
  loadState(ppu, lineState(f, (first + last) / 2));
  if(m != 0) leftOrRight(ppu, f, first, last);
  int sprite[128];
  for(int i = 0; i < 128; i++) {
    sprite[i] = -1;
    if(ppu->objGround[i] != PPU_NO_GROUND) continue;
    int size = ppu_objSize(ppu, i);
    int oy = ppu->oam[i * 2] >> 8;
    if(oy >= 224) oy -= 256;
    int x = ppu->oam[i * 2] & 0xff;
    if((ppu->highOam[i >> 2] >> ((i & 3) * 2)) & 1) x -= 256;
    HudBox b = {(int16_t) x, (int16_t) (x + size - 1), (int16_t) (oy - 1), (int16_t) (oy + size - 2)};
    if(b.y1 < first || b.y0 > last || b.x1 < 0 || b.x0 > 255) continue;
    int id = n + i;
    box[id] = b;
    parent[id] = id;
    sprite[i] = id;
    for(int j = 0; j < n; j++) if(boxesNear(&box[j], &b, 1, 1)) hudJoin(parent, j, id);
    for(int j = 0; j < i; j++) if(sprite[j] >= 0 && boxesNear(&box[sprite[j]], &b, 1, 1)) hudJoin(parent, sprite[j], id);
  }
  // Each part's extent, at its root.
  static HudBox part[HUD_PARTS + 128];
  static bool isRoot[HUD_PARTS + 128], hasBg[HUD_PARTS + 128];
  int total = n + 128;
  for(int i = 0; i < total; i++) isRoot[i] = hasBg[i] = false;
  for(int i = 0; i < total; i++) {
    if(i >= n && sprite[i - n] < 0) continue;
    int p = hudRoot(parent, i);
    if(i < n) hasBg[p] = true;
    if(!isRoot[p]) { part[p] = box[i]; isRoot[p] = true; continue; }
    HudBox* q = &part[p];
    if(box[i].x0 < q->x0) q->x0 = box[i].x0;
    if(box[i].x1 > q->x1) q->x1 = box[i].x1;
    if(box[i].y0 < q->y0) q->y0 = box[i].y0;
    if(box[i].y1 > q->y1) q->y1 = box[i].y1;
  }
  int map = -1, lowLeft = SCREEN_LINES;
  for(int p = 0; p < total; p++) {
    if(!isRoot[p]) continue;
    const HudBox* q = &part[p];
    int cx = (q->x0 + q->x1) / 2, cy = (q->y0 + q->y1) / 2 - f->off, h = q->y1 - q->y0 + 1;
    dx[p] = (int16_t) (cx < HUD_LEFT ? -m : cx >= HUD_RIGHT ? m : 0);
    dy[p] = 0;
    // Sprites with no HUD art under them (race messages, the car in the pits) stay in the
    // middle; big ones are a picture and don't move at all.
    if(!hasBg[p]) {
      dx[p] = 0;
      if(q->x1 - q->x0 >= 40 || h >= 40) continue;
    }
    if(!full || h > 60) continue;  // a picture (the pits), not part of the HUD
    if(q->x1 - q->x0 >= 200 && h <= 8) { dy[p] = 2 * SCREEN_LINES; continue; }  // the view's border: not shown
    if(cx >= HUD_LEFT && cx < HUD_RIGHT) dy[p] = HUD_MOVE;
    else if(cy >= HUD_SPLIT) dy[p] = HUD_MOVE;
    if(cx < HUD_LEFT && dy[p] == 0 && h >= 16 && (map < 0 || h > part[map].y1 - part[map].y0 + 1)) map = p;
    if(cx < HUD_LEFT && dy[p] == HUD_MOVE && q->y0 - f->off < lowLeft) lowLeft = q->y0 - f->off;
  }
  if(map >= 0 && lowLeft < SCREEN_LINES) dy[map] = (int16_t) (lowLeft + HUD_MOVE - 6 - (part[map].y1 - f->off));
  // The pixels to their new places.
  for(int k = 0; k < rows; k++) {
    int r = first + k;
    for(int j = rowStart[k]; j < rowStart[k + 1]; j++) {
      int p = hudRoot(parent, j);
      int y = (full ? r - f->off : r) + dy[p];
      if(y < 0 || y >= SCREEN_LINES) continue;
      int32_t* out = f->hudSrc + (size_t) y * PPU_LINE_BUF;
      for(int x = box[j].x0; x <= box[j].x1; x++) {
        int ox = x + dx[p];
        if(opaque[k][x] && ox >= -PPU_WIDE_MAX && ox < 256 + PPU_WIDE_MAX) out[PPU_OBJX(ox)] = r << 9 | x;
      }
    }
  }
  for(int i = 0; i < 128; i++) {
    int p = sprite[i] >= 0 ? hudRoot(parent, sprite[i]) : -1;
    f->objDx[view][i] = p >= 0 ? dx[p] : 0;
    f->objDy[view][i] = p >= 0 ? dy[p] : 0;
  }
}

// The HUD for screen row y, of view 0 (top) or 1.
static void setHudLine(Ppu* ppu, PpuFullView* f, int y, int view) {
  ppu->hudLine = f->hudSrc + (size_t) y * PPU_LINE_BUF;
  ppu->hudRowH = f->hudH;
  ppu->hudRowV = f->hudV;
  memcpy(ppu->objHudDx, f->objDx[view], sizeof(ppu->objHudDx));
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
  int view = o != 0;
  bool crest = !road && sky < o + VIEW_TOP && f->skyState[view] && bg3Row(f, o + VIEW_TOP) != f->skyBg3Row[view];
  if(crest) {
    // A hill: the road reaches the top of the view, so above it is sky. The sky row as it
    // last was, with this frame's sprites.
    const Ppu* cur = asPpu(lineState(f, use));
    loadState(ppu, f->skyState[view]);
    memcpy(ppu->oam, cur->oam, sizeof(ppu->oam));
    memcpy(ppu->highOam, cur->highOam, sizeof(ppu->highOam));
    memcpy(ppu->objGround, cur->objGround, sizeof(ppu->objGround));
    memcpy(ppu->objFlag, cur->objFlag, sizeof(ppu->objFlag));
    memcpy(ppu->objIdent, cur->objIdent, sizeof(ppu->objIdent));
    ppu->objSize = cur->objSize;
    ppu->objTileAdr1 = cur->objTileAdr1;
    ppu->objTileAdr2 = cur->objTileAdr2;
    bg3->vScroll = (uint16_t) ((f->skyBg3Row[view] - (y + 1)) & 0x3ff);
    ppu->layer[1].mainScreenEnabled = false;
    ppu->layer[1].subScreenEnabled = false;
  } else if(road && t > 0) {
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
  if(!crest) bg2->vScroll = (uint16_t) ((asPpu(lineState(f, use))->bgLayer[1].vScroll + row2 - y) & 0x3ff);
  if(!crest && !road && sky < o + VIEW_TOP && o + VIEW_TOP - sky > f->skyRows) {
    ppu->layer[1].mainScreenEnabled = false;
    ppu->layer[1].subScreenEnabled = false;
  }
  setHudLine(ppu, f, y, o != 0);
  // Sprites move with the ground under their object; the HUD's with the HUD.
  for(int i = 0; i < 128; i++) {
    int oy = ppu->oam[i * 2] >> 8;
    int g = ppu->objGround[i];
    int shift = 0;
    if(g != PPU_NO_GROUND) {
      shift = ppu_fullMapRow(ppu, g) - g;
      if(oy >= 224) oy -= 256;  // the part above the top of the view
    } else if(oy < 224) {
      shift = f->objDy[o != 0][i] - o;
    }
    ppu->objShift[i] = (int16_t) shift;
    if(shift) {
      int ny = oy + shift;
      ppu->oam[i * 2] = (uint16_t) ((ppu->oam[i * 2] & 0xff) | ((ny < 224 ? ny & 0xff : 0xf0) << 8));
    }
  }
  ppu_runLine(ppu, y + 1);
  ppu->hudLine = NULL;
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
  int view = off != 0;
  // Sky: a long run of the same road-layer row above the road.
  if(f->horizon >= VIEW_TOP + 20) {
    if(f->skyState[view] == NULL) f->skyState[view] = malloc(stateSize());
    if(f->skyState[view]) {
      memcpy(f->skyState[view], lineState(f, off + VIEW_TOP), stateSize());
      f->skyBg3Row[view] = bg3Row(f, off + VIEW_TOP);
    }
  }
  // Over a hill the road can reach the top of the view: those top rows are road too.
  if(f->skyState[view] && bg3Row(f, off + VIEW_TOP) != f->skyBg3Row[view]) f->horizon = VIEW_TOP;
  // Above the horizon the sky and scenery keep their size. On level ground that leaves a gap
  // above the view's top row, filled with more sky; as the road climbs towards the top of
  // the view the gap closes and the road is drawn taller, so nothing has to be made up
  // above a hill.
  if(f->horizon < ROAD_ROW) {
    float gap = f->outRoad - (ROAD_ROW - VIEW_TOP);  // on level ground
    float top = gap * (f->horizon - VIEW_TOP) / (ROAD_ROW - VIEW_TOP) + (f->horizon - VIEW_TOP);
    f->scale = (SCREEN_LINES - 1 - top) / (VIEW_LINES - 1 - f->horizon);
    f->outRoad = SCREEN_LINES - 1 - (VIEW_LINES - 1 - ROAD_ROW) * f->scale;
  }
  f->outHorizon = (int) floorf(f->outRoad + (f->horizon - ROAD_ROW) * f->scale + 0.5f);
  // The scenery (mountains, buildings) that the top of the view cut off carries on above
  // it, up to its first empty or solid row.
  f->skyRows = 0;
  loadState(ppu, lineState(f, off + VIEW_TOP));
  for(int d = 1; d <= SKY_ROWS; d++) {
    // Scenery has sky between its shapes: stop at an empty row, or a solid one (not scenery).
    int count = 0;
    for(int x = 0; x < 256; x++) count += ppu_bgOpaque(ppu, 1, x, off + VIEW_TOP + 1 - d);
    if(count == 0 || count == 256) break;
    f->skyRows = d;
  }
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
  uint8_t* lines = f->lines;
  f->lines = f->blend;
  composeFrame(ppu, f, f->curFlags, f->curMargin);
  f->lines = lines;
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
      useVram(ppu, f, v == 0);
      setView(ppu, f, v * VIEW_STEP);
      layoutHud(ppu, f, v * VIEW_STEP + VIEW_TOP, v * VIEW_STEP + VIEW_LINES - 1, ppu->hudMargin, true, v);
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
    ppu->lineOut = NULL;
    loadState(ppu, f->live);
    return;
  }
  if(full) {
    useVram(ppu, f, true);
    setView(ppu, f, 0);
    layoutHud(ppu, f, VIEW_TOP, VIEW_LINES - 1, m, true, 0);
  } else if(m > 0) {
    useVram(ppu, f, true);
    layoutHud(ppu, f, 0, VIEW_STEP - 1, m, false, 0);
    useVram(ppu, f, false);
    layoutHud(ppu, f, VIEW_STEP, SCREEN_LINES - 1, m, false, 1);
  }
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
      if(m > 0) setHudLine(ppu, f, y, y >= VIEW_STEP);
      ppu_runLine(ppu, y + 1);
      if(m > 0 && (flags & (y < VIEW_STEP ? PPU_PIT_TOP : PPU_PIT_BOTTOM))) {
        // The pit scene is scrolled art (the car too): past its sides there is only the HUD.
        uint32_t* out = ppu->lineOut;
        for(int x = -m; x < 256 + m; x++) {
          if(x == 0) x = 256;
          if(ppu->hudLine[PPU_OBJX(x)] < 0) out[x + m] = 0;
        }
      }
      ppu->hudLine = NULL;
    }
  }
  ppu->fullComposing = false;
  ppu->lineLeft = 0;
  ppu->lineRight = 256;
  ppu->hudMargin = 0;
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
  bool got[SCREEN_LINES] = {false};
  if(f != NULL) {
    memcpy(got, f->got, sizeof(got));
    memset(f->got, 0, sizeof(f->got));
  }
  if(!all && ppu->fullSkipDraw && f != NULL && (flags & PPU_RACE_FRAME)) {
    // This frame's lines were left undrawn, but it can't be redrawn: draw the lines that
    // were kept as they were.
    memcpy(f->live, (uint8_t*) ppu + stateStart(), stateSize());
    ppu->fullComposing = true;
    for(int y = 0; y < SCREEN_LINES; y++) {
      if(!got[y]) continue;
      loadState(ppu, lineState(f, y));
      ppu_runLine(ppu, y + 1);
    }
    ppu->fullComposing = false;
    loadState(ppu, f->live);
  }
  ppu->fullSkipDraw = all;
  if(!all) {
    if(f != NULL) f->haveCur = f->havePrev = false;
    if(m > 0) pillarbox(ppu);
    return;
  }
  if(ppu->interp) keepFrame(f, flags, m);
  composeFrame(ppu, f, flags, m);
}
