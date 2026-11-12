// Top Gear 2's decompressor ($01:DE85): a byte stream from ROM unpacked into work RAM
// through the WRAM port ($2180).
//
// In: A's low byte the source bank and bit 0 of its high byte the destination bank ($7E or
// $7F), X the source, Y the destination. Out: A the length unpacked, X the end of the
// stream, Y the end of the output. Uses direct page $94-$A3.
//
// Commands, by the first byte c (n = c & $F):
//   $00        end
//   $01-$3F    c literal bytes follow
//   $40-$4F    n + 3 bytes continuing the step between the last two bytes
//   $50-$5F    n + 2 words continuing the step between the last two words
//   $60-$6F    the last byte n + 2 more times
//   $70-$7F    the last word n + 2 more times
//   $80-$BF    3 bytes copied from (c & $3F) + 3 back
//   $C0-$DF    ((c >> 2) & 7) + 4 bytes copied from a 10-bit distance
//   $E0-$FF    (third byte) + 5 bytes copied from a 13-bit distance

#include "../port/port.h"

static uint16_t g_d;
static uint16_t z(uint8_t o) { return ram16((uint16_t) (g_d + o)); }
static void zs(uint8_t o, uint16_t v) { ram16_set((uint16_t) (g_d + o), v); }
static void zbs(uint8_t o, uint8_t v) { g_ram[(uint16_t) (g_d + o)] = v; }

static uint32_t g_src;  // the stream's start, 24 bits
static uint32_t g_dst;  // the output's start in work RAM (0-$1FFFF)

static uint8_t in(uint16_t y) { return tg_bus_read8((g_src + y) & 0xFFFFFF); }
static uint8_t out(uint16_t y) { return g_ram[(g_dst + y) & 0x1FFFF]; }
static void put(uint8_t v) { tg_bus_write8(0x2180, v); }

void Tg2Decompress(TgRegs* r) {
  g_d = r->d;
  uint8_t bank = (uint8_t) ((r->a >> 8 & 1) | 0x7E);
  zs(0x94, r->x);
  zbs(0x96, (uint8_t) r->a);
  zs(0x98, r->y);
  tg_bus_write16(0x2181, r->y);
  zbs(0x9A, bank);
  tg_bus_write8(0x2183, bank);
  g_src = (uint32_t) (uint8_t) r->a << 16 | r->x;
  g_dst = (uint32_t) (bank & 1) << 16 | r->y;
  uint16_t y = 0;
  zs(0x9E, 0);
  for(;;) {
    uint8_t c = in(y);
    if(c == 0) break;
    zbs(0xA2, c);
    y++;
    uint16_t pos = z(0x9E);
    if(c < 0x40) {
      zs(0x9E, (uint16_t) (pos + c));
      for(int i = 0; i < c; i++) put(in(y++));
      continue;
    }
    zs(0x9C, y);
    int n = (c & 0x0F) + 1;
    if(c < 0x50) {
      // Bytes stepping on.
      zs(0x9E, (uint16_t) (pos + n + 1 + 1));
      uint8_t last = out((uint16_t) (pos - 1));
      zbs(0xA2, last);
      uint8_t step = (uint8_t) (last - out((uint16_t) (pos - 2)));
      zbs(0xA3, step);
      uint8_t v = last;
      for(int i = 0; i < n + 2; i++) put(v = (uint8_t) (v + step));
    } else if(c < 0x60) {
      // Words stepping on.
      uint16_t last = (uint16_t) (out((uint16_t) (pos - 2)) | out((uint16_t) (pos - 1)) << 8);
      uint16_t prev = (uint16_t) (out((uint16_t) (pos - 4)) | out((uint16_t) (pos - 3)) << 8);
      uint16_t step = (uint16_t) (last - prev);
      zs(0xA2, step);
      zs(0x9E, (uint16_t) (pos + ((n + 1) << 1)));
      uint16_t v = last;
      for(int i = 0; i < n + 1; i++) {
        v = (uint16_t) (v + step);
        put((uint8_t) v);
        put((uint8_t) (v >> 8));
      }
    } else if(c < 0x70) {
      // The last byte again.
      zs(0x9E, (uint16_t) (pos + n + 1 + 1));
      uint8_t v = out((uint16_t) (pos - 1));
      zbs(0xA2, v);
      for(int i = 0; i < n + 2; i++) put(v);
    } else if(c < 0x80) {
      // The last word again.
      zs(0x9E, (uint16_t) (pos + ((n + 1) << 1)));
      uint8_t lo = out((uint16_t) (pos - 2)), hi = out((uint16_t) (pos - 1));
      for(int i = 0; i < n + 1; i++) {
        put(lo);
        put(hi);
      }
    } else {
      uint16_t from, len;
      if(c < 0xC0) {
        from = (uint16_t) (pos - (c & 0x3F) - 3);
        len = 3;
      } else if(c < 0xE0) {
        uint16_t v = (uint16_t) (c << 8 | in(y++));
        zs(0x9C, y);
        from = (uint16_t) (pos - (v & 0x03FF) - 3);
        len = (uint16_t) (((c & 0x1C) >> 2) + 4);
      } else {
        uint16_t v = (uint16_t) (c << 8 | in(y));
        uint8_t k = in((uint16_t) (y + 1));
        y = (uint16_t) (y + 2);
        zbs(0xA2, k);
        zs(0x9C, y);
        from = (uint16_t) (pos - (v & 0x1FFF) - 3);
        len = (uint16_t) (k + 5);
      }
      zs(0x9E, (uint16_t) (pos + len));
      for(uint16_t i = 0; i < len; i++) put(out((uint16_t) (from + i)));
    }
    y = z(0x9C);
  }
  y++;
  r->x = (uint16_t) (y + z(0x94));
  uint32_t sum = (uint32_t) z(0x9E) + z(0x98);
  r->y = (uint16_t) sum;
  r->c = sum > 0xFFFF;
  r->v = ((~(z(0x9E) ^ z(0x98)) & (z(0x9E) ^ (uint16_t) sum)) & 0x8000) != 0;
  r->a = z(0x9E);
  r->n = (r->a & 0x8000) != 0;
  r->z = r->a == 0;
}
