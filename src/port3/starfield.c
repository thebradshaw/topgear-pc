// Top Gear 3000: the intro's flying stars (bank $00).
//
// $00:F950 moves each of the 128 stars at $0F00 (six bytes: X, Y, depth, and a speed
// byte) toward the viewer by A, projects it with the CPU's multiplier and divider around
// the centre (128, 92) at zoom X, and adds it to the sprite buffer at slot $74. A star
// that leaves the screen or passes the viewer is put back far away at a random place
// ($00:87E9) and tried again.

#include "../port/port.h"

static uint16_t z(const TgRegs* r, uint8_t o) { return ram16((uint16_t) (r->d + o)); }
static void zs(const TgRegs* r, uint8_t o, uint16_t v) { ram16_set((uint16_t) (r->d + o), v); }

static uint32_t at(const TgRegs* r, uint16_t a) { return ((uint32_t) r->db << 16) | a; }
static uint8_t rd8(const TgRegs* r, uint16_t a) { return tg_bus_read8(at(r, a)); }
static uint16_t rd16(const TgRegs* r, uint16_t a) {
  return (uint16_t) (rd8(r, a) | rd8(r, (uint16_t) (a + 1)) << 8);
}
static void wr16(const TgRegs* r, uint16_t a, uint16_t v) { tg_bus_write16(at(r, a), v); }

// $00:87E9: the 32-bit shift-register random number at $10.
static uint16_t rng(const TgRegs* r) {
  uint16_t lo = z(r, 0x10), hi = z(r, 0x12);
  uint16_t c = lo >> 15;
  lo = (uint16_t) (lo << 1);
  hi = (uint16_t) ((hi << 1) | c);
  if(!(z(r, 0x12) & 0x8000)) {
    lo ^= 0x2B45;
    hi ^= 0x1D87;
  }
  zs(r, 0x10, lo);
  zs(r, 0x12, hi);
  return lo;
}

static uint16_t asr(uint16_t v) { return (uint16_t) ((v >> 1) | (v & 0x8000)); }

static uint16_t divide(const TgRegs* r, uint16_t n, uint16_t y) {
  tg_bus_write16(0x004204, n);
  tg_bus_write8(0x004206, rd8(r, (uint16_t) (0x0F05 + y)));
  return (uint16_t) (tg_bus_read8(0x004214) | tg_bus_read8(0x004215) << 8);
}

void Tg3Starfield(TgRegs* r) {
  zs(r, 0x18, r->x);
  zs(r, 0x16, r->a);
  uint16_t y = 0, x = z(r, 0x74);
  while(y != 0x300) {
    tg_bus_write8(0x004202, rd8(r, (uint16_t) (0x0F05 + y)));
    tg_bus_write8(0x004203, g_ram[(uint16_t) (r->d + 0x18)]);
    zs(r, 0x19, (uint16_t) (tg_bus_read8(0x004216) | tg_bus_read8(0x004217) << 8));
    bool ok = false;
    uint16_t a = (uint16_t) (z(r, 0x19) + rd16(r, (uint16_t) (0x0F00 + y)));
    if(a & 0x8000) a = (uint16_t) (0x80 - divide(r, (uint16_t) -a, y));
    else a = (uint16_t) (0x80 + divide(r, a, y));
    if(!(a & 0xFF00)) {
      zs(r, 0x14, a);
      a = (uint16_t) (rd16(r, (uint16_t) (0x0F02 + y)) - z(r, 0x19));
      if(a & 0x8000) a = (uint16_t) (0x5C - divide(r, (uint16_t) -a, y));
      else a = (uint16_t) (0x5C + divide(r, a, y));
      if(a < 0xE0) {
        wr16(r, (uint16_t) (0x0900 + x), (uint16_t) ((a & 0xFF) << 8 | z(r, 0x14)));
        uint16_t t = rd16(r, (uint16_t) (0xD692 + (rd16(r, (uint16_t) (0x0F05 + y)) & 0x7C)));
        wr16(r, (uint16_t) (0x0902 + x), (uint16_t) (t | z(r, 0x80)));
        wr16(r, (uint16_t) (0x0B01 + x), 0);
        uint16_t depth = (uint16_t) (rd16(r, (uint16_t) (0x0F04 + y)) - z(r, 0x16));
        if(depth >= 0x100) {
          wr16(r, (uint16_t) (0x0F04 + y), depth);
          x = (uint16_t) (x + 4);
          y = (uint16_t) (y + 6);
          ok = true;
        }
      }
    }
    if(!ok) {
      wr16(r, (uint16_t) (0x0F00 + y), asr(rng(r)));
      uint16_t v = rng(r);
      wr16(r, (uint16_t) (0x0F02 + y), asr((uint16_t) (v << 8 | v >> 8)));
      wr16(r, (uint16_t) (0x0F04 + y), 0x7F00);
    }
  }
  zs(r, 0x74, x);
  r->a = 0x300;
  r->x = x;
  r->y = 0x300;
  r->n = false;
  r->z = true;
  r->c = true;
  r->v = false;
}
