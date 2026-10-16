// The roadside object tile cache (bank $00): each view keeps the graphics of the objects
// coming up in a 256-tile area of VRAM ($3000 for view 1, $4000 for view 2), loading them
// in $400-byte DMA steps.
//
// $1BB8 is the current view's map row (track position / 16, even), shared scratch.
// Per view (view 2's copy is $14E higher):
//   $1BBA  next source address; while choosing a slot, the size wanted
//   $1BBC  source bank
//   $1BBE  next VRAM word address
//   $1BC0  bytes still to send
//   $1BC2  head and $1BC4 tail of an 8-entry queue of objects wanted: ids at $1BC6,
//          map rows at $1BD6
//   $1BE6  objects resident (sorted by tile slot): ids at $1BE8, map rows at $1BF8
//   $1C08  per id: first tile slot; $1C88: end slot
// Object records are 8 bytes at $E882 + id x 4 (id is even): bank, source, bytes, tiles.

#include "port.h"

typedef struct TileCacheView {
  uint16_t base;      // offset of this view's variables from view 1's
  uint16_t hidden;    // direct page word; bit 15 = view off
  uint16_t position;  // direct page word: track position
  uint16_t vram;
} TileCacheView;

static const TileCacheView kViews[2] = {
  { 0x000, 0x40, 0x46, 0x3000 },
  { 0x14E, 0x53, 0x59, 0x4000 },
};

typedef struct Cache {
  TgRegs* r;
  uint16_t base;
} Cache;

static uint16_t get(const Cache* c, uint16_t adr) { return ram16((uint16_t) (adr + c->base)); }
static void put(const Cache* c, uint16_t adr, uint16_t v) { ram16_set((uint16_t) (adr + c->base), v); }
static uint16_t table(const Cache* c, uint16_t adr) { return tg_read16((uint32_t) c->r->db << 16 | adr); }

// Notes that object id is wanted at map row x: updates its row if it is already queued
// or resident, otherwise queues it.
static void want(const Cache* c, uint16_t id, uint16_t x) {
  for(uint16_t y = get(c, 0x1BC2); y != get(c, 0x1BC4); y = (uint16_t) ((y + 2) & 0x000E)) {
    if(get(c, (uint16_t) (0x1BC6 + y)) == id) {
      put(c, (uint16_t) (0x1BD6 + y), x);
      return;
    }
  }
  uint16_t y = 0;
  for(uint16_t n = get(c, 0x1BE6); n; n--, y = (uint16_t) (y + 2)) {
    if(get(c, (uint16_t) (0x1BE8 + y)) == id) {
      put(c, (uint16_t) (0x1BF8 + y), x);
      return;
    }
  }
  y = get(c, 0x1BC4);
  put(c, (uint16_t) (0x1BC6 + y), id);
  put(c, (uint16_t) (0x1BD6 + y), x);
  put(c, 0x1BC4, (uint16_t) ((y + 2) & 0x000E));
}

// Starts loading object record t (id x 4) into tile slot start.
static void begin_load(const Cache* c, const TileCacheView* v, uint16_t id, uint16_t t, uint16_t start) {
  ram16_set((uint16_t) (0x1C08 + c->base + id), start);
  ram16_set((uint16_t) (0x1C88 + c->base + id), (uint16_t) (start + get(c, 0x1BBA)));
  put(c, 0x1BBE, (uint16_t) ((start << 4) + v->vram));
  put(c, 0x1BBC, table(c, (uint16_t) (0xE882 + t)));
  put(c, 0x1BBA, table(c, (uint16_t) (0xE884 + t)));
  put(c, 0x1BC0, table(c, (uint16_t) (0xE886 + t)));
}

// Sends up to $400 more bytes of the object being loaded.
static void send(const Cache* c, const TileCacheView* v, uint16_t left) {
  if(ram16((uint16_t) (c->r->d + v->hidden)) & 0x8000) {
    put(c, 0x1BC0, 0);
    return;
  }
  uint16_t x = 0x0400;
  if(left < 0x0400) {
    x = left;
    left = 0;
  } else {
    left = (uint16_t) (left - 0x0400);
  }
  put(c, 0x1BC0, left);
  tg_bus_write8(0x420B, 0x00);
  tg_bus_write8(0x4374, (uint8_t) get(c, 0x1BBC));
  tg_bus_write16(0x4370, 0x1801);
  tg_bus_write16(0x4372, get(c, 0x1BBA));
  tg_bus_write16(0x4375, x);
  tg_bus_write16(0x2116, get(c, 0x1BBE));
  tg_bus_write8(0x420B, 0x80);
  put(c, 0x1BBA, (uint16_t) (x + get(c, 0x1BBA)));
  put(c, 0x1BBE, (uint16_t) ((x >> 1) + get(c, 0x1BBE)));
}

static void update_view(TgRegs* r, const TileCacheView* v) {
  Cache cache = { r, v->base };
  const Cache* c = &cache;
  if(ram16((uint16_t) (r->d + v->hidden)) & 0x8000) {
    put(c, 0x1BC0, 0);
    return;
  }
  uint16_t row = (uint16_t) ((ram16((uint16_t) (r->d + v->position)) >> 4) & 0xFFFE);
  ram16_set(0x1BB8, row);  // shared by both views

  // Forget resident objects $5A or more rows behind (the map wraps at $1BB6).
  uint16_t n = get(c, 0x1BE6);
  for(uint16_t x = 0; n;) {
    uint16_t at = get(c, (uint16_t) (0x1BF8 + x));
    uint16_t d = (uint16_t) (at - row);
    if(at < row) d = (uint16_t) (d + ram16(0x1BB6));
    if(d < 0x005A) {
      x = (uint16_t) (x + 2);
      n--;
      continue;
    }
    put(c, 0x1BE6, (uint16_t) (get(c, 0x1BE6) - 1));
    if(!--n) break;
    for(uint16_t k = 0, i = x; k < n; k++, i = (uint16_t) (i + 2)) {
      put(c, (uint16_t) (0x1BF8 + i), get(c, (uint16_t) (0x1BFA + i)));
      put(c, (uint16_t) (0x1BE8 + i), get(c, (uint16_t) (0x1BEA + i)));
    }
  }

  // Objects on the map 40 rows ahead (4 words): bit 15 = a list of ids, else one id.
  uint16_t x = (uint16_t) (row + 0x50);
  for(int k = 0; k < 4; k++, x = (uint16_t) (x + 2)) {
    uint16_t a = ram16(0x6400 + (uint32_t) x);
    if(!a) continue;
    if(a & 0x8000) {
      uint16_t y = table(c, (uint16_t) (0xE888 + (a & 0x3FFE)));
      for(uint16_t count = table(c, (uint16_t) (0xE89A + y)); count; count--) {
        y = (uint16_t) (y + 2);
        want(c, table(c, (uint16_t) (0xE89A + y)), x);
      }
    } else {
      want(c, table(c, (uint16_t) (0xE9BA + ((a >> 8) & 0x007E))), x);
    }
  }

  uint16_t left = get(c, 0x1BC0);
  if(left) {
    send(c, v, left);
    return;
  }
  n = get(c, 0x1BE6);
  if(n == 0) {
    uint16_t head = get(c, 0x1BC2);
    if(head == get(c, 0x1BC4)) return;
    put(c, 0x1BE6, (uint16_t) (get(c, 0x1BE6) + 1));
    put(c, 0x1BC2, (uint16_t) ((head + 2) & 0x000E));
    put(c, 0x1BF8, get(c, (uint16_t) (0x1BD6 + head)));
    uint16_t id = get(c, (uint16_t) (0x1BC6 + head));
    put(c, 0x1BE8, id);
    uint16_t t = (uint16_t) (id << 2);
    put(c, 0x1BBE, v->vram);
    put(c, 0x1BBC, table(c, (uint16_t) (0xE882 + t)));
    put(c, 0x1BBA, table(c, (uint16_t) (0xE884 + t)));
    put(c, 0x1BC0, table(c, (uint16_t) (0xE886 + t)));
    ram16_set((uint16_t) (0x1C08 + c->base + id), 0);
    ram16_set((uint16_t) (0x1C88 + c->base + id), table(c, (uint16_t) (0xE888 + t)));
    return;
  }
  if(n == 8) {  // full: forget the queue
    put(c, 0x1BC2, get(c, 0x1BC4));
    return;
  }

  // Find a gap for the next queued object: before the first resident one, between two,
  // or after the last (slots end at $100). Objects that fit nowhere are dropped.
  for(;;) {
    uint16_t head = get(c, 0x1BC2);
    if(head == get(c, 0x1BC4)) return;
    uint16_t t = (uint16_t) (get(c, (uint16_t) (0x1BC6 + head)) << 2);
    uint16_t size = table(c, (uint16_t) (0xE888 + t));
    put(c, 0x1BBA, size);
    uint16_t id = get(c, 0x1BE8);
    uint16_t insert = 0, start = 0;
    bool found = false;
    if(ram16((uint16_t) (0x1C08 + c->base + id)) >= size) {
      found = true;
    } else {
      for(uint16_t i = 1; i < n; i++) {
        uint16_t end = ram16((uint16_t) (0x1C88 + c->base + id));
        id = get(c, (uint16_t) (0x1BE8 + 2 * i));
        if((uint16_t) (ram16((uint16_t) (0x1C08 + c->base + id)) - end) >= size) {
          found = true;
          insert = i;
          start = end;
          break;
        }
      }
      if(!found) {
        uint16_t end = ram16((uint16_t) (0x1C88 + c->base + id));
        if((uint16_t) (0x0100 - end) >= size) {
          found = true;
          insert = n;
          start = end;
        }
      }
    }
    if(!found) {
      put(c, 0x1BC2, (uint16_t) ((head + 2) & 0x000E));
      continue;
    }
    for(uint16_t i = n; i > insert; i--) {
      uint16_t o = (uint16_t) (2 * i - 2);
      put(c, (uint16_t) (0x1BEA + o), get(c, (uint16_t) (0x1BE8 + o)));
      put(c, (uint16_t) (0x1BFA + o), get(c, (uint16_t) (0x1BF8 + o)));
    }
    put(c, 0x1BC2, (uint16_t) ((head + 2) & 0x000E));
    put(c, 0x1BE6, (uint16_t) (get(c, 0x1BE6) + 1));
    uint16_t o = (uint16_t) (2 * insert);
    put(c, (uint16_t) (0x1BF8 + o), get(c, (uint16_t) (0x1BD6 + head)));
    id = get(c, (uint16_t) (0x1BC6 + head));
    put(c, (uint16_t) (0x1BE8 + o), id);
    begin_load(c, v, id, t, start);
    send(c, v, get(c, 0x1BC0));
    return;
  }
}

// $00:E20D. Updates one view's tile cache per call, alternating ($1BB4 flips each call;
// view 1 when it becomes negative). $1BB6 = the map's length in rows. Registers and flags
// are don't-care afterwards.
void RaceUpdateTileCache(TgRegs* r) {
  ram16_set(0x1BB6, (uint16_t) ((ram16(0x0200) >> 4) & 0xFFFE));
  uint16_t flip = (uint16_t) ~ram16(0x1BB4);
  ram16_set(0x1BB4, flip);
  update_view(r, &kViews[(flip & 0x8000) ? 0 : 1]);
}

// After a pit stop the pit crew's graphics have overwritten the view's object tiles; at
// pit state -9 these reload every resident object, $400 bytes per frame on the frames the
// cache leaves this view alone. Variables (player 2's are $E higher): $0572 next resident
// entry (offset), $0574 objects left, $0576 VRAM address, $0578 source, $057A its bank,
// $057C bytes left of the object, $057E bytes sent this frame.
typedef struct PitReload {
  uint16_t state, v, cache, vram, limit;
  bool view_flag;  // the $1BB4 value (bit 15) the view's turn needs
} PitReload;

static const PitReload kReloads[2] = {
  { 0x1E86, 0x0000, 0x000, 0x3000, 0x0401, true },
  { 0x1E88, 0x000E, 0x14E, 0x4000, 0x0400, false },
};

static void pit_reload(TgRegs* r, const PitReload* p) {
#define PV(a) ((uint16_t) ((a) + p->v))
  uint16_t s = ram16(p->state);
  if(!(s & 0x8000) || s != 0xFFF7) return;
  if(((ram16(0x1BB4) & 0x8000) != 0) != p->view_flag) return;
  ram16_set(PV(0x057E), 0);
  uint16_t y = ram16(PV(0x0572));
  bool load = false;
  if(!y) {
    uint16_t n = ram16((uint16_t) (0x1BE6 + p->cache));
    if(!n) {
      ram16_set(p->state, (uint16_t) (s - 1));
      return;
    }
    ram16_set(PV(0x0574), n);
    load = true;
  }
  for(;;) {
    if(load) {
      uint16_t x = ram16((uint16_t) (0x1BE8 + p->cache + y));
      ram16_set(PV(0x0576), (uint16_t) ((ram16((uint16_t) (0x1C08 + p->cache + x)) << 4) + p->vram));
      uint16_t t = (uint16_t) (x << 2);
      uint32_t db = (uint32_t) r->db << 16;
      g_ram[PV(0x057A)] = tg_read8(db | (uint16_t) (0xE882 + t));
      ram16_set(PV(0x0578), tg_read16(db | (uint16_t) (0xE884 + t)));
      ram16_set(PV(0x057C), tg_read16(db | (uint16_t) (0xE886 + t)));
      y = (uint16_t) (y + 2);
    }
    load = true;
    uint16_t left = ram16(PV(0x057C));
    if(!left) {
      ram16_set(PV(0x0574), (uint16_t) (ram16(PV(0x0574)) - 1));
      if(ram16(PV(0x0574))) continue;
      ram16_set(p->state, (uint16_t) (ram16(p->state) - 1));
      return;
    }
    uint16_t total = (uint16_t) (left + ram16(PV(0x057E)));
    if(total >= p->limit) {  // fill this frame's $400 and stop
      ram16_set(PV(0x057C), (uint16_t) (total - 0x0400));
      uint16_t x = (uint16_t) (0x0400 - ram16(PV(0x057E)));
      tg_bus_write16(0x2116, ram16(PV(0x0576)));
      ram16_set(PV(0x0576), (uint16_t) ((x >> 1) + ram16(PV(0x0576))));
      tg_bus_write8(0x420B, 0x00);
      tg_bus_write8(0x4374, g_ram[PV(0x057A)]);
      tg_bus_write16(0x4370, 0x1801);
      tg_bus_write16(0x4372, ram16(PV(0x0578)));
      ram16_set(PV(0x0578), (uint16_t) (x + ram16(PV(0x0578))));
      tg_bus_write16(0x4375, x);
      tg_bus_write8(0x420B, 0x80);
      ram16_set(PV(0x0572), y);
      return;
    }
    // The rest of the object fits.
    tg_bus_write16(0x2116, ram16(PV(0x0576)));
    tg_bus_write8(0x420B, 0x00);
    tg_bus_write8(0x4374, g_ram[PV(0x057A)]);
    tg_bus_write16(0x4370, 0x1801);
    tg_bus_write16(0x4372, ram16(PV(0x0578)));
    tg_bus_write16(0x4375, left);
    if(total == 0x0400) {
      tg_bus_write8(0x420B, 0x80);
      ram16_set(PV(0x0574), (uint16_t) (ram16(PV(0x0574)) - 1));
      if(!ram16(PV(0x0574))) ram16_set(p->state, (uint16_t) (ram16(p->state) - 1));
      return;  // without saving the entry offset, as in the original
    }
    ram16_set(PV(0x057E), total);
    tg_bus_write8(0x420B, 0x80);
    ram16_set(PV(0x0574), (uint16_t) (ram16(PV(0x0574)) - 1));
    if(!ram16(PV(0x0574))) {
      ram16_set(p->state, (uint16_t) (ram16(p->state) - 1));
      return;
    }
  }
#undef PV
}

// $00:8DA8 (player 1) and $00:8EB4 (player 2). Registers and flags are don't-care
// afterwards.
void RacePitReloadP1(TgRegs* r) { pit_reload(r, &kReloads[0]); }
void RacePitReloadP2(TgRegs* r) { pit_reload(r, &kReloads[1]); }
