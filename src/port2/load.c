// Top Gear 2: loading a race: the PPU's settings, the track's scenery, dashboard and
// backdrop graphics, and the race's palettes and tables ($01:8717).

#include <stddef.h>

#include "../port/port.h"

void Tg2Decompress(TgRegs* r);
void Tg2AddToWords(TgRegs* r);
void Tg2VramDma7E(TgRegs* r);
void Tg2VramDma7F(TgRegs* r);
void Tg2PlanarTiles(TgRegs* r);
void Tg2Colours(TgRegs* r);

static inline uint16_t dp16(const TgRegs* r, uint8_t o) { return ram16((uint16_t) (r->d + o)); }
static inline void dp16_set(const TgRegs* r, uint8_t o, uint16_t v) { ram16_set((uint16_t) (r->d + o), v); }


// $01:F657: the PPU's and the interrupt timers' settings from a 68-byte table at $82:X
// (bank $82): $2100-$2133 (the scroll and Mode 7 registers written twice), then $4200,
// $4207-$420A and $420D. Leaves A's low byte the last byte, and N/Z from the restored
// data bank (the closing PLB).
static const uint16_t kPpuRegs[0x44] = {
  0x2100, 0x2101, 0x2102, 0x2103, 0x2105, 0x2106, 0x2107, 0x2108, 0x2109, 0x210A, 0x210B,
  0x210C, 0x210D, 0x210D, 0x210E, 0x210E, 0x210F, 0x210F, 0x2110, 0x2110, 0x2111, 0x2111,
  0x2112, 0x2112, 0x2113, 0x2113, 0x2114, 0x2114, 0x2115, 0x2116, 0x2117, 0x211A, 0x211B,
  0x211B, 0x211C, 0x211C, 0x211D, 0x211D, 0x211E, 0x211E, 0x211F, 0x211F, 0x2120, 0x2120,
  0x2121, 0x2123, 0x2124, 0x2125, 0x2126, 0x2127, 0x2128, 0x2129, 0x212A, 0x212B, 0x212C,
  0x212D, 0x212E, 0x212F, 0x2130, 0x2131, 0x2132, 0x2133, 0x4200, 0x4207, 0x4208, 0x4209,
  0x420A, 0x420D,
};

void Tg2SetPpu(TgRegs* r) {
  uint8_t v = 0;
  for(int i = 0; i < 0x44; i++) {
    v = tg_bus_read8(0x820000 | (uint16_t) (r->x + i));
    tg_bus_write8(kPpuRegs[i], v);
  }
  r->a = (uint16_t) ((r->a & 0xFF00) | v);
  r->n = (r->db & 0x80) != 0;
  r->z = r->db == 0;
}

// $01:DE85 then $01:EF6C: unpacks bank A:X to $7F:0000 and sends the result (A = its
// length in bytes) to VRAM word address vram.
static void unpack_to_vram(TgRegs* r, uint16_t bank, uint16_t src, uint16_t vram) {
  r->a = bank;
  r->x = src;
  r->y = 0;
  Tg2Decompress(r);
  r->a >>= 1;
  r->x = 0;
  r->y = vram;
  Tg2VramDma7F(r);
}

// Unpacks bank:src to $7F:0000, then adds add to each of its words (count words).
static void unpack_and_add(TgRegs* r, uint16_t bank, uint16_t src, uint16_t count,
                           uint16_t add) {
  r->a = bank;
  r->x = src;
  r->y = 0;
  Tg2Decompress(r);
  dp16_set(r, 0xA0, count);
  dp16_set(r, 0x98, 0x0000);
  dp16_set(r, 0x9A, 0x007F);
  dp16_set(r, 0xA2, add);
  Tg2AddToWords(r);
}

static void vram_from_7f(TgRegs* r, uint16_t count, uint16_t src, uint16_t vram) {
  r->a = count;
  r->x = src;
  r->y = vram;
  Tg2VramDma7F(r);
}

// $01:D30F: the race's dashboard: its tiles to VRAM $0000, its map (+$0C00 to each word,
// $0600 words) to $7100 and $7500.
void Tg2DashboardGraphics(TgRegs* r) {
  ram16_set(0x0A70, 0x1062);
  ram16_set(0x0A72, 0x41EE);
  unpack_to_vram(r, 0x7F8D, 0xB848, 0x0000);
  unpack_and_add(r, 0x7F8D, 0xB560, 0x0600, 0x0C00);
  vram_from_7f(r, 0x0300, 0x0000, 0x7100);
  vram_from_7f(r, 0x0300, 0x0600, 0x7500);
}

// $01:D2C2: the backdrop: tiles to VRAM $3720, its map (+$20E4 to each word) to $7C00.
void Tg2BackdropGraphics(TgRegs* r) {
  ram16_set(0x0140, 0x0080);
  ram16_set(0x0144, 0x0080);
  unpack_to_vram(r, 0x7F87, 0xC720, 0x3720);
  r->a = 0x7F87;
  r->x = 0xC4B6;
  r->y = 0;
  Tg2Decompress(r);
  r->a >>= 1;
  dp16_set(r, 0xA0, r->a);
  dp16_set(r, 0x98, 0x0000);
  dp16_set(r, 0x9A, 0x007F);
  dp16_set(r, 0xA2, 0x20E4);
  Tg2AddToWords(r);
  vram_from_7f(r, dp16(r, 0xA0), 0x0000, 0x7C00);
}

// The track's entry in bank $87: $87:0000 + ($87:A656 + 2 x (track / 4 x 2)) + 2 x
// (track & 3), from the track number at $0108.
static uint16_t track_entry(void) {
  uint16_t track = ram16(0x0108);
  uint16_t x = (uint16_t) ((track & 0x3C) >> 1);
  uint16_t i = (uint16_t) (((track & 3) << 1) + tg_read16(0x87A656 + x));
  return tg_read16(0x870000 + i);
}

static uint16_t t87(uint16_t entry, uint16_t o) { return tg_read16(0x870000 + (uint16_t) (entry + o)); }

static void decompress(TgRegs* r, uint16_t bank, uint16_t src, uint16_t dst) {
  r->a = bank;
  r->x = src;
  r->y = dst;
  Tg2Decompress(r);
}

// The backdrop gradient's three channels. Each keeps five words at $1F00 + 10 x n: the
// colour it falls back to, the running colour (8.8), the steps for the two halves, and
// bits saying which lines use the fallback.
typedef struct { uint16_t base; uint16_t mask; int shift; } Channel;
static const Channel kChannels[3] = {
  { 0x1F00, 0x001F, 8 }, { 0x1F0A, 0x03E0, 3 }, { 0x1F14, 0x7C00, -2 },
};

static uint16_t shifted(uint16_t v, int s) {
  return (uint16_t) (s >= 0 ? v << s : v >> -s);
}

static uint16_t gradient_step(uint16_t to, uint16_t from) {
  uint16_t d = (uint16_t) (to - from);
  if(d & 0x8000) d |= 0x00FF;
  d = (uint16_t) ((d >> 8) | (d << 8));
  return (uint16_t) (d << 1);
}

static void gradient_lines(TgRegs* r, uint16_t step_at, uint16_t flags) {
  uint32_t dst = (uint32_t) dp16(r, 0x98) | (uint32_t) g_ram[(uint16_t) (r->d + 0x9A)] << 16;
  for(int line = 0; line < 0x80; line++) {
    uint16_t colour = 0;
    for(int k = 0; k < 3; k++) {
      uint16_t b = kChannels[k].base;
      uint16_t bits = ram16(b + 8);
      uint16_t a = (bits & 1) ? ram16(b) : ram16(b + 2);
      ram16_set(b + 8, (uint16_t) (bits >> 1));
      a &= 0x1F00;
      colour |= k == 0 ? (uint16_t) (a >> 8) : k == 1 ? (uint16_t) (a >> 3) : (uint16_t) (a << 2);
      dp16_set(r, 0x80, colour);
    }
    uint16_t y = dp16(r, 0x9E);
    tg_bus_write16((dst + y) & 0xFFFFFF, 0x2F2F);
    tg_bus_write16((dst + (uint16_t) (y + 2)) & 0xFFFFFF, colour);
    dp16_set(r, 0x9E, (uint16_t) (y + 4));
    for(int k = 0; k < 3; k++) {
      uint16_t b = kChannels[k].base;
      uint16_t x = ram16(b + 2);
      uint16_t next = (uint16_t) (x + ram16(b + step_at));
      if((x & 0xFF00) != (next & 0xFF00)) {
        ram16_set(b + 8, dp16(r, flags));
        ram16_set(b, x);
      }
      ram16_set(b + 2, next);
    }
  }
}

// $01:D473: loads the track's scenery: its tiles (unpacked and converted to VRAM $17C0),
// map ($7000/$7400), colours, settings ($76-$7E, $0110-$0116, $0286/$0288), the backdrop
// gradient's HDMA table ($7E:8600) and the table at $7E:9C00.
void Tg2LoadTrack(TgRegs* r) {
  ram16_set(0x0238, 0x0100);
  ram16_set(0x0274, 0x0100);
  uint16_t e = track_entry();
  ram16_set(0x010E, e);
  decompress(r, (uint16_t) (t87(e, 2) | 0x7F00), t87(e, 0), 0);
  e = ram16(0x010E);
  dp16_set(r, 0xA0, (uint16_t) (t87(e, 6) >> 5));
  dp16_set(r, 0x94, t87(e, 4));
  dp16_set(r, 0x96, 0x007F);
  dp16_set(r, 0x98, 0x8000);
  dp16_set(r, 0x9A, 0x007E);
  Tg2PlanarTiles(r);
  e = ram16(0x010E);
  r->a = (uint16_t) (t87(e, 6) >> 1);
  r->x = 0x8000;
  r->y = 0x17C0;
  Tg2VramDma7E(r);
  decompress(r, 0x7F87, 0x8006, 0);
  e = ram16(0x010E);
  dp16_set(r, 0xA0, 0x0200);
  dp16_set(r, 0x98, t87(e, 8));
  dp16_set(r, 0x9A, 0x007F);
  dp16_set(r, 0xA2, 0x097C);
  Tg2AddToWords(r);
  e = ram16(0x010E);
  vram_from_7f(r, 0x0100, t87(e, 8), 0x7000);
  e = ram16(0x010E);
  vram_from_7f(r, 0x0100, (uint16_t) (t87(e, 8) + 0x0200), 0x7400);
  decompress(r, 0x7F87, 0x9D2C, 0);
  e = ram16(0x010E);
  dp16_set(r, 0x94, t87(e, 0x0A));
  dp16_set(r, 0x96, 0x007F);
  uint32_t lo = (uint32_t) dp16(r, 0xD6) + 0x40;
  dp16_set(r, 0x98, (uint16_t) lo);
  dp16_set(r, 0x9A, (uint16_t) (dp16(r, 0xD8) + (lo >> 16)));
  uint16_t kind = tg_read16(0x8A8006 + (uint16_t) (ram16(0x0108) << 3)) & 7;
  r->a = 0x0010;
  if(kind == 6) {
    static const uint16_t kLight[6] = { 0x20, 0x20, 0x20, 0x0E, 0x0F, 0x0D };
    uint16_t saved[6];
    for(int i = 0; i < 6; i++) {
      saved[i] = dp16(r, (uint8_t) (0xB2 + 2 * i));
      dp16_set(r, (uint8_t) (0xB2 + 2 * i), kLight[i]);
    }
    // The original saves them on the stack.
    for(int i = 0; i < 6; i++) tg_push16(saved[i]);
    Tg2Colours(r);
    for(int i = 5; i >= 0; i--) dp16_set(r, (uint8_t) (0xB2 + 2 * i), tg_pull16());
  } else {
    Tg2Colours(r);
  }
  e = ram16(0x010E);
  for(int i = 0; i < 5; i++) dp16_set(r, (uint8_t) (0x76 + 2 * i), t87(e, (uint16_t) (0x0C + 2 * i)));
  ram16_set(0x0288, t87(e, 0x16));
  ram16_set(0x0286, t87(e, 0x18));
  ram16_set(0x0114, t87(e, 0x1A));
  ram16_set(0x0116, t87(e, 0x1C));
  ram16_set(0x0110, t87(e, 0x1E));
  ram16_set(0x0112, t87(e, 0x20));
  dp16_set(r, 0x98, 0x8600);
  dp16_set(r, 0x9A, 0x007E);
  dp16_set(r, 0x9E, 0x0000);
  for(int k = 0; k < 3; k++) {
    const Channel* c = &kChannels[k];
    uint16_t top = shifted(dp16(r, 0x76) & c->mask, c->shift);
    uint16_t mid = shifted(dp16(r, 0x78) & c->mask, c->shift);
    uint16_t bottom = shifted(dp16(r, 0x7A) & c->mask, c->shift);
    ram16_set(c->base, top);
    ram16_set(c->base + 2, top);
    ram16_set(c->base + 4, mid);
    ram16_set(c->base + 6, bottom);
    ram16_set(c->base + 8, 0);
    ram16_set(c->base + 6, gradient_step(bottom, mid));
    ram16_set(c->base + 4, gradient_step(mid, top));
  }
  dp16_set(r, 0xA0, 0x0080);
  gradient_lines(r, 4, 0x7C);
  dp16_set(r, 0xA0, 0x0080);
  gradient_lines(r, 6, 0x7E);
  dp16_set(r, 0xA0, 0);
  uint32_t dst = 0x7E8600;
  uint16_t y = dp16(r, 0x9E);
  for(int i = 0; i < 0x40; i++, y = (uint16_t) (y + 4)) {
    tg_bus_write16(dst + y, 0x2F2F);
    tg_bus_write16(dst + (uint16_t) (y + 2), dp16(r, 0x7A));
  }
  dp16_set(r, 0x98, 0x9C00);
  dp16_set(r, 0x9A, 0x007E);
  y = 0;
  for(uint16_t n = 0xF0; n != 0; n--, y = (uint16_t) (y + 4)) {
    tg_bus_write16(0x7E9C00 + y, 0x0180);
    tg_bus_write16(0x7E9C00 + (uint16_t) (y + 2), (uint16_t) (0x031F + n));
  }
  dp16_set(r, 0xA0, 0);
  r->a = 0x0320;
  r->x = 0;
  r->y = y;
  r->n = false;
  r->z = true;
  r->c = false;
  r->v = false;
}

// $01:D196, $01:D1F2, $01:D24E: a weather backdrop: 32 colours from $87:colours to the
// palette buffer at $D8:$D6, its tiles to VRAM $3720 and its map (+$00E4 to each word) to
// $7C00. D196 is never used (no track has its kind).
static void weather_backdrop(TgRegs* r, uint16_t colours, uint16_t tiles, uint16_t map) {
  dp16_set(r, 0x94, colours);
  dp16_set(r, 0x96, 0x0087);
  dp16_set(r, 0x98, dp16(r, 0xD6));
  dp16_set(r, 0x9A, dp16(r, 0xD8));
  r->a = 0x0020;
  Tg2Colours(r);
  unpack_to_vram(r, 0x7F87, tiles, 0x3720);
  r->a = 0x7F87;
  r->x = map;
  r->y = 0;
  Tg2Decompress(r);
  r->a >>= 1;
  dp16_set(r, 0xA0, r->a);
  dp16_set(r, 0x98, 0x0000);
  dp16_set(r, 0x9A, 0x007F);
  dp16_set(r, 0xA2, 0x00E4);
  Tg2AddToWords(r);
  vram_from_7f(r, dp16(r, 0xA0), 0x0000, 0x7C00);
}
void Tg2FogBackdrop(TgRegs* r) { weather_backdrop(r, 0xF732, 0xF810, 0xF792); }
void Tg2NightBackdrop(TgRegs* r) { weather_backdrop(r, 0xF822, 0xF900, 0xF882); }

// $01:E7F5: the palette buffer at $D8:$D6 to CGRAM. Leaves A = $0080.
void Tg2SendCgram(TgRegs* r) {
  tg_bus_write8(0x2121, 0x00);
  tg_bus_write8(0x420B, 0x00);
  tg_bus_write8(0x4374, g_ram[(uint16_t) (r->d + 0xD8)]);
  tg_bus_write16(0x4370, 0x2202);
  tg_bus_write16(0x4372, dp16(r, 0xD6));
  tg_bus_write16(0x4375, 0x0200);
  tg_bus_write8(0x420B, 0x80);
  r->a = 0x0280;
  r->n = true;
  r->z = false;
}

// $01:D8AF: the track's roadside objects: their placement lists for the race's difficulty
// ($0120, at most 2) unpacked to $7E:6400, $5800 and $5D00 (their ends in $0300, $035A
// and $035C), and the graphics of each kind of object they use unpacked once, one after
// another, into $7F:0000 ($1BF0 + 2 x kind: where each starts).
void Tg2TrackObjects(TgRegs* r) {
  uint16_t level = ram16(0x0120);
  if(level >= 3) level = 2;
  dp16_set(r, 0x76, level);
  uint16_t e = (uint16_t) (track_entry() + level * 12);
  ram16_set(0x010E, e);
  decompress(r, (uint16_t) (t87(e, 0x24) | 0x7E00), t87(e, 0x22), 0x6400);
  ram16_set(0x0300, r->a);
  e = ram16(0x010E);
  decompress(r, (uint16_t) (t87(e, 0x28) | 0x7E00), t87(e, 0x26), 0x5800);
  ram16_set(0x035A, r->a);
  tg_bus_write16(0x7E0000 + r->y, 0xFFFF);
  e = ram16(0x010E);
  decompress(r, (uint16_t) (t87(e, 0x2C) | 0x7E00), t87(e, 0x2A), 0x5D00);
  ram16_set(0x035C, r->a);
  tg_bus_write16(0x7E0000 + r->y, 0xFFFF);
  ram16_set(0x0178, 3);
  ram16_set(0x017A, 3);
  for(int x = 0xFE; x >= 0; x -= 2) ram16_set((uint16_t) (0x1BF0 + x), 0xFFFF);
  dp16_set(r, 0x76, 0);
  uint16_t x = 0;
  for(;; x = (uint16_t) (x + 4)) {
    if(tg_read16(0x7E5800 + x) == 0xFFFF) break;
    uint16_t kind = tg_read16(0x7E5802 + x) >> 8;
    if(kind == 0) continue;
    dp16_set(r, 0x78, x);
    uint16_t i = (uint16_t) (kind << 2);
    uint32_t bank = (uint32_t) tg_bus_read8(0x8BDBE5 + i) << 16;
    uint16_t p = tg_read16(0x8BDBE3 + i);
    p = tg_read16(bank | (uint16_t) (p + 8));
    uint16_t obj = tg_read16(bank | (uint16_t) (p + 2));
    if(ram16((uint16_t) (0x1BF0 + obj)) == 0xFFFF) {
      ram16_set((uint16_t) (0x1BF0 + obj), dp16(r, 0x76));
      uint16_t j = (uint16_t) (obj << 3);
      decompress(r, (uint16_t) (tg_read16(0x8BF353 + j) | 0x7F00), tg_read16(0x8BF351 + j),
                 dp16(r, 0x76));
      dp16_set(r, 0x76, r->y);
    }
    x = dp16(r, 0x78);
  }
  r->a = 0xFFFF;
  r->x = x;
  r->n = false;
  r->z = true;
  r->c = true;
}

void Tg2InitVectors(TgRegs* r);
void Tg2ClearVram1(TgRegs* r);
void Tg2ClearCgram(TgRegs* r);
void Tg2ClearOam(TgRegs* r);
void Tg2ClearPaletteBuffer(TgRegs* r);
void Tg2ClearSpriteBuffer(TgRegs* r);
void Tg2Fill7E(TgRegs* r);
uint16_t tg2_random16(uint16_t a, uint16_t x, uint16_t y, bool carry);
void Tg2StreamStart1(TgRegs* r);
void Tg2StreamStart2(TgRegs* r);

static void fill7e(TgRegs* r, uint16_t dst, uint16_t count) {
  r->a = 0;
  r->x = dst;
  r->y = count;
  Tg2Fill7E(r);
}

// count + 1 bytes from src to dst, as MVN does.
static void move(uint32_t src, uint32_t dst, uint16_t count) {
  for(uint32_t i = 0; i <= count; i++)
    tg_bus_write8((dst & 0xFF0000) | (uint16_t) (dst + i),
                  tg_bus_read8((src & 0xFF0000) | (uint16_t) (src + i)));
}

static void colours(TgRegs* r, uint16_t src, uint32_t dst, uint16_t count) {
  dp16_set(r, 0x94, src);
  dp16_set(r, 0x96, 0x0082);
  dp16_set(r, 0x98, (uint16_t) dst);
  dp16_set(r, 0x9A, (uint16_t) (dst >> 16));
  r->a = count;
  Tg2Colours(r);
}

static void invert(uint16_t adr) { ram16_set(adr, ram16(adr) ^ 0x7FFF); }

// The rain tracks' backdrop map: each 32-word row's palettes changed to a fixed mix.
static const uint16_t kRainPalettes[32] = {
  0x0000, 0x1400, 0x0400, 0x1C00, 0x1800, 0x0000, 0x0C00, 0x1400, 0x1000, 0x1C00, 0x0800,
  0x0C00, 0x0400, 0x0000, 0x1800, 0x0800, 0x1400, 0x1000, 0x1C00, 0x0C00, 0x0000, 0x1800,
  0x0400, 0x0800, 0x1400, 0x0C00, 0x1C00, 0x1000, 0x1800, 0x0800, 0x0400, 0x1000,
};

// The rain and snow tracks' backdrop: tiles to VRAM $3720, map (+$00E4) to $7C00, with
// the rain's palettes mixed in.
static void storm_backdrop(TgRegs* r, bool rain) {
  ram16_set(0x0140, 0x00C0);
  ram16_set(0x0144, 0x0080);
  unpack_to_vram(r, 0x7F87, 0xC29A, 0x3720);
  decompress(r, 0x7F87, 0xBCF6, 0);
  r->a >>= 1;
  dp16_set(r, 0xA0, r->a);
  dp16_set(r, 0x98, 0x0000);
  dp16_set(r, 0x9A, 0x007F);
  dp16_set(r, 0xA2, 0x00E4);
  Tg2AddToWords(r);
  if(rain) {
    for(uint16_t x = 0; x < 0x800; x += 0x40) {
      for(int i = 0; i < 32; i++) {
        uint32_t adr = 0x7F0000 + (uint16_t) (x + 2 * i);
        tg_bus_write16(adr, (uint16_t) ((tg_read16(adr) & 0xE3FF) | kRainPalettes[i]));
      }
    }
  }
  vram_from_7f(r, dp16(r, 0xA0), 0x0000, 0x7C00);
}

// $01:8717: loads a race: blanks the screen, clears video memory and the race's work
// areas, then loads the dashboard, the track's scenery, the road's tables, the backdrop
// for the track's weather (its kind, from $8A:8006: 2 clear, 3 rain, 4 snow, 5 fog, 6 night),
// the cars' palettes and the track's starting values.
void Tg2RaceSetup(TgRegs* r) {
  uint16_t kind = tg_read16(0x8A8006 + (uint16_t) (ram16(0x0108) << 3)) & 7;
  if(kind < 2 || kind == 7) {  // no track has these
    tg_port_decline();
    return;
  }
  tg_call_jsr(r, 0xF43D, 0x871C, TG_MX_16);
  Tg2InitVectors(r);
  dp16_set(r, 0xE0, 0xFFFF);
  Tg2ClearVram1(r);
  Tg2ClearCgram(r);
  Tg2ClearOam(r);
  Tg2ClearPaletteBuffer(r);
  Tg2ClearSpriteBuffer(r);
  fill7e(r, 0x0200, 0x0100);
  fill7e(r, 0x0300, 0x0100);
  fill7e(r, 0x0400, 0x0100);
  fill7e(r, 0x0800, 0x1800);
  fill7e(r, 0x5000, 0x3400);
  fill7e(r, 0x8000, 0x6150);
  colours(r, 0xC0B0, 0x000A00, 0x0080);
  uint16_t road;
  if(kind == 5) {
    colours(r, 0xC6B0, 0x000B00, 0x0080);
    road = 0xD40F;
  } else if(kind == 6) {
    colours(r, 0xC530, 0x000B00, 0x0080);
    road = 0xC3BB;
  } else {
    colours(r, 0xC3B0, 0x000B00, 0x0080);
    road = 0xB393;
  }
  unpack_to_vram(r, 0x7F8E, road, 0x4000);
  r->a = 0x7F8C;
  r->x = 0xF787;
  r->y = 0;
  Tg2Decompress(r);
  r->a >>= 1;
  r->x = 0;
  r->y = 0x2A80;
  Tg2VramDma7F(r);
  Tg2LoadTrack(r);

  uint16_t e = track_entry();
  ram16_set(0x010E, e);
  decompress(r, (uint16_t) (t87(e, 0x48) | 0x7E00), t87(e, 0x46), 0x6200);
  tg_bus_write8((0x7E6200 + r->a) & 0xFFFFFF, tg_bus_read8(0x7E6200));
  e = ram16(0x010E);
  decompress(r, (uint16_t) (t87(e, 0x4C) | 0x7F00), t87(e, 0x4A), 0);
  e = ram16(0x010E);
  vram_from_7f(r, 0x0230, t87(e, 0x4E), 0x3370);
  decompress(r, 0x7F88, 0xEAEB, 0);
  uint16_t ry = r->y;

  // The road's curve and hill tables, summed into $7E:5000.
  e = ram16(0x010E);
  dp16_set(r, 0x76, t87(e, 0x50));
  uint16_t n = (uint16_t) ((uint16_t) (t87(e, 0x52) - dp16(r, 0x76)) >> 1);
  dp16_set(r, 0x7A, n);
  dp16_set(r, 0x78, (uint16_t) (n + dp16(r, 0x76)));
  dp16_set(r, 0x7C, 0);
  dp16_set(r, 0x7E, 0);
  uint16_t base = r->d;
  do {
    uint16_t i = dp16(r, 0x76);
    g_ram[(uint16_t) (base + 0x7E)] += tg_bus_read8(0x7F0000 + i);
    dp16_set(r, 0x76, (uint16_t) (i + 1));
    i = dp16(r, 0x78);
    g_ram[(uint16_t) (base + 0x7F)] += tg_bus_read8(0x7F0000 + i);
    dp16_set(r, 0x78, (uint16_t) (i + 1));
    i = dp16(r, 0x7C);
    tg_bus_write16(0x7E5000 + i, dp16(r, 0x7E));
    dp16_set(r, 0x7C, (uint16_t) (i + 2));
    n = (uint16_t) (dp16(r, 0x7A) - 1);
    dp16_set(r, 0x7A, n);
  } while(n != 0);
  tg_bus_write16(0x7E5000 + dp16(r, 0x7C), 0xFFFF);
  ram16_set(0x015A, 0);
  ram16_set(0x013C, 0);
  ram16_set(0x015C, 0);
  ram16_set(0x013E, 0);

  // Some tracks start reversed half the time.
  uint16_t x = (uint16_t) (ram16(0x0108) << 3);
  uint16_t flags = tg_read16(0x8A8006 + x);
  uint16_t reversed = 0;
  if((flags & 0x8000) && tg2_random16(flags, x, ry, false) < 0x8000) reversed = 0xFFFF;
  ram16_set(0x0500, reversed);
  ram16_set(0x0502, 0);

  switch(kind) {
  case 2:  // clear
    r->x = 0xBEF8;
    Tg2SetPpu(r);
    Tg2DashboardGraphics(r);
    Tg2BackdropGraphics(r);
    tg_bus_write16(0x7E85FC, 0x2F2F);
    tg_bus_write16(0x7E85FE, 0x7FFF);
    ram16_set(0x012C, 0x4040);
    ram16_set(0x010C, 1);
    break;
  case 3:  // rain: the palettes inverted
    r->x = 0xBF3C;
    Tg2SetPpu(r);
    Tg2DashboardGraphics(r);
    storm_backdrop(r, true);
    move(0x87F57A, 0x000A00, 0x0040);
    move(0x87F57A, 0x7ED110, 0x0040);
    for(int i = 0x1E; i >= 0; i -= 2) {
      static const uint16_t kBlocks[10] = { 0x0A40, 0x0A60, 0x0B00, 0x0B20, 0x0B40,
                                            0x0B60, 0x0B80, 0x0BA0, 0x0BC0, 0x0BE0 };
      for(int b = 0; b < 10; b++) invert((uint16_t) (kBlocks[b] + i));
    }
    for(int i = 0x04FE; i >= 0; i -= 4) {
      uint32_t adr = 0x7E8600 + (uint32_t) i;
      tg_bus_write16(adr, tg_read16(adr) ^ 0x7FFF);
    }
    tg_bus_write16(0x7E85FC, 0x2F2F);
    tg_bus_write16(0x7E85FE, 0x0000);
    ram16_set(0x012C, 0x0000);
    ram16_set(0x010C, 4);
    break;
  case 4:  // snow
    r->x = 0xBF80;
    Tg2SetPpu(r);
    Tg2DashboardGraphics(r);
    storm_backdrop(r, false);
    move(0x87F5BA, 0x000A00, 0x0040);
    tg_bus_write16(0x7E85FC, 0x2F2F);
    tg_bus_write16(0x7E85FE, 0x7FFF);
    ram16_set(0x012C, 0x0000);
    ram16_set(0x010C, 5);
    break;
  case 5:  // fog
    r->x = 0xBFC4;
    Tg2SetPpu(r);
    Tg2DashboardGraphics(r);
    Tg2FogBackdrop(r);
    tg_bus_write16(0x7E85FC, 0x2F2F);
    tg_bus_write16(0x7E85FE, 0x7FFF);
    ram16_set(0x012C, 0x4040);
    ram16_set(0x010C, 3);
    break;
  default:  // 6: night
    r->x = 0xC008;
    Tg2SetPpu(r);
    Tg2DashboardGraphics(r);
    Tg2NightBackdrop(r);
    tg_bus_write16(0x7E85FC, 0x2F2F);
    tg_bus_write16(0x7E85FE, 0x7FFF);
    ram16_set(0x012C, 0x4040);
    ram16_set(0x010C, 2);
    break;
  }

  // $8D92: the track's objects, the palettes, the players' car colours.
  Tg2TrackObjects(r);
  Tg2SendCgram(r);
  colours(r, 0xC18E, 0x000A40, 0x0026);
  colours(r, 0xC18E, 0x000AAC, 0x0026);
  colours(r, 0xC18E, 0x000B18, 0x0026);
  colours(r, 0xC18E, 0x000B84, 0x0026);
  for(int p = 0; p < 2; p++) {
    uint16_t car = ram16(p == 0 ? 0x0104 : 0x0106) & 7;
    dp16_set(r, 0x76, car);
    uint16_t dst = p == 0 ? 0x0A8C : 0x0B64;
    colours(r, (uint16_t) (car * 48 + 0xC830), 0x7E0000 | dst, 0x0010);
    move(0x820000 | dst, 0x820000 | (uint16_t) (dst + 0x6C), 0x0020);
  }
  if(kind == 3) {
    for(int i = 0x1E; i >= 0; i -= 2) {
      invert((uint16_t) (0x0A8C + i));
      invert((uint16_t) (0x0AF8 + i));
      invert((uint16_t) (0x0B64 + i));
      invert((uint16_t) (0x0BD0 + i));
    }
  }
  move(0x8C8000, 0x7E8000, 0x05FC);
  move(0x8C8B00, 0x7E8B00, 0x0040);
  fill7e(r, 0x0A00, 0x0040);
  uint8_t bank = tg_port_set_bank(0x88);
  Tg2StreamStart1(r);
  Tg2StreamStart2(r);
  tg_port_set_bank(bank);
  x = (uint16_t) ((ram16(0x010C) & 7) << 1);
  for(int i = 0; i < 10; i++)
    ram16_set((uint16_t) (0x03C2 + 2 * i), tg_read16((uint32_t) r->db << 16 | (uint16_t) (0xDCF6 + x + 0x10 * i)));

  e = track_entry();
  uint16_t lo = t87(e, 0x54), hi = t87(e, 0x56);
  uint32_t neg = (uint32_t) (uint16_t) ~lo + 1;
  uint16_t start_lo = (uint16_t) neg;
  uint16_t start_hi = (uint16_t) ((uint16_t) ~hi + (neg >> 16));
  ram16_set(0x0216, start_lo);
  ram16_set(0x0252, start_lo);
  ram16_set(0x021A, start_lo);
  ram16_set(0x0256, start_lo);
  ram16_set(0x0218, start_hi);
  ram16_set(0x0254, start_hi);
  ram16_set(0x021C, start_hi);
  ram16_set(0x0258, start_hi);
  tg_bus_write16(0x4204, 0x0040);
  tg_bus_write8(0x4206, (uint8_t) (ram16(0x010A) >> 6));
  ram16_set(0x019E, (uint16_t) (tg_bus_read8(0x4214) + (tg_bus_read8(0x4215) << 8) + 1));
  ram16_set(0x00EC, 0);
  if(ram16(0x0514) & 0x8000) {
    ram16_set(0x0504, 0xFFFF);
    ram16_set(0x0506, 0xFFFF);
    ram16_set(0x050C, 0xFFFF);
    ram16_set(0x050E, 0xFFFF);
  }
}

// The dashboard's map, built at $7E:C910 from pieces in bank $8C. A piece of n words from
// $8C:src (a table of one or two choices picked by a sign) goes to each destination.
static uint16_t c8(uint16_t adr) { return tg_read16(0x8C0000 | adr); }
static void d7e(uint16_t adr, uint16_t v) { tg_bus_write16(0x7E0000 | adr, v); }

static void piece(uint16_t src, int words, uint16_t dst, uint16_t dst2) {
  for(int i = words - 1; i >= 0; i--) {
    uint16_t v = c8((uint16_t) (src + 2 * i));
    d7e((uint16_t) (dst + 2 * i), v);
    if(dst2) d7e((uint16_t) (dst2 + 2 * i), v);
  }
}

// A piece that is the end of one of two tables, picked by the sign of choice: the last
// word at last (or other), the ones before it before.
static void piece_from_end(uint16_t choice, uint16_t last, uint16_t other, int words,
                           uint16_t dst) {
  uint16_t y = (ram16(choice) & 0x8000) ? other : last;
  for(int i = words - 1; i >= 0; i--, y = (uint16_t) (y - 2)) d7e((uint16_t) (dst + 2 * i), c8(y));
}

// The rows of tile numbers $2F37 on, seven to a row, five rows, for the map at dst
// (and dst2).
static uint16_t numbered_rows(uint16_t dst, uint16_t dst2) {
  uint16_t a = 0x2F37;
  for(uint16_t x = 0; x != 0x140; x = (uint16_t) (x + 0x32)) {
    for(int i = 0; i < 7; i++, a++, x = (uint16_t) (x + 2)) {
      d7e((uint16_t) (dst + x), a);
      if(dst2) d7e((uint16_t) (dst2 + x), a);
    }
  }
  return a;
}

static void dashboard_done(TgRegs* r, uint16_t a) {
  r->a = a;
  r->x = 0x0140;
  r->y = a;
  r->n = (r->db & 0x80) != 0;
  r->z = r->db == 0;
}

// $01:8FAB: the one-player dashboard.
void Tg2DashboardMap1P(TgRegs* r) {
  if(ram16(0x0514) & 0x8000) {  // stops after the frame; never seen
    tg_port_decline();
    return;
  }
  for(int x = 0x07FE; x >= 0; x -= 2) d7e((uint16_t) (0xC910 + x), c8(0xEE26));
  for(int x = 0x0700; x >= 0; x -= 0x40) {
    d7e((uint16_t) (0xC8D0 + x), c8(0xEE28));
    d7e((uint16_t) (0xC90E + x), c8(0xEE2A));
  }
  for(int x = 0x0E; x >= 0; x -= 2) {
    d7e((uint16_t) (0xC996 + x), c8((uint16_t) (0xF0C8 + x)));
    d7e((uint16_t) (0xC9D6 + x), c8((uint16_t) (0xF0D8 + x)));
    d7e((uint16_t) (0xCA16 + x), c8((uint16_t) (0xF0E8 + x)));
    d7e((uint16_t) (0xCA56 + x), c8((uint16_t) (0xF0F8 + x)));
  }
  piece_from_end(0x019A, 0xEE44, 0xEE4C, 4, 0xCA9E);
  piece_from_end(0x01AA, 0xF080, 0xF08E, 7, 0xCB18);
  for(int x = 0x0C; x >= 0; x -= 2) {
    d7e((uint16_t) (0xCB98 + x), c8((uint16_t) (0xF090 + x)));
    d7e((uint16_t) (0xCBD8 + x), c8((uint16_t) (0xF09E + x)));
    d7e((uint16_t) (0xCC18 + x), c8((uint16_t) (0xF0AC + x)));
  }
  piece(0xF0BA, 7, 0xCC58, 0);
  piece(0xEE4E, 7, 0xC9FA, 0);
  for(int x = 0x0C; x >= 0; x -= 2) {
    d7e((uint16_t) (0xCBBA + x), c8((uint16_t) (0xEE98 + x)));
    d7e((uint16_t) (0xCBFA + x), c8((uint16_t) (0xEEA6 + x)));
  }
  uint16_t laps = (uint16_t) (ram16(0x019E) << 2);
  d7e(0xCBC6, c8((uint16_t) (0xEE70 + laps)));
  d7e(0xCC06, c8((uint16_t) (0xEE72 + laps)));
  dashboard_done(r, numbered_rows(0xCA3A, 0));
}

// $01:90BD: the two players' dashboards, one above the other ($7E:C950 and $CCD0).
void Tg2DashboardMap2P(TgRegs* r) {
  if(ram16(0x0514) & 0x8000) {  // stops after the frame; never seen
    tg_port_decline();
    return;
  }
  for(int x = 0x07FE; x >= 0; x -= 2) d7e((uint16_t) (0xC910 + x), c8(0xEE26));
  for(int x = 0x0300; x >= 0; x -= 0x40) {
    d7e((uint16_t) (0xC950 + x), c8(0xEE28));
    d7e((uint16_t) (0xC98E + x), c8(0xEE2A));
    d7e((uint16_t) (0xCCD0 + x), c8(0xEE28));
    d7e((uint16_t) (0xCD0E + x), c8(0xEE2A));
  }
  for(int x = 0x3E; x >= 0; x -= 2) {
    uint16_t v = c8(0xEE2C);
    d7e((uint16_t) (0xC910 + x), v);
    d7e((uint16_t) (0xCC90 + x), v);
    d7e((uint16_t) (0xD010 + x), v);
  }
  for(int x = 0x0E; x >= 0; x -= 2) {
    uint16_t v = c8((uint16_t) (0xF108 + x));
    d7e((uint16_t) (0xC950 + x), v);
    d7e((uint16_t) (0xCCD0 + x), v);
    v = c8((uint16_t) (0xF118 + x));
    d7e((uint16_t) (0xC990 + x), v);
    d7e((uint16_t) (0xCD10 + x), v);
    v = c8((uint16_t) (0xF128 + x));
    d7e((uint16_t) (0xC9D0 + x), v);
    d7e((uint16_t) (0xCD50 + x), v);
    v = c8((uint16_t) (0xF138 + x));
    d7e((uint16_t) (0xCA10 + x), v);
    d7e((uint16_t) (0xCD90 + x), v);
  }
  piece_from_end(0x019A, 0xEE44, 0xEE4C, 4, 0xCA58);
  piece_from_end(0x019C, 0xEE44, 0xEE4C, 4, 0xCDD8);
  piece_from_end(0x01AA, 0xF080, 0xF08E, 7, 0xCAD2);
  piece_from_end(0x01C4, 0xF080, 0xF08E, 7, 0xCE52);
  for(int x = 0x0C; x >= 0; x -= 2) {
    uint16_t v = c8((uint16_t) (0xF090 + x));
    d7e((uint16_t) (0xCB52 + x), v);
    d7e((uint16_t) (0xCED2 + x), v);
    v = c8((uint16_t) (0xF09E + x));
    d7e((uint16_t) (0xCB92 + x), v);
    d7e((uint16_t) (0xCF12 + x), v);
    v = c8((uint16_t) (0xF0AC + x));
    d7e((uint16_t) (0xCBD2 + x), v);
    d7e((uint16_t) (0xCF52 + x), v);
  }
  piece(0xF0BA, 7, 0xCC12, 0xCF92);
  piece(0xEE4E, 7, 0xC9C0, 0xCD40);
  for(int x = 0x0C; x >= 0; x -= 2) {
    uint16_t v = c8((uint16_t) (0xEE98 + x));
    d7e((uint16_t) (0xCB80 + x), v);
    d7e((uint16_t) (0xCF00 + x), v);
    v = c8((uint16_t) (0xEEA6 + x));
    d7e((uint16_t) (0xCBC0 + x), v);
    d7e((uint16_t) (0xCF40 + x), v);
  }
  uint16_t laps = (uint16_t) (ram16(0x019E) << 2);
  uint16_t v = c8((uint16_t) (0xEE70 + laps));
  d7e(0xCB8C, v);
  d7e(0xCF0C, v);
  v = c8((uint16_t) (0xEE72 + laps));
  d7e(0xCBCC, v);
  d7e(0xCF4C, v);
  dashboard_done(r, numbered_rows(0xCA00, 0xCD80));
}
