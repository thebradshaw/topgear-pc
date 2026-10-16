// The road renderer: the routine the race unpacks into work RAM at $7E:2000 and calls
// once per update with JSL. For each player view it walks the track segments ahead of the
// camera, projects them with the PPU multiplier and the CPU divider, and fills that
// view's per-line BG3 scroll table, its colour-pointer runs at $0C12 (the HDMA tables
// the raster IRQs use), and its list of roadside objects for RaceBuildSprites.
//
// The original code modifies its own instructions: several immediates and store
// addresses are patched at run time. This version writes the same bytes into the code
// (they are work RAM, and later calls read them), and reads them back where the original
// executes them.

#include <stddef.h>

#include "port.h"
#include "ppu.h"
#include "variables.h"

// What differs between the bottom view (player 2, run first) and the top view (player 1).
typedef struct RenderView {
  uint16_t off;          // view off flag (dp): bit 15 when nobody is in it
  uint8_t scroll_x;      // $5B / $48: the view's sideways scroll
  uint8_t base;          // $55 / $42
  uint8_t pos;           // $59 / $46: camera track position
  uint8_t height;        // $5D / $4A: camera height
  uint8_t height2;       // $57 / $44
  uint8_t far_seg;       // $5F / $4C
  uint8_t far_line;      // $61 / $4E
  uint8_t objects;       // $63 / $50: object count (bytes while filling, entries at the end)
  uint8_t ptr;           // $B4 / $A0: pointer into the colour-pointer runs
  uint8_t sky;           // $B6 / $A2: sky table pointer
  uint8_t sky_line;      // $B8 / $A4
  uint8_t horizon;       // $BE / $AA
  uint16_t player_object;   // $1834 / $1832: object entry of the player's own car
  uint16_t car_pos;         // $1830 / $182E: the car's sprite angle word
  uint16_t object_src;      // $17DC / $178A: visible cars, 4-byte entries
  uint16_t object_dst;      // $19D6 / $1836: object list, 8-byte entries
  uint16_t scroll_add;      // added to $E7 for the view's scroll table
  uint16_t seg_bias, line_bias;
  uint16_t sky_full, sky_top, sky_part;  // sky table addresses
  uint16_t horizon_base, horizon_min;
  uint16_t end_count;       // the count byte that ends the colour runs
  uint16_t fill_src;        // bank $05 source of the scroll fill for unused lines
  uint16_t filled_a, filled_b;  // lines already filled in each scroll buffer
  uint16_t lines_left;      // $1EBC / $1EBA
  bool shared_ptr;          // the bottom view steps $94 along with its pointer
  bool flag_high;           // $F5 from $1EDE's high byte (else its low byte)
  // Patched code bytes.
  uint16_t sm_colour_lo, sm_colour_hi;   // STA $0C11,Y / STA $0C12,Y addresses
  uint16_t sm_scroll_hi, sm_scroll_lo;   // STA $0734,Y / STA $0732,Y addresses
  uint16_t sm_first_add, sm_obj_b, sm_obj_or, sm_seg_add, sm_obj2_b, sm_obj2_or;
  uint16_t sm_count, sm_fill_dst, sm_fill_dst2, sm_horizon, sm_count2;
} RenderView;

static const RenderView kViews[2] = {
  {  // bottom (player 2)
    0x0053, 0x5B, 0x55, 0x59, 0x5D, 0x57, 0x5F, 0x61, 0x63, 0xB4, 0xB6, 0xB8, 0xBE,
    0x1834, 0x1830, 0x17DC, 0x19D6, 0x01A0, 0x00DB, 0x0074,
    0xB5B6, 0xB545, 0xB57B, 0x0319, 0x028B, 0x0008, 0xFB69, 0x1ED2, 0x1ED4, 0x1EBC,
    true, true,
    0x2805, 0x280A, 0x2828, 0x2838,
    0x226A, 0x23A2, 0x235E, 0x2515, 0x26E0, 0x269B,
    0x2881, 0x290B, 0x2964, 0x296F, 0x29E8,
  },
  {  // top (player 1)
    0x0040, 0x48, 0x42, 0x46, 0x4A, 0x44, 0x4C, 0x4E, 0x50, 0xA0, 0xA2, 0xA4, 0xAA,
    0x1832, 0x182E, 0x178A, 0x1836, 0x0000, 0x006B, 0x0004,
    0xB49B, 0xB42F, 0xB462, 0x0389, 0x02FB, 0x0004, 0xF7E9, 0x1ECE, 0x1ED0, 0x1EBA,
    false, false,
    0x325C, 0x3261, 0x327F, 0x328F,
    0x2CC1, 0x2DF9, 0x2DB5, 0x2F6C, 0x3137, 0x30F2,
    0x32D2, 0x3356, 0x33A9, 0x33B4, 0x342D,
  },
};

static uint16_t z(uint8_t o) { return ram16(o); }
static void z_set(uint8_t o, uint16_t v) { ram16_set(o, v); }
static uint16_t asl(uint16_t v, int n) { return (uint16_t) (v << n); }

static void m7a(uint16_t a) {
  tg_bus_write8(0x211B, (uint8_t) a);
  tg_bus_write8(0x211B, (uint8_t) (a >> 8));
}
static void m7b(uint8_t b) { tg_bus_write8(0x211C, b); }
// MPYL read as a byte and shifted left: the bit that falls out is the rounding carry.
static bool mpy_round(void) { return (tg_bus_read8(0x2134) & 0x80) != 0; }
static uint16_t mpy_mh(void) { return (uint16_t) (tg_bus_read8(0x2135) | tg_bus_read8(0x2136) << 8); }
static uint16_t mpy_lm(void) { return (uint16_t) (tg_bus_read8(0x2134) | tg_bus_read8(0x2135) << 8); }
static uint16_t divide(uint16_t a, uint8_t b) {
  tg_bus_write8(0x4204, (uint8_t) a);
  tg_bus_write8(0x4205, (uint8_t) (a >> 8));
  tg_bus_write8(0x4206, b);
  return (uint16_t) (tg_bus_read8(0x4214) | tg_bus_read8(0x4215) << 8);
}

// The 24-bit product of a 16 x 8 multiply into dp o..o+3: byte o cleared, o+1..o+2 the low
// word, o+3 the high byte; then the word at o+1 gets a quarter of itself added (rounded).
static void product_5_4(uint16_t a, uint8_t b, uint8_t o) {
  m7a(a);
  m7b(b);
  g_ram[o] = 0;
  g_ram[o + 3] = tg_bus_read8(0x2136);
  uint16_t p = mpy_lm();
  z_set((uint8_t) (o + 1), p);
  uint16_t q = (uint16_t) ((p >> 1) | (p & 0x8000));
  q = (uint16_t) ((q >> 1) | (q & 0x8000));
  bool c = ((p >> 1) & 1) != 0;  // the bit the second shift drops
  z_set((uint8_t) (o + 1), (uint16_t) (q + p + c));
}

// The rotated low 3 bits of a word, as ASL then ROL x3 leave them: bits 13-15.
static uint16_t top3(uint16_t v) { return (v >> 13) & 7; }

// $219B / $2446: an 8-step long division of |a| (a fixed-point slope) by $8E, written
// out in shifts and subtractions. Returns the quotient (negated with the carry set for a
// negative a, ready for the original's ADC).
static uint16_t slope_divide(uint16_t a, bool* c_out) {
  bool neg = (a & 0x8000) != 0;
  if(neg) a = (uint16_t) -a;
  bool c = (a >> 15) & 1;
  a = (uint16_t) (a << 1);
  for(int i = 0; i < 2; i++) {
    bool t = (a >> 15) & 1;
    a = (uint16_t) (a << 1 | c);
    c = t;
  }
  uint16_t x = a;
  z_set(0x80, (uint16_t) ((a & 0x00F8) << 8));
  a = x & 0xFF03;
  bool t = (a >> 7) & 1;
  a = (uint16_t) ((a & 0xFF00) | (uint8_t) ((a << 1) | c));
  c = t;
  a = (uint16_t) (a << 8 | a >> 8);
  x = a;
  uint16_t d = z(0x8E);
  for(int i = 0; i < 7; i++) {
    a = (uint16_t) (x - d);
    c = x >= d;
    if(!c) a = x;
    bool t80 = (z(0x80) >> 15) & 1;
    z_set(0x80, (uint16_t) (z(0x80) << 1 | c));
    c = t80;
    t = (a >> 15) & 1;
    a = (uint16_t) (a << 1 | c);
    c = t;
    x = a;
  }
  c = a >= d;
  if(!neg) {
    z_set(0x80, (uint16_t) (z(0x80) << 1 | c));
    *c_out = false;
    return z(0x80);
  }
  *c_out = true;
  return (uint16_t) ~(z(0x80) << 1 | c);
}

// Adds the next stepping into the scroll position ($70/$72 += $74/$76).
static void step_scroll(void) {
  uint32_t lo = (uint32_t) z(0x70) + z(0x74);
  z_set(0x70, (uint16_t) lo);
  z_set(0x72, (uint16_t) (z(0x72) + z(0x76) + (lo > 0xFFFF)));
}
static void step_camera(void) {
  uint32_t lo = (uint32_t) z(0x78) + z(0x74);
  z_set(0x78, (uint16_t) lo);
  z_set(0x7A, (uint16_t) (z(0x7A) + z(0x76) + (lo > 0xFFFF)));
}

// High frame rate: which list an object entry goes in (0 the top view's), and each line's
// road depth, recorded by the address of its scroll entry.
int16_t tg_port_line_depth[0x2000];
uint16_t tg_port_line_cam[0x2000];

static int list_of(const RenderView* v) { return v->object_dst == 0x1836 ? 0 : 1; }

static void line_depth(const RenderView* v, uint16_t ly, int16_t depth) {
  uint16_t a = (uint16_t) ((ram16(v->sm_scroll_lo) + ly) & 0x1FFF);
  tg_port_line_depth[a] = depth;
  tg_port_line_cam[a] = z(v->pos);
}

// One object entry: kind word, line ($8E - $20), x, y. ident: the object, for high frame
// rate (see tg_port_list_ident).
static void add_object(const RenderView* v, uint16_t x, uint16_t kind, uint16_t ident) {
  if(x < 0x200) tg_port_list_ident[list_of(v)][x >> 3] = ident;
  ram16_set((uint16_t) (v->object_dst + 2 + x), kind);
  ram16_set((uint16_t) (v->object_dst + x), (uint16_t) (z(0x8E) - 0x20));
  ram16_set((uint16_t) (v->object_dst + 4 + x), (uint16_t) (0x0200 - z(0x7A)));
  ram16_set((uint16_t) (v->object_dst + 6 + x), (uint16_t) (z(0x96) + z(0x98)));
}

// The visible cars between the previous segment and this one; x, y are the list offsets.
// first: the cars before the first segment (projected with $6A/$6E), else with $74/$80.
static void add_cars(const RenderView* v, uint16_t* xp, uint16_t* yp, bool first) {
  uint16_t x = *xp, y = *yp;
  for(;;) {
    uint16_t a = ram16((uint16_t) (v->object_src + y));
    if(a >= z(0x8E)) break;
    ram16_set((uint16_t) (v->object_dst + x), a);
    if(x < 0x200 && y < 0x50) tg_port_list_ident[list_of(v)][x >> 3] = (uint16_t) (0x8000 | tg_port_view_car[v->object_src == 0x178A ? 0 : 1][y >> 2]);
    a = ram16((uint16_t) (v->object_src + 2 + y));
    uint16_t sm_b = first ? v->sm_obj_b : v->sm_obj2_b;
    if(a & 0x8000) {  // the player's own car
      ram16_set(v->player_object, x);
      uint16_t d = (uint16_t) (ram16((uint16_t) (v->object_dst + x)) - (first ? 0x0080 : z(0x8E)));
      g_ram[sm_b] = first ? (uint8_t) d : (uint8_t) (d & 0x1F);
      ram16_set((uint16_t) (v->object_dst + 2 + x), ram16(v->car_pos) & 0x7E00);
      a = 0x007F;
    } else {
      ram16_set((uint16_t) (v->object_dst + 2 + x), a);
      uint16_t sm_or = first ? v->sm_obj_or : v->sm_obj2_or;
      ram16_set(sm_or, asl(z(first ? 0x6C : 0x76), 3));
      a = (uint16_t) (((top3(z(first ? 0x6A : 0x74)) | ram16(sm_or))) + 4);
      m7a(a);
      uint16_t d = (uint16_t) (ram16((uint16_t) (v->object_dst + x)) - (first ? 0x0080 : z(0x8E)));
      uint8_t b = first ? (uint8_t) d : (uint8_t) (d & 0x1F);
      g_ram[sm_b] = b;
      m7b(b);
      a = (uint16_t) (0x0200 - mpy_mh() - z(0x7A));
    }
    ram16_set((uint16_t) (v->object_dst + 4 + x), a);
    m7a(asl(z(first ? 0x6E : 0x80), 3));
    m7b(g_ram[sm_b]);
    if(first) a = (uint16_t) (0x0067 - mpy_mh() + z(0x98));
    else a = (uint16_t) (z(0x96) - mpy_mh() + z(0x98));
    ram16_set((uint16_t) (v->object_dst + 6 + x), a);
    y = (uint16_t) (y + 4);
    x = (uint16_t) (x + 8);
  }
  *xp = x;
  *yp = y;
}

// Counts down the run length byte at the pointer by n; true when it reached zero.
static bool shorten_run(const RenderView* v, uint8_t* sm, uint8_t n) {
  *sm = n;
  uint16_t p = z(v->ptr);
  g_ram[p] = (uint8_t) (g_ram[p] - n);
  return g_ram[p] == 0;
}

static void back_ptr(const RenderView* v) {
  z_set(v->ptr, (uint16_t) (z(v->ptr) - 3));
  if(v->shared_ptr) z_set(0x94, (uint16_t) (z(0x94) - 3));
}

static void render_view(const RenderView* v) {
  z_set(0xED, (uint16_t) (z(v->scroll_x) + 0x04FC));
  z_set(0xF1, z(0xED));
  z_set(0xEF, (uint16_t) (z(v->scroll_x) + 0x0304));
  z_set(0xF3, z(0xEF));
  uint16_t flags = ram16(0x1EDE);
  z_set(0xF5, (v->flag_high ? flags : (uint16_t) (flags << 8 | flags >> 8)) & 0xFF00);
  ram16_set(v->player_object, 0xFFFF);
  if(v->shared_ptr) z_set(0x94, 0x011E);
  z_set(0x86, 0);
  z_set(0x68, 0);
  z_set(v->objects, 0);
  z_set(0x82, 0);
  z_set(0x84, 0x00C0);
  z_set(0x8A, 0x00C0);
  z_set(v->far_seg, 0x7FFF);
  z_set(v->far_line, 0x0068);
  z_set(0x92, 0x019C);
  z_set(0x96, 0x0067);
  z_set(0x78, 0x8000);
  z_set(0x7A, (uint16_t) (z(v->scroll_x) + 0x0180));
  z_set(0x7C, (uint16_t) (z(v->base) - z(v->scroll_x)));
  uint16_t pos = z(v->pos);
  uint16_t y = (pos >> 4) & 0xFFFE;
  z_set(0x9A, y);
  z_set(0x8E, (uint16_t) (((pos & 0x001F) ^ 0x001F) + 1));
  // Patch the stores for this view's tables.
  ram16_set(v->sm_colour_hi, z(0xE9));
  ram16_set(v->sm_colour_lo, (uint16_t) (z(0xE9) - 1));
  uint16_t s = (uint16_t) (z(0xE7) + v->scroll_add);
  if(tg_port_interp) {
    for(uint16_t i = 0; i < 0x1A0; i++) tg_port_line_depth[(s + i) & 0x1FFF] = PPU_NOT_ENTRY;
    for(uint16_t i = 0; i < 0x1A0; i += 4) tg_port_line_depth[(s + i) & 0x1FFF] = PPU_NO_DEPTH;
  }
  ram16_set(v->sm_scroll_lo, s);
  ram16_set(v->sm_scroll_hi, (uint16_t) (s + 2));
  // The first, partial segment.
  uint16_t w = ram16((uint16_t) (0x5800 + y));
  z_set(0x9E, w);
  z_set(0x9C, (uint16_t) (ram16((uint16_t) (0x3960 + (w & 0x7FFE))) - v->seg_bias));
  m7a(asl(ram16((uint16_t) (0x4C02 + y)), 3));
  m7b(g_ram[0x8E]);
  bool c = mpy_round();
  z_set(0x7C, (uint16_t) (mpy_mh() + z(0x7C) + c));
  uint16_t h = z(v->height);
  z_set(0x98, h);
  uint16_t a = (uint16_t) ((z(v->height2) - h) << 1);
  z_set(0x6E, a);
  m7a(asl(a, 3));
  m7b(g_ram[0x8E]);
  g_ram[0x8E] = (uint8_t) (g_ram[0x8E] + 0x80);
  c = mpy_round();
  z_set(0x8C, (uint16_t) (mpy_mh() + c));
  uint16_t hill = ram16((uint16_t) (0x4000 + y));
  z_set(0x90, (uint16_t) (z(0x6E) - hill));
  m7a(asl((uint16_t) (hill - ram16((uint16_t) (0x4002 + y))), 3));
  m7b((uint8_t) (((g_ram[0x8E] - 1) & 0x1F) ^ 0x1F));
  c = mpy_round();
  z_set(0x90, (uint16_t) (mpy_mh() + z(0x90) + c));
  uint16_t x = g_ram[0x342E + z(0x8E)];
  a = x;
  if(a >= 0x00C0) {  // never happens in the recordings
    a |= 0xFF00;
    x = 0;
  }
  z_set(0x8A, a);
  z_set(0x88, (uint16_t) (0x00C0 - a));
  ram16_set(v->sm_first_add, g_ram[0x38A0 + x]);
  a = z(0x8C);
  c = false;
  if(a != 0) a = slope_divide(a, &c);  // never happens in the recordings
  a = (uint16_t) (a + ram16(v->sm_first_add) + c);
  z_set(0x7E, a);
  z_set(0x80, a);
  product_5_4(z(0x7C), g_ram[0x88], 0x74);
  product_5_4(z(0x7C), 0x2D, 0x6A);
  a = z(0x6E);
  if(!(a & 0x8000)) {
    a = (uint16_t) (divide(a, 0xA0) + 0x12);
  } else {  // never happens in the recordings
    a = (uint16_t) (0x12 - divide((uint16_t) -a, 0xA0));
  }
  z_set(0x6E, a);
  x = 0;
  y = 0;
  add_cars(v, &x, &y, true);
  // $2704 ends each segment; $23C7 starts the next.
  for(;;) {
    z_set(v->objects, x);
    z_set(0x68, y);
    a = z(0x80);
    bool skip = false;
    if(a != 0 && (a & 0x8000)) {
      z_set(0x98, (uint16_t) (z(0x98) - a));
      if(z(v->far_line) == 0x0068) {
        uint16_t l = (uint16_t) (z(0x96) + 1);
        if(l != 0x0068) {
          z_set(v->far_line, l);
          z_set(v->far_seg, (uint16_t) (z(0x8E) - 0x20));
        }
      }
    } else if(a != 0) {
      x = a;
      uint16_t t = (uint16_t) (a - z(0x98) - 1);
      if(t & 0x8000) {
        z_set(0x98, (uint16_t) ~t);
      } else {
        skip = true;
      }
    }
    if(!skip) {
      if(z(0x8A) & 0x8000) goto finish_past;
      step_camera();
      goto next_segment;
    }
    // $2753: lines to draw for this segment.
    uint16_t lines = x, line = 0;  // high frame rate: this segment's lines, the one drawn next
    z_set(0x70, z(0x78));
    z_set(0x72, z(0x7A));
    step_camera();
    if(x != 1) {
      uint16_t yv = z(0x75);
      uint16_t q = divide(z(0x87), g_ram[0x80]);
      z_set(0x87, q);
      if(!(yv & 0x8000)) {
        q = divide(yv, g_ram[0x80]);
        g_ram[0x74] = 0;
        g_ram[0x77] = 0;
        z_set(0x75, q);
      } else {
        q = divide((uint16_t) -yv, g_ram[0x80]);
        g_ram[0x74] = 0;
        g_ram[0x77] = 0xFF;
        q = (uint16_t) -q;
        z_set(0x75, q);
        if(q == 0) g_ram[0x77] = 0;
      }
    }
    // Lines hidden behind the hill so far.
    while(z(0x98) != 0) {
      uint32_t d = (uint32_t) z(0x82) - z(0x86);
      z_set(0x82, (uint16_t) d);
      a = (uint16_t) (z(0x84) - z(0x88) - (d > 0xFFFF));
      if(a & 0x8000) goto finish_past;
      z_set(0x84, a);
      step_scroll();
      z_set(0x98, (uint16_t) (z(0x98) - 1));
      x--;
      line++;
    }
    {
      // A colour-pointer run: x lines of segment $9E.
      uint16_t py = z(0x94);
      ram16_set((uint16_t) (ram16(v->sm_colour_lo) + py), x);
      ram16_set((uint16_t) (ram16(v->sm_colour_hi) + py), z(0x9E));
      z_set(0x94, (uint16_t) (py - 3));
      uint16_t ly = z(0x92);
      for(;;) {
        uint32_t d = (uint32_t) z(0x82) - z(0x86);
        z_set(0x82, (uint16_t) d);
        a = (uint16_t) (z(0x84) - z(0x88) - (d > 0xFFFF));
        if(a & 0x8000) {
          // $2863: the road went over the horizon part way through.
          z_set(0x92, ly);
          z_set(0x96, ly >> 2);
          z_set(v->ptr, (uint16_t) (z(0x94) + ram16(v->sm_colour_lo) + 1 + 2));
          if(!shorten_run(v, &g_ram[v->sm_count], (uint8_t) x)) back_ptr(v);
          goto sky;
        }
        z_set(0x84, a);
        a = (uint16_t) (a + z(0x9C));
        z_set(0x9C, (uint16_t) (z(0x9C) + 1));
        ram16_set((uint16_t) (ram16(v->sm_scroll_hi) + ly), a);
        step_scroll();
        ram16_set((uint16_t) (ram16(v->sm_scroll_lo) + ly), z(0x72));
        if(tg_port_interp && lines) {
          // Depth: through the segment from its near end to its far end ($8E).
          line_depth(v, ly, (int16_t) ((int16_t) (z(0x8E) - 0x20) * 8 + (256 * line + 128) / lines));
          line++;
        }
        ly = (uint16_t) (ly - 4);
        if(ly & 0x8000) {
          // $284D: every line is drawn.
          ram16_set(v->lines_left, 0);
          z_set(0x96, 0xFFFF);
          z_set(v->ptr, (uint16_t) (z(0x94) + ram16(v->sm_colour_lo) + 1 + 2));
          ram16_set(v->sky, v->sky_full);
          x--;
          if(shorten_run(v, &g_ram[v->sm_count2], (uint8_t) x)) goto end_runs;
          goto back_and_end;
        }
        if(--x == 0) break;
      }
      z_set(0x92, ly);
      z_set(0x96, ly >> 2);
    }
next_segment:
    // $23C7: the next segment.
    y = (uint16_t) (z(0x9A) + 2);
    z_set(0x9A, y);
    w = ram16((uint16_t) (0x5800 + y));
    z_set(0x9E, w);
    z_set(0x9C, (uint16_t) (ram16((uint16_t) (0x3960 + (w & 0x7FFE))) - v->line_bias - z(0x96)));
    a = (uint16_t) (z(0x8E) + 0x20);
    z_set(0x8E, a);
    z_set(0x82, 0);
    z_set(0x84, z(0x8A));
    x = g_ram[0x342E + a];
    a = x;
    if(a >= 0x00C0) {
      a |= 0xFF00;
      x = 0;
    }
    z_set(0x8A, a);
    z_set(0x88, (uint16_t) (z(0x84) - a));
    ram16_set(v->sm_seg_add, g_ram[0x38A0 + x]);
    // The slope, unless it overflows (the road leaves the view).
    {
      uint16_t s1 = (uint16_t) (ram16((uint16_t) (0x4000 + y)) + z(0x90));
      uint16_t n = (uint16_t) (s1 + z(0x8C));
      if(z(0x8C) & 0x8000) {
        if((s1 & 0x8000) && !(n & 0x8000)) goto finish_past;
      } else {
        if(!(s1 & 0x8000) && (n & 0x8000)) goto finish_past;  // never happens in the recordings
      }
      z_set(0x8C, n);
    }
    a = z(0x8C);
    c = false;
    if(a != 0) a = slope_divide(a, &c);
    a = (uint16_t) (a + ram16(v->sm_seg_add) + c);
    x = a;
    z_set(0x80, (uint16_t) (a - z(0x7E)));
    z_set(0x7E, x);
    a = (uint16_t) (ram16((uint16_t) (0x4C00 + y)) + z(0x7C));
    z_set(0x7C, a);
    product_5_4(a, g_ram[0x88], 0x74);
    if(z(0xF5) != 0) {
      // Track edges for the window: where the road's sides cross, the limits so far.
      z_set(0x1C, 0);
      bool cc = (uint32_t) z(0x78) + z(0x74) > 0xFFFF;
      a = (uint16_t) (z(0x7A) + z(0x76) + cc);
      z_set(0x1A, (uint16_t) (0x0200 - a + 0x0400));
      uint16_t t = g_ram[0x342E + z(0x8E)];
      if(t >= 0x00C0) t = 0;
      ram16_set(0x1B7E, (uint16_t) (t + 9));
      tg_bus_write8(0x4202, 0xA2);
      tg_bus_write8(0x4203, g_ram[0x1B7E]);
      a = (uint16_t) (tg_bus_read8(0x4216) | tg_bus_read8(0x4217) << 8);
      a = (uint16_t) (a + 1);
      a = (uint16_t) (a << 8 | a >> 8);
      a = (uint16_t) (((a << 1) | (a >> 15)) & 0x01FF);
      uint16_t half = a;
      uint16_t n = (uint16_t) (half + z(0x1A));
      uint16_t f1 = z(0xF1);
      z_set(0xF1, n);
      if(n > f1 && n < z(0xED)) {
        z_set(0xED, n);
        z_set(0x1C, 0xFFFF);
      }
      n = (uint16_t) (z(0x1A) - half);
      uint16_t f3 = z(0xF3);
      z_set(0xF3, n);
      uint16_t kind = 0;
      if(n < f3 && n > z(0xEF)) {
        z_set(0xEF, n);
        kind = (z(0x1C) & 0x8000) ? 0x1200 : 0xC00A;  // 0x1200 never happens in the recordings
      } else if(z(0x1C) & 0x8000) {
        kind = 0xC008;
      }
      if(kind != 0) {
        x = z(v->objects);
        add_object(v, x, kind, (uint16_t) (((z(0x9A) << 1) | 1) + 1));
        z_set(v->objects, (uint16_t) (x + 8));
      }
    }
    // The segment's roadside object.
    x = z(v->objects);
    a = ram16((uint16_t) (0x6400 + y));
    if(a != 0) {
      if(a == 0xC004) z_set(0xF5, 0xFFFF);
      else if(a == 0xC006) z_set(0xF5, 0);
      add_object(v, x, a, (uint16_t) ((z(0x9A) << 1) + 1));
      x = (uint16_t) (x + 8);
    }
    y = z(0x68);
    add_cars(v, &x, &y, false);
  }

finish_past:
  // $2853: the road ended (went past the top of the view) between runs.
  z_set(v->ptr, (uint16_t) (z(0x94) + ram16(v->sm_colour_lo)));
  if(v->shared_ptr) z_set(0x94, (uint16_t) (z(0x94) - 3));
sky:
  // $2894: the horizon line, and the sky above it.
  ram16_set(v->lines_left, (uint16_t) (z(0x96) + 1));
  a = z(0x90);
  if(!(a & 0x8000)) a = (uint16_t) (0x1A - divide(a, 0x0B));
  else a = (uint16_t) (divide((uint16_t) -a, 0x0B) + 0x1A);
  a = (uint16_t) (a + z(v->height));
  if(a & 0x8000) {
    // The horizon is above the view: the remaining lines show the road's last segment.
    uint16_t p = z(v->ptr);
    ram16_set(p, (uint16_t) (z(0x96) + 1));
    ram16_set((uint16_t) (p + 1), ram16((uint16_t) (0x5800 + z(0x9A) + 2)) & 0xFFF0);
    ram16_set(v->sm_fill_dst, ram16(v->sm_scroll_hi));
    a = (uint16_t) (0 - 1 - v->line_bias - z(0x96));
    uint16_t ly = z(0x92);
    do {
      ram16_set((uint16_t) (ram16(v->sm_fill_dst) + ly), a);
      a++;
      ly = (uint16_t) (ly - 4);
    } while(!(ly & 0x8000));
    z_set(v->sky, v->sky_full);
    z_set(0x96, 0xFFFF);
    goto back_and_end;
  }
  z_set(v->horizon, a);
  if(a < z(0x96)) {
    // Lines between the road's end and the horizon.
    ram16_set(v->sm_horizon, a);
    x = (uint16_t) (z(0x96) - a);
    uint16_t p = z(v->ptr);
    ram16_set(p, x);
    ram16_set((uint16_t) (p + 1), ram16((uint16_t) (0x5800 + z(0x9A) + 2)) & 0xFFF0);
    back_ptr(v);
    ram16_set(v->sm_fill_dst2, ram16(v->sm_scroll_hi));
    a = (uint16_t) (0 - 1 - v->line_bias - z(0x96));
    uint16_t ly = z(0x92);
    do {
      ram16_set((uint16_t) (ram16(v->sm_fill_dst2) + ly), a);
      a++;
      ly = (uint16_t) (ly - 4);
    } while(--x != 0);
    z_set(0x96, ram16(v->sm_horizon));
  }
  a = z(0x96);
  if(a == 0x0067) {  // never happens in the recordings
    z_set(v->sky, v->sky_top);
  } else {
    z_set(v->sky_line, (uint16_t) (a + 1 + v->line_bias));
    z_set(v->sky, v->sky_part);
  }
  {
    uint16_t p = z(v->ptr);
    ram16_set(p, (uint16_t) ((z(0x96) + 1) | 0x0080));
    a = (uint16_t) (0x00B8 - z(v->horizon));
    if(a & 0x8000) a = 0;  // never happens in the recordings
    else if(a >= 0x00B9) a = 0x00B8;  // never happens in the recordings
    ram16_set((uint16_t) (p + 1), (uint16_t) ((a << 1) + ram16(0x0104)));
    a = (uint16_t) (v->horizon_base - z(v->horizon));
    if(a < v->horizon_min) a = v->horizon_min;  // never happens in the recordings
    z_set(v->horizon, a);
  }
back_and_end:
  back_ptr(v);
end_runs:
  {
    uint16_t p = z(v->ptr);
    ram16_set(p, v->end_count);
    ram16_set((uint16_t) (p + 1), ram16(0x0104));
  }
  z_set(v->objects, z(v->objects) >> 3);
  // Lines past those filled last time get the default scroll from ROM.
  uint16_t filled = z(0xE7) == 0x08D2 ? v->filled_b : v->filled_a;
  uint16_t lines = (uint16_t) (z(0x96) + 1);
  if(lines > ram16(filled)) {
    uint16_t n = (uint16_t) ((lines - ram16(filled)) << 2);
    uint16_t off = (uint16_t) (ram16(filled) << 2);
    uint16_t src = (uint16_t) (off + v->fill_src);
    uint16_t dst = (uint16_t) (off + ram16(v->sm_scroll_lo));
    for(uint16_t i = 0; i < n; i++) g_ram[(uint16_t) (dst + i)] = tg_read8(0x050000 | (uint16_t) (src + i));
  }
  ram16_set(filled, lines);
}

// $7E:2000, JSL. Renders the bottom view, then the top view. A view that's off gets a
// single blank run. Registers and flags are don't-care afterwards.
void RaceRenderViews(TgRegs* r) {
  static const uint8_t kStart[] = { 0xE2, 0x30, 0x8B, 0x4B, 0xAB, 0xC2, 0x30, 0xA5, 0x53 };
  for(size_t i = 0; i < sizeof(kStart); i++) {
    if(g_ram[0x2000 + i] != kStart[i]) {  // some other code is unpacked there
      tg_port_decline();
      return;
    }
  }
  if(r->d != 0) {
    tg_port_decline();
    return;
  }
  const RenderView* bottom = &kViews[0];
  if(z(bottom->off) & 0x8000) {
    z_set(0x94, 0x011B);
    z_set(bottom->objects, 0);
    uint16_t p = (uint16_t) (z(0xE9) + 0x011D);
    z_set(bottom->ptr, p);
    ram16_set(p, 0x0074);
    ram16_set((uint16_t) (p + 1), ram16(0x0104));
    z_set(bottom->sky, 0xB511);
  } else {
    render_view(bottom);
  }
  const RenderView* top = &kViews[1];
  if(z(top->off) & 0x8000) {
    z_set(top->objects, 0);
    uint16_t p = (uint16_t) (z(bottom->ptr) - 3);
    z_set(top->ptr, p);
    ram16_set(p, 0x006C);
    ram16_set((uint16_t) (p + 1), ram16(0x0104));
    z_set(top->sky, 0xB3FE);
  } else {
    render_view(top);
  }
}
