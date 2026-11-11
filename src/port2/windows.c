// Top Gear 2: the window builders (bank $0E, $8B40-$B392), which finish the object pass.
//
// Tunnels, bridges and walls hide the sprites behind them with the hardware window. Each
// builder turns what the object pass saw ($52 picks the builder) into two HDMA lists for
// one player's view: the window edges per scanline (list at $9A, built downwards in
// memory, pointing at edge pairs at $9C) and the colour-window setting per scanline (list
// at $C4, pointing into the table at $9C40).
//
// The lists are built last line first: every routine here pushes below the list's start.

#include "../port/port.h"

#define DPB 0x0400u

static uint16_t z(uint8_t o) { return ram16(DPB + o); }
static void zs(uint8_t o, uint16_t v) { ram16_set(DPB + o, v); }
static uint8_t zb(uint8_t o) { return g_ram[DPB + o]; }
static uint16_t w(uint16_t adr) { return ram16(adr); }
static void ws(uint16_t adr, uint16_t v) { ram16_set(adr, v); }
static void wb(uint16_t adr, uint8_t v) { g_ram[adr] = v; }
static bool neg(uint16_t v) { return (v & 0x8000) != 0; }
// The N flag after CMP: a - b negative.
static bool lt(uint16_t a, uint16_t b) { return neg((uint16_t) (a - b)); }
static bool le(uint16_t a, uint16_t b) { return a == b || lt(a, b); }

// An edge pair for the lines above: two words pushed below $9C.
static void pair(uint16_t first, uint16_t second) {
  uint16_t x = z(0x9C);
  x = (uint16_t) (x - 2);
  ws(x, first);
  x = (uint16_t) (x - 2);
  ws(x, second);
  zs(0x9C, x);
}

// An HDMA entry of `lines` scanlines using the pair at $9C. More than $7F lines takes two
// entries ($70 lines and the rest).
static void entry(uint16_t lines) {
  uint16_t x = z(0x9A);
  if(lines < 0x80) {
    x = (uint16_t) (x - 3);
    ws(x, lines);
    ws((uint16_t) (x + 1), z(0x9C));
  } else {
    x = (uint16_t) (x - 6);
    ws(x, (uint16_t) (lines + 0x90));
    ws((uint16_t) (x + 3), 0x0070);
    ws((uint16_t) (x + 1), z(0x9C));
    ws((uint16_t) (x + 4), z(0x9C));
  }
  zs(0x9A, x);
}

// The span from the last boundary ($AE) to line y, if any, with the given pair.
static bool span_to(uint16_t y) {
  if(y == z(0xAE)) return false;
  zs(0xB2, (uint16_t) (y - z(0xAE)));
  zs(0xAE, y);
  return true;
}

// The colour-window list for the view: per-line settings (repeat mode) from the table at
// $9C40, four bytes a line, ending at the view's last line ($2E).
static void colour_list(TgRegs* r) {
  r->c = false;
  uint16_t y = adc16(r, (uint16_t) ((uint16_t) (z(0x2E) + 1 - z(0x30)) << 2), 0x9C40);
  uint16_t x = z(0xC4);
  uint16_t a = z(0x30);
  if(a < 0x80) {
    x = (uint16_t) (x - 3);
    ws(x, a | 0x80);
    ws((uint16_t) (x + 1), y);
    a = y;
    r->c = false;
    flags_nz16(r, a);
  } else {
    x = (uint16_t) (x - 6);
    ws((uint16_t) (x + 3), (uint16_t) (a + 0x10));
    ws(x, 0x00F0);
    ws((uint16_t) (x + 1), y);
    r->c = false;
    a = adc16(r, y, 0x01C0);
    ws((uint16_t) (x + 4), a);
  }
  zs(0xC4, x);
  r->a = a;
  r->x = x;
  r->y = y;
}

// $8BCC: nothing seen: the window is open (left $00, right $FF... stored as $FF00 pairs)
// on every line, and the colour window uses the one setting at $CA.
void Tg2WindowsNone(TgRegs* r) {
  zs(0xC8, 0);
  pair(0xFF00, 0xFF00);
  entry(z(0x30));
  uint16_t x = z(0xC4);
  uint16_t a = z(0x30);
  if(a < 0x80) {
    x = (uint16_t) (x - 3);
    ws(x, a);
    ws((uint16_t) (x + 1), z(0xCA));
    r->c = false;
  } else {
    x = (uint16_t) (x - 6);
    r->c = false;
    ws((uint16_t) (x + 3), adc16(r, a, 0x0090));
    ws(x, 0x0070);
    ws((uint16_t) (x + 1), z(0xCA));
    ws((uint16_t) (x + 4), z(0xCA));
  }
  zs(0xC4, x);
  r->a = z(0xCA);
  r->x = x;
  flags_nz16(r, r->a);
}

// $8C4B: a tunnel mouth ahead: open above it ($72), then the opening's left and right
// edges strip by strip ($64 strips from the span records at $C0C0, eight bytes each), then
// the fixed bands below ($74, $70, $6A, $78) down to the view's height ($30).
void Tg2WindowsTunnel(TgRegs* r) {
  zs(0xC8, w(0x0114));
  zs(0x9E, (uint16_t) (zb(0x6C) | zb(0x6E) << 8));
  zs(0xA0, (uint16_t) (g_ram[0xC0C0] | g_ram[0xC0C2] << 8));
  zs(0xAE, z(0x72));
  if(z(0x72) != 0) {
    pair(0xFF00, 0xFF00);
    entry(z(0xAE));
  }
  zs(0x64, (uint16_t) (z(0x64) - 1));
  while(z(0x64) != 0) {
    zs(0x66, (uint16_t) (z(0x66) + 8));
    uint16_t x = z(0x66);
    if(span_to(w((uint16_t) (0xC0C4 + x)))) {
      pair(z(0xA0), z(0x9E));
      entry(z(0xB2));
    }
    zs(0xA0, (uint16_t) (g_ram[(uint16_t) (0xC0C0 + x)] | g_ram[(uint16_t) (0xC0C2 + x)] << 8));
    zs(0x64, (uint16_t) (z(0x64) - 1));
  }
  if(span_to(z(0x74))) {
    pair(z(0xA0), z(0x9E));
    entry(z(0xB2));
  }
  if(span_to(z(0x70))) {
    pair(0x00FF, z(0x9E));
    entry(z(0xB2));
  }
  if(span_to(z(0x6A))) {
    pair(0x00FF, 0x00FF);
    entry(z(0xB2));
  }
  if(span_to(z(0x78))) {
    pair(0x00FF, 0xFF00);
    entry(z(0xB2));
  }
  if(span_to(z(0x30))) {
    pair(0xFF00, 0xFF00);
    entry(z(0xB2));
  }
  colour_list(r);
}

// $B250: a band across the view: closed down to $98, one edge pair ($92/$94) down to $96,
// closed below.
void Tg2WindowsBand(TgRegs* r) {
  zs(0xAE, z(0x98));
  if(z(0x98) != 0) {
    pair(0x00FF, 0x00FF);
    entry(z(0xAE));
  }
  if(span_to(z(0x96))) {
    pair(0x00FF, (uint16_t) (zb(0x92) | zb(0x94) << 8));
    entry(z(0xB2));
  }
  if(span_to(z(0x30))) {
    pair(0x00FF, 0x00FF);
    entry(z(0xB2));
  }
  colour_list(r);
}

// --- $8F1A: a wall beside the road ---
//
// The wall's top edge is a line through the span records ($7E to $80 in $C0C0, eight bytes
// each: x at +0/+2, y at +4/+6). Every line of the view gets its own edge pair (four
// bytes a line below $A2, last line first), and the wall's edge is drawn into byte 2 of
// the pairs by walking the line: first down the screen from line $7A (records' +4 y),
// then back up from where that ended (records' +6 y). $B8 is the running edge (it only
// moves right), $A6-$A8 the x position in 1/256ths, $B4 the step per line.

// The pair address for line y.
static uint16_t line_adr(uint16_t y) { return (uint16_t) (((uint16_t) ~y << 2) + z(0xA2)); }

// The step per line from one record to the next: (x difference * 256) / lines.
static void set_step(uint16_t lines) {
  zs(0xB2, lines);
  uint16_t d = (uint16_t) (z(0xAB) - z(0xA7));
  d = (uint16_t) (d << 8 | d >> 8);
  if(lines == 1) {
  } else if(lines == 2) {
    d >>= 1;
  } else {
    tg_bus_write16(0x4204, d);
    tg_bus_write8(0x4206, (uint8_t) lines);
    d = (uint16_t) (tg_bus_read8(0x4214) | tg_bus_read8(0x4215) << 8);
  }
  zs(0xB4, d);
}

// Half a step (rounded) to start in the middle of a line, then whole steps. Both return
// the new high part ($A8).
static uint16_t half_step(void) {
  uint16_t s = z(0xB4);
  uint32_t t = (uint32_t) (s >> 1) + z(0xA6) + (s & 1);
  zs(0xA6, (uint16_t) t);
  uint16_t a = (uint16_t) (z(0xA8) + (t >> 16));
  zs(0xA8, a);
  return a;
}
static uint16_t full_step(void) {
  uint32_t t = (uint32_t) z(0xB4) + z(0xA6);
  zs(0xA6, (uint16_t) t);
  uint16_t a = (uint16_t) (z(0xA8) + (t >> 16));
  zs(0xA8, a);
  return a;
}

// The edge for a line whose x is on screen: the further right of $B8 and x (low bytes).
static uint8_t edge_here(void) {
  uint8_t a = zb(0xB8);
  if((uint8_t) (a - zb(0xA7)) & 0x80) {
    a = zb(0xA7);
    g_ram[DPB + 0xB8] = a;
  }
  return a;
}

enum { kWalkDone, kWalkOffRight, kWalkOffRightDrawn };

// Walks the line between two records, skipping `skip` lines that are already done (0 for
// none) and drawing `draw` lines. dir is -4 going down the screen, +4 going back up; *px is
// the pair address (pre-moved when going up).
static int walk(uint16_t skip, uint16_t draw, int dir, uint16_t* px) {
  uint16_t x = *px;
  uint16_t a = half_step();
  if(skip) {
    uint16_t y = skip;
    zs(0xB6, draw);
    for(;;) {
      a &= 0x00FF;
      if(a == 0) {
        if(lt(z(0xB8), z(0xA7))) zs(0xB8, z(0xA7));
      } else if(!(a & 0x80)) {
        return kWalkOffRight;
      }
      y--;
      if(y == 0) break;
      a = full_step();
    }
    a = full_step();
  }
  uint16_t y = draw;
  for(;;) {
    uint8_t e;
    a &= 0x00FF;
    if(a == 0) {
      e = edge_here();
    } else if(a & 0x80) {
      e = zb(0xB8);
    } else {
      *px = x;
      return kWalkOffRightDrawn;
    }
    if(dir > 0) {
      x = (uint16_t) (x + 4);
      wb((uint16_t) (x + 2), e);
      zs(0xBC, (uint16_t) (z(0xBC) - 1));
    } else {
      wb((uint16_t) (x + 2), e);
      x = (uint16_t) (x - 4);
      zs(0xBA, (uint16_t) (z(0xBA) + 1));
    }
    y--;
    if(y == 0) break;
    a = full_step();
  }
  *px = x;
  return kWalkDone;
}

// Fills n lines' edge bytes with e, going down (-4) or up (+4, pre-moved) from x.
static uint16_t fill(uint16_t x, uint16_t n, uint8_t e, int dir) {
  do {
    if(dir > 0) {
      x = (uint16_t) (x + 4);
      wb((uint16_t) (x + 2), e);
    } else {
      wb((uint16_t) (x + 2), e);
      x = (uint16_t) (x - 4);
    }
  } while(--n);
  return x;
}

// Moves the running edge to the next record's x if that is further right. False if it is
// off the right of the screen.
static bool edge_to_next(void) {
  if(lt(z(0xB8), z(0xAB))) {
    if(!lt(z(0xAB), 0x0100)) return false;
    zs(0xB8, z(0xAB));
  }
  return true;
}

// The next record becomes the current one.
static void advance(void) {
  zs(0xAE, z(0xB0));
  zs(0xA7, z(0xAB));
}

// The start of a walk: the segment count, the running edge and the first record's x.
static uint16_t walk_start(uint16_t line) {
  zs(0xA6, 0);
  zs(0xA8, 0);
  zs(0xAA, 0);
  zs(0xAC, 0);
  zs(0xC2, (uint16_t) ((uint16_t) (z(0x80) - z(0x7E)) >> 3));
  zs(0xB8, z(0x82));
  zs(0xBA, line);
  uint16_t x = z(0x7E);
  uint16_t a = w((uint16_t) (0xC0C0 + x));
  zs(0xA7, a);
  if(!le(a, z(0xB8))) zs(0xB8, a);
  return x;
}

// The walk down the screen ($8FFD-$92DC).
static void wall_down(void) {
  uint16_t x = walk_start(z(0x7A));
  zs(0xA4, line_adr(z(0x7A)));
  zs(0xAE, w((uint16_t) (0xC0C4 + x)));
  do {
    x = (uint16_t) (x + 8);
    zs(0xAB, w((uint16_t) (0xC0C0 + x)));
    zs(0xB0, w((uint16_t) (0xC0C4 + x)));
    bool flat; // the edge stays at $B8 down to the next record
    if(!lt(z(0xA7), z(0xAB))) {
      if(!lt(z(0xBA), z(0xB0))) {
        advance();
        continue;
      }
      flat = true;
    } else if(!lt(z(0xBA), z(0xB0))) {
      if(!edge_to_next()) {
        zs(0xB8, 0x00FF);
        return;
      }
      advance();
      continue;
    } else {
      flat = !lt(z(0xB8), z(0xAB));
    }
    if(flat) {
      if(!lt(z(0xB0), z(0x30))) {
        // To the bottom of the view.
        uint16_t n = (uint16_t) (z(0x30) - z(0xBA));
        if(n != 0) {
          zs(0xBA, z(0x30));
          fill(z(0xA4), n, zb(0xB8), -4);
        }
        return;
      }
      uint16_t n = (uint16_t) (z(0xB0) - z(0xBA));
      zs(0xBA, z(0xB0));
      zs(0x66, x);
      zs(0xA4, fill(z(0xA4), n, zb(0xB8), -4));
      advance();
      continue;
    }
    // A sloping piece, to the next record or the bottom of the view.
    bool whole = lt(z(0xB0), z(0x30));
    uint16_t end = whole ? z(0xB0) : z(0x30);
    set_step((uint16_t) (z(0xB0) - z(0xAE)));
    if(whole) zs(0x66, x);
    uint16_t p = z(0xA4);
    int res;
    if(!lt(z(0xAE), z(0xBA))) res = walk(0, (uint16_t) (end - z(0xBA)), -4, &p);
    else res = walk((uint16_t) (z(0xBA) - z(0xAE)), (uint16_t) (end - z(0xBA)), -4, &p);
    if(res != kWalkDone) {
      zs(0xB8, 0x00FF);
      if(res == kWalkOffRightDrawn) zs(0xA4, p);
      return;
    }
    if(!whole) return;
    zs(0xA4, p);
    if(!edge_to_next()) {
      zs(0xB8, 0x00FF);
      return;
    }
    advance();
  } while(zs(0xC2, (uint16_t) (z(0xC2) - 1)), z(0xC2) != 0);
}

// Skips the records that do not rise above line $BC, lowering $BC to each one's y (not
// past the view's height), and sets the pair address ($9664-$96A7).
static void wall_up_skip(uint16_t x) {
  uint16_t a = z(0xB0);
  for(;;) {
    if(!le(a, z(0xBC))) {
      if(!lt(a, z(0x30))) {
        zs(0xBC, z(0x30));
        break;
      }
      zs(0xBC, a);
    }
    zs(0xC2, (uint16_t) (z(0xC2) - 1));
    if(z(0xC2) == 0) break;
    x = (uint16_t) (x + 8);
    a = w((uint16_t) (0xC0C6 + x));
  }
  zs(0xA4, line_adr(z(0xBC)));
}

// The walk back up the screen ($92DF-$96B0). $BC is the line it has reached.
static void wall_up(void) {
  zs(0xBE, z(0xB8));
  zs(0xC0, z(0xBA));
  uint16_t x = walk_start(z(0xBA));
  uint16_t a = w((uint16_t) (0xC0C6 + x));
  zs(0xAE, a);
  bool lead; // skipping the leading records at or above the start line
  if(!le(a, z(0x30))) {
    a = z(0x30);
    if(a == z(0xBA)) {
      zs(0xBC, a);
      zs(0xA4, line_adr(a));
      return;
    }
    lead = false;
  } else {
    lead = a == z(0xBA) || lt(a, z(0xBA));
  }
  if(!lead) {
    zs(0xBC, a);
    zs(0xA4, line_adr(a));
  }
  for(;;) {
    if(lead) {
      // $9336.
      for(;;) {
        x = (uint16_t) (x + 8);
        zs(0xA7, w((uint16_t) (0xC0C2 + x)));
        a = w((uint16_t) (0xC0C6 + x));
        zs(0xAE, a);
        if(!le(a, z(0xBA))) break;
        zs(0xC2, (uint16_t) (z(0xC2) - 1));
        if(z(0xC2) == 0) {
          zs(0xBC, z(0xBA));
          zs(0xA4, line_adr(z(0xBA)));
          return;
        }
      }
      if(!le(a, z(0x30))) a = z(0x30);
      zs(0xBC, a);
      zs(0xA4, line_adr(a));
      zs(0xC2, (uint16_t) (z(0xC2) - 1));
      if(z(0xC2) == 0) return;
      lead = false;
    }
    // $9388.
    x = (uint16_t) (x + 8);
    zs(0xAB, w((uint16_t) (0xC0C0 + x)));
    zs(0xB0, w((uint16_t) (0xC0C6 + x)));
    bool flat;
    if(!lt(z(0xA7), z(0xAB))) {
      a = z(0xB0);
      if(!lt(a, z(0xBC))) {
        // $93DA: the next record is below the line reached: move down to it.
        if(a != z(0xBC)) {
          if(!le(a, z(0x30))) a = z(0x30);
          uint16_t y = z(0xBC);
          zs(0xBC, a);
          zs(0xA4, (uint16_t) (((uint16_t) (y - a) << 2) + z(0xA4)));
        }
        advance();
        goto next;
      }
      flat = true;
    } else {
      a = z(0xB0);
      if(!lt(a, z(0xBC))) {
        // $9429.
        if(a != z(0xBC)) {
          if(!le(a, z(0x30))) a = z(0x30);
          uint16_t y = z(0xBC);
          zs(0xBC, a);
          zs(0xA4, (uint16_t) (((uint16_t) (y - a) << 2) + z(0xA4)));
        }
        if(!edge_to_next()) {
          wall_up_skip(x);
          return;
        }
        advance();
        goto next;
      }
      flat = !lt(z(0xB8), z(0xAB));
    }
    if(flat) {
      // $93A7.
      if(!lt(z(0xBA), z(0xB0))) {
        // Up to the start line, and done with this side.
        uint16_t n = (uint16_t) (z(0xBC) - z(0xBA));
        if(n != 0) {
          zs(0xBC, z(0xBA));
          zs(0x66, x);
          fill(z(0xA4), n, zb(0xB8), 4);
        }
        wall_up_skip(x);
        return;
      }
      uint16_t n = (uint16_t) (z(0xBC) - z(0xB0));
      zs(0xBC, z(0xB0));
      zs(0x66, x);
      zs(0xA4, fill(z(0xA4), n, zb(0xB8), 4));
      advance();
      goto next;
    }
    {
      // A sloping piece, up to the next record or the start line.
      bool whole = lt(z(0xBA), z(0xB0));
      uint16_t end = whole ? z(0xB0) : z(0xBA);
      set_step((uint16_t) (z(0xAE) - z(0xB0)));
      zs(0x66, x);
      uint16_t p = z(0xA4);
      int res;
      if(!lt(z(0xBC), z(0xAE))) res = walk(0, (uint16_t) (z(0xBC) - end), 4, &p);
      else res = walk((uint16_t) (z(0xAE) - z(0xBC)), (uint16_t) (z(0xBC) - end), 4, &p);
      if(res != kWalkDone) {
        wall_up_skip(x);
        return;
      }
      if(!whole) {
        // $9661: on to the leading-record loop's count.
        zs(0xC2, (uint16_t) (z(0xC2) - 1));
        if(z(0xC2) == 0) {
          zs(0xBC, z(0xBA));
          zs(0xA4, line_adr(z(0xBA)));
          return;
        }
        lead = true;
        continue;
      }
      zs(0xA4, p);
      if(!edge_to_next()) {
        zs(0xB8, 0x00FF);
        return;
      }
      advance();
    }
  next:
    zs(0xC2, (uint16_t) (z(0xC2) - 1));
    if(z(0xC2) == 0) return;
  }
}

void Tg2WindowsWall(TgRegs* r) {
  zs(0xC8, w(0x0116));
  // Every line open, with its own pair.
  uint16_t x = z(0x9C);
  zs(0xA2, x);
  uint16_t y = z(0x30);
  do {
    x = (uint16_t) (x - 4);
    ws(x, 0xFF00);
    ws((uint16_t) (x + 2), 0xFF00);
  } while(--y);
  zs(0x9C, x);
  x = z(0x9A);
  uint16_t a = z(0x30);
  if(a < 0x80) {
    x = (uint16_t) (x - 3);
    ws(x, a | 0x80);
    ws((uint16_t) (x + 1), z(0x9C));
  } else {
    x = (uint16_t) (x - 6);
    ws((uint16_t) (x + 3), (uint16_t) (a + 0x10));
    ws(x, 0x00F0);
    ws((uint16_t) (x + 1), z(0x9C));
    ws((uint16_t) (x + 4), (uint16_t) (z(0x9C) + 0x01C0));
  }
  zs(0x9A, x);

  a = (uint16_t) (z(0x80) - z(0x7E));
  if(a == 0) {
    // One record: a straight edge from line $7A down to its y.
    zs(0xA4, line_adr(z(0x7A)));
    x = z(0x7E);
    a = w((uint16_t) (0xC0C0 + x));
    if(neg(a)) a = 0;
    else if(a >= 0x0100) a = 0x00FF;
    zs(0xA0, a);
    a = w((uint16_t) (0xC0C6 + x));
    if(!lt(a, z(0x30))) a = z(0x30);
    a = (uint16_t) (a - z(0x7A));
    if(!neg(a) && a != 0) {
      y = a;
      x = z(0xA4);
      do {
        wb(x, zb(0x82));
        wb((uint16_t) (x + 2), zb(0xA0));
        x = (uint16_t) (x - 4);
      } while(--y);
    }
    colour_list(r);
    return;
  }
  zs(0xC2, a >> 3);
  // The other edge ($82) on the wall's lines.
  x = z(0x7E);
  a = w((uint16_t) (0xC0C6 + x));
  if(!lt(a, z(0x30))) a = z(0x30);
  a = (uint16_t) (a - z(0x7A));
  if(!neg(a) && a != 0) {
    y = a;
    x = line_adr(z(0x7A));
    do {
      wb(x, zb(0x82));
      x = (uint16_t) (x - 4);
    } while(--y);
  }
  wall_down();
  wall_up();
  // The lines the walk up did not reach get the edge the walk down ended with.
  a = (uint16_t) (z(0xBC) - z(0xC0));
  if(!neg(a) && a != 0) fill(z(0xA4), a, zb(0xBE), 4);
  colour_list(r);
}

// $0E:8B40: the builder for what the object pass saw ($52). False for a builder that is
// not translated yet.
bool Tg2Windows(TgRegs* r) {
  switch(z(0x52)) {
    case 1: Tg2WindowsTunnel(r); return true;
    case 2: Tg2WindowsWall(r); return true;
    case 32: Tg2WindowsBand(r); return true;
    case 4: case 6: case 8: case 16: case 24: return false;
    default: Tg2WindowsNone(r); return true;
  }
}
