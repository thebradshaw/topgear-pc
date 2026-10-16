
// Top Gear port enhancement (not part of LakeSnes): objects drawn smoothly scaled from
// their art and filtered, in place of the sprites the game put in OAM for them.
//
// The game's sprite builder tags every OAM entry it writes for an object with the object's
// record (PpuObjScale: its art as a list of sprite parts, an origin and a scale). The
// first tagged entry of an object stands for the whole object in the sprite order, so it
// keeps its place in front of or behind other sprites; the others are hidden. The object
// is rendered once a frame into a bitmap, sampling the art several times per pixel when it
// is shrunk, so each screen pixel gets a coverage and an average colour; the PPU then
// blends that over whatever is behind it.

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "ppu.h"
#include "snes.h"

#define BITMAP_SLOTS 64
#define BITMAP_MAX 96

static const int kSizes[8][2] = {
  {8, 16}, {8, 32}, {8, 64}, {16, 32}, {16, 64}, {32, 64}, {16, 32}, {16, 32}
};

static bool isAnchor(Ppu* ppu, uint8_t index) {
  uint16_t tag = ppu->objTag[index >> 1];
  return index == 0 || ppu->objTag[(index >> 1) - 1] != tag;
}

static int partSize(Ppu* ppu, const PpuObjPart* p) {
  return kSizes[ppu->objSize][p->big ? 1 : 0];
}

// The object's box on screen, in whole pixels.
static void objectBox(Ppu* ppu, const PpuObjScale* s, int* x0, int* y0, int* x1, int* y1) {
  float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
  for(int i = 0; i < s->count; i++) {
    const PpuObjPart* p = &s->parts[i];
    int size = partSize(ppu, p);
    if(p->dx < minx) minx = p->dx;
    if(p->dy < miny) miny = p->dy;
    if(p->dx + size > maxx) maxx = (float) (p->dx + size);
    if(p->dy + size > maxy) maxy = (float) (p->dy + size);
  }
  *x0 = (int) floorf(s->ax + minx * s->f);
  *y0 = (int) floorf(s->ay + miny * s->f);
  *x1 = (int) ceilf(s->ax + maxx * s->f);
  *y1 = (int) ceilf(s->ay + maxy * s->f);
  if(*x1 - *x0 > BITMAP_MAX) *x1 = *x0 + BITMAP_MAX;
  if(*y1 - *y0 > BITMAP_MAX) *y1 = *y0 + BITMAP_MAX;
}

// The colour index (1-15, 0 for none) of the art at (u, v), from the top part there.
static int artPixel(Ppu* ppu, const PpuObjScale* s, float u, float v, int* palette) {
  for(int i = 0; i < s->count; i++) {
    const PpuObjPart* p = &s->parts[i];
    int size = partSize(ppu, p);
    int col = (int) floorf(u - p->dx), row = (int) floorf(v - p->dy);
    if(col < 0 || row < 0 || col >= size || row >= size) continue;
    if(p->attr & 0x4000) col = size - 1 - col;
    if(p->attr & 0x8000) row = size - 1 - row;
    int tile = p->attr & 0xff;
    uint8_t usedTile = (((tile >> 4) + (row / 8)) << 4) | (((tile & 0xf) + (col / 8)) & 0xf);
    uint16_t objAdr = (p->attr & 0x100) ? ppu->objTileAdr2 : ppu->objTileAdr1;
    uint16_t plane1 = ppu->vram[(objAdr + usedTile * 16 + (row & 7)) & 0x7fff];
    uint16_t plane2 = ppu->vram[(objAdr + usedTile * 16 + 8 + (row & 7)) & 0x7fff];
    int shift = 7 - (col & 7);
    int pixel = (plane1 >> shift) & 1;
    pixel |= ((plane1 >> (8 + shift)) & 1) << 1;
    pixel |= ((plane2 >> shift) & 1) << 2;
    pixel |= ((plane2 >> (8 + shift)) & 1) << 3;
    if(pixel) {
      *palette = (p->attr >> 9) & 7;
      return pixel;
    }
  }
  return 0;
}

static PpuObjBitmap* bitmapFor(Ppu* ppu, uint16_t tag) {
  if(ppu->objBitmaps == NULL) {
    ppu->objBitmaps = calloc(BITMAP_SLOTS, sizeof(PpuObjBitmap));
    if(ppu->objBitmaps == NULL) return NULL;
  }
  uint32_t frame = ppu->snes->frames + 1;  // 0 marks an empty slot
  PpuObjBitmap* bm = &ppu->objBitmaps[tag % BITMAP_SLOTS];
  if(bm->tag == tag && bm->frame == frame) return bm;
  const PpuObjScale* s = &ppu->objScales[tag];
  bm->tag = tag;
  bm->frame = frame;
  int x1, y1;
  objectBox(ppu, s, &bm->x0, &bm->y0, &x1, &y1);
  bm->w = x1 - bm->x0;
  bm->h = y1 - bm->y0;
  // Samples per pixel along each axis: enough to see every art pixel when shrinking.
  int n = (int) ceilf(1.0f / s->f);
  if(n < 1) n = 1;
  if(n > 4) n = 4;
  for(int j = 0; j < bm->h; j++) {
    for(int i = 0; i < bm->w; i++) {
      int hits = 0, last = 0;
      float r = 0, g = 0, b = 0;
      for(int sy = 0; sy < n; sy++) {
        float v = (bm->y0 + j + (sy + 0.5f) / n - s->ay) / s->f;
        for(int sx = 0; sx < n; sx++) {
          float u = (bm->x0 + i + (sx + 0.5f) / n - s->ax) / s->f;
          int palette;
          int pixel = artPixel(ppu, s, u, v, &palette);
          if(!pixel) continue;
          last = 0x80 + 16 * palette + pixel;
          uint16_t c = ppu->cgram[last];
          r += c & 0x1f;
          g += (c >> 5) & 0x1f;
          b += (c >> 10) & 0x1f;
          hits++;
        }
      }
      int k = j * bm->w + i;
      bm->a[k] = (float) hits / (n * n);
      bm->index[k] = (uint8_t) last;
      if(hits) {
        bm->r[k] = r / hits;
        bm->g[k] = g / hits;
        bm->b[k] = b / hits;
      }
    }
  }
  return bm;
}

// -1: a hidden entry of an object; 0: the object's first entry, not on this line; 1: on it.
// The lines an object may cover; in the full-screen view, where they moved to.
static void clipLines(Ppu* ppu, const PpuObjScale* s, int* top, int* bottom) {
  *top = s->clipTop;
  *bottom = s->clipBottom;
  if(!ppu->fullComposing) return;
  int off = ppu_fullViewOff(ppu);
  *top = *top <= off + 4 ? 0 : ppu_fullMapRow(ppu, *top);
  *bottom = *bottom >= off + 108 ? 224 : ppu_fullMapRow(ppu, *bottom);
}

static int shiftOf(Ppu* ppu, uint8_t index) {
  return ppu->fullComposing ? ppu->objShift[index >> 1] + ppu->objInterpY[index >> 1] : 0;
}

static int shiftXOf(Ppu* ppu, uint8_t index) { return ppu->fullComposing ? ppu->objInterpX[index >> 1] : 0; }

int ppu_smoothOnLine(Ppu* ppu, uint8_t index, int line) {
  if(!isAnchor(ppu, index)) return -1;
  const PpuObjScale* s = &ppu->objScales[ppu->objTag[index >> 1]];
  int top, bottom;
  clipLines(ppu, s, &top, &bottom);
  if(line < top || line >= bottom || s->count == 0) return 0;
  int x0, y0, x1, y1;
  objectBox(ppu, s, &x0, &y0, &x1, &y1);
  int dy = shiftOf(ppu, index), dx = shiftXOf(ppu, index);
  return line >= y0 + dy && line < y1 + dy && x1 + dx > ppu->lineLeft && x0 + dx < ppu->lineRight;
}

void ppu_smoothDrawLine(Ppu* ppu, uint8_t index, int line) {
  uint16_t tag = ppu->objTag[index >> 1];
  const PpuObjScale* s = &ppu->objScales[tag];
  PpuObjBitmap* bm = bitmapFor(ppu, tag);
  if(bm == NULL) return;
  int j = line - shiftOf(ppu, index) - bm->y0;
  if(j < 0 || j >= bm->h) return;
  int priority = (s->parts[0].attr & 0x3000) >> 12;
  int dx = shiftXOf(ppu, index);
  for(int i = 0; i < bm->w; i++) {
    int x = bm->x0 + i + dx;
    if(x < ppu->lineLeft || x >= ppu->lineRight) continue;
    int k = j * bm->w + i;
    int ox = PPU_OBJX(x);
    float a = bm->a[k];
    if(a <= 0) continue;
    if(ppu->objFlag[index >> 1] & PPU_OBJ_GHOST) a *= PPU_GHOST_ALPHA;
    // Over whatever sprite pixel is already there (drawn earlier, so behind).
    float da = 0, dr = 0, dg = 0, db = 0;
    if(ppu->objPixelBuffer[ox]) {
      if(ppu->objAlpha[ox] > 0) {
        da = ppu->objAlpha[ox];
        dr = ppu->objR[ox];
        dg = ppu->objG[ox];
        db = ppu->objB[ox];
      } else {
        uint16_t c = ppu->cgram[ppu->objPixelBuffer[ox]];
        da = 1;
        dr = c & 0x1f;
        dg = (c >> 5) & 0x1f;
        db = (c >> 10) & 0x1f;
      }
    }
    float oa = a + da * (1 - a);
    ppu->objR[ox] = (bm->r[k] * a + dr * da * (1 - a)) / oa;
    ppu->objG[ox] = (bm->g[k] * a + dg * da * (1 - a)) / oa;
    ppu->objB[ox] = (bm->b[k] * a + db * da * (1 - a)) / oa;
    ppu->objAlpha[ox] = oa;
    ppu->objPixelBuffer[ox] = bm->index[k];
    ppu->objPriorityBuffer[ox] = (uint8_t) priority;
  }
}
