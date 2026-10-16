// Race sprites (bank $01): turning sprite definitions into OAM entries.
//
// RaceBuildSprites runs with the data bank set to $01, so its absolute addresses are
// bank $01 ROM tables and (below $2000) low RAM. It fills an OAM buffer through the
// direct page pointer $CC, keeping its place in these words:
//   $1B92  offset of the next 4-byte entry
//   $1B94  offset of the high-table byte for that entry
//   $1B96  which 2-bit pair of that byte (0-3)

#include "port.h"
#include "ppu.h"

// Smooth scaling (an enhancement, off by default): the OAM entries of each object are
// tagged with a record of the object's largest art and its exact scale, and the PPU draws
// that, filtered, in their place (ppu.h, ppu_smooth.c). Objects then grow smoothly instead
// of jumping between the game's few pre-drawn sizes. The OAM buffers stay as the original
// builds them.
bool tg_port_smooth_sprites;
uint16_t tg_port_obj_tags[0x2000];  // by low RAM address of an OAM buffer entry
PpuObjScale tg_port_obj_scales[0x1000];
static uint16_t g_next_scale = 1;
static bool g_skip_scale;  // the player's own car keeps its size

// The size factor the original uses for a key ($01:A55B), as a smooth curve.
static float size_curve(uint16_t key) { return 27650.0f / key - 16.5f; }

// The four copies of the sprite adder differ only in these.
typedef struct Variant {
  uint16_t tiles;       // tile base table, indexed by the definition's first word
  uint8_t oam_ptr;      // direct page pointer to the OAM buffer
  uint8_t y_add;        // added to each part's screen y
  uint8_t bottom_dp;    // direct page word with the view's bottom line, or 0 for $68
  bool split;           // 2-player views: full when $1B92 reaches $1BC, checked after
                        // every part; otherwise when $1B94 passes $21F
} Variant;

static const Variant kVariants[4] = {
  { 0x1C08, 0xCC, 0x04, 0x00, false },  // $01:8A87
  { 0x1C08, 0xCC, 0x04, 0x4E, true },   // $01:8C5C
  { 0x1D56, 0xD8, 0x74, 0x00, false },  // $01:8E33
  { 0x1D56, 0xD8, 0x74, 0x61, true },   // $01:9008
};

// Absolute and (dp),y addressing in the routine's data bank.
static uint16_t abs16(TgRegs* r, uint16_t adr) { return tg_read16((uint32_t) r->db << 16 | adr); }
static uint32_t oam_adr(TgRegs* r, const Variant* v, uint16_t y) {
  return ((uint32_t) r->db << 16) + (uint16_t) (ram16((uint16_t) (r->d + v->oam_ptr))) + y;
}
static uint8_t oam_read(TgRegs* r, const Variant* v, uint16_t y) { return tg_read8(oam_adr(r, v, y) & 0xffffff); }
static void oam_write(TgRegs* r, const Variant* v, uint16_t y, uint8_t val) { tg_bus_write8(oam_adr(r, v, y) & 0xffffff, val); }

// A part list: a count, then 6-byte entries of x offset, y offset and tile word. The first
// list is small sprites (8x8), the second large, which differ in the lowest y they allow
// and in the size and sign bits they put in the high table.
// High-table update for one part: OR and AND with per-slot mask tables (0 = none).
typedef struct HighBits {
  uint16_t or_mask, and_mask;
  bool and_first;
} HighBits;

typedef struct PartList {
  uint16_t base;          // table address the count is read from, plus x
  uint16_t lowest_y;      // entries with y below this (signed) are dropped
  HighBits left, right;   // for an x below 0, and for an x of 0 or more
  bool first;             // the small-parts list
} PartList;

static const PartList kLists[2] = {
  { 0xB245, 0xFFF1, { 0xADF5, 0, false }, { 0xADED, 0xADFD, true }, true },
  { 0xB247, 0xFFF9, { 0xADF1, 0xADF9, false }, { 0, 0xAE01, false }, false },
};

static uint8_t high_bits(TgRegs* r, const HighBits* h, uint8_t v, uint16_t slot) {
  uint8_t o = h->or_mask ? tg_read8((uint32_t) r->db << 16 | (uint16_t) (h->or_mask + slot)) : 0;
  uint8_t a = h->and_mask ? tg_read8((uint32_t) r->db << 16 | (uint16_t) (h->and_mask + slot)) : 0xFF;
  return h->and_first ? (uint8_t) ((v & a) | o) : (uint8_t) ((v | o) & a);
}

static bool negative(uint16_t v) { return v & 0x8000; }

// Adds one list's parts at the sprite position. Returns false when the original returns
// early with carry set (OAM full). x is the table offset of the list's count. *x_out gets
// the X register the original ends the list with: for the first list the offset after its
// last entry, for the second whatever the last part left there.
static bool add_parts(TgRegs* r, const Variant* v, const PartList* l, uint16_t x, uint16_t* x_out) {
  uint16_t count = abs16(r, (uint16_t) (l->base + x));
  *x_out = x;
  if(count == 0) return true;
  ram16_set(0x1B8C, count);
  ram16_set(0x1B8E, x);
  do {
    x = ram16(0x1B8E);
    *x_out = x;
    ram16_set(0x1B8E, (uint16_t) (x + 6));
    uint16_t y = ram16(0x1B92);
    uint16_t sx = (uint16_t) (abs16(r, (uint16_t) (l->base + 2 + x)) + ram16(0x1B88));
    // The original compares with CMP and tests N alone, so these are not true signed
    // comparisons near the ends of the range.
    if(!negative((uint16_t) (sx - ram16((uint16_t) (r->d + 0xF7)))) ||
       negative((uint16_t) (sx - ram16((uint16_t) (r->d + 0xF9))))) continue;
    bool left = sx >= 0x8000;
    oam_write(r, v, y, (uint8_t) sx);
    y++;
    uint16_t sy = (uint16_t) (abs16(r, (uint16_t) (l->base + 4 + x)) + ram16(0x1B8A));
    uint16_t bottom = v->bottom_dp ? ram16((uint16_t) (r->d + v->bottom_dp)) : 0x0068;
    if(!negative((uint16_t) (sy - bottom)) || negative((uint16_t) (sy - l->lowest_y))) continue;
    oam_write(r, v, y, (uint8_t) (sy + v->y_add));
    y++;
    uint16_t tile = (uint16_t) (abs16(r, (uint16_t) (l->base + 6 + x)) + ram16(0x1B90) + ram16(0x1B9A));
    oam_write(r, v, y, (uint8_t) tile);
    oam_write(r, v, (uint16_t) (y + 1), (uint8_t) (tile >> 8));
    y = (uint16_t) (y + 2);
    ram16_set(0x1B92, y);
    uint16_t hy = ram16(0x1B94), slot = ram16(0x1B96);
    oam_write(r, v, hy, high_bits(r, left ? &l->left : &l->right, oam_read(r, v, hy), slot));
    *x_out = (uint16_t) (slot + 1);
    slot = (uint16_t) ((slot + 1) & 3);
    ram16_set(0x1B96, slot);
    if(slot == 0) {
      ram16_set(0x1B94, (uint16_t) (hy + 1));
      if(!v->split && hy >= 0x021F) return false;
    }
    if(v->split && ram16(0x1B92) >= 0x01BC) return false;
  } while(ram16_set(0x1B8C, (uint16_t) (ram16(0x1B8C) - 1)), ram16(0x1B8C) != 0);
  if(l->first) *x_out = ram16(0x1B8E);
  return true;
}

// A sprite definition (bank $01 $B245 + offset): a tile base index, a repeat count, then
// that many blocks of [palette index, small parts, large parts]. Every definition in the
// recordings has one block. Returns false when the buffer filled.
static uint16_t oam_base(TgRegs* r, const Variant* v);

// Full-screen view (an enhancement, off by default): each OAM buffer entry an object adds
// records the line the object stands on, so the PPU can move it with the road (ppu_full.c).
bool tg_port_full_view;
bool tg_port_full_frame;
int16_t tg_port_obj_grounds[0x2000];  // by low RAM address of an OAM buffer entry

static void mark_ground(TgRegs* r, const Variant* v, uint16_t from, int16_t ground) {
  uint16_t base = oam_base(r, v);
  for(uint16_t y = from; y < ram16(0x1B92); y = (uint16_t) (y + 4)) tg_port_obj_grounds[(uint16_t) (base + y) & 0x1FFF] = ground;
}

static bool add_parts_all(TgRegs* r, const Variant* v);

static bool add_sprite(TgRegs* r, const Variant* v) {
  if(!tg_port_full_view) return add_parts_all(r, v);
  uint16_t y0 = ram16(0x1B92);
  bool ok = add_parts_all(r, v);
  mark_ground(r, v, y0, (int16_t) (int16_t) (ram16(0x1B8A) + v->y_add));
  return ok;
}

static bool add_parts_all(TgRegs* r, const Variant* v) {
  uint16_t def = abs16(r, (uint16_t) (0xAE15 + (uint16_t) (ram16(0x1B86) + ram16(0x1B76))));
  ram16_set(0x1B90, abs16(r, (uint16_t) (v->tiles + abs16(r, (uint16_t) (0xB245 + def)))));
  ram16_set(0x1B98, abs16(r, (uint16_t) (0xB245 + def + 2)));
  uint16_t x = (uint16_t) (def + 4);
  for(;;) {
    ram16_set(0x1B9A, abs16(r, (uint16_t) (0x1B9C + abs16(r, (uint16_t) (0xB245 + x)))));
    x = (uint16_t) (x + 2);
    if(!add_parts(r, v, &kLists[0], x, &x)) return false;
    if(!add_parts(r, v, &kLists[1], x, &x)) return false;
    uint16_t left = (uint16_t) (ram16(0x1B98) - 1);
    ram16_set(0x1B98, left);
    if(left == 0) return true;
  }
}

static void add_sprite_call(TgRegs* r, const Variant* v) { r->c = !add_sprite(r, v); }

// $01:8A87, $01:8C5C, $01:8E33, $01:9008. Add the parts of sprite definition ($1B86 +
// $1B76) at position $1B88/$1B8A to an OAM buffer, with tile base from the definition's
// header. Return carry clear, or carry set when the buffer is full. The first two fill
// the buffer at $CC for player 1's view, the others the one at $D8 for player 2's; the
// second and fourth are for split-screen views (bottom line in direct page).
// A, X, Y, N, V and Z are don't-care.
void RaceAddSprite(TgRegs* r) { add_sprite_call(r, &kVariants[0]); }
void RaceAddSpriteSplit(TgRegs* r) { add_sprite_call(r, &kVariants[1]); }
void RaceAddSpriteP2(TgRegs* r) { add_sprite_call(r, &kVariants[2]); }
void RaceAddSpriteP2Split(TgRegs* r) { add_sprite_call(r, &kVariants[3]); }

// ---------------------------------------------------------------------------------------
// RaceBuildSprites: each view's OAM buffer from its list of visible objects.

typedef struct HeaderSprite {
  uint16_t pos;   // RAM word with the sprite's x and y
  uint16_t attr;  // tile and attributes
} HeaderSprite;

typedef struct SpriteView {
  uint8_t off_dp;      // bit 15 set: view not drawn
  uint8_t half_dp;     // set to half the buffer offset where the split starts
  uint8_t count_dp;    // objects in the list
  uint8_t split_dp;    // objects with a key at or past this go in the split part
  uint8_t bottom_dp;   // the split part's bottom line
  uint8_t bottom_add;  // added to it for the row of blank sprites closing the split part
  uint16_t used;       // entries used last time ($1B92 / 4), for hiding leftovers
  uint16_t list;       // 8-byte records: key, sprite word, x, y
  uint16_t player;     // offset of the player's car record (negative: none)
  uint16_t flame;      // bit 15: draw the exhaust flame on the player's car
  uint16_t smoke;      // bit 15: draw tyre smoke (every other race frame)
  uint16_t car_pos;    // bits 9-11 pick the car's angle frames
  uint16_t pos_add;    // added to the header sprites' positions
  HeaderSprite header[3];
  const Variant* normal;
  const Variant* split;
  uint16_t special, special_ret;              // handler table for special objects, and
  uint16_t special_split, special_split_ret;  // where the original's JSR returns to
} SpriteView;

static const SpriteView kSpriteViews[2] = {
  { 0x40, 0xCA, 0x50, 0x4C, 0x4E, 0x04, 0x00C8, 0x1836, 0x1832, 0x1E6C, 0x1E66, 0x182E, 0x0000,
    { { 0x0112, 0x307F }, { 0x0116, 0x30FF }, { 0x0114, 0x327F } }, &kVariants[0], &kVariants[1],
    0x91DF, 0x831F, 0x91EF, 0x848A },
  { 0x53, 0xD6, 0x63, 0x5F, 0x61, 0x74, 0x00D4, 0x19D6, 0x1834, 0x1E6E, 0x1E68, 0x1830, 0x7000,
    { { 0x0114, 0x327F }, { 0x0116, 0x32FF }, { 0x0112, 0x307F } }, &kVariants[2], &kVariants[3],
    0x91FF, 0x883C, 0x920F, 0x89A7 },
};

static uint16_t dp16(TgRegs* r, uint8_t o) { return ram16((uint16_t) (r->d + o)); }
static void dp16_set(TgRegs* r, uint8_t o, uint16_t v) { ram16_set((uint16_t) (r->d + o), v); }
static uint8_t abs8(TgRegs* r, uint16_t adr) { return tg_read8((uint32_t) r->db << 16 | adr); }
static uint16_t rec(const SpriteView* w, uint16_t x, uint16_t f) { return ram16((uint16_t) (w->list + x + f)); }
static uint16_t swap16(uint16_t v) { return (uint16_t) (v << 8 | v >> 8); }

static void oam_write16(TgRegs* r, const Variant* v, uint16_t y, uint16_t val) {
  oam_write(r, v, y, (uint8_t) val);
  oam_write(r, v, (uint16_t) (y + 1), (uint8_t) (val >> 8));
}

// a x b on the CPU's multiplier, rounded and scaled down: ((a * b + 1) >> 7) & $1FF.
static uint16_t scaled(uint8_t a, uint8_t b) {
  tg_bus_write8(0x4202, a);
  tg_bus_write8(0x4203, b);
  return (uint16_t) ((uint16_t) (a * b + 1) >> 7) & 0x01FF;
}

// The angle frames of the player's car: bits 9-11 of its position word, times 8.
static uint16_t car_frames(const SpriteView* w) { return (uint16_t) (((ram16(w->car_pos) & 0x0E00) >> 8) << 3); }

// The flame and smoke drawn over the player's car. False if the buffer filled.
static bool car_effects(TgRegs* r, const SpriteView* w, const Variant* v) {
  if(ram16(w->flame) & 0x8000) {
    ram16_set(0x1B86, (uint16_t) (((dp16(r, 0x0E) << 1) & 0x0002) + 0x03BC + car_frames(w)));
    if(!add_sprite(r, v)) return false;
  }
  uint16_t t = ram16(0x1EAE);
  if((ram16(w->smoke) & 0x8000) && !(t & 1)) {
    uint16_t a = (t >> 1) & 7;
    if(a >= 6) a = (uint16_t) (a - 4);
    ram16_set(0x1B86, (uint16_t) (a * 2 + 0x03B0 + car_frames(w)));
    if(!add_sprite(r, v)) return false;
  }
  return true;
}

enum { DRAWN, FULL, FULL_IN_GROUP, SPECIAL };

static uint16_t oam_base(TgRegs* r, const Variant* v) { return (uint16_t) oam_adr(r, v, 0); }

// The art of the sprite definition for frame word w (as $1B86) into rec, scaled by f about
// the origin. False if it has no parts or uses other tiles than the frame the game draws
// (only the drawn frame's tiles are sure to be in VRAM).
static bool record_art(TgRegs* r, const Variant* v, uint16_t w, uint16_t drawn_tiles, PpuObjScale* rec) {
  uint16_t def = abs16(r, (uint16_t) (0xAE15 + w));
  uint16_t id = abs16(r, (uint16_t) (0xB245 + def));
  if(id != drawn_tiles) return false;
  uint16_t tiles = abs16(r, (uint16_t) (v->tiles + id));
  uint16_t palette = ram16((uint16_t) (0x1B9C + abs16(r, (uint16_t) (0xB245 + def + 4))));
  uint16_t x = (uint16_t) (def + 6);
  rec->count = 0;
  for(int list = 0; list < 2; list++) {
    uint16_t base = list == 0 ? 0xB245 : 0xB247;
    uint16_t n = abs16(r, (uint16_t) (base + x));
    for(uint16_t i = 0; i < n; i++) {
      uint16_t e = (uint16_t) (base + 2 + x + i * 6);
      if(rec->count == PPU_OBJ_PARTS) return false;
      PpuObjPart* p = &rec->parts[rec->count++];
      p->dx = (int16_t) abs16(r, e);
      p->dy = (int16_t) abs16(r, (uint16_t) (e + 2));
      p->attr = (uint16_t) (abs16(r, (uint16_t) (e + 4)) + tiles + palette);
      p->big = list == 0;  // the first list is 16x16 parts, the second 8x8
    }
    x = (uint16_t) (x + n * 6);
  }
  return rec->count != 0;
}

// Draws the sprite at $1B88/$1B8A (frame word $1B86) as the game does, and with smooth
// scaling tags its OAM entries with the object's largest art (frame base 14) at the exact
// size for its key, or its own frame shrunk from the size where the game starts using it.
static bool add_sprite_smooth(TgRegs* r, const Variant* v, uint16_t key) {
  uint16_t y0 = ram16(0x1B92);
  float ax = (float) (int16_t) ram16(0x1B88), ay = (float) (int16_t) ram16(0x1B8A) + v->y_add;
  uint16_t word = ram16(0x1B86);
  bool ok = add_sprite(r, v);
  if(!tg_port_smooth_sprites || g_skip_scale || key < 0x80 || key >= 0x3F0 || ram16(0x1B92) == y0) return ok;
  uint16_t id = g_next_scale;
  PpuObjScale* rec = &tg_port_obj_scales[id];
  uint16_t drawn_tiles = abs16(r, (uint16_t) (0xB245 + abs16(r, (uint16_t) (0xAE15 + word))));
  uint16_t frame = word & 0x000F;
  // The size each frame is drawn at: where the game starts using it, the nearest key.
  uint16_t big = (uint16_t) ((word & ~0x000F) | 0x000E);
  float f;
  if(record_art(r, v, big, drawn_tiles, rec)) {
    f = size_curve(key) / size_curve(0x80) * 1.16f;  // the original's size on average
  } else if(record_art(r, v, word, drawn_tiles, rec)) {
    uint16_t start = key;
    while(start > 0x80 && abs8(r, (uint16_t) (0xA94D + start - 1)) == frame) start--;
    f = size_curve(key) / size_curve(start);
  } else {
    return ok;
  }
  if(!(f > 0.02f && f <= 1.2f)) return ok;
  rec->ax = ax;
  rec->ay = ay;
  rec->f = f;
  uint16_t bottom = v->bottom_dp ? ram16((uint16_t) (r->d + v->bottom_dp)) : 0x0068;
  rec->clipTop = v->y_add;
  rec->clipBottom = (int16_t) (bottom + v->y_add);
  g_next_scale = (uint16_t) (g_next_scale + 1 < 0x1000 ? g_next_scale + 1 : 1);
  uint16_t base = oam_base(r, v);
  for(uint16_t y = y0; y < ram16(0x1B92); y = (uint16_t) (y + 4)) tg_port_obj_tags[(uint16_t) (base + y) & 0x1FFF] = id;
  return ok;
}

// One object: $1B7A key, $1B86 sprite word, $1B88/$1B8A position. The key gives a size
// ($A55B, used to scale x offsets) and a frame base ($A94D). Sprite words with bit 15
// clear are one sprite (bits 9-14 frame, bit 8 offset to the left, bits 0-7 x offset);
// with bit 15 set, bits 1-13 pick a group of sprites from the $AE03 table, or with bit 14
// also set, a routine that draws the object (SPECIAL: the caller runs it).
static int draw_object(TgRegs* r, const Variant* v) {
  uint16_t key = ram16(0x1B7A);
  ram16_set(0x1B7C, abs8(r, (uint16_t) (0xA94D + key)));
  uint16_t size = abs8(r, (uint16_t) (0xA55B + key));
  if(size >= 0xC0) size = 0;
  ram16_set(0x1B7E, (uint16_t) (size + 9));
  uint16_t word = ram16(0x1B86);
  if(!(word & 0x8000)) {
    uint16_t d = scaled((uint8_t) word, (uint8_t) ram16(0x1B7E));
    uint16_t x = ram16(0x1B88);
    ram16_set(0x1B88, (word & 0x0100) ? (uint16_t) (x - d) : (uint16_t) (x + d));
    ram16_set(0x1B86, (uint16_t) ((((uint16_t) (swap16(word) << 3)) & 0x07F0) + ram16(0x1B7C)));
    return add_sprite_smooth(r, v, key) ? DRAWN : FULL;
  }
  if((word & 0x7FFE) >= 0x4000) return SPECIAL;
  ram16_set(0x1B84, ram16(0x1B88));
  uint16_t x = abs16(r, (uint16_t) (0xAE03 + (word & 0x7FFE)));
  ram16_set(0x1B80, abs16(r, (uint16_t) (0xAE07 + x)));
  ram16_set(0x1B82, (uint16_t) (x + 2));
  do {
    x = ram16(0x1B82);
    ram16_set(0x1B82, (uint16_t) (x + 4));
    uint16_t a = abs16(r, (uint16_t) (0xAE09 + x));
    uint16_t d = scaled((uint8_t) a, (uint8_t) ram16(0x1B7E));
    uint16_t base = ram16(0x1B84);
    ram16_set(0x1B88, (a & 0x8000) ? (uint16_t) (base - d) : (uint16_t) (base + d));
    ram16_set(0x1B86, (uint16_t) (abs16(r, (uint16_t) (0xAE07 + x)) + ram16(0x1B7C)));
    if(!add_sprite_smooth(r, v, key)) return FULL_IN_GROUP;
    ram16_set(0x1B80, (uint16_t) (ram16(0x1B80) - 1));
  } while(ram16(0x1B80) != 0);
  return DRAWN;
}

static void load_object(const SpriteView* w, uint16_t x) {
  ram16_set(0x1B86, rec(w, x, 2));
  ram16_set(0x1B88, rec(w, x, 4));
  ram16_set(0x1B8A, rec(w, x, 6));
}

// Moves every 4th byte from y + 1 (the y coordinates) off screen, count + 1 entries.
static void hide_entries(TgRegs* r, const Variant* v, uint16_t y, uint16_t count) {
  do {
    y++;
    oam_write(r, v, y, 0xF0);
    y = (uint16_t) (y + 3);
    count--;
  } while(!(count & 0x8000));
}

// Closes the split part with 17 blank sprites on the line below it, all large.
static void close_split(TgRegs* r, const SpriteView* w, const Variant* v) {
  uint16_t y = ram16(0x1B92);
  uint16_t pos = swap16((uint16_t) (dp16(r, w->bottom_dp) + w->bottom_add));
  uint16_t y0 = y;
  for(int i = 0; i < 17; i++, y = (uint16_t) (y + 4)) {
    oam_write16(r, v, y, pos);
    oam_write16(r, v, (uint16_t) (y + 2), 0);
  }
  ram16_set(0x1B92, y);
  if(tg_port_full_view) mark_ground(r, v, y0, (int16_t) (pos >> 8));
  uint16_t hy = ram16(0x1B94), slot = ram16(0x1B96);
  uint8_t a = oam_read(r, v, hy);
  for(uint16_t k = slot; k != 4; k++) a |= (uint8_t) (abs8(r, (uint16_t) (0xADF1 + k)) | abs8(r, (uint16_t) (0xADED + k)));
  oam_write(r, v, hy, a);
  hy++;
  for(int i = 0; i < 3; i++, hy++) oam_write(r, v, hy, 0xFF);
  a = oam_read(r, v, hy);
  for(uint16_t k = slot; !(k & 0x8000); k--) a |= (uint8_t) (abs8(r, (uint16_t) (0xADF1 + k)) | abs8(r, (uint16_t) (0xADED + k)));
  oam_write(r, v, hy, a);
  slot = (uint16_t) ((slot + 1) & 3);
  ram16_set(0x1B96, slot);
  if(slot == 0) {
    if(hy >= 0x021F) return;  // never happens in the recordings
    hy++;
  }
  ram16_set(0x1B94, hy);
}

// Runs the original routine for a special object (bridges, tunnels and the like: they
// draw sprites and adjust the 2-player window bands), as the original's JSR (table,x).
static void run_special(TgRegs* r, uint16_t table, uint16_t ret) {
  uint16_t x = ram16(0x1B86) & 0x3FFE;
  TgRegs h = *r;
  h.a = x;
  h.x = x;
  h.y = 0;
  h.c = true;
  h.v = false;
  flags_nz16(&h, x);
  tg_call_jsr(&h, abs16(r, (uint16_t) (table + x)), ret, TG_MX_16);
}

enum { END_HIDE, END_KEEP, BACK_TO_NORMAL };

// The objects from the split point on, drawn with the split variant; x is the record at
// the split point. A special object here sends the original back to the normal loop.
static int build_split_part(TgRegs* r, const SpriteView* w, uint16_t x) {
  const Variant* v = w->normal;  // the player's car still goes in the normal part
  g_skip_scale = false;
  dp16_set(r, w->half_dp, (uint16_t) (ram16(0x1B92) >> 1));
  dp16_set(r, 0x1A, x);
  uint16_t p = ram16(w->player);
  if(!(p & 0x8000)) {
    ram16_set(0x1B88, rec(w, p, 4));
    ram16_set(0x1B8A, rec(w, p, 6));
    if(!car_effects(r, w, v)) return END_HIDE;
    // The car's shadow.
    ram16_set(0x1B86, (uint16_t) ((((uint16_t) (swap16(rec(w, p, 2)) << 3)) & 0x07F0) | 0x000E));
    ram16_set(0x1B88, rec(w, p, 4));
    ram16_set(0x1B8A, rec(w, p, 6));
    if(!add_sprite(r, v)) return END_HIDE;
  }
  load_object(w, dp16(r, 0x1A));
  if(ram16(0x1B92) >= 0x01BC) return END_HIDE;
  v = w->split;
  for(;;) {
    int res = draw_object(r, v);
    if(res == SPECIAL) {
      run_special(r, w->special_split, w->special_split_ret);
      return BACK_TO_NORMAL;
    }
    if(res != DRAWN) break;
    dp16_set(r, w->count_dp, (uint16_t) (dp16(r, w->count_dp) - 1));
    if(dp16(r, w->count_dp) == 0) break;
    x = ram16(0x1B78);
    ram16_set(0x1B78, (uint16_t) (x + 8));
    load_object(w, x);
    ram16_set(0x1B7A, rec(w, x, 0));
  }
  close_split(r, w, v);
  return END_HIDE;
}

// The objects before the split point, then the split part if one is reached.
static int build_objects(TgRegs* r, const SpriteView* w) {
  const Variant* v = w->normal;
  for(;;) {
    uint16_t x = ram16(0x1B78);
    ram16_set(0x1B78, (uint16_t) (x + 8));
    ram16_set(0x1B7A, rec(w, x, 0));
    if(x == ram16(w->player)) {
      ram16_set(0x1B88, rec(w, x, 4));
      ram16_set(0x1B8A, rec(w, x, 6));
      if(!car_effects(r, w, v)) return END_HIDE;
    }
    g_skip_scale = x == ram16(w->player);
    if(ram16(0x1B7A) >= dp16(r, w->split_dp)) {
      int res = build_split_part(r, w, x);
      if(res != BACK_TO_NORMAL) return res;
    } else {
      load_object(w, x);
      int res = draw_object(r, v);
      if(res == FULL) return END_HIDE;
      if(res == FULL_IN_GROUP) return END_KEEP;
      if(res == SPECIAL) run_special(r, w->special, w->special_ret);
    }
    dp16_set(r, w->count_dp, (uint16_t) (dp16(r, w->count_dp) - 1));
    if(dp16(r, w->count_dp) == 0) return END_HIDE;
  }
}

static void build_view(TgRegs* r, const SpriteView* w) {
  const Variant* v = w->normal;
  if(dp16(r, w->off_dp) & 0x8000) return;
  if(tg_port_smooth_sprites || tg_port_full_view) {
    uint16_t base = oam_base(r, v);
    for(uint16_t y = 0; y < 0x200; y++) {
      tg_port_obj_tags[(uint16_t) (base + y) & 0x1FFF] = 0;
      tg_port_obj_grounds[(uint16_t) (base + y) & 0x1FFF] = PPU_NO_GROUND;
    }
  }
  dp16_set(r, 0xF7, 0x0100);
  dp16_set(r, 0xF9, 0xFFF1);
  dp16_set(r, w->half_dp, 0);
  ram16_set(0x1B78, 0);
  ram16_set(0x1B92, 0x000C);
  ram16_set(0x1B96, 0x0003);
  ram16_set(0x1B94, 0x0200);
  for(int i = 0; i < 3; i++) {
    oam_write16(r, v, (uint16_t) (i * 4), (uint16_t) (ram16(w->header[i].pos) + w->pos_add));
    oam_write16(r, v, (uint16_t) (i * 4 + 2), w->header[i].attr);
  }
  oam_write(r, v, 0x0200, 0x00);
  if(dp16(r, w->count_dp) == 0) {
    uint16_t n = (uint16_t) (ram16(w->used) - 3);
    if(n & 0x8000) return;
    hide_entries(r, v, 0x000C, n);
  } else if(build_objects(r, w) == END_HIDE) {
    uint16_t y = ram16(0x1B92);
    uint16_t n = (uint16_t) ((y >> 2) - ram16(w->used));
    if(n & 0x8000) hide_entries(r, v, y, (uint16_t) ~n);
  }
  ram16_set(w->used, (uint16_t) (ram16(0x1B92) >> 2));
}

// $01:8000. Builds both views' OAM buffers (through $CC and $D8) from their lists of
// visible objects, and resets the 2-player window bands at $0151-$0179 that the object
// routines adjust. Special objects are still drawn by the original routines.
// Leaves 16-bit A and index; registers and flags are don't-care.
void RaceBuildSprites(TgRegs* r) {
  uint8_t db = r->db;
  r->db = 0x01;
  ram16_set(0x0167, 0xFF00);
  ram16_set(0x0169, 0);
  ram16_set(0x016B, 0);
  ram16_set(0x016D, 0x0070);
  if(ram16(0x1EDE) & 0x00FF) {  // never happens in the recordings
    ram16_set(0x016F, ram16(0x1EBA));
    ram16_set(0x0153, 0);
  }
  ram16_set(0x015D, 0xFF00);
  ram16_set(0x1EC0, 0);
  ram16_set(0x0171, 0xFF00);
  ram16_set(0x0173, 0);
  ram16_set(0x0175, 0);
  ram16_set(0x0177, 0x0070);
  if(ram16(0x1EDE) & 0xFF00) {
    ram16_set(0x0179, ram16(0x1EBC));
    ram16_set(0x013F, 0);
  }
  ram16_set(0x0149, 0xFF00);
  ram16_set(0x1EC2, 0);
  build_view(r, &kSpriteViews[0]);
  build_view(r, &kSpriteViews[1]);
  r->db = db;
}
