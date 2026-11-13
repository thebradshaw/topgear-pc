// Top Gear 3000: the road ahead, step by step (bank $02).
//
// $02:DFAD runs each frame of a race. The track is a list of road steps ($019A of them);
// $0226 is the step the camera has reached and $023E the last one prepared. For every
// step newly in view it fills the 32-entry ring of step settings at $7E:3AEC..$7E:402C
// (curve, hill, edges, scenery, colours: two copies each, $40 apart) from the course's
// pieces in banks $84/$85 (the piece table at $84:0000+$9C+$A2, $14 bytes a piece) and
// runs the handler of anything placed on the step (the list at $7E:8CB4, handlers at
// $02:819E). Then it places the course's roadside objects that have come into range (the
// list at $7E:5B91, 5 bytes each, through 05:CDAF), wraps the lap round, and works out the
// road's tilt ($0237) from the heights either side of the camera.

#include <stddef.h>

#include "../port/port.h"

// $02:E503 is the second player's copy on the two-player screen: its record ($02B3 on), its
// ring of step settings ($580 on), its own handler table, and no check for a join piece.
typedef struct Ahead {
  uint16_t rec, trk, s2, s4, sa;  // shifts: the record, the ring, and $1Exx/$01xx words
  uint8_t obj_dp, piece_dp;       // $A0, $A2
  uint32_t handlers, kinds;       // the step handlers ($02:819E), the special kinds ($02:E261)
  uint16_t kind_edge, kind_flat;  // that table's two targets
  uint16_t site_handler, site_near, site_far;
  uint16_t far_flag, row_add, pal;
  bool join_check;
} Ahead;
static const Ahead* V;
static uint16_t g_d;
static uint16_t z(uint8_t o) { return ram16((uint16_t) (g_d + o)); }
static void zs(uint8_t o, uint16_t v) { ram16_set((uint16_t) (g_d + o), v); }
// Bank $7E (and on into $7F), as data bank $7E or long $7E addressing reaches it.
static uint16_t m(uint32_t a) { return ram16(a & 0x1FFFF); }
static void ms(uint32_t a, uint16_t v) { ram16_set(a & 0x1FFFF, v); }
static uint16_t w(uint16_t a) { return ram16(a); }
static void ws(uint16_t a, uint16_t v) { ram16_set(a, v); }
// Both copies of a ring entry.
static void ring(uint16_t a, uint16_t y, uint16_t v) {
  ms((uint32_t) a + y, v);
  ms((uint32_t) a + 0x40 + y, v);
}
static uint16_t rd(uint32_t a) { return tg_read16(a); }

static void dput(uint16_t v) { tg_bus_write16(0x308000, v); }
static uint16_t dget(void) {
  uint8_t lo = tg_bus_read8(0x308000);
  uint8_t hi = tg_bus_read8(0x308001);
  return (uint16_t) (lo | hi << 8);
}
static uint16_t swap(uint16_t v) { return (uint16_t) (v << 8 | v >> 8); }
static bool neg(uint16_t v) { return (v & 0x8000) != 0; }

static void call(TgRegs* r, uint32_t target, uint16_t site, bool jsl) {
  const TgPortFunc* fn = tg_port_find(target);
  if(fn != NULL && fn->call == (jsl ? TG_CALL_JSL : TG_CALL_JSR)) {
    fn->fn(r);
    if(!tg_port_take_declined()) return;
  }
  if(jsl) tg_call_jsl(r, target, (uint16_t) (site + 4), TG_MX_16);
  else tg_call_jsr(r, (uint16_t) target, (uint16_t) (site + 3), TG_MX_16);
}

// $02:EA49: the height of the road at step a (the chip finds the step's piece).
static uint16_t height(uint16_t a) {
  dput(0);
  dput((uint16_t) (((uint16_t) (a << 2)) >> 8));
  dput(0x0A);
  uint32_t s = (uint32_t) dget() + z(0x98);
  zs(0x18, (uint16_t) s);
  uint16_t x = (uint16_t) s;
  dget();
  uint16_t t = (uint16_t) ((a & 0x3E) + rd(0x840002u + x) + (s >> 16));
  return rd(0x850000u + t);
}

// $02:E1D3-$02:E2C1: a step at a piece's join or a special piece. Returns false where the
// original goes on to $02:E2D5 (the plain settings without the scenery colour); true when
// it has set everything itself.
static bool special_step(uint16_t a, uint16_t x, uint16_t y) {
  while(a >= 0x40) {
    uint16_t f = (uint16_t) (m(0x3D6Cu + V->trk + y) | 8);
    ring(0x3D6C + V->trk, y, f);
    ring(0x3FEC + V->trk, y, m(0x3CEAu + V->trk + y));
    zs(V->piece_dp, (uint16_t) (z(V->piece_dp) + 0x14));
    if(!neg(w(0x1E66 + V->s2))) {
      uint16_t t = rd(0x840010u + x);
      ws(0x1EE6 + V->s2, t);
      zs(0x16, (uint16_t) (t + z(0x16)));
    }
    x = (uint16_t) (z(0x9C) + z(V->piece_dp));
    uint16_t b = rd(0x840000u + x);
    if(z(0x16) < b) return false;
    a = (uint16_t) (z(0x16) - b);
  }
  if(a == 0x3E && neg(w(0x1E66 + V->s2))) {
    ring(0x3D6C + V->trk, y, 0x8001);
    a = 0x3E;
  }
  a = (uint16_t) (a << 1);
  zs(0x1E, a);
  zs(0x20, x);
  ring(0x3DEC + V->trk, y, rd(0x850000u + (uint16_t) (a + rd(0x840004u + x))));
  ring(0x3E6C + V->trk, y, rd(0x850000u + (uint16_t) (z(0x1E) + rd(0x840006u + z(0x20)))));
  uint16_t target = rd(V->kinds + rd(0x84000Au + z(0x20)));
  if(target == V->kind_flat) {
    ring(0x3C6C + V->trk, y, 0xFF00);
    ring(0x3F6C + V->trk, y, 0x0100);
  } else if(target == V->kind_edge) {
    uint16_t e = m(0x3DECu + V->trk + y);
    if(neg(e)) {
      ring(0x3C6C + V->trk, y, (uint16_t) (e >> 1 | 0x8000));
      ring(0x3F6C + V->trk, y, 0x3FFF);
    } else {
      ring(0x3F6C + V->trk, y, (uint16_t) (e >> 1));
      ring(0x3C6C + V->trk, y, 0xC001);
    }
  }
  x = z(0x20);
  a = z(0x1E);
  if(a >= 0x40) {
    a = (uint16_t) (a - 0x40);
    x = (uint16_t) (x + 2);
  }
  ring(0x3FEC + V->trk, y, m(0x5391u + (uint16_t) (a + rd(0x84000Cu + x))));
  return true;
}

// $02:E0C1: one road step comes into view.
static void prepare_step(TgRegs* r) {
  if(z(0x16) >= z(0x22)) {
    zs(0x16, (uint16_t) (z(0x16) + w(0x1EE0 + V->s4)));
    zs(0x22, 0x7FFF);
  }
  uint16_t s16 = z(0x16);
  dput(0);
  dput((uint16_t) (((uint16_t) (s16 << 2)) >> 8));
  dput(0x0A);
  uint16_t x = (uint16_t) (dget() + z(0x98));
  zs(0x18, x);
  dget();
  uint16_t c = s16 >> 15;  // from ASL
  uint16_t y = (uint16_t) ((s16 << 1) & 0x3E);
  zs(0x1A, y);
  zs(0x1C, s16 & 0x3E);
  x = (uint16_t) (z(0x1C) + rd(0x840000u + x) + c);
  ring(0x3AEC + V->trk, y, rd(0x850000u + x));
  x = (uint16_t) (z(0x1C) + rd(0x840002u + z(0x18)));
  ring(0x3B6C + V->trk, y, (uint16_t) (rd(0x850000u + x) - w(0x1E1E + V->s2)));
  if((y & 6) == 6) ws(0x1E1E + V->s2, rd(0x850000u + x));
  uint16_t a = (y & 2) ? (uint16_t) (z(0x1C) | 1) : z(0x1C);
  c = a >> 15;
  a = (uint16_t) (a << 1);
  if(!(a & 0x40)) x = (uint16_t) (a + rd(0x840006u + z(0x18)) + c);
  else x = (uint16_t) ((a & 0x3F) + rd(0x840008u + z(0x18)) + c);
  ring(0x3CEC + V->trk, y, m(0x5391u + x));
  x = rd(0x840004u + z(0x18));
  uint16_t v;
  for(;;) {
    v = rd(0x850000u + x);
    if(rd(0x850002u + x) >= z(0x1A)) break;
    x = (uint16_t) (x + 4);
  }
  y = z(0x1A);
  ring(0x3BEC + V->trk, y, v);
  ring(0x3D6C + V->trk, y, 0);
  x = (uint16_t) (z(0x9C) + z(V->piece_dp));
  uint16_t b = rd(0x840000u + x);
  bool plain = true, colour = true;
  if(z(0x16) < b) {
    if((uint16_t) (z(0x16) + 1) == z(0x22) && neg(w(0x1E66 + V->s2))) ring(0x3D6C + V->trk, y, 0x8002);
  } else {
    colour = false;
    plain = !special_step((uint16_t) (z(0x16) - b), x, y);
  }
  if(colour) ring(0x3FEC + V->trk, y, (uint16_t) (((z(0x16) & 7) << 3) + w(0x1F0A + V->s2)));
  if(plain) {
    // $02:E2D5
    ring(0x3DEC + V->trk, y, 0);
    ring(0x3E6C + V->trk, y, 0);
    ring(0x3C6C + V->trk, y, w(0x1F0E + V->s4));
    ring(0x3F6C + V->trk, y, w(0x1F10 + V->s4));
  }
  x = (uint16_t) (z(0x16) << 2);
  uint16_t k = m(0x8CB4u + x) & 0xFF;
  if(k) {
    r->db = 0x7E;
    r->x = (uint16_t) (k << 1);
    r->y = x;
    r->a = r->x;
    call(r, 0x020000u | rd(V->handlers + r->x), V->site_handler, false);
  }
  zs(0x16, (uint16_t) (z(0x16) + 1));
  if(z(0x16) == w(0x019A)) {
    zs(0x16, 0);
    zs(V->piece_dp, 0);
  }
}

// $02:E31E: the roadside objects now in range. False when the lap wrapped (go again).
static bool place_objects(TgRegs* r) {
  uint16_t x = z(V->obj_dp);
  if(w(0x0226 + V->rec) >= m(0x5B91u + x)) {
    uint16_t y = w(0x0100);
    do {
      uint16_t a = m(0x5B94u + x);
      if(a == 0) {
        ms(0x45EC + V->pal, (uint16_t) ((m(0x5B93u + x) & 0xFF) << 1));
      } else {
        bool far = (a & 0xFF00) != 0;
        uint16_t t = rd((far ? 0x86AC36u : 0x86AE36u) + (uint16_t) (a << 1));
        ms(0xF533u + y, rd(0x860000u + (uint16_t) (t + (far ? 0x22 : 0x1E))));
        ms(0xF433u + y, rd(0x860000u + (uint16_t) (t + (far ? 0x24 : 0x20))));
        ms(0xF733u + y, V->far_flag);
        uint16_t i = rd(0x860000u + t);
        ms(0xF533u + y, (uint16_t) (m(0x6591u + i) + m(0xF533u + y)));
        x = z(V->obj_dp);
        uint16_t b = m(0x5B93u + x);
        ms(0xF633u + y, (uint16_t) ((((b & 0xFF) << 8) >> 3) + V->row_add));
        ms(0xF933u + y, (uint16_t) ((b & 0xFF) << 1));
        ms(0xF833u + y, (uint16_t) (((b >> 8) << 1) + (far ? 0x495C : 0x45EC) + V->pal));
        r->db = 0x7E;
        r->x = x;
        r->y = y;
        r->a = m(0xF833u + y);
        call(r, 0x85CDAF, far ? V->site_far : V->site_near, true);
        x = r->x;
        y = (uint16_t) ((r->y + 2) & 0xFF);
      }
      x = (uint16_t) (x + 5);
      zs(V->obj_dp, x);
    } while(w(0x0226 + V->rec) >= m(0x5B91u + x));
    ws(0x0100, y);
  }
  // $02:E429
  uint16_t a = w(0x0226 + V->rec);
  if(a >= w(0x019A)) {
    a = (uint16_t) (a - w(0x019A));
    ws(0x0226 + V->rec, a);
    ws(0x023E + V->rec, a);
    zs(V->obj_dp, 0);
    ws(0x1E5E + V->s4, 0);
    ws(0x1E60 + V->s4, 0);
    ms(0x45EC + V->pal, 0x0140);
    return false;
  }
  ws(0x0226 + V->rec, a);
  ws(0x023E + V->rec, a);
  ws(0x0260 + V->rec, 0);
  x = z(0x9C);
  for(;;) {
    uint16_t p = (uint16_t) (w(0x0226 + V->rec) + 2);
    uint16_t b = rd(0x840000u + x);
    if(p < b) break;
    if((uint16_t) (p - b) < 0x41) {
      if(!(V->join_check && (rd(0x840002u + x) & 0xFF) == 0x0A && (uint16_t) (w(0x1EDE + V->s4) + 1) == rd(0x840000u + x)))
        ws(0x0260 + V->rec, rd(0x840002u + x));
      break;
    }
    x = (uint16_t) (x + 0x14);
  }
  ws(0x0142 + V->s2, w(0x0260 + V->rec) & 0xFF);
  return true;
}

static void road_ahead(TgRegs* r, const Ahead* v) {
  V = v;
  g_d = r->d;
  r->db = 0x7E;  // PEA $837E; PLB
  uint16_t x = (uint16_t) (((w(0x023E + V->rec) + w(0x1EE6 + V->s2)) << 1) & 0x3E);
  uint16_t n = (uint16_t) (w(0x0226 + V->rec) - w(0x023E + V->rec));
  if(n != 0) {
    dput(0);
    uint16_t c = w(0x023C + V->rec) == 0;  // from 0 - $023C
    dput((uint16_t) (((uint16_t) -w(0x023C + V->rec)) >> 8));
    dput(m(0x3AEEu + V->trk + x));
    uint16_t t = swap(dget());
    zs(0x14, t & 0xFF00);
    zs(0x16, t & 0xFF);
    uint16_t a = (uint16_t) ((swap(dget()) & 0xFF00) | z(0x16));
    ws(0x01CE + V->s4, a);
    x = (uint16_t) (x + 2);
    if(--n != 0) {
      do {
        uint32_t s = (uint32_t) a + m(0x3AEEu + V->trk + x) + c;
        a = (uint16_t) s;
        c = (uint16_t) (s >> 16);
        x = (uint16_t) (x + 2);
      } while(--n != 0);
      ws(0x01CE + V->s4, a);
    }
    dput(0);
    dput((uint16_t) ((w(0x0224 + V->rec) & 0xFF00) >> 1));
    dput((uint16_t) (m(0x3AEEu + V->trk + x) << 1));
    dget();
    ws(0x01CE + V->s4, (uint16_t) (dget() + w(0x01CE + V->s4)));
  } else {
    dput(0);
    dput((uint16_t) (((uint16_t) ((w(0x0224 + V->rec) & 0xFF00) - w(0x023C + V->rec))) >> 1));
    dput((uint16_t) (m(0x3AEEu + V->trk + x) << 1));
    dget();
    ws(0x01CE + V->s4, dget());
  }

  // $02:E069
  {
    uint16_t a = (uint16_t) (w(0x023E + V->rec) + 0x20 + w(0x1EE6 + V->s2));
    if(a >= w(0x019A)) a = (uint16_t) (a - w(0x019A));
    zs(0x16, a);
  }
  ws(0x023C + V->rec, w(0x0224 + V->rec) & 0xFF00);
  n = (uint16_t) (w(0x0226 + V->rec) - w(0x023E + V->rec));
  if(n != 0) {
    zs(0x14, n);
    ws(0x0240 + V->rec, (uint16_t) (n + w(0x0240 + V->rec)));
    zs(0x22, w(0x1EDE + V->s4));
    x = (uint16_t) ((w(0x023E + V->rec) << 1) & 0x3E);
    ws(0x1EF8 + V->sa, (uint16_t) (m(0x3E6Eu + V->trk + x) - m(0x3E6Cu + V->trk + x)));
    ws(0x1EFA + V->sa, (uint16_t) (m(0x3DEEu + V->trk + x) - m(0x3DECu + V->trk + x)));
    do {
      prepare_step(r);
      zs(0x14, (uint16_t) (z(0x14) - 1));
    } while(z(0x14) != 0);
    while(!place_objects(r)) {
    }
  }

  // $02:E497: the road's tilt.
  uint16_t a = (uint16_t) (w(0x0226 + V->rec) + 1);
  if(a >= w(0x019A)) a = (uint16_t) (a - w(0x019A));
  zs(0x14, height(a));
  a = w(0x0226 + V->rec);
  a = a >= 3 ? (uint16_t) (a - 3) : (uint16_t) (a - 3 + w(0x019A));
  zs(0x16, height(a));
  dput(0);
  dput((uint16_t) (z(0x14) - z(0x16)));
  dput((uint16_t) ((w(0x0225 + V->rec) + 0x100) & 0x3FF));
  uint16_t a0 = z(0x16);
  uint16_t a1 = (uint16_t) (a0 << 1 | (a0 >= 0x8000));
  uint16_t a2 = (uint16_t) (a1 << 1 | (a0 >> 15));
  uint16_t h = swap(a2) & 0x3FF;
  if(h & 0x80) h |= 0xFF00;
  dput(h);
  h = (uint16_t) (h + dget() + 9);
  ws(0x0237 + V->rec, (uint16_t) -h);
  r->db = 0x83;
  r->n = true;
  r->z = false;
}

void Tg3RoadAhead(TgRegs* r) {
  static const Ahead k = { 0, 0, 0, 0, 0, 0xA0, 0xA2, 0x02819E, 0x02E261, 0xE279, 0xE265,
                           0xE307, 0xE394, 0xE3FA, 0x7F, 0, 0, true };
  road_ahead(r, &k);
}
void Tg3RoadAheadBottom(TgRegs* r) {
  static const Ahead k = { 0xB3, 0x580, 2, 4, 0x0A, 0xA4, 0xA6, 0x02826E, 0x02E7B7, 0xE7CF, 0xE7BB,
                           0xE85D, 0xE8ED, 0xE956, 0x17F, 0x1000, 0xDC, false };
  road_ahead(r, &k);
}
