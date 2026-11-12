// Top Gear 2: drawing one scaled object as hardware sprites (bank $0B, $A6EA).
//
// An object frame is a list of records, each a bounding box and two lists of parts (16x16
// and 8x8 sprites). The record that fits the object's distance is drawn: parts entirely
// outside the view window are dropped, and parts that cross its edge are drawn with blank
// "cover" sprites (tile $C0) over the part that's outside, since the SNES can't clip
// sprites to a window.

#include "../port/port.h"

#define DPB 0x0400u
#define PPU_OBJ_LEFT_FLAG 2   // PPU_OBJ_LEFT (ppu.h)
#define PPU_OBJ_GHOST_FLAG 1  // PPU_OBJ_GHOST
#define TG_NO_GROUND (-32768) // PPU_NO_GROUND

static uint16_t z(uint8_t o) { return ram16(DPB + o); }
static void zs(uint8_t o, uint16_t v) { ram16_set(DPB + o, v); }
static bool neg(uint16_t v) { return (v & 0x8000) != 0; }

// Widescreen (an enhancement, off by default): objects are kept out to the wider edges,
// and each OAM entry an object adds is marked as the road's (not the HUD's) for the PPU
// (ppu_full.c), with which side of the screen it is on. g_mark[entry] == g_stamp: drawn
// this frame by an object.
static uint8_t g_mark[0x2000], g_stamp = 1;
static int16_t g_ground;
bool tg2_sprite_road;  // set by the road objects' pass (objpass.c) while it draws
static bool g_road, g_mark_on;
static uint8_t g_flags;

// The record's bank (the original's data bank while drawing).
static uint8_t g_bank;
static uint16_t rec(uint16_t y, uint16_t o) { return tg_read16((uint32_t) g_bank << 16 | (uint16_t) (y + o)); }

// A 16 x 8 multiply by M7A (set from the object's scale), rounded: 0 when b is 0.
static uint16_t scaled(uint8_t b) {
  if(b == 0) return 0;
  tg_bus_write8(0x211C, b);
  bool c = (tg_bus_read8(0x2134) & 0x80) != 0;
  return (uint16_t) (tg_bus_read8(0x2135) + (tg_bus_read8(0x2136) << 8) + c);
}

// The ways a part can be drawn.
typedef struct PartKind {
  bool big;     // 16x16 (else 8x8)
  uint8_t top;  // dp word of the window top the top cover sits against
} PartKind;

// One OAM entry at the buffer slot x (and its high-table copy at x + $200).
static void oam(uint16_t x, uint16_t px, uint16_t py, uint16_t tile, bool big) {
  if(g_mark_on) {
    g_mark[x & 0x1FFF] = g_stamp;
    tg_port_obj_grounds[x & 0x1FFF] = g_ground;
    tg_port_obj_flags[x & 0x1FFF] = g_flags;
  }
  ram16_set(x, px);
  ram16_set((uint16_t) (x + 0x200), big ? (uint16_t) (px | 0x0200) : (uint16_t) (px & 0xFDFF));
  ram16_set((uint16_t) (x + 1), py);
  ram16_set((uint16_t) (x + 2), tile);
}

// Draws the part at ($1C, $1E) with tile $1A, plus a cover sprite for each edge in mask
// (1 top, 2 left, 4 right, 8 bottom). False when the sprite slots ran out (the original
// then abandons the whole object).
static bool emit_part(const PartKind* k, unsigned mask) {
  uint16_t n = 1;
  for(unsigned m = mask; m; m >>= 1) n += m & 1;
  uint16_t left = z(0x28);
  if(mask != 0) {
    left = (uint16_t) (left - n);
    if(neg(left)) return false;
  } else {
    left = (uint16_t) (left - 1);
  }
  zs(0x28, left);
  uint16_t size = k->big ? 16 : 8;
  uint16_t x = z(0x2C), sx = z(0x1C), sy = (uint16_t) (z(0x2E) - z(0x1E));
  if(mask & 1) oam(x, sx, (uint16_t) (z(0x2E) - z(k->top) + 1), 0x00C0, k->big), x += 4;
  if(mask & 2) oam(x, (uint16_t) (z(0x34) - size), sy, 0x00C0, k->big), x += 4;
  if(mask & 4) oam(x, z(0x36), sy, 0x00C0, k->big), x += 4;
  if(mask & 8) oam(x, sx, (uint16_t) (z(0x2E) - z(0x38) - (size - 1)), 0x00C0, k->big), x += 4;
  oam(x, sx, sy, z(0x1A), k->big);
  zs(0x2C, (uint16_t) (x + 4));
  return left != 0;
}

// The part's screen position: $1A tile, $1C x, $1E y from the record's part entry.
static void part_pos(uint16_t tile, uint16_t dx, uint16_t dy) {
  zs(0x3E, 0);
  zs(0x1A, (uint16_t) (tile + z(0x14)));
  zs(0x1C, (uint16_t) (z(0x16) + dx));
  zs(0x1E, (uint16_t) (z(0x18) - dy));
}

// Edge tests for a part of size s (16 or 8) against the window ($3A top, $34 left, $36
// right, $38 bottom). Sets the crossed edges in $3E; false if the part is entirely out.
static bool clip_part(uint16_t s) {
  uint16_t lim = (uint16_t) (s - 1), a;
  a = (uint16_t) (z(0x3A) + (s - 2) - z(0x1E));
  if(!neg(a)) {
    if(a >= lim) return false;
    zs(0x3E, (uint16_t) (z(0x3E) + 1));
  }
  a = (uint16_t) (z(0x1C) - z(0x34));
  if(neg(a)) {
    if(a < (uint16_t) -lim) return false;
    zs(0x3E, z(0x3E) | 2);
  }
  a = (uint16_t) (z(0x1C) + lim - z(0x36));
  if(!neg(a)) {
    if(a >= lim) return false;
    zs(0x3E, z(0x3E) | 4);
  }
  a = (uint16_t) (z(0x1E) - z(0x38));
  if(!neg(a)) {
    if(a >= lim) return false;
    zs(0x3E, z(0x3E) | 8);
  }
  return true;
}

// The upper window of a split view: the top at $32, the screen's sides, the bottom $30.
static bool clip_part_upper(uint16_t s) {
  uint16_t lim = (uint16_t) (s - 1);
  uint16_t a = (uint16_t) (z(0x32) + (s - 2) - z(0x1E));
  if(!neg(a)) {
    if(a >= lim) return false;
    zs(0x3E, (uint16_t) (z(0x3E) + 1));
  }
  if(neg((uint16_t) (z(0x1C) + lim))) return false;
  if(!neg((uint16_t) (z(0x1C) - 0x0100))) return false;
  return neg((uint16_t) (z(0x1E) - s - z(0x30)));
}

// Records that aren't drawn: skips its two part lists. y is the record.
static uint16_t skip_record(uint16_t y) {
  do y = (uint16_t) (y + 6); while(rec(y, 0x0A) != 0);
  do y = (uint16_t) (y + 6); while(rec(y, 0x06) != 0);
  return (uint16_t) (y + 8);
}

// A record with a ground shadow ($0C wide, at $0E) takes width from the shadow budget
// $20, less what's outside [left, right). Returns -1 when the budget is spent (the object
// isn't drawn), else 0.
static int shadow_budget(uint16_t width, uint16_t y, uint16_t left, uint16_t right) {
  if(width == 0) return 0;
  zs(0x22, width);
  zs(0x24, (uint16_t) (width << 3));
  uint16_t pos = (uint16_t) (rec(y, 0x0E) + z(0x16));
  zs(0x26, pos);
  uint16_t a = (uint16_t) (pos - left);
  if(neg(a)) {
    a = (uint16_t) (((a >> 3) | 0xE000) + z(0x22) + 1);
    if(neg(a) || a == 0) return 0;
    zs(0x22, a);
  }
  pos = (uint16_t) (pos + z(0x24));
  zs(0x26, pos);
  a = (uint16_t) (right - pos);
  if(neg(a)) {
    a = (uint16_t) (((a >> 3) | 0xE000) + z(0x22) + 1);
    if(neg(a) || a == 0) return 0;
    zs(0x22, a);
  }
  a = (uint16_t) (z(0x20) - z(0x22));
  if(neg(a)) return -1;
  zs(0x20, a);
  return 0;
}

static const PartKind kBig = { true, 0x3A }, kSmall = { false, 0x3A };
static const PartKind kBigUpper = { true, 0x32 }, kSmallUpper = { false, 0x32 };

// $0B:A6EA. Draws the object frame $08 (scaled by $0A) at ($0C, $0E), from the slot
// count $28 and buffer position $2C.
static void draw_records(uint16_t y, bool split);

void Tg2DrawSprite(TgRegs* r) {
  (void) r;
  if(z(0x28) == 0) return;
  uint16_t frame = (uint16_t) (z(0x08) << 2), scale = z(0x0A);
  uint16_t s = rom16(0x8F9B80 + scale);
  s = (uint16_t) ((s >> 3) + ((s >> 2) & 1));
  tg_bus_write8(0x211B, (uint8_t) s);
  tg_bus_write8(0x211B, (uint8_t) (s >> 8));
  g_bank = tg_rom(0x8BDBE5 + frame)[0];
  uint16_t x = (uint16_t) (rom16(0x8FCC40 + scale) + rom16(0x8BDBE3 + frame));
  uint16_t y = tg_read16((uint32_t) g_bank << 16 | x);
  uint16_t count = rec(y, 0);
  if(count == 0) return;
  y = (uint16_t) (y + 2);
  zs(0x12, count);
  zs(0x3C, (uint16_t) ((z(0x3A) != 0) | (z(0x34) != 0) << 1 | (z(0x36) != 0x0100) << 2 |
                       (z(0x38) != z(0x30)) << 3));
  bool split = z(0x30) != z(0x32);
  // Widescreen: a window as wide as the screen reaches the wider edges (no covers there).
  uint16_t left0 = z(0x34), right0 = z(0x36);
  g_road = tg_port_wide_margin && tg2_sprite_road;
  bool widen = g_road && !split && left0 == 0 && right0 == 0x0100;
  if(widen) {
    zs(0x34, (uint16_t) -tg_port_wide_margin);
    zs(0x36, (uint16_t) (0x0100 + tg_port_wide_margin));
  }
  // The screen row the object stands on: $0E is its height above the view's bottom line ($2E).
  g_ground = (int16_t) (z(0x2E) + 1 - z(0x0E));
  g_flags = (uint8_t) (((int16_t) z(0x0C) < 128 ? PPU_OBJ_LEFT_FLAG : 0) | (tg2_sprite_ghost ? PPU_OBJ_GHOST_FLAG : 0));
  g_mark_on = g_road || (tg_port_ghost && tg2_sprite_road);
  draw_records(y, split);
  if(widen) {
    zs(0x34, left0);
    zs(0x36, right0);
  }
  g_road = g_mark_on = false;
}

static void draw_records(uint16_t y, bool split) {

  for(;;) {
    // The record's position, scaled.
    zs(0x14, rec((uint16_t) (rec(y, 0) + z(0x10)), 0x1CF0));
    uint16_t sc = rec(y, 2);
    zs(0x16, (uint16_t) (scaled((uint8_t) sc) + z(0x0C)));
    zs(0x18, (uint16_t) (scaled((uint8_t) -(sc >> 8)) + z(0x0E)));
    uint16_t top = (uint16_t) (z(0x18) - rec(y, 8)), bottom = (uint16_t) (z(0x18) - rec(y, 0x0A));
    uint16_t xl = (uint16_t) (z(0x16) + rec(y, 6)), xr = (uint16_t) (z(0x16) + rec(y, 4));

    if(!split) {
      bool visible = neg((uint16_t) (bottom - z(0x38))) && !neg((uint16_t) (xl - z(0x34))) &&
                     neg((uint16_t) (xr - z(0x36))) && !neg((uint16_t) (top + 1 - z(0x3A)));
      if(visible && shadow_budget(rec(y, 0x0C), y, z(0x34), z(0x36)) < 0) visible = false;
      if(!visible) {
        y = skip_record(y);
      } else {
        for(uint16_t t; (t = rec(y, 0x10)) != 0; y = (uint16_t) (y + 6)) {
          part_pos(t, rec(y, 0x12), rec(y, 0x14));
          if(!clip_part(16)) continue;
          if(!emit_part(&kBig, z(0x3E) & z(0x3C))) return;
        }
        for(uint16_t t; (t = rec(y, 0x12)) != 0; y = (uint16_t) (y + 6)) {
          part_pos(t, rec(y, 0x14), rec(y, 0x16));
          if(!clip_part(8)) continue;
          if(!emit_part(&kSmall, z(0x3E) & z(0x3C))) return;
        }
        y = (uint16_t) (y + 0x14);
      }
    } else {
      // Split window: the part above $32 is the upper view's (its bottom at $30).
      bool visible;
      if(neg((uint16_t) (top - z(0x32)))) {  // reaches into the lower window
        visible = neg((uint16_t) (bottom - z(0x38))) && !neg((uint16_t) (xl - z(0x34))) &&
                  neg((uint16_t) (xr - z(0x36))) && !neg((uint16_t) (top + 1 - z(0x3A)));
        // (the original reads the shadow width from the bottom field here)
        if(visible && shadow_budget(rec(y, 0x0A), y, z(0x34), z(0x36)) < 0) visible = false;
      } else if(!neg((uint16_t) (bottom - z(0x38)))) {  // entirely in the upper window
        visible = neg((uint16_t) (bottom - z(0x30))) && neg((uint16_t) (xr - 0x0100)) && !neg(xl) &&
                  !neg((uint16_t) (top + 1 - z(0x32)));
        if(visible && shadow_budget(rec(y, 0x0C), y, 0, 0x0100) < 0) visible = false;
      } else {  // crosses from the upper window into the lower
        visible = neg((uint16_t) (bottom - z(0x30))) && neg((uint16_t) (xr - 0x0100)) && !neg(xl) &&
                  !neg((uint16_t) (top + 1 - z(0x3A)));
        if(visible && shadow_budget(rec(y, 0x0C), y, 0, 0x0100) < 0) visible = false;
      }
      if(!visible) {
        y = skip_record(y);
      } else {
        for(uint16_t t; (t = rec(y, 0x10)) != 0; y = (uint16_t) (y + 6)) {
          part_pos(t, rec(y, 0x12), rec(y, 0x14));
          if(neg((uint16_t) (z(0x1E) - 15 - z(0x38)))) {
            if(!clip_part(16)) continue;
            if(!emit_part(&kBig, z(0x3E) & z(0x3C))) return;
          } else {
            if(!clip_part_upper(16)) continue;
            if(!emit_part(&kBigUpper, z(0x3E))) return;
          }
        }
        for(uint16_t t; (t = rec(y, 0x12)) != 0; y = (uint16_t) (y + 6)) {
          part_pos(t, rec(y, 0x14), rec(y, 0x16));
          if(neg((uint16_t) (z(0x1E) - 7 - z(0x38)))) {
            if(!clip_part(8)) continue;
            if(!emit_part(&kSmall, z(0x3E) & z(0x3C))) return;
          } else {
            if(!clip_part_upper(8)) continue;
            if(!emit_part(&kSmallUpper, z(0x3E))) return;
          }
        }
        y = (uint16_t) (y + 0x14);
      }
    }
    uint16_t c = (uint16_t) (z(0x12) - 1);
    zs(0x12, c);
    if(c == 0) return;
  }
}

// $0B:D7E7. Finishes the sprite buffer: hides the slots that were used last frame and
// aren't now, marks every unused slot off-screen, and packs the high table (X bit 8 and
// the size bit, two bits a sprite) into 32 bytes at the start of the $200 copy.
void Tg2FinishOam(TgRegs* r) {
  uint16_t left = z(0x28), end = z(0x2C);
  uint16_t stale = (uint16_t) (left - z(0x2A));
  if(!neg(stale) && stale != 0) {
    for(uint16_t x = end; stale; stale--, x = (uint16_t) (x + 4)) ram16_set(x, 0x5555);
  }
  for(uint16_t n = left, x = end; n; n--, x = (uint16_t) (x + 4)) ram16_set((uint16_t) (x + 0x200), 0x0100);
  uint16_t t = (uint16_t) (((uint16_t) (left - 0x80) << 2) + end);
  uint16_t base = (uint16_t) (t + 0x200);
  // The flags it returns with, which an interrupt can push: V from that adc #$0200, C clear
  // (the eighth ror of a byte that started at zero), N and Z from the plb.
  r->v = (~t & base & 0x8000) != 0;
  r->c = false;
  r->n = (r->db & 0x80) != 0;
  r->z = r->db == 0;
  if(tg_port_wide_margin || tg_port_ghost) {
    // The entries no object drew this frame (the HUD's, wherever their buffer) are not
    // the road's.
    for(uint16_t a = 0; a < 0x2000; a++) {
      if(g_mark[a] != g_stamp) {
        tg_port_obj_grounds[a] = TG_NO_GROUND;
        tg_port_obj_flags[a] = 0;
      }
    }
    if(++g_stamp == 0) g_stamp = 1;
  }
  for(int k = 0; k < 32; k++) {
    uint8_t packed = 0;
    for(int j = 0; j < 4; j++) {
      uint8_t* hi = &g_ram[(uint16_t) (base + (4 * k + j) * 4 + 1)];
      packed = (uint8_t) (packed >> 2 | (*hi & 3) << 6);
      *hi >>= 2;
    }
    g_ram[(uint16_t) (base + k)] = packed;
  }
}
