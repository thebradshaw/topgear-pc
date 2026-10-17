// Horizon remix prototype: see remix.h.
//
// The track is the game's own: per segment (32 track units) the tables in work RAM give
// the curve ($4C00: how fast the road turns; the first course only turns left), the slope
// ($4000, relative to the camera's), the stripe phase ($5800) and a roadside object
// ($6400). The road is drawn the classic way, segment by segment from the camera out,
// each segment's road a trapezoid between its two ends; segments hidden behind a crest are
// skipped. Everything is flat colours and polygons, drawn supersampled for smooth edges.

#include "remix.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SEG_UNITS 32       // track units per segment
#define DRAW_SEGS 120      // segments drawn ahead (the original: 32)
#define ROAD_HALF 1.0f     // world units
#define SEG_LEN 0.42f      // a segment's length, world units
#define CAM_HEIGHT 1.3f
#define CAM_BACK 7.5f      // camera behind player 1's car, segments
#define CURVE_K 0.0013f    // sideways turn per segment per unit of $4C00
#define SLOPE_K 0.0016f    // rise per world unit per unit of $4000
#define SIDE_UNIT 0x2800   // the game's sideways units at the road's edge
#define MAX_SPRITES 256

static uint16_t r16(const uint8_t* m, uint32_t a) { return (uint16_t) (m[a] | m[a + 1] << 8); }
static int16_t s16(const uint8_t* m, uint32_t a) { return (int16_t) r16(m, a); }

typedef struct { float r, g, b; } Col;
static Col rgb(int hex) { return (Col) {(hex >> 16 & 255) / 255.0f, (hex >> 8 & 255) / 255.0f, (hex & 255) / 255.0f}; }
static Col mix(Col a, Col b, float t) { return (Col) {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
static Col shade(Col a, float f) { return (Col) {a.r * f, a.g * f, a.b * f}; }

// The canvas: XRGB, supersampled, kept between frames.
typedef struct {
  int w, h;
  int y0, y1;  // the rows this call draws (a band, so threads can share a frame)
  uint32_t* px;
  float fog;   // haze: how far (0-1) what is drawn now fades into the haze colour
} Canvas;

static uint32_t pack(Col k) {
  int r = (int) (k.r * 255 + 0.5f), g = (int) (k.g * 255 + 0.5f), b = (int) (k.b * 255 + 0.5f);
  r = r < 0 ? 0 : r > 255 ? 255 : r;
  g = g < 0 ? 0 : g > 255 ? 255 : g;
  b = b < 0 ? 0 : b > 255 ? 255 : b;
  return (uint32_t) (r << 16 | g << 8 | b);
}

static void fill(uint32_t* p, int n, uint32_t v) {
  for(int i = 0; i < n; i++) p[i] = v;
}

static void blend(uint32_t* p, int n, uint32_t v, int a) {  // a: 0-256
  uint32_t vrb = v & 0xFF00FF, vg = v & 0x00FF00;
  for(int i = 0; i < n; i++) {
    uint32_t d = p[i], rb = d & 0xFF00FF, g = d & 0x00FF00;
    rb = (rb + (((vrb - rb) * (uint32_t) a) >> 8)) & 0xFF00FF;
    g = (g + (((vg - g) * (uint32_t) a) >> 8)) & 0x00FF00;
    p[i] = rb | g;
  }
}

static void span(Canvas* c, int y, float x0, float x1, Col k) {
  if(y < c->y0 || y >= c->y1) return;
  int a = (int) ceilf(x0 - 0.5f), b = (int) ceilf(x1 - 0.5f);
  if(a < 0) a = 0;
  if(b > c->w) b = c->w;
  if(b > a) fill(&c->px[y * c->w + a], b - a, pack(k));
}

// Haze, by distance.
static const int kHaze = 0xF0DCC0;

static float fog_at(float z) {
  float f = z / (DRAW_SEGS * SEG_LEN);
  if(f > 1) f = 1;
  return f * f * 0.85f;
}

// A polygon (any shape, even-odd), filled between rows top and bottom (exclusive) only.
static void poly(Canvas* c, const float* xy, int n, Col k, float alpha, int clipBottom) {
  k = mix(k, rgb(kHaze), c->fog);
  uint32_t v = pack(k);
  int ia = (int) (alpha * 256 + 0.5f);
  float ymin = 1e9f, ymax = -1e9f;
  for(int i = 0; i < n; i++) {
    if(xy[i * 2 + 1] < ymin) ymin = xy[i * 2 + 1];
    if(xy[i * 2 + 1] > ymax) ymax = xy[i * 2 + 1];
  }
  int y0 = (int) ceilf(ymin - 0.5f), y1 = (int) ceilf(ymax - 0.5f);
  if(y0 < c->y0) y0 = c->y0;
  if(y1 > clipBottom) y1 = clipBottom;
  if(y1 > c->y1) y1 = c->y1;
  for(int y = y0; y < y1; y++) {
    float cy = y + 0.5f, xs[16];
    int m = 0;
    for(int i = 0; i < n && m < 16; i++) {
      float ax = xy[i * 2], ay = xy[i * 2 + 1];
      float bx = xy[((i + 1) % n) * 2], by = xy[((i + 1) % n) * 2 + 1];
      if((ay <= cy && by > cy) || (by <= cy && ay > cy)) xs[m++] = ax + (cy - ay) / (by - ay) * (bx - ax);
    }
    for(int i = 1; i < m; i++)
      for(int j = i; j > 0 && xs[j] < xs[j - 1]; j--) { float t = xs[j]; xs[j] = xs[j - 1]; xs[j - 1] = t; }
    for(int i = 0; i + 1 < m; i += 2) {
      int a = (int) ceilf(xs[i] - 0.5f), b = (int) ceilf(xs[i + 1] - 0.5f);
      if(a < 0) a = 0;
      if(b > c->w) b = c->w;
      if(b <= a) continue;
      if(ia >= 256) fill(&c->px[y * c->w + a], b - a, v);
      else blend(&c->px[y * c->w + a], b - a, v, ia);
    }
  }
}

static void quad(Canvas* c, float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3,
                 Col k, int clip) {
  float p[8] = {x0, y0, x1, y1, x2, y2, x3, y3};
  poly(c, p, 4, k, 1, clip);
}

static void ellipse(Canvas* c, float cx, float cy, float rx, float ry, Col k, float alpha, int clip) {
  float p[32];
  for(int i = 0; i < 16; i++) {
    float a = (float) i / 16 * 6.2831853f;
    p[i * 2] = cx + cosf(a) * rx;
    p[i * 2 + 1] = cy + sinf(a) * ry;
  }
  poly(c, p, 16, k, alpha, clip);
}

// The scene.
typedef struct {
  const uint8_t* m;
  int nseg;
  float w, h, cx, horizon, focal;
  float camX;
} Scene;

typedef struct {
  float sx, sy, scale;  // screen position of the road's centre, pixels per world unit
  float y;              // world height
  int clip;             // rows from here down were drawn by nearer segments
} Proj;

typedef struct {
  float depth;
  int kind;     // 0 car, 1 roadside object
  int type;     // the car's colour or the object's art
  float sx, sy, scale;
  int clip;
  bool flip;
} Sprite;

static const int kCarColours[] = {0xD7263D, 0xF49D37, 0x1B998B, 0x3F88C5, 0xE8E9EB, 0x8E4585, 0x2E294E,
                                  0xF2D0A4, 0x52489C, 0x59C3C3, 0xEBEBEB, 0xC5283D, 0x255F85, 0xF08700,
                                  0x00A6A6, 0x6A994E, 0x9D4EDD, 0xE63946, 0x457B9D, 0xFFB703};

// A polygon in an object's own units (x across, y up from the ground), placed on screen.
static void part(Canvas* c, const Sprite* s, float u, const float* pts, int n, Col k, float alpha) {
  float p[32];
  for(int i = 0; i < n && i < 16; i++) {
    p[i * 2] = s->sx + pts[i * 2] * u;
    p[i * 2 + 1] = s->sy - pts[i * 2 + 1] * u;
  }
  poly(c, p, n, k, alpha, s->clip);
}

#define PART(c, s, u, k, ...) do { const float pts_[] = {__VA_ARGS__}; part(c, s, u, pts_, (int) (sizeof(pts_) / sizeof(float) / 2), k, 1); } while(0)

static void draw_car(Canvas* c, const Sprite* s) {
  float u = s->scale * 0.66f;  // the car's width
  Col body = rgb(kCarColours[s->type % 20]);
  Col dark = shade(body, 0.6f), deep = shade(body, 0.42f), light = mix(body, rgb(0xFFFFFF), 0.3f);
  Col tyre = rgb(0x17171B), glass = rgb(0x1E2A38), glassHi = rgb(0x3E5873), trim = rgb(0x2A2A30);
  ellipse(c, s->sx, s->sy, u * 0.6f, u * 0.07f, rgb(0x000000), 0.3f, s->clip);
  // tyres
  PART(c, s, u, tyre, -0.5f, 0, -0.32f, 0, -0.32f, 0.17f, -0.5f, 0.17f);
  PART(c, s, u, tyre, 0.32f, 0, 0.5f, 0, 0.5f, 0.17f, 0.32f, 0.17f);
  // lower body and its shaded skirt
  PART(c, s, u, body, -0.53f, 0.07f, 0.53f, 0.07f, 0.55f, 0.18f, 0.5f, 0.3f, -0.5f, 0.3f, -0.55f, 0.18f);
  PART(c, s, u, dark, -0.53f, 0.07f, 0.53f, 0.07f, 0.55f, 0.13f, -0.55f, 0.13f);
  PART(c, s, u, trim, -0.36f, 0.03f, 0.36f, 0.03f, 0.38f, 0.1f, -0.38f, 0.1f);
  // exhausts
  ellipse(c, s->sx - u * 0.2f, s->sy - u * 0.075f, u * 0.035f, u * 0.025f, rgb(0x9A9AA2), 1, s->clip);
  ellipse(c, s->sx + u * 0.2f, s->sy - u * 0.075f, u * 0.035f, u * 0.025f, rgb(0x9A9AA2), 1, s->clip);
  // light bar: dark band, lamps
  PART(c, s, u, trim, -0.5f, 0.17f, 0.5f, 0.17f, 0.49f, 0.25f, -0.49f, 0.25f);
  PART(c, s, u, rgb(0xFF3B30), -0.47f, 0.185f, -0.27f, 0.185f, -0.27f, 0.235f, -0.46f, 0.235f);
  PART(c, s, u, rgb(0xFF3B30), 0.27f, 0.185f, 0.47f, 0.185f, 0.46f, 0.235f, 0.27f, 0.235f);
  PART(c, s, u, rgb(0xFFB199), -0.44f, 0.215f, -0.32f, 0.215f, -0.32f, 0.23f, -0.44f, 0.23f);
  PART(c, s, u, rgb(0xFFB199), 0.32f, 0.215f, 0.44f, 0.215f, 0.44f, 0.23f, 0.32f, 0.23f);
  PART(c, s, u, rgb(0xF2F2EC), -0.07f, 0.19f, 0.07f, 0.19f, 0.07f, 0.235f, -0.07f, 0.235f);
  // rear deck, lit from above
  PART(c, s, u, light, -0.5f, 0.3f, 0.5f, 0.3f, 0.42f, 0.36f, -0.42f, 0.36f);
  // cabin: shaded sides, glass, roof
  PART(c, s, u, deep, -0.36f, 0.36f, 0.36f, 0.36f, 0.27f, 0.52f, -0.27f, 0.52f);
  PART(c, s, u, glass, -0.3f, 0.375f, 0.3f, 0.375f, 0.23f, 0.5f, -0.23f, 0.5f);
  PART(c, s, u, glassHi, -0.3f, 0.375f, -0.14f, 0.375f, -0.2f, 0.5f, -0.23f, 0.5f);
  PART(c, s, u, light, -0.27f, 0.52f, 0.27f, 0.52f, 0.22f, 0.55f, -0.22f, 0.55f);
  // spoiler
  PART(c, s, u, trim, -0.3f, 0.3f, -0.26f, 0.3f, -0.26f, 0.4f, -0.3f, 0.4f);
  PART(c, s, u, trim, 0.26f, 0.3f, 0.3f, 0.3f, 0.3f, 0.4f, 0.26f, 0.4f);
  PART(c, s, u, dark, -0.52f, 0.39f, 0.52f, 0.39f, 0.52f, 0.43f, -0.52f, 0.43f);
  PART(c, s, u, light, -0.52f, 0.43f, 0.52f, 0.43f, 0.49f, 0.45f, -0.49f, 0.45f);
}

static void draw_cactus(Canvas* c, const Sprite* s) {
  float u = s->scale, x = s->sx, y = s->sy;
  int clip = s->clip;
  Col lit = rgb(0x4E9F3D), dim = rgb(0x2F6B2A);
  float f = s->flip ? -1 : 1;
  ellipse(c, x, y, u * 0.2f, u * 0.04f, rgb(0x6B4B2A), 0.3f, clip);
  // trunk: lit and shaded halves
  quad(c, x - u * 0.07f, y, x, y, x, y - u * 0.75f, x - u * 0.06f, y - u * 0.72f, lit, clip);
  quad(c, x, y, x + u * 0.07f, y, x + u * 0.06f, y - u * 0.72f, x, y - u * 0.75f, dim, clip);
  // arms
  float ax = x + f * u * 0.06f;
  quad(c, ax, y - u * 0.32f, ax + f * u * 0.16f, y - u * 0.32f, ax + f * u * 0.16f, y - u * 0.4f, ax, y - u * 0.4f, dim, clip);
  quad(c, ax + f * u * 0.1f, y - u * 0.32f, ax + f * u * 0.17f, y - u * 0.32f, ax + f * u * 0.17f, y - u * 0.58f, ax + f * u * 0.11f, y - u * 0.56f, lit, clip);
  float bx = x - f * u * 0.06f;
  quad(c, bx, y - u * 0.42f, bx - f * u * 0.13f, y - u * 0.42f, bx - f * u * 0.13f, y - u * 0.49f, bx, y - u * 0.49f, lit, clip);
  quad(c, bx - f * u * 0.08f, y - u * 0.42f, bx - f * u * 0.14f, y - u * 0.42f, bx - f * u * 0.14f, y - u * 0.62f, bx - f * u * 0.09f, y - u * 0.6f, dim, clip);
}

static void draw_bush(Canvas* c, const Sprite* s) {
  float u = s->scale, x = s->sx, y = s->sy;
  int clip = s->clip;
  float p[] = {x - u * 0.22f, y, x - u * 0.18f, y - u * 0.1f, x - u * 0.06f, y - u * 0.17f, x + u * 0.08f, y - u * 0.15f,
               x + u * 0.2f, y - u * 0.07f, x + u * 0.22f, y};
  poly(c, p, 6, rgb(0x7A8F3A), 1, clip);
  float q[] = {x - u * 0.06f, y - u * 0.17f, x + u * 0.08f, y - u * 0.15f, x + u * 0.2f, y - u * 0.07f, x + u * 0.22f, y,
               x + u * 0.02f, y};
  poly(c, q, 5, rgb(0x5C6E2B), 1, clip);
}

static void draw_sign(Canvas* c, const Sprite* s) {
  float u = s->scale, x = s->sx, y = s->sy;
  int clip = s->clip;
  // post
  quad(c, x - u * 0.015f, y, x + u * 0.015f, y, x + u * 0.015f, y - u * 0.3f, x - u * 0.015f, y - u * 0.3f, rgb(0x8A8F98), clip);
  // board
  float bw = u * 0.34f, top = y - u * 0.52f, bot = y - u * 0.28f;
  quad(c, x - bw / 2, bot, x + bw / 2, bot, x + bw / 2, top, x - bw / 2, top, rgb(0xE23B3B), clip);
  quad(c, x - bw / 2, top + u * 0.025f, x + bw / 2, top + u * 0.025f, x + bw / 2, top, x - bw / 2, top, rgb(0xFF6B5E), clip);
  // two chevrons pointing the way the road turns (left)
  float d = s->flip ? -1 : 1;
  for(int i = 0; i < 2; i++) {
    float cx = x + d * (i * 0.1f - 0.02f) * u, mid = (top + bot) / 2, hh = (bot - top) * 0.36f, t = u * 0.035f;
    float p[] = {cx - d * u * 0.05f, mid, cx + d * u * 0.03f, mid - hh, cx + d * (u * 0.03f + t), mid - hh,
                 cx - d * (u * 0.05f - t), mid, cx + d * (u * 0.03f + t), mid + hh, cx + d * u * 0.03f, mid + hh};
    poly(c, p, 6, rgb(0xFFFFFF), 1, clip);
  }
}

static void draw_rock(Canvas* c, const Sprite* s) {
  float u = s->scale, x = s->sx, y = s->sy;
  int clip = s->clip;
  float p[] = {x - u * 0.16f, y, x - u * 0.11f, y - u * 0.1f, x + u * 0.02f, y - u * 0.14f, x + u * 0.15f, y - u * 0.06f, x + u * 0.17f, y};
  poly(c, p, 5, rgb(0xB9835A), 1, clip);
  float q[] = {x + u * 0.02f, y - u * 0.14f, x + u * 0.15f, y - u * 0.06f, x + u * 0.17f, y, x + u * 0.03f, y};
  poly(c, q, 4, rgb(0x8E5F3E), 1, clip);
}

// Dunes on the horizon: a ridge from a few waves, each face lit or shaded by its slope.
static float ridge(float x, float seed) {
  return 0.55f + 0.25f * sinf(x * 1.3f + seed) + 0.15f * sinf(x * 3.1f + seed * 2.0f) + 0.08f * sinf(x * 7.3f + seed * 3.0f);
}

static void draw_backdrop(Canvas* c, const Scene* sc, float heading) {
  Col skyTop = rgb(0x2D6BD3), skyLow = rgb(0xF3E2C8);
  for(int y = c->y0; y < c->y1; y++) {
    float t = (float) y / (sc->horizon > 1 ? sc->horizon : 1);
    if(t > 1) t = 1;
    Col k = mix(skyTop, skyLow, t * t);
    span(c, y, 0, (float) c->w, k);
  }
  // sun
  ellipse(c, c->w * 0.78f, sc->horizon * 0.28f, sc->h * 0.075f, sc->h * 0.075f, rgb(0xFFFBEA), 0.22f, c->h);
  ellipse(c, c->w * 0.78f, sc->horizon * 0.28f, sc->h * 0.11f, sc->h * 0.11f, rgb(0xFFFBEA), 0.1f, c->h);
  ellipse(c, c->w * 0.78f, sc->horizon * 0.28f, sc->h * 0.05f, sc->h * 0.05f, rgb(0xFFF4C2), 1, c->h);
  // two ranges of dunes, the far one paler and slower
  for(int layer = 0; layer < 2; layer++) {
    float height = sc->h * (layer == 0 ? 0.16f : 0.1f);
    float speed = layer == 0 ? 0.5f : 1.0f;
    Col lit = layer == 0 ? rgb(0xE9B98A) : rgb(0xE59A5B), dim = layer == 0 ? rgb(0xD39C72) : rgb(0xC0743F);
    lit = mix(lit, skyLow, layer == 0 ? 0.45f : 0.2f);
    dim = mix(dim, skyLow, layer == 0 ? 0.45f : 0.2f);
    float seed = layer == 0 ? 1.7f : 4.2f;
    float prev = 0;
    for(int x = 0; x < c->w; x++) {
      float wx = ((float) x / sc->h - heading * speed) * (layer == 0 ? 1.0f : 1.6f);
      float r = ridge(wx, seed), r2 = ridge(wx + 0.01f, seed);
      float top = sc->horizon - r * height;
      Col k = r2 > r ? lit : dim;
      uint32_t v = pack(k);
      int y0 = top < c->y0 ? c->y0 : (int) top, y1 = (int) (sc->horizon + sc->h * 0.2f);
      if(y1 > c->y1) y1 = c->y1;
      for(int y = y0; y < y1; y++) c->px[y * c->w + x] = v;
      prev = r;
    }
    (void) prev;
  }
}

// Car positions for drawing. The game moves the cars once per race update (every third
// frame); tg_remix_tick keeps the last two updates so that drawing can go smoothly between
// them, a step behind. Without ticks, the positions are read straight from RAM.
#define CARS 20
typedef struct {
  uint16_t pos[CARS];
  int16_t side[CARS];
} Snap;
static Snap s_prev, s_cur;
static bool s_valid;
static int s_since, s_period = 3;
static uint16_t s_time, s_len;
static float s_sub;

static void read_snap(const uint8_t* m, Snap* s) {
  for(int k = 0; k < CARS; k++) {
    uint32_t b = 0x0200 + 0x12 + (uint32_t) k * 0x12;
    s->pos[k] = r16(m, b + 7);
    s->side[k] = s16(m, b + 2);
  }
}

void tg_remix_tick(const uint8_t* m) {
  uint16_t time = r16(m, 0x1EAE), len = r16(m, 0x0200);
  if(!s_valid || len != s_len || time < s_time) {
    read_snap(m, &s_cur);
    s_prev = s_cur;
    s_valid = true;
    s_since = 0;
    s_time = time;
    s_len = len;
    return;
  }
  if(time != s_time) {
    s_prev = s_cur;
    read_snap(m, &s_cur);
    s_period = s_since + 1 < 1 ? 1 : s_since + 1 > 6 ? 6 : s_since + 1;
    s_since = 0;
    s_time = time;
  } else {
    s_since++;
  }
}

void tg_remix_set_subframe(float t) { s_sub = t; }

// Car k's track position (in segments) and side position (world units).
static void car_at(const uint8_t* m, int k, float* pos, float* side) {
  uint32_t b = 0x0200 + 0x12 + (uint32_t) k * 0x12;
  if(!s_valid || s_len != r16(m, 0x0200)) {
    *pos = (float) r16(m, b + 7) / SEG_UNITS;
    *side = (float) s16(m, b + 2) / SIDE_UNIT * ROAD_HALF;
    return;
  }
  float t = (s_since + s_sub) / s_period;
  if(t > 1) t = 1;
  int d = (int) s_cur.pos[k] - (int) s_prev.pos[k];
  if(d > s_len / 2) d -= s_len;
  if(d < -(int) s_len / 2) d += s_len;
  if(d > 0x200 || d < -0x200) t = 1;  // a jump: no sliding
  float p = s_prev.pos[k] + d * t;
  if(p < 0) p += s_len;
  if(p >= s_len) p -= s_len;
  *pos = p / SEG_UNITS;
  *side = (s_prev.side[k] + (s_cur.side[k] - s_prev.side[k]) * t) / (float) SIDE_UNIT * ROAD_HALF;
}

bool tg_remix_active(const uint8_t* wram) {
  uint16_t len = r16(wram, 0x0200);
  return len >= 0x400 && len < 0x8000 && (len & 0x1F) == 0;
}

static int clamp_ss(int ss) { return ss < 1 ? 1 : ss > 4 ? 4 : ss; }

size_t tg_remix_work_size(int w, int h, int ss) {
  ss = clamp_ss(ss);
  return (size_t) w * ss * h * ss * sizeof(uint32_t);
}

void tg_remix_draw(const uint8_t* m, uint32_t* out, int w, int h, int ss) {
  void* work = malloc(tg_remix_work_size(w, h, ss));
  if(work == NULL) return;
  tg_remix_draw_rows(m, out, w, h, ss, work, 0, h);
  free(work);
}

void tg_remix_draw_rows(const uint8_t* m, uint32_t* out, int w, int h, int ss, void* work, int row0, int row1) {
  ss = clamp_ss(ss);
  Canvas c = {w * ss, h * ss, row0 * ss, row1 * ss, work, 0};
  Scene sc = {.m = m};
  sc.nseg = r16(m, 0x0200) / SEG_UNITS;
  sc.w = (float) c.w;
  sc.h = (float) c.h;
  sc.cx = sc.w / 2;
  sc.horizon = sc.h * 0.42f;
  sc.focal = sc.h * 1.0f;

  // Player 1 and the camera behind it.
  float p1seg, p1x;
  car_at(m, r16(m, 0x0204) / 0x12, &p1seg, &p1x);
  float camPos = p1seg - CAM_BACK;
  if(camPos < 0) camPos += sc.nseg;
  int camSeg = (int) camPos;
  float frac = camPos - camSeg;
  sc.camX = p1x * 0.9f;

  // Which way the road heads here, for the backdrop: the curves summed from the start.
  float heading = 0;
  for(int i = 0; i < camSeg; i++) heading += r16(m, 0x4C00 + 2 * i) * CURVE_K * SEG_LEN;
  heading += r16(m, 0x4C00 + 2 * camSeg) * CURVE_K * SEG_LEN * frac;
  Col haze = rgb(kHaze);
  draw_backdrop(&c, &sc, heading * 2.2f);

  // The road's centre line at each segment end, from the camera out: the curves bend it,
  // the slopes raise it. Then it is turned and moved so that it runs straight ahead from
  // player 1's car, so the view looks along the road where the car is.
  float wx[DRAW_SEGS + 1], wy[DRAW_SEGS + 1];
  {
    float x = 0, dx = 0, y = 0;
    for(int i = 0; i <= DRAW_SEGS; i++) {
      int seg = (camSeg + i) % sc.nseg;
      wx[i] = x;
      wy[i] = y;
      float curve = r16(m, 0x4C00 + 2 * seg) * CURVE_K;
      float slope = s16(m, 0x4000 + 2 * seg) * SLOPE_K;
      if(slope > 1.5f) slope = 1.5f;
      if(slope < -1.5f) slope = -1.5f;
      float part = i == 0 ? 1 - frac : 1;
      x += dx * part;
      dx -= curve * part;
      y += slope * SEG_LEN * part;
    }
    float at = CAM_BACK + frac;
    int k = (int) at;
    float t = at - k;
    float xp = wx[k] + (wx[k + 1] - wx[k]) * t, gx = wx[k + 1] - wx[k];
    float yp = wy[k] + (wy[k + 1] - wy[k]) * t, gy = wy[k + 1] - wy[k];
    for(int i = 0; i <= DRAW_SEGS; i++) {
      wx[i] -= xp + gx * (i - at);
      wy[i] -= yp + gy * (i - at);
    }
  }
  Proj pr[DRAW_SEGS + 1];
  int clip = c.h;
  Sprite sprites[MAX_SPRITES];
  int ns = 0;
  Col sandA = rgb(0xEBC48D), sandB = rgb(0xE3B87E), roadA = rgb(0x4A5059), roadB = rgb(0x464C55);
  Col red = rgb(0xD8343F), white = rgb(0xF4F1EA), line = rgb(0xF4F1EA);
  for(int i = 0; i <= DRAW_SEGS; i++) {
    float z = (i - frac) * SEG_LEN;
    if(z < 0.05f) z = 0.05f;
    Proj* p = &pr[i];
    p->scale = sc.focal / z;
    p->sx = sc.cx + (wx[i] - sc.camX) * p->scale;
    p->sy = sc.horizon + (CAM_HEIGHT - wy[i]) * p->scale;
    p->y = wy[i];
    p->clip = clip;
    if(i == 0) continue;
    // Draw segment i - 1: from its near end (pr[i - 1]) to its far end (p).
    Proj* n = &pr[i - 1];
    int prevSeg = (camSeg + i - 1) % sc.nseg;
    if(p->sy < clip) {
      uint16_t colw = r16(m, 0x5800 + 2 * prevSeg);
      int phase = (colw >> 1) & 7;
      bool alt = phase < 4;
      int top = (int) ceilf(p->sy - 0.5f), bottom = (int) ceilf(n->sy - 0.5f);
      if(bottom > clip) bottom = clip;
      if(top < 0) top = 0;
      for(int row = top > c.y0 ? top : c.y0; row < bottom && row < c.y1; row++) {
        float t = (n->sy - (row + 0.5f)) / (n->sy - p->sy);
        float rx = n->sx + (p->sx - n->sx) * t;
        float rs = n->scale + (p->scale - n->scale) * t;
        float half = ROAD_HALF * rs, rumble = 0.12f * rs, lw = 0.035f * rs;
        float f = fog_at(sc.focal / rs);
        span(&c, row, 0, sc.w, mix(alt ? sandA : sandB, haze, f));
        span(&c, row, rx - half - rumble, rx + half + rumble, mix(alt ? red : white, haze, f));
        span(&c, row, rx - half, rx + half, mix(alt ? roadA : roadB, haze, f));
        if(alt) {
          for(int k = -1; k <= 1; k += 2) {
            float lx = rx + k * half / 3;
            span(&c, row, lx - lw, lx + lw, mix(line, haze, f));
          }
        }
      }
      // The segment's roadside object.
      uint16_t ow = r16(m, 0x6400 + 2 * prevSeg);
      if(ow != 0 && !(ow & 0x8000) && ns < MAX_SPRITES) {
        float off = (ow & 0xFF) / 96.0f * ROAD_HALF + 0.25f;
        float side = (ow & 0x0100) ? -1.0f : 1.0f;
        Sprite* s = &sprites[ns++];
        s->depth = (float) i;
        s->kind = 1;
        s->type = (ow >> 9) & 0x7F;
        s->sx = n->sx + side * (ROAD_HALF + off) * n->scale;
        s->sy = n->sy;
        s->scale = n->scale;
        s->clip = clip;
        s->flip = side < 0;
      }
      clip = top < clip ? top : clip;
    }
  }

  // Cars on the road.
  for(int k = 0; k < 20 && ns < MAX_SPRITES; k++) {
    uint32_t b = 0x0200 + 0x12 + (uint32_t) k * 0x12;
    uint16_t kind = r16(m, b);
    if(kind == 0) continue;
    float pos, side;
    car_at(m, k, &pos, &side);
    float d = pos - camPos;
    if(d < 0) d += sc.nseg;
    if(d >= DRAW_SEGS - 1 || d < 0.6f) continue;
    float at = d + frac;  // in pr[] steps
    int i = (int) at;
    float t = at - i;
    Proj* a = &pr[i];
    Proj* bb = &pr[i + 1];
    float scale = 1 / (1 / a->scale + (1 / bb->scale - 1 / a->scale) * t);
    float cx = a->sx + (bb->sx - a->sx) * t, cy = a->sy + (bb->sy - a->sy) * t;
    float cx0 = cx + side * scale;
    Sprite* s = &sprites[ns++];
    s->depth = at;
    s->kind = 0;
    s->type = kind & 0xFF;
    s->sx = cx0;
    s->sy = cy;
    s->scale = scale;
    s->clip = a->clip;
    s->flip = false;
  }
  // Far to near.
  for(int i = 1; i < ns; i++)
    for(int j = i; j > 0 && sprites[j].depth > sprites[j - 1].depth; j--) {
      Sprite t = sprites[j]; sprites[j] = sprites[j - 1]; sprites[j - 1] = t;
    }
  for(int i = 0; i < ns; i++) {
    const Sprite* s = &sprites[i];
    c.fog = fog_at(sc.focal / s->scale);
    if(s->kind == 0) draw_car(&c, s);
    else if(s->type == 9) draw_sign(&c, s);
    else if(s->type >= 0x1B && s->type <= 0x1C) draw_cactus(&c, s);
    else if(s->type >= 0x1D) draw_bush(&c, s);
    else draw_rock(&c, s);
  }

  // Down to the output size.
  uint32_t inv = (uint32_t) (65536 / (ss * ss)) + (ss == 3);  // exact for 1, 2 and 4
  for(int y = row0; y < row1; y++)
    for(int x = 0; x < w; x++) {
      uint32_t rb = 0, g = 0;
      for(int j = 0; j < ss; j++) {
        const uint32_t* p = &c.px[(y * ss + j) * c.w + x * ss];
        for(int i = 0; i < ss; i++) {
          rb += p[i] & 0xFF00FF;
          g += p[i] & 0x00FF00;
        }
      }
      uint32_t r = ((rb >> 16) * inv) >> 16, b = ((rb & 0xFFFF) * inv) >> 16;
      out[y * w + x] = r << 16 | (((g >> 8) * inv) >> 16) << 8 | b;
    }
}

void tg_remix_hud(const uint8_t* m, TgRemixHud* hud) {
  bool kph = m[0x1FB0] != 0 && !(m[0x1FF6] & 0x80);
  uint16_t speed = r16(m, 0x1E5A);
  int factor = kph ? 0x18 : 0x0F;
  hud->speed = (speed & 0x8000) ? 0 : (int) ((uint16_t) (((int32_t) (int16_t) speed * factor) >> 8) >> 2);
  hud->kph = kph;
  uint16_t pos = r16(m, 0x1F00);
  if(pos & 0x8000) pos = r16(m, 0x1E7A);
  hud->position = pos + 1;
  int laps = r16(m, 0x1E56);
  int lap = (int16_t) r16(m, 0x1E76);
  hud->laps = laps;
  hud->lap = lap < 0 ? 1 : lap + 1 > laps ? laps : lap + 1;
  uint16_t t = r16(m, 0x1F26);
  if(t & 0x8000) t = r16(m, 0x1EAE);
  hud->minutes = t / 3600;
  hud->seconds = t / 60 % 60;
  hud->hundredths = t % 60 * 100 / 60;
  hud->gear = r16(m, 0x1E72) + 1;
  uint16_t n = r16(m, 0x1E6C);
  int d = n & 3;
  if((n & 0x8000) && d != 0) d--;
  hud->nitros = d;
  int full = (r16(m, 0x1E62) >> 8) >> 3;
  hud->fuel = (full > 20 ? 20 : full) * 5;
}
