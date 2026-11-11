// Top Gear 2: the object pass ($0B:8578), which draws one player's view of the cars and
// roadside objects, nearest strip of road first, as hardware sprites.
//
// The view is cut into strips of road ($40 depth units each, $4C strips). Each strip has
// one roadside object slot (the road data at $48) and the cars whose distance falls in it
// (the visible-car list at $C340). The pass also tracks the overhead and side scenery that
// hides sprites (tunnels, walls, bridges: state $50, seen this frame $52) by narrowing the
// clip window ($3A top, $38 bottom, $34 left, $36 right) and recording window spans for the
// HDMA builders in bank $0E, which it finishes by calling.

#include "../port/port.h"

#define DPB 0x0400u

void Tg2DrawSprite(TgRegs* r);
void Tg2ListVisibleCars(TgRegs* r);
void Tg2FinishOam(TgRegs* r);
bool Tg2Windows(TgRegs* r);
void Tg2ObjectType0(TgRegs* r);
void Tg2ObjectType1(TgRegs* r);
void Tg2ObjectType2(TgRegs* r);
void Tg2ObjectType3(TgRegs* r);
void Tg2ObjectType4(TgRegs* r);
void Tg2ObjectType5(TgRegs* r);
void Tg2ObjectType6(TgRegs* r);
void Tg2ObjectType7(TgRegs* r);
void Tg2ObjectType8(TgRegs* r);
void Tg2ObjectType9(TgRegs* r);
void Tg2ObjectType10(TgRegs* r);
void Tg2ObjectType11(TgRegs* r);
void Tg2ObjectType12(TgRegs* r);
void Tg2ObjectType13(TgRegs* r);
void Tg2ObjectType14(TgRegs* r);
void Tg2ObjectType15(TgRegs* r);

static uint16_t z(uint8_t o) { return ram16(DPB + o); }
static void zs(uint8_t o, uint16_t v) { ram16_set(DPB + o, v); }
static uint8_t zb(uint8_t o) { return g_ram[DPB + o]; }
static void zor(uint8_t o, uint16_t m) { zs(o, (uint16_t) (z(o) | m)); }
static void zand(uint8_t o, uint16_t m) { zs(o, (uint16_t) (z(o) & m)); }
static uint16_t w(uint16_t adr) { return ram16(adr); }
static void ws(uint16_t adr, uint16_t v) { ram16_set(adr, v); }
static bool neg(uint16_t v) { return (v & 0x8000) != 0; }
// The N flag after CMP: a - b negative.
static bool lt(uint16_t a, uint16_t b) { return neg((uint16_t) (a - b)); }
static bool le(uint16_t a, uint16_t b) { return a == b || lt(a, b); }
// A long read from bank $8B/$8F tables, index added across the bank.
static uint16_t lr(uint32_t base, uint16_t i) { return rom16((base + i) & 0xFFFFFF); }

// The hardware multiplier: M7A (16 bits) times M7B (8 bits, signed).
static uint8_t rd(uint16_t adr) { return tg_bus_read8(adr); }
static uint16_t rd16(uint16_t adr) { return (uint16_t) (rd(adr) | rd((uint16_t) (adr + 1)) << 8); }
static void m7a(uint16_t v) {
  tg_bus_write8(0x211B, (uint8_t) v);
  tg_bus_write8(0x211B, (uint8_t) (v >> 8));
}
static void m7b(uint8_t v) { tg_bus_write8(0x211C, v); }
// base + the product / 256, rounded.
static uint16_t add_prod(uint16_t base) {
  bool c = (rd(0x2134) & 0x80) != 0;
  return (uint16_t) (base + rd16(0x2135) + c);
}
// The same, reading the product's low byte with a 16-bit read from $2133.
static uint16_t add_prod_wide(uint16_t base) {
  bool c = (rd16(0x2133) & 0x8000) != 0;
  return (uint16_t) (base + rd16(0x2135) + c);
}
// base - the product / 256, rounded.
static uint16_t sub_prod(uint16_t base) {
  bool c = (rd(0x2134) & 0x80) != 0;
  return (uint16_t) (base - rd16(0x2135) - c);
}
// The object scale for depth d into M7A ($8F9B80 / 8, rounded).
static void set_scale(uint16_t d) {
  uint16_t s = rom16(0x8F9B80 + d);
  m7a((uint16_t) ((s >> 3) + ((s >> 2) & 1)));
}
// v times the 16-bit factor $5E (two 8-bit multiplies), shifted left by n: the result's
// top 16 bits.
static uint16_t mul_wide(uint16_t v, int n) {
  m7a(v);
  m7b(zb(0x5F));
  uint16_t x = rd16(0x2133), y = rd16(0x2135);
  m7b(zb(0x5E));
  uint16_t c = (uint16_t) (rd(0x2136) ^ 0x80);
  uint32_t lo = (uint32_t) (x & 0xFF00) + rd16(0x2134);
  uint16_t a = (uint16_t) (y + c + (lo >> 16));
  uint16_t l = (uint16_t) lo;
  for(int i = 0; i < n; i++) {
    a = (uint16_t) (a << 1 | l >> 15);
    l = (uint16_t) (l << 1);
  }
  return a;
}

static void draw(TgRegs* r) {
  r->d = DPB;
  r->db = 0x7E;
  Tg2DrawSprite(r);
}

// Player-car slot o ($DC, $E2, $E8): its sprite set, OAM bits and track position.
static void player_car(TgRegs* r, uint8_t o) {
  ws((uint16_t) (0x1DF2 + z(0x10)), z((uint8_t) (o + 4)));
  zs(0x08, z((uint8_t) (o + 2)));
  uint16_t t = w((uint16_t) (0x5000 + ((z(o) >> 2) & 0xFFFE)));
  zs(0x0C, (uint16_t) ((t & 0xFF) + z(0xD8)));
  zs(0x0E, (uint16_t) (z(0xDA) - (t >> 8)));
  draw(r);
}

// The camera car's effects block at $D6 (+2: active parts; +4: last frame's; +6: kind;
// +8..+$22: each part's animation step). Each part is drawn from its table T (frame +$E,
// y +$C, x +6 or +2 by kind, and the step's length +0).
static const uint8_t kPartSlot[14] = { 0x08, 0x0A, 0x0C, 0x0E, 0x14, 0x16, 0x18, 0x1A, 0x1C, 0x1E, 0x20, 0x22, 0x10, 0x12 };
static const uint16_t kPartTable[14] = { 0xFB47, 0xFB87, 0xFBC7, 0xFC47, 0xFCC7, 0xFD07, 0xFD47,
                                         0xFD77, 0xFDA7, 0xFDF7, 0xFE47, 0xFEA7, 0xFF07, 0xFF67 };

static void effect_part(TgRegs* r, uint16_t blk, int i, bool kind_offset) {
  uint8_t s = kPartSlot[i];
  uint32_t t = 0x8B0000u | kPartTable[i];
  uint16_t xv = w((uint16_t) (blk + s));
  zs(0xF2, xv);
  zs(0xF4, blk);
  zs(0x08, lr(t + 0xE, xv));
  if(kind_offset) {
    zs(0x0E, lr(t + 0xC, xv));
    zs(0x0C, lr(t + 2, (uint16_t) (xv + z(0xF0))));
  } else {
    zs(0x0E, (uint16_t) (lr(t + 0xC, xv) + z(0xF0)));
    zs(0x0C, lr(t + 6, xv));
  }
  draw(r);
  xv = z(0xF2);
  blk = z(0xF4);
  uint16_t f2 = (uint16_t) ((uint16_t) (1 - lr(t, xv)) << 4);
  zs(0xF2, f2);
  ws((uint16_t) (blk + s), (uint16_t) (xv + f2));
}

// Shifts the part bits at $EE right n times; true when the last bit out was set.
static bool shift_bits(int n) {
  uint16_t v = z(0xEE);
  zs(0xEE, (uint16_t) (v >> n));
  return (v >> (n - 1)) & 1;
}

// The camera car's effects. Returns false when the pass skips the shadow ($CE negative).
static bool effects(TgRegs* r) {
  uint16_t x = z(0xD6);
  uint16_t ee = w((uint16_t) (x + 2));
  zs(0xEE, ee);
  uint16_t f0 = (uint16_t) ((ee & w((uint16_t) (x + 4))) ^ ee);
  zs(0xF0, f0);
  ws((uint16_t) (x + 4), ee);
  if(ee != 0) {
    for(int i = 0; i < 14; i++)
      if(f0 & (1u << i)) ws((uint16_t) (x + kPartSlot[i]), 0);
  }
  if(neg(z(0xCE))) return false;
  if(ee == 0) return true;
  uint16_t a = w(x);
  if(a == 0) return true;
  if(!neg(a)) {
    zs(0xF0, lr(0x8BFFC7, (uint16_t) (a << 1)));
    static const int kParts[6] = { 2, 3, 4, 5, 12, 13 };
    for(int k = 0, bit = 0; k < 6; k++) {
      int b = kParts[k];
      if(shift_bits(b - bit + 1)) effect_part(r, x, b, false);
      bit = b + 1;
    }
    return true;
  }
  uint16_t kind = w((uint16_t) (x + 6));
  if(!lt(kind, 6) || lt(kind, 1)) return true;
  zs(0xF0, (uint16_t) ((kind - 1) << 1));
  for(int b = 0; b < 14; b++)
    if(shift_bits(1)) effect_part(r, x, b, true);
  return true;
}

// $8BC0: the camera car's shadow.
static void shadow(TgRegs* r) {
  zs(0x08, 0x80);
  zs(0x0A, 0);
  zs(0x0C, 0x81);
  m7a(z(0xD4));
  m7b(tg_rom(0x8F8B60)[0]);
  g_ram[DPB + 0xF1] = 0;
  g_ram[DPB + 0xEE] = rd(0x2134);
  g_ram[DPB + 0xEF] = rd(0x2135);
  g_ram[DPB + 0xF0] = rd(0x2136);
  if(g_ram[DPB + 0xF0] & 0x80) g_ram[DPB + 0xF1] = 0xFF;
  m7b(tg_rom(0x8F8B61)[0]);
  unsigned s = rd(0x2134) + zb(0xEF);
  g_ram[DPB + 0xEF] = (uint8_t) s;
  uint16_t a = (uint16_t) (rd16(0x2135) + z(0xF0) + (s >> 8));
  zs(0xF0, a);
  uint16_t e = z(0xEE);
  for(int i = 0; i < 2; i++) {
    a = (uint16_t) (a << 1 | e >> 15);
    e = (uint16_t) (e << 1);
  }
  zs(0xEE, e);
  zs(0x0E, (uint16_t) (a + 0x000E));
  draw(r);
}

// The clip-state helpers return true when the pass must stop here (the view is covered).

// $95A5 table: the first strip, by state $50.
static void first_strip_state(void) {
  uint16_t s = z(0x50);
  uint16_t x = z(0x66), a;
  switch(s) {
  case 1:
    zs(0x68, 0);
    zs(0x74, 0);
    zs(0x76, 0);
    ws((uint16_t) (0xC0C4 + x), 0);
    m7b(0x15);
    a = add_prod(z(0xF4));
    if(neg(a)) {
      a = 0;
      zs(0x38, a);
    } else if(lt(a, z(0x38))) {
      zs(0x38, a);
    } else {
      a = z(0x38);
    }
    zs(0x6A, a);
    zs(0x70, a);
    m7b(0x1E);
    a = add_prod(z(0xF4));
    if(neg(a)) a = 0;
    else if(!lt(a, z(0x30))) a = z(0x30);
    zs(0x32, a);
    zs(0x78, a);
    a = (uint16_t) (z(0xF2) - z(0x5A));
    if(le(a, z(0x34))) a = z(0x34);
    else zs(0x34, a);
    ws((uint16_t) (0xC0C0 + x), a);
    a = (uint16_t) (z(0xF2) + z(0x5A));
    if(lt(a, z(0x36))) zs(0x36, a);
    else a = z(0x36);
    ws((uint16_t) (0xC0C2 + x), (uint16_t) (a - 1));
    zs(0x6C, 0);
    zs(0x6E, 0x00FF);
    zs(0x72, 0);
    zs(0x66, (uint16_t) (x + 8));
    return;
  case 2:
  case 4:
  case 6:
    ws((uint16_t) (0xC0C4 + x), z(0xF4));
    m7b(0x28);
    ws((uint16_t) (0xC0C6 + x), add_prod(z(0xF4)));
    m7b(0x1E);
    if(s & 2) {
      a = sub_prod(z(0xF2));
      if(!le(a, z(0x34))) zs(0x34, a);
      ws((uint16_t) (0xC0C0 + x), a);
    }
    if(s & 4) {
      a = s == 6 ? add_prod_wide(z(0xF2)) : add_prod(z(0xF2));
      if(lt(a, z(0x36))) zs(0x36, a);
      ws((uint16_t) (0xC0C2 + x), (uint16_t) (a - 1));
    }
    if(s & 2) {
      zs(0x7A, 0);
      if(s == 6) zs(0x7C, 0);
      zs(0x7E, 0);
      zs(0x80, 0);
      zs(0x82, 0);
    } else {
      zs(0x7C, 0);
    }
    if(s & 4) {
      zs(0x84, 0);
      zs(0x86, 0);
      zs(0x88, 0x00FF);
    }
    zs(0x66, (uint16_t) (x + 8));
    return;
  case 8:
    zs(0x8A, 0);
    zs(0x8C, 0);
    return;
  case 16:
    zs(0x8E, 0);
    zs(0x90, 0);
    return;
  case 24:
    zs(0x8A, 0);
    zs(0x8C, 0);
    zs(0x8E, 0);
    zs(0x90, 0);
    return;
  default:
    zs(0x68, 0);
    return;
  }
}

// $9CFE: a later strip with no side state: the top edge follows the road.
static bool strip_top(void) {
  if(!neg(z(0x68))) {
    uint16_t a = z(0xF4);
    if(le(a, z(0x3A))) return false;
    if(lt(a, z(0x38))) {
      zs(0x3A, a);
      return false;
    }
    zs(0x3A, z(0x38));
    if(z(0x30) == z(0x32)) return true;
    zs(0x68, (uint16_t) (z(0x68) - 1));
  }
  uint16_t a = z(0xF4);
  if(le(a, z(0x32))) return false;
  if(!lt(a, z(0x30))) return true;
  zs(0x32, a);
  return false;
}

// $9D32: a later strip inside a tunnel (state 1).
static bool strip_tunnel(void) {
  uint16_t a;
  if(neg(z(0x68))) {
    m7b(0x1E);
    a = add_prod(z(0xF4));
    if(le(a, z(0x32))) return false;
    if(lt(a, z(0x30))) {
      zs(0x32, a);
      zs(0x78, a);
      return false;
    }
    zs(0x32, z(0x30));
    zs(0x78, z(0x30));
    return true;
  }
  uint16_t x = z(0x66);
  a = z(0xF4);
  if(le(a, z(0x3A))) {
    a = z(0x3A);
  } else if(lt(a, z(0x38))) {
    zs(0x3A, a);
    zs(0x74, a);
    zs(0x76, a);
  } else {
    zs(0x3A, z(0x38));
    goto closed;
  }
  ws((uint16_t) (0xC0C4 + x), a);
  m7b(0x15);
  a = add_prod(z(0xF4));
  if(lt(a, z(0x38))) {
    if(le(a, z(0x3A))) goto closed;
    zs(0x38, a);
  }
  m7b(0x1E);
  a = add_prod(z(0xF4));
  if(!le(a, z(0x32))) {
    if(!lt(a, z(0x30))) a = z(0x30);
    zs(0x32, a);
    zs(0x78, a);
  }
  a = (uint16_t) (z(0xF2) - z(0x5A));
  if(le(a, z(0x34))) {
    a = z(0x34);
  } else {
    if(!lt(a, z(0x36))) goto sides;
    zs(0x34, a);
  }
  ws((uint16_t) (0xC0C0 + x), a);
  a = (uint16_t) (z(0xF2) + z(0x5A));
  if(!lt(a, z(0x36))) {
    a = z(0x36);
  } else {
    if(le(a, z(0x34))) goto sides;
    zs(0x36, a);
  }
  ws((uint16_t) (0xC0C2 + x), (uint16_t) (a - 1));
  zs(0x66, (uint16_t) (x + 8));
  return false;

sides:  // $9E26
  if(z(0x32) == z(0x30)) return true;
  zs(0x68, (uint16_t) (z(0x68) - 1));
  zs(0x38, z(0x3A));
  zs(0x34, 0x0080);
  zs(0x36, 0x007F);
  return false;

closed:  // $9D76
  a = z(0x3A);
  zs(0x38, a);
  zs(0x74, a);
  zs(0x76, a);
  zs(0x34, 0x0080);
  zs(0x36, 0x007F);
  m7b(0x1E);
  a = add_prod(z(0xF4));
  if(lt(a, z(0x32))) {
    a = z(0x32);
  } else if(a != z(0x32)) {
    if(!lt(a, z(0x30))) {
      zs(0x32, z(0x30));
      zs(0x78, z(0x30));
      return true;
    }
    zs(0x32, a);
    zs(0x78, a);
  }
  if(a == z(0x30)) return true;
  zs(0x68, (uint16_t) (z(0x68) - 1));
  return false;
}

static void raise_top(void) {
  uint16_t a = z(0xF4);
  if(!le(a, z(0x3A))) zs(0x3A, a);
}

// $9E61, $9EB4, $9F04: a later strip beside a wall on the left (2), right (4) or both (6).
static void strip_walls(uint16_t s) {
  uint16_t x = z(0x66), a;
  ws((uint16_t) (0xC0C4 + x), z(0xF4));
  raise_top();
  m7b(0x28);
  ws((uint16_t) (0xC0C6 + x), add_prod(z(0xF4)));
  m7b(0x1E);
  if(s & 2) {
    a = sub_prod(z(0xF2));
    if(!le(a, z(0x34))) zs(0x34, a);
    ws((uint16_t) (0xC0C0 + x), a);
  }
  if(s & 4) {
    a = s == 6 ? add_prod_wide(z(0xF2)) : add_prod(z(0xF2));
    if(lt(a, z(0x36))) zs(0x36, a);
    ws((uint16_t) (0xC0C2 + x), (uint16_t) (a - 1));
  }
  if(s & 2) zs(0x80, x);
  if(s & 4) zs(0x86, x);
  zs(0x66, (uint16_t) (x + 8));
}

// $9F6E, $9F7B, $9F88: overhead spans (8, 16, both).
static void strip_overhead(uint16_t s) {
  uint16_t a = z(0xF4);
  if(le(a, z(0x3A))) return;
  zs(0x3A, a);
  if(s & 8) zs(0x8C, a);
  if(s & 16) zs(0x90, a);
}

// The $9CBE table, by state $50.
static bool later_strip_state(void) {
  uint16_t s = z(0x50);
  switch(s) {
  case 1: return strip_tunnel();
  case 2:
  case 4:
  case 6: strip_walls(s); return false;
  case 8:
  case 16:
  case 24: strip_overhead(s); return false;
  default: return strip_top();
  }
}

// $97C1 / $9861: a sign gantry (its record word at $0110 / $0112): two posts, and the
// span between them hides sprites behind it ($92-$98, flag $20).
static void gantry(TgRegs* r, uint16_t word) {
  zs(0xC8, w(word));
  m7b(0x14);
  uint16_t a = add_prod(z(0xF4));
  if(lt(a, z(0x38))) {
    if(lt(a, z(0x3A))) a = z(0x3A);
    zs(0x98, a);
    m7b(0x1C);
    a = add_prod(z(0xF4));
    if(!lt(a, z(0x38))) a = z(0x38);
    if(!le(a, z(0x98))) {
      zs(0x96, a);
      a = (uint16_t) (z(0xF2) - z(0x5A));
      if(lt(a, z(0x34))) a = z(0x34);
      zs(0x92, a);
      a = (uint16_t) (z(0xF2) + z(0x5A));
      if(!lt(a, z(0x36))) a = z(0x36);
      a = (uint16_t) (a - 1);
      if(!lt(a, z(0x92))) {
        zs(0x94, a);
        zor(0x52, 0x0020);
      }
    }
  }
  zs(0x08, 0x005B);
  zs(0x0A, z(0x56));
  zs(0x0C, (uint16_t) (z(0xF2) - z(0x5A)));
  zs(0x0E, z(0xF4));
  draw(r);
  zs(0x08, 0x005C);
  zs(0x0A, z(0x56));
  zs(0x0C, (uint16_t) (z(0xF2) + z(0x5A)));
  zs(0x0E, z(0xF4));
  draw(r);
}

static void close_sides(uint16_t x) {
  zs(0x34, 0x0080);
  zs(0x6C, 0x0080);
  ws((uint16_t) (0xC0C0 + x), 0x0080);
  zs(0x36, 0x007F);
  zs(0x6E, 0x007F);
  ws((uint16_t) (0xC0C2 + x), 0x007F);
}

// $9901: a tunnel entrance.
static bool tunnel_start(void) {
  zs(0x68, 0);
  uint16_t x = z(0x66), a = z(0xF4);
  if(le(a, z(0x3A))) {
    a = z(0x3A);
  } else {
    if(!lt(a, z(0x38))) return true;
    zs(0x3A, a);
  }
  zs(0x72, a);
  zs(0x74, a);
  zs(0x76, a);
  ws((uint16_t) (0xC0C4 + x), a);
  m7b(0x15);
  a = add_prod(z(0xF4));
  if(!lt(a, z(0x38))) {
    a = z(0x38);
  } else if(!le(a, z(0x3A))) {
    zs(0x38, a);
  } else {
    zs(0x68, (uint16_t) (z(0x68) - 1));
    a = z(0x3A);
    zs(0x38, a);
    zs(0x70, a);
    m7b(0x1E);
    a = add_prod(z(0xF4));
    if(!lt(a, z(0x30))) a = z(0x30);
    else if(lt(a, z(0x3A))) a = z(0x3A);
    zs(0x32, a);
    zs(0x78, a);
    zs(0x6A, a);
    close_sides(x);
    goto done;
  }
  zs(0x70, a);
  m7b(0x1E);
  a = add_prod(z(0xF4));
  if(!lt(a, z(0x30))) a = z(0x30);
  zs(0x32, a);
  zs(0x78, a);
  zs(0x6A, a);
  a = (uint16_t) (z(0xF2) - z(0x5A));
  if(le(a, z(0x34))) {
    a = z(0x34);
  } else {
    if(!lt(a, z(0x36))) goto closed;
    zs(0x34, a);
  }
  zs(0x6C, a);
  ws((uint16_t) (0xC0C0 + x), a);
  a = (uint16_t) (z(0xF2) + z(0x5A));
  if(!lt(a, z(0x36))) {
    a = z(0x36);
  } else {
    if(le(a, z(0x34))) goto closed;
    zs(0x36, a);
  }
  a = (uint16_t) (a - 1);
  zs(0x6E, a);
  ws((uint16_t) (0xC0C2 + x), a);
  goto done;
closed:
  zs(0x68, (uint16_t) (z(0x68) - 1));
  close_sides(x);
done:
  zor(0x50, 0x0001);
  zor(0x52, 0x0001);
  zs(0x66, (uint16_t) (x + 8));
  return false;
}

// The left wall's edge at slot x: C0C0 and the new left clip, returned for $82.
static uint16_t wall_left(uint16_t x, uint16_t a) {
  if(le(a, z(0x34))) {
    ws((uint16_t) (0xC0C0 + x), a);
    return z(0x34);
  }
  if(lt(a, z(0x36))) {
    zs(0x34, a);
    ws((uint16_t) (0xC0C0 + x), a);
    return a;
  }
  a = z(0x36);
  zs(0x34, a);
  a = (uint16_t) (a - 1);
  ws((uint16_t) (0xC0C0 + x), a);
  return a;
}

// The right wall's edge: C0C2 and the new right clip, returned for $88.
static uint16_t wall_right(uint16_t x, uint16_t a) {
  if(!lt(a, z(0x36))) {
    ws((uint16_t) (0xC0C2 + x), (uint16_t) (a - 1));
    return (uint16_t) (z(0x36) - 1);
  }
  if(le(a, z(0x34))) {
    a = z(0x34);
    zs(0x36, a);
    ws((uint16_t) (0xC0C2 + x), a);
    return a;
  }
  zs(0x36, a);
  a = (uint16_t) (a - 1);
  ws((uint16_t) (0xC0C2 + x), a);
  return a;
}

// $9A1E / $9AEA / $9BB6: a wall starts on the left (2), right (4) or both sides (6).
static bool wall_start(uint16_t side) {
  zor(0x52, side);
  if(side & 2) zs(0x7A, z(0x3A));
  if(side & 4) zs(0x7C, z(0x3A));
  uint16_t before = z(0x50);
  zor(0x50, side);
  uint16_t other = side == 2 ? 4 : 2;
  bool fresh = side == 6 || !(before & other);
  uint16_t x;
  if(fresh) {
    x = z(0x66);
    ws((uint16_t) (0xC0C4 + x), z(0xF4));
    m7b(0x28);
    ws((uint16_t) (0xC0C6 + x), add_prod(z(0xF4)));
  } else {
    x = (uint16_t) (z(0x66) - 8);
  }
  m7b(0x1E);
  if(side & 2) zs(0x82, wall_left(x, sub_prod(z(0xF2))));
  if(side & 4) zs(0x88, wall_right(x, side == 6 ? add_prod_wide(z(0xF2)) : add_prod(z(0xF2))));
  if(side & 2) {
    zs(0x7E, x);
    zs(0x80, x);
  }
  if(side & 4) {
    zs(0x84, x);
    zs(0x86, x);
  }
  if(!fresh) return false;
  zs(0x66, (uint16_t) (x + 8));
  if(side == 6) return false;
  return !lt(z(0x34), z(0x36));
}

// The $97A1 table: scenery markers in the road data ($C000 | n).
static bool marker(TgRegs* r, uint16_t n) {
  switch(n) {
  case 0: gantry(r, 0x0110); return false;
  case 1: gantry(r, 0x0112); return false;
  case 2: return tunnel_start();
  case 3:
    zs(0x74, z(0x38));
    zand(0x50, 0xFFFE);
    return false;
  case 4: return wall_start(2);
  case 5: zand(0x50, 0xFFFD); return false;
  case 6: return wall_start(4);
  case 7: zand(0x50, 0xFFFB); return false;
  case 8: return wall_start(6);
  case 9: zand(0x50, 0xFFF9); return false;
  case 10:
  case 12:
  case 14: {
    uint16_t m = n == 10 ? 0x08 : n == 12 ? 0x10 : 0x18;
    zor(0x50, m);
    zor(0x52, m);
    uint16_t t = z(0x3A);
    if(m & 0x08) {
      zs(0x8A, t);
      zs(0x8C, t);
    }
    if(m & 0x10) {
      zs(0x8E, t);
      zs(0x90, t);
    }
    return false;
  }
  case 11: zand(0x50, 0xFFF7); return false;
  case 13: zand(0x50, 0xFFEF); return false;
  default: zand(0x50, 0xFFE7); return false;
  }
}

static void (*const kObjectTypes[16])(TgRegs*) = {
  Tg2ObjectType0, Tg2ObjectType1, Tg2ObjectType2,  Tg2ObjectType3,  Tg2ObjectType4,  Tg2ObjectType5,
  Tg2ObjectType6, Tg2ObjectType7, Tg2ObjectType8,  Tg2ObjectType9,  Tg2ObjectType10, Tg2ObjectType11,
  Tg2ObjectType12, Tg2ObjectType13, Tg2ObjectType14, Tg2ObjectType15,
};

// A roadside object at road offset ($0C) and height ($0E) from its type, scaled by the
// strip's depth d.
static void place_object(TgRegs* r, uint16_t y, uint16_t d) {
  r->y = y;
  kObjectTypes[g_ram[0x6400 + y] & 0x0F](r);
  zs(0x08, r->a);
  zs(0x0C, r->x);
  zs(0x0E, r->y);
  zs(0x0A, d);
  zs(0x5E, rom16(0x8F8B40 + d));
  zs(0x0C, (uint16_t) (mul_wide(z(0x0C), 4) - 0x0800 + z(0xF2)));
  uint16_t e = z(0x0E);
  zs(0x0E, (uint16_t) ((e ? (uint16_t) (mul_wide(e, 4) - 0x0800) : 0) + z(0xF4)));
  draw(r);
}

// The strip's roadside slot (road data word at $48 + 6). Returns true to stop the pass.
static bool roadside(TgRegs* r, uint16_t d, bool first) {
  uint16_t a = w((uint16_t) (z(0x48) + 6));
  if(a == 0) return false;
  if(!neg(a)) {
    // A plain sprite: frame in the low byte, lateral offset in the high byte.
    zs(0x08, a & 0x00FF);
    zs(0x0A, d);
    m7b((uint8_t) ((a >> 8) - 0x40));
    bool c = (rd(0x2134) & 0x80) != 0;
    zs(0x0C, (uint16_t) (z(0xF2) + rd16(0x2135) + c));
    zs(0x0E, z(0xF4));
    draw(r);
    return false;
  }
  if(a & 0x4000) {
    if(first && a >= 0xC002) return false;
    return marker(r, a & 0x000F);
  }
  uint16_t y = a & 0x3FFF;
  if(g_ram[0x6400 + y] & 0x80) return false;
  place_object(r, y, d);
  return false;
}

// Moves a car's position from the strip start to its depth: (end - start) * f / n, with
// f = the depth step and n = the strip length (the first strip divides; later strips
// use a fixed fraction).
static uint16_t lerp_first(uint16_t from, uint16_t to) {
  uint16_t d = (uint16_t) (from - to);
  if(d == 0) return 0;
  bool n = neg(d);
  m7a(n ? (uint16_t) (0 - d) : d);
  m7b((uint8_t) (zb(0x0A) >> 1));
  uint16_t s = z(0x58);
  uint16_t num = (uint16_t) ((s >> 2) + rd16(0x2134) + ((s >> 1) & 1));
  tg_bus_write16(0x4204, num);
  tg_bus_write8(0x4206, (uint8_t) (zb(0x58) >> 1));
  uint16_t q = rd16(0x4214);
  return n ? (uint16_t) (0 - q) : q;
}

static uint16_t lerp_later(uint16_t from, uint16_t to) {
  uint16_t d = (uint16_t) (from - to);
  if(d == 0) return 0;
  bool n = neg(d);
  m7a(n ? (uint16_t) (0 - d) : d);
  m7b(zb(0x62));
  bool c = (rd(0x2134) & 0x80) != 0;
  uint16_t v = (uint16_t) (rd16(0x2135) << 1 | c);
  return n ? (uint16_t) (0 - v) : v;
}

// One car from the visible list at y: its screen position (perspective by the strip's
// depth factor $5E) and frame set by its angle to the camera.
static void draw_car(TgRegs* r, uint16_t y, bool first) {
  ws((uint16_t) (0x1DF2 + z(0x10)), (uint16_t) ((w((uint16_t) (0xC340 + y)) & 7) << 9));
  zs(0x0C, w((uint16_t) (0xC342 + y)));
  zs(0x0E, w((uint16_t) (0xC344 + y)));
  uint16_t dist = w((uint16_t) (0xC346 + y));
  zs(0x0A, dist);
  zs(0x5E, rom16(0x8F8B40 + dist));
  zs(0x60, rom16(0x8FBC00 + dist));
  uint16_t a = first ? lerp_first(z(0xEE), z(0xF6)) : lerp_later(z(0xEE), z(0xF6));
  zs(0x0C, (uint16_t) (a - z(0xEE) + z(0x0C)));
  a = first ? lerp_first(z(0xF8), z(0xF0)) : lerp_later(z(0xF8), z(0xF0));
  zs(0x0E, (uint16_t) (a + z(0xF0) + z(0x0E)));
  uint16_t c = z(0x0C);
  if(neg(c)) zs(0x08, c < 0xFFD1 ? 0x83 : 0x82);
  else zs(0x08, c >= 0x0030 ? 0x81 : 0x82);
  if(neg(c)) zs(0x0C, (uint16_t) (0x0280 - mul_wide((uint16_t) (0 - c), 2)));
  else zs(0x0C, (uint16_t) (mul_wide(c, 2) - 0x0180));
  uint16_t e = z(0x0E);
  if(e == 0) a = 0;
  else if(neg(e)) a = (uint16_t) (0x0200 - mul_wide((uint16_t) (0 - e), 2));
  else a = (uint16_t) (mul_wide(e, 2) - 0x0200);
  zs(0x0E, (uint16_t) (a + z(0x60)));
  draw(r);
}

// The cars in the current strip (nearer than $58), from the list position $46; $42 counts
// the cars left.
static void strip_cars(TgRegs* r, bool first) {
  while(z(0x42) != 0) {
    uint16_t y = z(0x46);
    uint16_t v = w((uint16_t) (0xC346 + y));
    if(v >= z(0x58)) return;
    if(!first) zs(0x62, (uint16_t) (((v - z(0x58)) & 0x003E) << 1));
    zs(0x46, (uint16_t) (y + 8));
    if(!neg(w((uint16_t) (0xC340 + y)))) draw_car(r, y, first);
    zs(0x42, (uint16_t) (z(0x42) - 1));
  }
}

// Advances to the next strip: road data, depth and scale.
static void next_strip(void) {
  uint16_t x = (uint16_t) (z(0x48) + 2);
  uint16_t a = (uint16_t) ((z(0x4A) + 1) & 3);
  zs(0x4A, a);
  if(a == 0) x = (uint16_t) (x + 8);
  zs(0x48, x);
  zs(0x56, z(0x58));
  x = z(0x58);
  zs(0x58, (uint16_t) (x + 0x40));
  zs(0x5A, rom16(0x8FABC0 + x));
  set_scale(x);
}

// $0B:8578.
void Tg2ObjectPass(TgRegs* r) {
  uint16_t d0 = r->d;
  uint8_t db0 = r->db;
  zs(0x4C, (uint16_t) (z(0x4C) - 1));
  zs(0x20, 0x0024);
  zs(0x28, 0x0080);
  if(!neg(w(0x0514))) {
    player_car(r, 0xDC);
    player_car(r, 0xE2);
    player_car(r, 0xE8);
  }
  if(effects(r)) shadow(r);

  // $8C24: the first strip.
  zs(0x48, (uint16_t) (z(0x48) + (z(0x4A) << 1) + 0x0010));
  zs(0x64, 0);
  zs(0x66, 0);
  zs(0x56, 0);
  Tg2ListVisibleCars(r);
  zs(0x46, 0);
  zs(0x42, 0x0014);
  zs(0x5A, rom16(0x8FABC0));
  set_scale(0);
  uint16_t x = z(0x4E);
  zs(0xEE, w(x));
  zs(0xF0, w((uint16_t) (x + 2)));
  zs(0xF2, (uint16_t) (0x0200 - w((uint16_t) (x + 4))));
  zs(0xF4, w((uint16_t) (x + 6)));
  zs(0xF6, w((uint16_t) (x + 8)));
  zs(0xF8, w((uint16_t) (x + 10)));
  zs(0xFA, (uint16_t) (0x0200 - w((uint16_t) (x + 12))));
  zs(0xFC, w((uint16_t) (x + 14)));
  zs(0x4E, (uint16_t) (x + 0x10));
  zs(0x52, z(0x50));
  first_strip_state();
  zs(0x58, (uint16_t) (0x0040 - z(0x54)));
  bool stop = false;
  if(z(0x54) == 0) stop = roadside(r, 0, true);
  if(!stop) {
    strip_cars(r, true);
    // The middle strips.
    for(;;) {
      zs(0x4C, (uint16_t) (z(0x4C) - 1));
      if(z(0x4C) == 0) break;
      next_strip();
      x = z(0x4E);
      zs(0xEE, z(0xF6));
      zs(0xF6, w(x));
      zs(0xF0, z(0xF8));
      zs(0xF8, w((uint16_t) (x + 2)));
      zs(0xF2, z(0xFA));
      zs(0xFA, (uint16_t) (0x0200 - w((uint16_t) (x + 4))));
      zs(0xF4, z(0xFC));
      zs(0xFC, w((uint16_t) (x + 6)));
      zs(0x4E, (uint16_t) (x + 8));
      if(later_strip_state() || roadside(r, z(0x56), false)) {
        stop = true;
        break;
      }
      strip_cars(r, false);
    }
  }
  if(!stop) {
    // $9419: the last strip.
    next_strip();
    zs(0xEE, z(0xF6));
    zs(0xF0, z(0xF8));
    zs(0xF2, z(0xFA));
    zs(0xF4, z(0xFC));
    if(!later_strip_state() && !roadside(r, z(0x56), false)) {
      uint16_t a = z(0x50) & 0x0006;
      if(a) {
        uint16_t v = w(0x045C);
        if(a & 2) zs(0x8C, v);
        if(a & 4) zs(0x90, v);
      }
    }
  }

  // $9594.
  zs(0x64, (uint16_t) (z(0x66) >> 3));
  zs(0x66, 0);
  r->d = DPB;
  r->db = 0x7E;
  Tg2FinishOam(r);
  // $0E:8B40: the window HDMA builder for what was seen ($52).
  r->a = r->x = (uint16_t) (z(0x52) << 1);
  if(!Tg2Windows(r)) {
    uint8_t bank = tg_port_set_bank(0x0E);
    tg_call_jsr(r, rom16(0x8E8B4C + r->x), 0x8B49, TG_MX_16);
    tg_port_set_bank(bank);
  }
  r->d = d0;
  r->db = db0;
}
