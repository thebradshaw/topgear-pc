// Top Gear 3000: the race view's objects, section by section (bank $02).
//
// $02:CBD4 starts the DSP-4's object pass: the two players' markers on the course map
// (02:D8C6), then for each road section built this frame ($14: its slot, $16: the track
// position in the scenery list at $7E:8CB3, 4 bytes an entry) it places the section's
// scenery through its kind's handler (the table at $02:80CE), and every car standing in
// that section: the player's own car (with its exhaust and the gauges) or another car,
// whose sprite is picked by how far it is turned against the road. The race order
// ($1E6A list, from $28) is walked alongside, by section ($2C).

#include <stddef.h>

#include "../port/port.h"

#define REC 0xFAB4u

void Tg3SendShapeRom(TgRegs* r);
void Tg3DrawBar(TgRegs* r);
extern uint16_t tg3_obj_ident;

static uint16_t z(const TgRegs* r, uint8_t o) { return ram16((uint16_t) (r->d + o)); }
static void zs(const TgRegs* r, uint8_t o, uint16_t v) { ram16_set((uint16_t) (r->d + o), v); }
static uint16_t w(uint16_t a) { return ram16(a); }
static void ws(uint16_t a, uint16_t v) { ram16_set(a, v); }
static uint16_t l7e(uint32_t a) { return ram16((a - 0x7E0000) & 0x1FFFF); }
static void l7e_set(uint32_t a, uint16_t v) { ram16_set((a - 0x7E0000) & 0x1FFFF, v); }
// Long reads ($7E:FAB4+o,y): a stray index runs on into bank $7F, not round to $7E:0000.
#define GHOST 0xFF00u  // the ghost car's record (ghost.c), in place of a record offset
extern uint8_t tg3_ghost_record[0x36];
extern uint16_t tg3_ghost_section;
extern bool tg3_obj_ghost;
void tg3_ghost_update(void);
static uint16_t car(uint16_t y, uint16_t o) {
  if(y == GHOST) return (uint16_t) (tg3_ghost_record[o] | tg3_ghost_record[o + 1] << 8);
  return ram16((REC + o + y) & 0x1FFFF);
}

// The view: one player's, or a half of the two-player screen. $02:D2CC is the bottom half's
// own copy, on the second player's record and tables (its sections indexed from $40).
typedef struct Pass {
  uint16_t list, list_add;        // the sprite list's start ($0186) and where it lies
  uint8_t on_dp, end_dp;          // the shadow flag ($C8) and the sections' end ($90)
  uint16_t rec;                   // the player's record: $0200 or $02B3
  uint16_t fire;                  // the exhaust flame's words at $1F18.. (+2 for the second)
  uint16_t pos1, flag1, tile1, pos2, flag2, tile2, colour;  // the course map's markers
  uint16_t view, bottom, keep, cam;
  uint16_t first;                 // the first section's slot
  uint16_t lap, piece, jump;      // $1EDE/$1EE0, $1E5E, $1E66
  uint16_t near, pal, me, bars, overlay, far, overlay_add, gauge;
  bool jump_tables;               // other cars in the air use the raised road tables
  bool top_init;                  // the first section set up as the one-player view does
} Pass;
static const Pass* P;
static bool g_ghost_pass;  // the ghost car is still to be drawn in this pass

static uint32_t g_bank;
static void dput(uint16_t v) { tg_bus_write16(g_bank | 0x8000, v); }
static uint16_t dget(void) { return (uint16_t) (tg_bus_read8(g_bank | 0x8000) | tg_bus_read8(g_bank | 0x8001) << 8); }
// LDA $C000 until bit 15.
static uint16_t dwait(void) {
  for(;;) {
    uint16_t s = (uint16_t) (tg_bus_read8(g_bank | 0xC000) | tg_bus_read8(g_bank | 0xC001) << 8);
    if(s & 0x8000) return s;
  }
}
static bool minus(uint16_t a, uint16_t b) { return ((uint16_t) (a - b) & 0x8000) != 0; }
static uint16_t sext8(uint16_t v) { v &= 0xFF; return (v & 0x80) ? (uint16_t) (v | 0xFF00) : v; }

// $02:D8C6: a player's marker on the course map (A: its colour, X: its place on the map,
// Y: its tile and flags, $14: its section). The chip's sprite goes after $0184.
static void map_marker(TgRegs* r, uint16_t a, uint16_t x, uint16_t y) {
  if(tg_read16(0x82DC68 + w(0x047B)) & 0x8000) {
    // Courses whose map wraps: the start's marker at either end.
    uint16_t sec = z(r, 0x14);
    if(sec != 0) x = (sec & 0x8000) ? 0 : (uint16_t) (((uint16_t) (w(0x019A) - 1) >> 2) & 0xFFFC);
  }
  r->c = false;
  uint16_t p = adc16(r, a, l7e(0x7E4F32 + x)) & 0xFF;
  dput(0x000B);
  r->c = false;
  dput(adc16(r, l7e(0x7E4F31 + x) & 0xFF, 0x00BE));
  dput(p);
  dput(y);
  // One sprite at most.
  y = w(0x0184);
  dwait();
  uint16_t v = dget();
  r->a = v;
  if(v != 0) {
    tg_bus_write16(g_bank | (uint16_t) (0x1576 + y), dget());
    r->a = dget();
    tg_bus_write16(g_bank | (uint16_t) (0x1578 + y), r->a);
    y = (uint16_t) (y + 4);
    ws(0x0184, y);
  }
  r->x = x;
  r->y = y;
  flags_nz16(r, v != 0 ? y : 0);
}

// $02:D267: the start of a car's sprite request (X: the slot after the section's, Y: the
// car): its place on the road, at its height plus $34 and across by $32. $2A: the chip's
// first answer (how far it is turned on screen).
static void car_request(TgRegs* r, uint16_t x, uint16_t y) {
  dput(l7e(0x7E386A + x));
  dput(0x9000);
  dput(car(y, 0x00));
  uint16_t s14 = z(r, 0x14);
  r->c = false;
  uint16_t a = adc16(r, sext8(w(0x0243 + P->rec)), 0x0033);
  r->c = false;
  a = adc16(r, a, z(r, 0x34));
  dput(a);
  dput(a);
  dput(0);
  dput(z(r, 0x32));
  r->c = false;
  uint16_t t = adc16(r, (uint16_t) ((car(y, 0x01) >> 1) & 0x7E), l7e(0x7E39EC + s14));
  dput(tg_read16(0x83ACEC + t));
  dput(0);
  zs(r, 0x2A, dget());
  r->x = y;
}

static void send_rom_shape(TgRegs* r, uint16_t x) {
  r->x = x;
  Tg3SendShapeRom(r);
}

// $D07E: car y (record offset) drawn from its record, turned to one of seven views
// (x: the section's slot + 2, or the first car's).
static void draw_other(TgRegs* r, const Pass* p, uint16_t x, uint16_t y) {
  uint16_t a;
  dput(l7e(0x7E386A + x));
  dput(0x9000);
  dput(car(y, 0x00));
  uint16_t s = z(r, 0x14), d;
  bool jumping = p->jump_tables && (car(y, 0x2A) & 0x22) == 0x20;
  if(jumping) {
    dput(l7e(0x7E376E + s));
    dput(l7e(0x7E376C + s));
    dput(l7e(0x7E356E + s));
    dput(l7e(0x7E356C + s));
  } else {
    dput(l7e(0x7E36EE + s));
    dput(l7e(0x7E36EC + s));
    dput(l7e(0x7E34EE + s));
    dput(l7e(0x7E34EC + s));
  }
  r->c = false;
  uint16_t t = adc16(r, (uint16_t) ((car(y, 0x01) >> 1) & 0x7E), l7e(0x7E39EC + s));
  dput(tg_read16(0x83ACEC + t));
  dput(car(y, 0x07));
  zs(r, 0x2A, dget());
  dput(car(y, 0x0B));
  r->c = true;
  if(jumping) {
    dput(0x1000 | car(y, 0x1D));
    d = sbc16(r, l7e(0x7E356C + s), l7e(0x7E356E + s));
  } else {
    dput(z(r, 0x3C) | car(y, 0x1D));
    d = sbc16(r, l7e(0x7E34EC + s), l7e(0x7E34EE + s));
  }
  // How far it is turned against the road picks one of seven views.
  r->c = true;
  a = adc16(r, (uint16_t) ~(uint16_t) (d << 1), z(r, 0x2A));
  static const uint16_t kTurn[6] = { 0xFFA0, 0xFFC0, 0xFFE8, 0x0018, 0x0040, 0x0060 };
  uint16_t v = 0;
  while(v < 12 && !minus(a, kTurn[v >> 1])) v = (uint16_t) (v + 2);
  a = tg_read16(0x7F0000 + v);
  r->c = false;
  a = adc16(r, a, tg_read16(0x849180 + t));
  tg3_obj_ident = (uint16_t) (0x4000 | (y & 0x3FFF));  // high frame rate: this car (its record)
  send_rom_shape(r, tg_read16(0x7F0000 + a));
}

// The section's scenery handler (X: its kind), in C when there is one. Returns carry.
static bool scenery(TgRegs* r, uint16_t kind, uint16_t site) {
  r->x = kind;
  uint16_t target = tg_read16(0x8280CE + kind);
  const TgPortFunc* fn = tg_port_find(0x020000 | target);
  if(fn != NULL && fn->call == TG_CALL_JSR) {
    fn->fn(r);
    if(!tg_port_take_declined()) return r->c;
  }
  tg_call_jsr(r, target, (uint16_t) (site + 3), TG_MX_16);
  return r->c;
}

// $02:CCC7, the two-player top half, is the same with the shorter view's $39/$6C, and runs
// on into the same section code.
static void scenery_loop(TgRegs* r, const Pass* p) {
  P = p;
  uint16_t rec = p->rec;
  r->db = 0x30;  // PEA $8330; PLB
  g_bank = 0x300000;
  r->c = false;
  ws(0x0184, adc16(r, w(p->list), p->list_add));
  dput(3);
  r->c = false;
  uint16_t a1a = adc16(r, z(r, 0x8E), 0x200);
  if(p->list_add != 0) {
    r->c = false;
    a1a = adc16(r, a1a, p->list_add);
  }
  zs(r, 0x1A, a1a);
  zs(r, 0x3C, z(r, p->on_dp) == 0 ? 0x1000 : 0);
  dwait();
  dput(5);
  // The first car in the order at or behind the view's place: $28.
  uint16_t y = 0x28, x = w(0x1E90);
  r->c = w(0x0225 + rec) >= car(x, 0x01);
  sbc16(r, w(0x0227 + rec), car(x, 0x03));
  if(!r->c) {
    for(y = 0x26; y != 0; y = (uint16_t) (y - 2)) {
      x = w((uint16_t) (0x1E68 + y));
      r->c = w(0x0225 + rec) >= car(x, 0x01);
      sbc16(r, w(0x0227 + rec), car(x, 0x03));
      if(r->c) break;
    }
  }
  zs(r, 0x28, y);
  zs(r, 0x2C, w(0x0226 + rec));
  dwait();
  // The two players' markers on the course map.
  x = (uint16_t) ((w(p->pos1) >> 2) & 0xFFFC);
  if(w(p->flag1) & 0x8000) x = (uint16_t) (x + 2);
  zs(r, 0x14, w(0x0235));
  map_marker(r, p->colour, x, l7e(0x7EFAD1 + p->tile1) | 0x30FD);
  x = (uint16_t) ((w(p->pos2) >> 2) & 0xFFFC);
  if(w(p->flag2) & 0x8000) x = (uint16_t) (x + 2);
  zs(r, 0x14, w(0x02E8));
  map_marker(r, p->colour, x, l7e(0x7EFAD1 + p->tile2) | 0x30FD);
  const uint16_t kStart[] = { 9, 0x80, p->view, 0, 0, 0x100, p->keep };
  for(unsigned i = 0; i < sizeof(kStart) / sizeof(kStart[0]); i++) dput(kStart[i]);
  uint16_t h = w(0x1EF2 + p->cam);
  dput((h & 0x8000) ? (uint16_t) (h + p->bottom) : p->bottom);
  if(h & 0x8000) {
    r->c = false;
    adc16(r, h, p->bottom);
  }

  // $CDB7: the sections.
  zs(r, 0x14, p->first);
  if(p->first != 0) {
    r->c = false;
    zs(r, p->end_dp, adc16(r, z(r, p->end_dp), p->first));
  }
  zs(r, 0x16, (uint16_t) (w(0x0226 + rec) << 2));
  zs(r, 0x18, (uint16_t) (w(0x019A) << 2));
  zs(r, 0x30, (uint16_t) (w(p->lap) << 2));
  r->c = false;
  x = adc16(r, w(0x019C), w(p->piece));
  r->c = false;
  zs(r, 0x2E, (uint16_t) (adc16(r, tg_read16(0x840000 + x), 0x003F) << 2));
  uint16_t a = (uint16_t) (tg_read16(0x840010 + x) << 2);
  if(w(p->jump) & 0x8000) a = 0;
  zs(r, 0x36, a);
  ws(p->near, 0);
  ws((uint16_t) (p->near + 2), 0);
  if(!p->top_init) {
    // The second player's first section (its tables are indexed from $40).
    r->c = true;
    l7e_set(0x7E352C, sbc16(r, l7e(0x7E352E), w(0x02BF)));
    r->c = true;
    l7e_set(0x7E372C, sbc16(r, l7e(0x7E372E), w(0x02BB)));
    l7e_set(0x7E38AC, 0x0033);
    r->c = true;
    l7e_set(0x7E3A2C, sbc16(r, l7e(0x7E3A2E), 0x0080));
    goto cars;
  }
  r->c = true;
  l7e_set(0x7E34EC, sbc16(r, l7e(0x7E34EE), w(0x020C)));
  r->c = true;
  l7e_set(0x7E36EC, adc16(r, (uint16_t) ~sext8(w(0x0209)), l7e(0x7E36EE)));
  r->c = false;
  l7e_set(0x7E356C, adc16(r, l7e(0x7E34EC), l7e(0x7E356C)));
  r->c = false;
  l7e_set(0x7E376C, adc16(r, l7e(0x7E36EC), l7e(0x7E376C)));
  l7e_set(0x7E386C, 0x0033);
  r->c = true;
  l7e_set(0x7E39EC, sbc16(r, l7e(0x7E39EE), 0x0080));
  goto cars;

section:
  // $CE4E: the section's scenery.
  x = z(r, 0x16);
  r->c = false;
  a = adc16(r, x, 4);
  r->c = a >= z(r, 0x18);
  if(r->c) a = sbc16(r, a, z(r, 0x18));
  if(a == z(r, 0x30)) {
    // The lap's join: the scenery list goes on from the start.
    zs(r, 0x16, a);
    r->c = false;
    zs(r, 0x2C, adc16(r, w((uint16_t) (p->lap + 2)), z(r, 0x2C)));
    uint16_t j = w((uint16_t) (p->lap + 2));
    r->c = (j >> 14) & 1;
    a = adc16(r, (uint16_t) (j << 2), z(r, 0x16));
  }
  if(a == z(r, 0x2E)) {
    r->c = false;
    a = adc16(r, a, z(r, 0x36));
    r->c = false;
    zs(r, 0x2C, adc16(r, (uint16_t) (z(r, 0x36) >> 2), z(r, 0x2C)));
  }
  zs(r, 0x16, a);
  {
    uint16_t entry = l7e(0x7E8CB3 + x);
    if(entry == 0) {
      // Nothing here: a blank request keeps the chip in step.
      dput(l7e(0x7E386C + z(r, 0x14)));
      dput(0);
    } else {
      zs(r, 0x1C, x);
      uint16_t kind = (uint16_t) (((entry >> 8) & 0x7F) << 1);
      zs(r, 0x1E, kind);
      zs(r, 0x3A, z(r, 0x3C));
      uint16_t far = l7e(0x7E39EC + z(r, 0x14));
      uint16_t site;
      if(far < 0x0082) {
        // Close: the near palette, and remembered for the overlay.
        zs(r, 0x26, l7e(0x7E45EC + p->pal));
        ws(p->near, kind);
        site = 0xCEE7;
        r->c = false;
      } else if(far >= 0x0442) {
        zs(r, 0x26, l7e(0x7E495C + p->pal + tg_read16(0x86AD66 + kind)));
        site = 0xCED0;
        r->c = true;
      } else {
        zs(r, 0x26, l7e(0x7E45EC + p->pal + tg_read16(0x86AD66 + kind)));
        site = 0xCEB7;
        r->c = false;
      }
      if(scenery(r, kind, site)) goto done;
    }
  }

cars:
  if(g_ghost_pass && z(r, 0x2C) == tg3_ghost_section) {
    // The ghost car (an enhancement), first of the cars in its section.
    g_ghost_pass = false;
    uint16_t gx = (uint16_t) (z(r, 0x14) + 2);
    if(gx != z(r, p->end_dp)) {
      TgRegs keep = *r;
      tg3_obj_ghost = true;
      draw_other(r, p, gx, GHOST);
      tg3_obj_ghost = false;
      *r = keep;
    }
  }
  // $CEFD: the cars standing in this section, in race order.
  for(;;) {
    x = w((uint16_t) (0x1E6A + z(r, 0x28)));
    a = car(x, 0x02);
    if(a == z(r, 0x2C)) break;
    if(!minus(a, z(r, 0x2C))) goto next_section;
    zs(r, 0x28, (uint16_t) (z(r, 0x28) + 2));
  }
car_here:
  // $CF1A: car x (record offset) is in this section.
  y = x;
  r->y = y;
  x = (uint16_t) (z(r, 0x14) + 2);
  if(x == z(r, p->end_dp)) goto next_section;
  if(y != p->me || (w(0x0244 + rec) & 1)) goto other_car;
  {
    // The player's car: the exhaust flame, the car, its shadow and the gauge.
    uint16_t gear = (uint16_t) ((w(0x023A + rec) >> 8) & 0xFF);
    uint16_t f = p->fire;
    if(w(0x021A + rec) == 0) {
      ws(0x1F24 + f, w(0x1F18 + f));
      if(!(w(0x1F18 + f) & 0x8000)) {
        zs(r, 0x32, w(0x1F1C + f));
        zs(r, 0x34, w(0x1F20 + f));
        car_request(r, x, y);
        dput(car(r->x, 0x0B));
        r->c = false;
        a = adc16(r, z(r, 0x3C), w(0x1F34));
        r->c = false;
        dput(adc16(r, a, 0x0138));
        ws(0x1F24 + f, w(0x1F18 + f) & 0xFFF8);
        send_rom_shape(r, tg_read16(0x86C8B2 + w(0x1F24 + f)));
        r->x = x;
        r->y = y;
      }
    }
    if(w(0x021D + rec) & 0x8000) {
      zs(r, 0x32, 0);
      zs(r, 0x34, 0);
      car_request(r, x, y);
      dput(car(r->x, 0x0B));
      r->c = false;
      dput(adc16(r, z(r, 0x3C), 0x0F00));
      send_rom_shape(r, tg_read16(0x86CBEC + (uint16_t) (gear << 1)));
      r->x = x;
      r->y = y;
    }
    zs(r, 0x32, 0);
    zs(r, 0x34, 0);
    car_request(r, x, y);
    dput(car(r->x, 0x0B));
    r->c = false;
    dput(adc16(r, z(r, 0x3C), 0x0F00));
    send_rom_shape(r, tg_read16(0x86C97A + (uint16_t) (gear << 3)));
    r->x = x;
    r->y = y;
    car_request(r, x, y);
    dput(0);
    dput(z(r, 0x3C));
    uint16_t i = (uint16_t) (gear << 3);
    r->x = tg_read16(0x86CC66 + i);
    r->y = tg_read16(0x86CC68 + i);
    r->a = tg_read16(0x86CC64 + i);
    Tg3DrawBar(r);
    if(w(p->bars) == 2) {
      dput(l7e(0x7E386A + z(r, 0x14)));
      static const uint16_t kRest[] = { 0x9000, 0, 0x33, 0x33, 0, 0, 0x7FFF };
      for(unsigned k = 0; k < sizeof(kRest) / sizeof(kRest[0]); k++) dput(kRest[k]);
      dput(w(0x028B + rec));
      dget();
      dput(w(0x028D + rec));
      dput(z(r, 0x3C));
      send_rom_shape(r, 0xAD4A);
    }
    ws(0x0281 + rec, 0xFFFF);
    r->c = false;
    goto after_car;
  }

other_car:
  // $D062: another car. The first one seen is remembered (its far distance in $0293).
  if(w(0x0281 + rec) & 0x8000) {
    ws(0x0281 + rec, y);
    ws(0x0283 + rec, x);
    r->c = false;
    uint16_t t = adc16(r, (uint16_t) ((car(y, 0x01) >> 1) & 0x7E), l7e(0x7E39EC + z(r, 0x14)));
    ws(0x0293 + rec, tg_read16(0x83ACEC + t));
    x = w(0x0283 + rec);
  }
  draw_other(r, p, x, y);

after_car:
  // $D1B5
  if(r->c) goto done;
  zs(r, 0x28, (uint16_t) (z(r, 0x28) + 2));
  x = w((uint16_t) (0x1E6A + z(r, 0x28)));
  if(car(x, 0x02) == z(r, 0x2C)) goto car_here;

next_section:
  // $D1CD
  zs(r, 0x2C, (uint16_t) (z(r, 0x2C) + 1));
  if(z(r, 0x2C) == w(0x019A)) {
    zs(r, 0x2C, 0);
    zs(r, 0x28, 0);
  }
  zs(r, 0x14, (uint16_t) (z(r, 0x14) + 2));
  if(z(r, 0x14) != z(r, p->end_dp)) goto section;
  r->c = true;

done:
  // $D1E7: the end of the pass, and the near scenery's overlay.
  dput(0);
  dput(0x8000);
  uint16_t o = p->overlay;
  ws(0x1E02 + o, 0);
  x = w(p->near);
  if(x != 0) {
    a = tg_read16(0x86AE36 + x);
    if(a != 0) {
      zs(r, 0x14, a);
      ws(0x1E04 + o, 0x007F);
      uint16_t m = tg_read16(0x848000 + l7e(0x7E39EE + p->far));
      r->c = (m & 0x8000) != 0;
      uint16_t t = adc16(r, (uint16_t) (m << 1), a);
      ws(0x1DFE + o, tg_read16(0x860006 + t));
      ws(0x1E02 + o, tg_read16(0x860008 + t));
      x = tg_read16(0x860000 + a);
      r->c = false;
      ws(0x1DFE + o, adc16(r, l7e(0x7E6591 + x), w(0x1DFE + o)));
      uint16_t v = (uint16_t) (l7e(0x7E45EC + p->pal) << 4);
      if(p->overlay_add != 0) {
        r->c = false;
        v = adc16(r, v, p->overlay_add);
      }
      ws(0x1E00 + o, v);
    }
  }
  if(p->list_add != 0) {
    r->c = true;
    ws(p->list, sbc16(r, w(0x0184), p->list_add));
    r->c = true;
    zs(r, p->end_dp, sbc16(r, z(r, p->end_dp), p->first));
  } else {
    ws(p->list, w(0x0184));
  }
  uint16_t i = (uint16_t) (((w(0x023A + rec) >> 8) & 0xFF) << 3);
  ws(0x1E52 + p->gauge, tg_read16(0x86C974 + i));
  ws(0x1E54 + p->gauge, tg_read16(0x86C976 + i));
  ws(0x1E56 + p->gauge, tg_read16(0x86C978 + i));
  r->a = w(0x1E56 + p->gauge);
  r->c = false;  // from ASL of the gear
  r->x = i;
  r->db = 0x83;
  r->n = true;  // PLB of $83
  r->z = false;
}

// list, list_add, on_dp, end_dp, rec, fire, pos1, flag1, tile1, pos2, flag2, tile2, colour,
// view, bottom, keep, cam, first, lap, piece, jump, near, pal, me, bars, overlay, far,
// overlay_add, gauge, jump_tables, top_init
static const Pass kOne = { 0x0186, 0, 0xC8, 0x90, 0, 0, 0x0226, 0x1E66, 0, 0x02D9, 0x1E68, 0x35, 0x1E,
                           0xA9, 0xDC, 4, 0, 0, 0x1EDE, 0x1E5E, 0x1E66, 0x1DF6, 0, 0, 0x014C, 0, 0,
                           0, 0, true, true };
static const Pass kTop = { 0x0186, 0, 0xC8, 0x90, 0, 0, 0x0226, 0x1E66, 0, 0x02D9, 0x1E68, 0x35, 0x1E,
                           0x39, 0x6C, 4, 0, 0, 0x1EDE, 0x1E5E, 0x1E66, 0x1DF6, 0, 0, 0x014C, 0, 0,
                           0, 0, true, true };
// The bottom half draws the markers' tiles and places crossed over, as the original does.
static const Pass kBottom = { 0x0188, 0x440, 0xCA, 0x92, 0xB3, 2, 0x02D9, 0x1E68, 0x35, 0x0226, 0x1E66, 0, 0x8E,
                              0xA9, 0xDC, 0x74, 0x0A, 0x40, 0x1EE2, 0x1E62, 0x1E68, 0x1DF8, 0xDC, 0x35, 0x014E,
                              8, 0x40, 0x1000, 6, false, false };

void Tg3SceneryLoop(TgRegs* r) {
  tg3_ghost_update();
  g_ghost_pass = tg3_ghost_section != 0xFFFF;
  scenery_loop(r, &kOne);
  g_ghost_pass = false;
}
void Tg3SceneryLoopTop(TgRegs* r) { scenery_loop(r, &kTop); }
void Tg3SceneryLoopBottom(TgRegs* r) { scenery_loop(r, &kBottom); }
