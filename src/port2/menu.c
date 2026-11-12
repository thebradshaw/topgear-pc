// Top Gear 2: the menus and the screens around them (bank $1F).

#include <stddef.h>
#include <string.h>

#include "../port/port.h"

static uint16_t dp(const TgRegs* r, uint8_t o) { return ram16((uint16_t) (r->d + o)); }
static void dp_set(const TgRegs* r, uint8_t o, uint16_t v) { ram16_set((uint16_t) (r->d + o), v); }
static void dp8_set(const TgRegs* r, uint8_t o, uint8_t v) { g_ram[(uint16_t) (r->d + o)] = v; }

// ---- $1F:BF14: the menus' unpacker ----
//
// Unpacks the stream at bank (A's low byte):X to bank (A's high byte):Y and returns its
// length in A. Each command byte c:
//   $00       the end
//   $01-$3F   c bytes as they are
//   $40-$4F   (c & $F) + 3 bytes, each the last one plus the last step between two bytes
//   $50-$5F   (c & $F) + 2 words, the same in words
//   $60-$6F   (c & $F) + 3 copies of the last byte
//   $70-$7F   (c & $F) + 2 copies of the last word
//   $80-$BF   3 bytes from (c & $3F) + 3 back
//   $C0-$DF   ((c >> 2) & 7) + 4 bytes from ((c & 3) << 8 | next) + 3 back
//   $E0-$FF   (third byte) + 5 bytes from ((c & $1F) << 8 | next) + 3 back
// The original copies in words (and some copies overlap what they write), so this does
// too, store for store.

static uint32_t g_out;  // the output bank, << 16

static uint16_t rd(uint32_t a) { return tg_read16(a & 0xFFFFFF); }
static void wr(uint32_t a, uint16_t v) { tg_bus_write16(a & 0xFFFFFF, v); }
static uint8_t rd8(uint32_t a) { return tg_bus_read8(a & 0xFFFFFF); }
static void wr8(uint32_t a, uint8_t v) { tg_bus_write8(a & 0xFFFFFF, v); }
static uint16_t o16(uint16_t y) { return rd(g_out + y); }
static uint8_t o8(uint16_t y) { return rd8(g_out + y); }
static void o16_set(uint16_t y, uint16_t v) { wr(g_out + y, v); }
static void o8_set(uint16_t y, uint8_t v) { wr8(g_out + y, v); }
// One word copied from earlier output (the original's `lda a:0,x / sta a:0,y` with the
// data bank on the output's).
static void back16(uint16_t x, uint16_t y) { o16_set(y, o16(x)); }

void Tg2Unpack(TgRegs* r) {
  uint16_t start = r->y;
  uint32_t in = (uint32_t) (uint8_t) r->a << 16;
  g_out = (uint32_t) (uint8_t) (r->a >> 8) << 16;
  dp8_set(r, 0x86, (uint8_t) (r->a >> 8));
  dp_set(r, 0x84, 0);
  uint16_t src = r->x;
  uint16_t y = r->y;
  for(;;) {
    dp_set(r, 0x87, src);
    uint16_t cmd = rd(in + src);
    uint8_t c = (uint8_t) cmd;
    if(c < 0x40) {
      uint16_t x = (uint16_t) (src + 1);
      if(c == 0) {
        src = x;
        break;
      }
      uint16_t n = c >> 1;
      dp_set(r, 0x89, n);
      if(c & 1) {
        if(n == 0) {
          o8_set(y, rd8(in + x));
          x++;
          y++;
          src = x;
          continue;
        }
        o16_set(y, rd(in + x));
        x++;
        y++;
      }
      do {
        o16_set(y, rd(in + x));
        x = (uint16_t) (x + 2);
        y = (uint16_t) (y + 2);
        dp_set(r, 0x89, (uint16_t) (dp(r, 0x89) - 1));
      } while(dp(r, 0x89) != 0);
      src = x;
      continue;
    }
    src++;
    dp_set(r, 0x87, src);
    switch(c >> 4) {
    case 0x4: {  // byte steps
      uint8_t step = (uint8_t) (o8((uint16_t) (y - 1)) - o8((uint16_t) (y - 2)));
      dp8_set(r, 0x8B, step);
      uint8_t a = o8((uint16_t) (y - 1));
      for(int i = 0; i < (c & 0x0F) + 3; i++) {
        a = (uint8_t) (a + step);
        o8_set(y++, a);
      }
      break;
    }
    case 0x5: {  // word steps
      uint16_t step = (uint16_t) (o16((uint16_t) (y - 2)) - o16((uint16_t) (y - 4)));
      dp_set(r, 0x8B, step);
      uint16_t a = o16((uint16_t) (y - 2));
      for(int i = 0; i < (c & 0x0F) + 2; i++) {
        a = (uint16_t) (a + step);
        o16_set(y, a);
        y = (uint16_t) (y + 2);
      }
      break;
    }
    case 0x6: {  // the last byte again
      uint8_t b = o8((uint16_t) (y - 1));
      for(int i = 0; i < (c & 0x0F) + 3; i++) o8_set(y++, b);
      break;
    }
    case 0x7: {  // the last word again
      uint16_t v = o16((uint16_t) (y - 2));
      for(int i = 0; i < (c & 0x0F) + 2; i++) {
        o16_set(y, v);
        y = (uint16_t) (y + 2);
      }
      break;
    }
    case 0x8: case 0x9: case 0xA: case 0xB: {  // 3 bytes from close behind
      dp_set(r, 0x8B, y);
      uint16_t from = (uint16_t) (y - ((c & 0x3F) + 2));
      uint16_t second = o16(from);
      uint16_t first = o16((uint16_t) (from - 1));
      o16_set(y, first);
      o16_set((uint16_t) (y + 1), second);
      y = (uint16_t) (y + 3);
      break;
    }
    case 0xC: case 0xD: {  // 4-11 bytes from up to $3FF back
      uint16_t dist = (uint16_t) ((((c & 3) << 8) | (cmd >> 8)) + 3);
      uint16_t x = (uint16_t) (y - dist);
      dp_set(r, 0x8B, x);
      src++;
      dp_set(r, 0x87, src);
      int n = ((c >> 2) & 7) + 4;
      if(n & 1) {
        for(; n > 5; n -= 2, x = (uint16_t) (x + 2), y = (uint16_t) (y + 2)) back16(x, y);
        back16(x, y);
        back16((uint16_t) (x + 2), (uint16_t) (y + 2));
        back16((uint16_t) (x + 3), (uint16_t) (y + 3));
        y = (uint16_t) (y + 5);
      } else {
        for(; n > 4; n -= 2, x = (uint16_t) (x + 2), y = (uint16_t) (y + 2)) back16(x, y);
        back16(x, y);
        back16((uint16_t) (x + 2), (uint16_t) (y + 2));
        y = (uint16_t) (y + 4);
      }
      break;
    }
    default: {  // $E0-$FF: 5-260 bytes from up to $1FFF back
      uint16_t dist = (uint16_t) ((((c & 0x1F) << 8) | (cmd >> 8)) + 3);
      uint16_t x = (uint16_t) (y - dist);
      dp_set(r, 0x8B, x);
      uint16_t n = (uint16_t) (rd8(in + (uint16_t) (src + 1)) + 1);
      src = (uint16_t) (src + 2);
      dp_set(r, 0x87, src);
      dp_set(r, 0x89, n >> 1);
      if((n & 1) && (n >> 1) == 0) {
        back16(x, y);
        back16((uint16_t) (x + 2), (uint16_t) (y + 2));
        o8_set((uint16_t) (y + 4), o8((uint16_t) (x + 4)));
        y = (uint16_t) (y + 5);
        break;
      }
      if(n & 1) {
        back16(x, y);
        x++;
        y++;
      }
      for(int i = 0; i < 2; i++, x = (uint16_t) (x + 2), y = (uint16_t) (y + 2)) back16(x, y);
      do {
        back16(x, y);
        x = (uint16_t) (x + 2);
        y = (uint16_t) (y + 2);
        dp_set(r, 0x89, (uint16_t) (dp(r, 0x89) - 1));
      } while(dp(r, 0x89) != 0);
      break;
    }
    }
  }
  // `sty $8B / pla / eor #$FFFF / sec / adc $8B`: the length, with that addition's flags.
  dp_set(r, 0x8B, y);
  uint16_t na = (uint16_t) ~start;
  uint32_t t = (uint32_t) na + y + 1;
  uint16_t len = (uint16_t) t;
  r->a = len;
  r->x = src;
  r->y = y;
  r->c = t > 0xFFFF;
  r->v = ((~(na ^ y)) & (na ^ len) & 0x8000) != 0;
  r->n = (len & 0x8000) != 0;
  r->z = len == 0;
}

// ---- $1F:DBDC / $1F:DD7A: strings as sprites ----
//
// Draws the string at bank (A's low byte):Y as sprites into the sprite table at $0500 from
// entry $AF, at X (low byte across, high byte down), attributes A's high byte. $DBDC uses
// the big letters (16 x 16 sprites), $DD7A the small ones; each letter moves on by its own
// width. Bytes $80-$86 are commands:
//   $80 nn nn  move to the start position plus nnnn
//   $81 nn nn  carry on reading at nnnn
//   $82        start a centred run    $83  centre it (on the run's start position)
//   $84 / $85  switch to the small / big letters (each is a no-op in its own font)
//   $86 nn     move right by nn
// The sprite table writes go through the string's data bank, as the original's do.

static const uint8_t kBigSpecial[][2] = {
  { '#', 0x24 }, { '!', 0x25 }, { '_', 0x26 }, { '$', 0x27 }, { '?', 0x28 }, { '(', 0x29 },
  { ')', 0x2A }, { '<', 0x2B }, { '>', 0x2C }, { '{', 0x2D }, { '}', 0x2E },
};

static uint8_t big_letter(uint8_t c) {
  for(size_t i = 0; i < sizeof kBigSpecial / sizeof kBigSpecial[0]; i++)
    if(kBigSpecial[i][0] == c) return kBigSpecial[i][1];
  if(c >= 0x41) c = (uint8_t) (c - 0x2B);
  return (uint8_t) (c - 0x16);
}

static uint8_t small_letter(uint8_t c) {
  if(c == '/') return 0xDE;
  if(c == '.') return 0xDF;
  if(c >= 0x41) {
    if(c >= 0x61) c = (uint8_t) (c + 4);
    c = (uint8_t) (c - 0x2B);
  }
  return (uint8_t) (c + 0x8A);
}

static void draw_string(TgRegs* r, bool big) {
  uint32_t db = (uint32_t) (uint8_t) r->a << 16;
  dp_set(r, 0x00, r->x);
  dp_set(r, 0x04, r->x);
  dp_set(r, 0x02, r->a & 0xFF00);
  dp_set(r, 0x06, 0);
  uint8_t b = (uint8_t) (r->a >> 8);  // A's high byte, as the 8-bit code leaves it
  uint16_t x = dp(r, 0xAF);
  uint16_t y = r->y;
  uint16_t d = r->d;
#define Z8(o) g_ram[(uint16_t) (d + (o))]
  for(;;) {
    uint8_t c = rd8(db | y);
    if(c == 0) break;
    if(c & 0x80) {
      b = 0;
      switch(c & 0x7F) {
      case 0: {
        uint16_t v = (uint16_t) (rd(db | (uint16_t) (y + 1)) + dp(r, 0x04));
        dp_set(r, 0x00, v);
        b = (uint8_t) (v >> 8);
        y = (uint16_t) (y + 3);
        break;
      }
      case 1: {
        y = rd(db | (uint16_t) (y + 1));
        b = (uint8_t) (y >> 8);
        break;
      }
      case 2:
        y++;
        dp_set(r, 0x0A, dp(r, 0x00) & 0x00FF);
        dp_set(r, 0x00, dp(r, 0x00) & 0xFF00);
        dp_set(r, 0x0C, x);
        b = Z8(0x01);
        break;
      case 3: {
        uint16_t from = dp(r, 0x0C);
        dp_set(r, 0x0C, x);
        uint16_t v = (uint16_t) (dp(r, 0x0A) - ((dp(r, 0x00) & 0x00FF) >> 1));
        b = (uint8_t) (v >> 8);
        Z8(0x0A) = (uint8_t) v;
        Z8(0x00) = (uint8_t) (Z8(0x0A) + Z8(0x00));
        uint16_t i = from;
        do {
          wr8(db | (uint16_t) (0x0500 + i), (uint8_t) (rd8(db | (uint16_t) (0x0500 + i)) + Z8(0x0A)));
          i = (uint16_t) (i + 4);
        } while(i != dp(r, 0x0C));
        y++;
        break;
      }
      case 4: y++; big = false; break;
      case 5: y++; big = true; break;
      case 6: {
        uint16_t v = (uint16_t) (rd8(db | (uint16_t) (y + 1)) + dp(r, 0x00));
        dp_set(r, 0x00, v);
        b = (uint8_t) (v >> 8);
        y = (uint16_t) (y + 2);
        break;
      }
      }
      continue;
    }
    if(c == 0x20) {
      Z8(0x00) = (uint8_t) (Z8(0x00) + 8);
      y++;
      continue;
    }
    uint8_t v = big ? big_letter(c) : small_letter(c);
    Z8(0x06) = v;
    uint8_t tile = v;
    if(big) {
      uint8_t t = (uint8_t) (v << 1);
      Z8(0x08) = (uint8_t) (t & 0x0F);  // the original's scratch
      tile = (uint8_t) ((t & 0x0F) | (uint8_t) ((t & 0xF0) << 1));
    }
    wr8(db | (uint16_t) (0x0502 + x), tile);
    wr8(db | (uint16_t) (0x0302 + x), big ? 1 : 0);
    wr8(db | (uint16_t) (0x0503 + x), Z8(0x03));
    wr8(db | (uint16_t) (0x0501 + x), Z8(0x01));
    wr8(db | (uint16_t) (0x0500 + x), Z8(0x00));
    x = (uint16_t) (x + 4);
    Z8(0x00) = (uint8_t) (Z8(0x00) + rd8(0x9F0000u | (uint16_t) ((big ? 0xDD4B : 0xDDF5) + v)));
    y++;
  }
#undef Z8
  dp_set(r, 0xAF, x);
  r->a = (uint16_t) (b << 8);
  r->x = x;
  r->y = y;
  r->n = (r->db & 0x80) != 0;  // `plb` sets them from the data bank it takes back
  r->z = r->db == 0;
}

void Tg2DrawBigString(TgRegs* r) { draw_string(r, true); }
void Tg2DrawSmallString(TgRegs* r) { draw_string(r, false); }

// ---- $1F:990E / $98CF: the sprite table's high part ----
//
// Packs each of the 128 sprites' two high bits (bit 7 of its byte 1: the X position's
// ninth bit; bit 0 of its byte 2: the size) four to a byte into the high part at x + $200,
// shifting those bytes as the original's asl/lsr do. The sprites' bytes start at d. Then,
// if fewer sprites were drawn this time ($AF) than last time ($B9), the left-over ones are
// moved off screen ($E0E0). Leaves A, X, Y and the flags as the original does.
static void sub_flags(TgRegs* r, uint16_t a, uint16_t b) {
  uint32_t t = (uint32_t) a + (uint16_t) ~b + 1;
  uint16_t v = (uint16_t) t;
  r->c = t > 0xFFFF;
  r->v = ((a ^ b) & (a ^ v) & 0x8000) != 0;
  r->n = (v & 0x8000) != 0;
  r->z = v == 0;
}

static void pack_high(TgRegs* r, uint16_t d, uint16_t x) {
  uint32_t db = (uint32_t) r->db << 16;
  for(int half = 0; half < 2; half++, d = (uint16_t) (d + 0x100)) {
    for(int j = 0; j < 16; j++) {
      uint8_t a = 0;
      for(int k = 0; k < 4; k++) {
        uint16_t p = (uint16_t) (d + 16 * j + 4 * k);
        uint8_t hi = g_ram[(uint16_t) (p + 1)];
        uint8_t sz = g_ram[(uint16_t) (p + 2)];
        g_ram[(uint16_t) (p + 1)] = (uint8_t) (hi << 1);
        g_ram[(uint16_t) (p + 2)] = (uint8_t) (sz >> 1);
        a = (uint8_t) ((a >> 1) | (hi & 0x80));
        a = (uint8_t) ((a >> 1) | ((sz & 1) << 7));
      }
      wr8(db | (uint16_t) (0x0200 + x + 16 * half + j), a);
    }
  }
  uint16_t af = dp(r, 0xAF), b9 = dp(r, 0xB9);
  sub_flags(r, af, b9);
  uint16_t diff = (uint16_t) (af - b9);
  r->a = diff;
  r->x = x;
  if(r->c) return;
  uint16_t n = (uint16_t) ((uint16_t) (~diff + 1));
  r->c = (n >> 1) & 1;  // the second lsr's carry
  n >>= 2;
  uint16_t i = af;
  do {
    wr(db | (uint16_t) (0x0500 + i), 0xE0E0);
    i = (uint16_t) (i + 4);
    n--;
  } while(n != 0);
  r->a = 0xE0E0;
  r->x = i;
  r->y = 0;
  r->n = false;
  r->z = true;
}

// $98C5: the high part for the table at $0500 (sprites' bytes at $0300), then $AF moves to
// $B9 and starts again at 0.
void Tg2PackSprites(TgRegs* r) {
  pack_high(r, 0x0300, 0x0500);
  uint16_t af = dp(r, 0xAF);
  r->a = af;
  dp_set(r, 0xB9, af);
  dp_set(r, 0xAF, 0);
  r->n = (af & 0x8000) != 0;
  r->z = af == 0;
}

// $98DA / $98F6: the same for the double-buffered tables ($B3 / $B5): pack the one at $B5,
// then swap them and start the next at $B3 - $500. $98F6 keeps A in $82.
static void pack_swap(TgRegs* r) {
  pack_high(r, (uint16_t) (dp(r, 0xB5) - 0x0200), dp(r, 0xB5));
  dp_set(r, 0xB9, dp(r, 0xB7));
  dp_set(r, 0xB7, dp(r, 0xAF));
  uint16_t b3 = dp(r, 0xB3);
  sub_flags(r, b3, 0x0500);
  dp_set(r, 0xAF, (uint16_t) (b3 - 0x0500));
}

void Tg2PackSpritesSwap(TgRegs* r) {
  pack_swap(r);
  uint16_t b5 = dp(r, 0xB5), b3 = dp(r, 0xB3);
  r->x = b5;
  r->a = b3;
  dp_set(r, 0xB5, b3);
  dp_set(r, 0xB3, b5);
  r->n = (b3 & 0x8000) != 0;
  r->z = b3 == 0;
}

void Tg2PackSpritesKeep(TgRegs* r) {
  uint16_t a = r->a;
  pack_swap(r);
  r->a = a;
  dp_set(r, 0x82, a);
  r->n = (a & 0x8000) != 0;
  r->z = a == 0;
}

// ---- $1F:97A9: a palette row queued for a fade ----
//
// Splits the 15 colours at bank (A's low byte):X into their red, green and blue parts
// (each scaled to bits 5-9) in the three fade lists at $1797 / $1957 / $1B17 from entry
// $99. Each row starts with a header: A's top bit, $0400 if it is set, and Y (the row's
// CGRAM place). Counts the row in $97 and asks for $20 steps ($95).
void Tg2QueueFadeRow(TgRegs* r) {
  uint32_t db = (uint32_t) (uint8_t) r->a << 16;
  uint16_t y = dp(r, 0x99);
  uint16_t top = r->a & 0x8000;
  ram16_set((uint16_t) (0x1797 + y), top);
  ram16_set((uint16_t) (0x1B17 + y), top ? 0x0400 : 0x0000);
  ram16_set((uint16_t) (0x1957 + y), r->y);
  y = (uint16_t) (y + 2);
  uint16_t x = r->x;
  dp_set(r, 0x02, 0x000F);
  do {
    uint16_t c = rd(db | x);
    dp_set(r, 0x00, c);
    ram16_set((uint16_t) (0x1797 + y), (uint16_t) ((c & 0x001F) << 5));
    ram16_set((uint16_t) (0x1957 + y), c & 0x03E0);
    ram16_set((uint16_t) (0x1B17 + y), (uint16_t) ((c & 0x7C00) >> 5));
    x = (uint16_t) (x + 2);
    y = (uint16_t) (y + 2);
    dp_set(r, 0x02, (uint16_t) (dp(r, 0x02) - 1));
  } while(dp(r, 0x02) != 0);
  dp_set(r, 0x99, y);
  dp_set(r, 0x97, (uint16_t) (dp(r, 0x97) + 1));
  dp_set(r, 0x95, 0x0020);
  r->a = 0x0020;
  r->x = x;
  r->y = y;
  r->c = false;
  r->n = (r->db & 0x80) != 0;
  r->z = r->db == 0;
}

// ---- $1F:9F13: a block of tile map words ----
//
// Copies $06 rows of $04 words from bank $0D:$00 to $7E:$02 (or $7F when the destination
// skip $0A isn't 0), adding $0E to each; after each row the source skips $08 bytes and the
// destination $0A.
void Tg2CopyMapBlock(TgRegs* r) {
  uint32_t db = (uint32_t) g_ram[(uint16_t) (r->d + 0x0D)] << 16;
  uint32_t out = dp(r, 0x0A) != 0 ? 0x7F0000 : 0x7E0000;
  uint16_t x = dp(r, 0x02), y = dp(r, 0x00);
  bool c = false, v = false;
  do {
    dp_set(r, 0x10, dp(r, 0x04));
    do {
      wr(out | x, (uint16_t) (rd(db | y) + dp(r, 0x0E)));
      x = (uint16_t) (x + 2);
      y = (uint16_t) (y + 2);
      dp_set(r, 0x10, (uint16_t) (dp(r, 0x10) - 1));
    } while(dp(r, 0x10) != 0);
    y = (uint16_t) (y + dp(r, 0x08));
    uint16_t k = dp(r, 0x0A);
    uint32_t t = (uint32_t) x + k;
    v = (~(x ^ k) & (x ^ (uint16_t) t) & 0x8000) != 0;
    x = (uint16_t) t;
    c = t > 0xFFFF;
    dp_set(r, 0x06, (uint16_t) (dp(r, 0x06) - 1));
  } while(dp(r, 0x06) != 0);
  r->a = x;
  r->x = x;
  r->y = y;
  r->c = c;
  r->v = v;
  r->n = (r->db & 0x80) != 0;
  r->z = r->db == 0;
}

// ---- $1F:9F74: a screen's PPU set-up ----
//
// Writes the $44 bytes at bank (A's low byte):X to the PPU and CPU registers below, in
// this order (the scroll and mode 7 ones twice, as their write-twice latches want).
static const uint16_t kScreenRegs[0x44] = {
  0x2100, 0x2101, 0x2102, 0x2103, 0x2105, 0x2106, 0x2107, 0x2108, 0x2109, 0x210A, 0x210B,
  0x210C, 0x210D, 0x210D, 0x210E, 0x210E, 0x210F, 0x210F, 0x2110, 0x2110, 0x2111, 0x2111,
  0x2112, 0x2112, 0x2113, 0x2113, 0x2114, 0x2114, 0x2115, 0x2116, 0x2117, 0x211A, 0x211B,
  0x211B, 0x211C, 0x211C, 0x211D, 0x211D, 0x211E, 0x211E, 0x211F, 0x211F, 0x2120, 0x2120,
  0x2121, 0x2123, 0x2124, 0x2125, 0x2126, 0x2127, 0x2128, 0x2129, 0x212A, 0x212B, 0x212C,
  0x212D, 0x212E, 0x212F, 0x2130, 0x2131, 0x2132, 0x2133, 0x4200, 0x4207, 0x4208, 0x4209,
  0x420A, 0x420D,
};

void Tg2SetScreenRegs(TgRegs* r) {
  uint32_t db = (uint32_t) (uint8_t) r->a << 16;
  uint8_t v = 0;
  for(int i = 0; i < 0x44; i++) {
    v = rd8(db | (uint16_t) (r->x + i));
    wr8(db | kScreenRegs[i], v);
  }
  r->a = (uint16_t) ((r->a & 0xFF00) | v);
  r->n = (r->db & 0x80) != 0;
  r->z = r->db == 0;
}

// ---- $1F:9D7B: the PPU's power-on set-up ----
// Every register $9F74 sets, plus $213E/$213F, to fixed values (screen off, mode 1).
static const uint8_t kPpuDefaults[0x46] = {
  0x8F, 0x62, 0x00, 0x00, 0x41, 0x00, 0x0C, 0x13, 0x13, 0x00, 0x50, 0x05, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x15, 0x00, 0x00, 0x00, 0x00, 0x00,
  0xE0, 0x00, 0x00, 0x00, 0x00, 0x84, 0x00, 0x01, 0x00, 0x01,
};

void Tg2ResetPpu(TgRegs* r) {
  uint32_t db = (uint32_t) r->db << 16;
  int k = 0;
  for(int i = 0; i < 0x44; i++) {
    wr8(db | kScreenRegs[i], kPpuDefaults[k++]);
    if(kScreenRegs[i] == 0x2133) {
      wr8(db | 0x213E, kPpuDefaults[k++]);
      wr8(db | 0x213F, kPpuDefaults[k++]);
    }
  }
  r->a = (uint16_t) ((r->a & 0xFF00) | 0x01);
  r->n = false;
  r->z = false;
}

// ---- $1F:9CA4: the front end's variables ----
// Clears $0000-$1CD6 and fills $2000-$FDFF with $FFFF, sets the fades' defaults, points
// the NMI and IRQ vectors in low RAM at $9F:9194 / $9F:92B7, and notes a PAL machine
// ($0027 = $FFFF).
void Tg2FillWords(TgRegs* r);

void Tg2FrontEndInit(TgRegs* r) {
  r->a = 0x0000;
  r->x = 0x0000;
  r->y = 0x1CD7;
  Tg2FillWords(r);
  r->a = 0xFFFF;
  r->x = 0x2000;
  r->y = 0xDE00;
  Tg2FillWords(r);
  ram16_set(0x0130, 0x8F00);
  ram16_set(0x012C, 0);
  ram16_set(0x012E, 0x8F00);
  g_ram[0x0000] = 0x5C;
  g_ram[0x0004] = 0x5C;
  g_ram[0x0003] = 0x9F;
  g_ram[0x0007] = 0x9F;
  ram16_set(0x0001, 0x9194);
  ram16_set(0x0005, 0x92B7);
  ram16_set(0x0010, 0x9274);
  ram16_set(0x012C, 0);
  ram16_set(0x012E, 0);
  ram16_set(0x0130, 0x8000);
  uint16_t a = tg_read16(0x00213F) & 0x0010;
  if(a) a = 0xFFFF;
  ram16_set(0x0027, a);
  ram16_set(0x01BF, 0);
  r->a = a;
  r->n = (a & 0x8000) != 0;
  r->z = a == 0;
}

// ---- $1F:B744: packed pixels to 4-bit tiles ----
//
// Turns Y bytes of two-pixels-a-byte art at bank (A's low byte):X into 4-bitplane tiles
// and sends them to VRAM from word address $00, one 32-byte tile at a time through a
// buffer at $0100 (the direct page's $00-$1F).
void Tg2PixelsToTiles(TgRegs* r) {
  uint32_t db = (uint32_t) (uint8_t) r->a << 16;
  uint16_t d = r->d;
  tg_bus_write16(0x002116, dp(r, 0x00));
  dp_set(r, 0x22, r->y);
  uint16_t y = r->x;
  do {
    for(uint16_t x = 0; x < 0x10; x = (uint16_t) (x + 2)) {
      uint8_t* p0 = &g_ram[(uint16_t) (d + x)];
      uint8_t* p1 = &g_ram[(uint16_t) (d + x + 1)];
      uint8_t* p2 = &g_ram[(uint16_t) (d + x + 0x10)];
      uint8_t* p3 = &g_ram[(uint16_t) (d + x + 0x11)];
      for(int i = 0; i < 4; i++) {
        uint8_t a = rd8(db | (uint16_t) (y + i));
        for(int px = 0; px < 2; px++, a = (uint8_t) (a << 4)) {
          *p3 = (uint8_t) (*p3 << 1 | (a >> 7 & 1));
          *p2 = (uint8_t) (*p2 << 1 | (a >> 6 & 1));
          *p1 = (uint8_t) (*p1 << 1 | (a >> 5 & 1));
          *p0 = (uint8_t) (*p0 << 1 | (a >> 4 & 1));
        }
      }
      y = (uint16_t) (y + 4);
    }
    tg_bus_write16(0x004302, 0x0100);
    tg_bus_write16(0x004300, 0x1801);
    tg_bus_write16(0x004305, 0x0020);
    tg_bus_write8(0x004304, 0x00);
    tg_bus_write8(0x00420B, 0x01);
    dp_set(r, 0x22, (uint16_t) (dp(r, 0x22) - 0x20));
  } while(dp(r, 0x22) != 0);
  r->a = 0;
  r->x = 0x0010;
  r->y = y;
  r->c = true;
  r->v = false;
  r->n = (r->db & 0x80) != 0;
  r->z = r->db == 0;
}

// ---- $1F:D819: a 112-bit number times A ----
// The seven words at $06-$12 (low first) times A, plus X, back into $06-$12 (the password's
// arithmetic). $14-$20 hold the sum as it builds.
void Tg2MultiplyLong(TgRegs* r) {
  dp_set(r, 0x04, r->a);
  dp_set(r, 0x14, r->x);
  for(uint8_t o = 0x16; o <= 0x20; o = (uint8_t) (o + 2)) dp_set(r, o, 0);
  bool c = r->c, v = r->v;
  do {
    uint16_t m = dp(r, 0x04);
    dp_set(r, 0x04, m >> 1);
    if(m & 1) {
      c = false;
      for(int i = 0; i < 7; i++) {
        uint16_t a = dp(r, (uint8_t) (0x06 + 2 * i)), b = dp(r, (uint8_t) (0x14 + 2 * i));
        uint32_t t = (uint32_t) a + b + c;
        v = (~(a ^ b) & (a ^ (uint16_t) t) & 0x8000) != 0;
        c = t > 0xFFFF;
        dp_set(r, (uint8_t) (0x14 + 2 * i), (uint16_t) t);
      }
    }
    c = false;
    for(int i = 0; i < 7; i++) {
      uint16_t a = dp(r, (uint8_t) (0x06 + 2 * i));
      dp_set(r, (uint8_t) (0x06 + 2 * i), (uint16_t) (a << 1 | c));
      c = (a & 0x8000) != 0;
    }
  } while(dp(r, 0x04) != 0);
  for(int i = 0; i < 7; i++) dp_set(r, (uint8_t) (0x06 + 2 * i), dp(r, (uint8_t) (0x14 + 2 * i)));
  uint16_t a = dp(r, 0x20);
  r->a = a;
  r->c = c;
  r->v = v;
  r->n = (a & 0x8000) != 0;
  r->z = a == 0;
}

// ---- $1F:D74F: the password's check number ----
// The sum of the progress words the password holds, modulo $6D (by the CPU's divider).
static const uint16_t kCheckWords[] = {
  0x1D03, 0x1D2C, 0x1D09, 0x1D32, 0x1D0B, 0x1D34, 0x1D0D, 0x1D36, 0x1D0F, 0x1D38, 0x1D11,
  0x1D3A, 0x1D13, 0x1D3C, 0x1D15, 0x1D3E, 0x1CFF, 0x1D28, 0x1D01, 0x1D2A, 0x1CDD, 0x1CE1,
  0x1D19, 0x1D42, 0x1D05, 0x1D2E, 0x1D07, 0x1D30, 0x1CDB,
};

void Tg2PasswordCheck(TgRegs* r) {
  uint32_t db = (uint32_t) r->db << 16;
  uint16_t sum = 0;
  bool c = false, v = false;
  for(size_t i = 0; i < sizeof kCheckWords / sizeof kCheckWords[0]; i++) {
    uint16_t b = rd(db | kCheckWords[i]);
    uint32_t t = (uint32_t) sum + b;
    v = (~(sum ^ b) & (sum ^ (uint16_t) t) & 0x8000) != 0;
    c = t > 0xFFFF;
    sum = (uint16_t) t;
  }
  wr(db | 0x4204, sum);
  wr8(db | 0x4206, 0x6D);
  uint16_t a = rd(db | 0x4216);
  r->a = a;
  r->c = c;
  r->v = v;
  r->n = (a & 0x8000) != 0;
  r->z = a == 0;
}

// ---- $1F:D33F: the password for the progress so far ----
//
// Builds one big number from the progress words (each times its range, as digits of a
// mixed-radix number, then the check number), then reads it out five bits at a time as 22
// letters at $1780, each scrambled by a fixed table and the letter before it.
static const struct { uint16_t adr; uint16_t range; } kPasswordDigits[] = {
  { 0x1D03, 4 }, { 0x1D05, 4 }, { 0x1D07, 4 }, { 0x1D09, 4 }, { 0x1D0B, 4 }, { 0x1D0D, 4 },
  { 0x1D0F, 5 }, { 0x1D11, 5 }, { 0x1D13, 5 }, { 0x1D15, 8 }, { 0x1CFF, 2 }, { 0x1D01, 10 },
  { 0x1D19, 0x0500 }, { 0x1D2C, 4 }, { 0x1D2E, 4 }, { 0x1D30, 4 }, { 0x1D32, 4 },
  { 0x1D34, 4 }, { 0x1D36, 4 }, { 0x1D38, 5 }, { 0x1D3A, 5 }, { 0x1D3C, 5 }, { 0x1D3E, 8 },
  { 0x1D28, 2 }, { 0x1D2A, 10 }, { 0x1D42, 0x0500 }, { 0x1CDD, 3 }, { 0x1CE1, 0x41 },
  { 0x1CDB, 2 },
};

void Tg2MakePassword(TgRegs* r) {
  uint32_t db = (uint32_t) r->db << 16;
  for(uint8_t o = 0x06; o <= 0x12; o = (uint8_t) (o + 2)) dp_set(r, o, 0);
  for(size_t i = 0; i < sizeof kPasswordDigits / sizeof kPasswordDigits[0]; i++) {
    uint16_t adr = kPasswordDigits[i].adr;
    uint16_t v = rd(db | adr);
    if(adr == 0x1D01 || adr == 0x1D2A) {
      if(v & 0x8000) v = (uint16_t) ((v & 0x7FFF) + 5);  // a negative value counts from 5
    } else if(adr == 0x1CE1) {
      v = (uint16_t) (v << 2);
    }
    r->x = v;
    r->a = kPasswordDigits[i].range;
    Tg2MultiplyLong(r);
  }
  Tg2PasswordCheck(r);
  r->x = r->a;
  r->a = 0x006D;
  Tg2MultiplyLong(r);
  dp_set(r, 0x00, 0x0016);
  dp_set(r, 0x02, 0);
  uint16_t y = 0;
  bool c = false, v = false;
  uint8_t ch = 0;
  do {
    uint16_t a = dp(r, 0x06) ^ rd(0x9F0000u | (uint16_t) (0xD7F9 + y));
    uint16_t b = dp(r, 0x02);
    uint32_t t = (uint32_t) a + b;
    v = (~(a ^ b) & (a ^ (uint16_t) t) & 0x8000) != 0;
    uint16_t k = (uint16_t) t & 0x001F;
    dp_set(r, 0x02, k);
    ch = rd8(0x9F0000u | (uint16_t) (0xD7D9 + k));
    wr8(db | (uint16_t) (0x1780 + y), ch);
    for(int s = 0; s < 5; s++) {
      c = false;
      for(int i = 6; i >= 0; i--) {
        uint8_t o = (uint8_t) (0x06 + 2 * i);
        uint16_t w = dp(r, o);
        dp_set(r, o, (uint16_t) (w >> 1 | (c ? 0x8000 : 0)));
        c = w & 1;
      }
    }
    y++;
    dp_set(r, 0x00, (uint16_t) (dp(r, 0x00) - 1));
  } while(dp(r, 0x00) != 0);
  r->a = ch;
  r->x = 0;
  r->y = y;
  r->c = c;
  r->v = v;
  r->n = false;
  r->z = true;
}

// ---- $1F:9131: unpack to $7F:0000, then send it to VRAM word address Y ----
void Tg2UnpackToVram(TgRegs* r) {
  uint16_t vram = r->y;
  r->y = 0;
  r->a = (uint16_t) ((r->a & 0x00FF) | 0x7F00);
  Tg2Unpack(r);
  r->y = vram;
  uint16_t len = r->a;
  r->c = true;
  r->v = false;
  r->n = (len & 0x8000) != 0;
  r->z = len == 0;
  if(len == 0) return;
  uint32_t db = (uint32_t) r->db << 16;
  wr(db | 0x4305, len);
  wr(db | 0x2116, vram);
  wr(db | 0x4300, 0x1801);
  wr(db | 0x4302, 0x0000);
  wr8(db | 0x4304, 0x7F);
  wr8(db | 0x420B, 0x01);
  r->a = 0x0001;
  r->n = false;
  r->z = false;
}

// ---- $1F:8503: the menus' starting state ----
//
// Copies each player's menu defaults (39 bytes from $9F:A16D and $9F:A192) to $012C and
// $022C, clears both sprite tables and both palette buffers, and sets each player's
// screen addresses, scroll and window positions. The 2-player screen ($1CDB = 0) starts the
// first player's view lower down.
void Tg2MenuStart(TgRegs* r) {
  ram16_set(0x0193, 0);
  ram16_set(0x0293, 0x4000);
  for(int i = 0; i < 0x27; i++) {
    g_ram[0x012C + i] = rd8(0x9FA16D + i);
    g_ram[0x022C + i] = rd8(0x9FA192 + i);
  }
  for(uint16_t x = 0; x < 0x200; x += 4) {
    ram16_set(0x0500 + x, 0xE0E0);
    ram16_set(0x0920 + x, 0xE0E0);
    ram16_set(0x0D40 + x, 0xE0E0);
    ram16_set(0x1160 + x, 0xE0E0);
    ram16_set(0x0D42 + x, 0x3002);
    ram16_set(0x1162 + x, 0x3002);
  }
  r->a = 0; r->x = 0x1380; r->y = 0x0200;
  Tg2FillWords(r);
  r->a = 0; r->x = 0x1580; r->y = 0x0200;
  Tg2FillWords(r);
  static const struct { uint16_t adr, val; } kSet[] = {
    {0x01B7, 0}, {0x01B9, 0x0420}, {0x01AF, 0x0420}, {0x01B3, 0x0500}, {0x01B5, 0x0920},
    {0x02B7, 0x0840}, {0x02B9, 0x0C60}, {0x02AF, 0x0C60}, {0x02B3, 0x0D40}, {0x02B5, 0x1160},
    {0x018D, 0}, {0x028D, 0}, {0x01B1, 0}, {0x02B1, 0x7400}, {0x0160, 0}, {0x0260, 0x8000},
    {0x0162, 0}, {0x0164, 0x0400}, {0x0166, 0x0800}, {0x0168, 0x0C00}, {0x016A, 0x1000},
    {0x016E, 0}, {0x0262, 0x4000}, {0x0264, 0x4400}, {0x0266, 0x4800}, {0x0268, 0x4C00},
    {0x026A, 0x5000}, {0x026E, 0x4000}, {0x016C, 0}, {0x026C, 0x0008}, {0x0170, 0x1380},
    {0x0270, 0x1580}, {0x0199, 0}, {0x019B, 0}, {0x0299, 0x00A0}, {0x029B, 0x00A0},
    {0x0197, 0}, {0x0195, 0}, {0x0297, 0}, {0x0295, 0}, {0x01A1, 0}, {0x02A1, 0},
    {0x01A3, 0}, {0x02A3, 0x0029}, {0x01E2, 0},
  };
  for(size_t i = 0; i < sizeof(kSet) / sizeof(kSet[0]); i++) ram16_set(kSet[i].adr, kSet[i].val);
  uint16_t a = ram16(0x1CDB);
  if(a == 0) {
    ram16_set(0x01B1, 0x1800);
    a = 0x0018;
    ram16_set(0x01E2, a);
  }
  r->a = a;
  r->n = (a & 0x8000) != 0;
  r->z = false;
}

// ---- $1F:8190 / $1F:81D4 / $1F:835D: the battery save ----
//
// The game keeps its records at $1CD7-$1DF2 and a copy at $7E:FE00, which survives a reset
// (Top Gear 2 has no battery, so the copy lasts until the power goes off). The copy's first
// word is the byte sum of $7E:FE04-$7E:FE99 and the second its complement.

// $8190: copies the records to $7E:FE00 and signs them.
void Tg2SaveRecords(TgRegs* r) {
  uint16_t sum = 0;
  bool c = false, v = false;
  ram16_set(0x0100, 0);
  for(uint16_t x = 4; x < 0x9A; x++) {
    uint8_t b = g_ram[0x1CD7 + x];
    g_ram[0xFE00 + x] = b;
    unsigned lo = (unsigned) b + g_ram[0x0100];
    g_ram[0x0100] = (uint8_t) lo;
    uint8_t hi = g_ram[0x0101];
    unsigned h = (unsigned) hi + (lo >> 8);
    v = !(hi & 0x80) && (h & 0x80);
    c = h > 0xFF;
    g_ram[0x0101] = (uint8_t) h;
  }
  (void) c;
  for(uint16_t x = 0x9A; x < 0x11C; x++) g_ram[0xFE00 + x] = g_ram[0x1CD7 + x];
  sum = ram16(0x0100);
  ram16_set(0xFE00, sum);
  r->a = (uint16_t) ~sum;
  ram16_set(0xFE02, r->a);
  r->x = 0x011C;
  r->n = (r->a & 0x8000) != 0;
  r->z = r->a == 0;
  r->c = true;
  r->v = v;
}

// $81D4: copies the records back from $7E:FE00, counts one more use of the copy, and adds
// the totals at $1D71/$1D73 to $1D19/$1D42.
void Tg2LoadRecords(TgRegs* r) {
  ram16_set(0x0100, 0);
  for(uint16_t x = 4; x < 0x11C; x++) g_ram[0x1CD7 + x] = g_ram[0xFE00 + x];
  ram16_set(0xFF1C, (uint16_t) (ram16(0xFF1C) + 1));
  ram16_set(0x1D19, (uint16_t) (ram16(0x1D71) + ram16(0x1D19)));
  uint16_t a = ram16(0x1D73), b = ram16(0x1D42);
  uint32_t s = (uint32_t) a + b;
  uint16_t res = (uint16_t) s;
  ram16_set(0x1D42, res);
  r->a = res;
  r->x = 0x011C;
  r->n = (res & 0x8000) != 0;
  r->z = res == 0;
  r->c = s > 0xFFFF;
  r->v = (~(a ^ b) & (a ^ res) & 0x8000) != 0;
}

void Tg2KeepStandings(TgRegs* r);

// $835D: at power on. A good copy at $7E:FE00 (the sums match) is loaded and counted; else
// records already set up ($1CD7 = $7E65, $1CD9 = $5543) are kept; else the defaults from
// $9F:83E7 go in (and carry is set). Either of the first two then keeps the best-lap
// number $1CE1 no higher than $1CE3.
void Tg2LoadSave(TgRegs* r) {
  uint16_t sum = 0;
  for(uint16_t x = 4; x < 0x9A; x++) {
    uint16_t b = g_ram[0xFE00 + x], s2 = (uint16_t) (sum + b);
    r->v = (~(b ^ sum) & (b ^ s2) & 0x8000) != 0;
    sum = s2;
  }
  ram16_set(0x0100, sum);
  r->x = 0x009A;
  if(sum == ram16(0xFE00) && (uint16_t) ~sum == ram16(0xFE02)) {
    memcpy(&g_ram[0x1CD7], &g_ram[0xFE00], 0x11C);
    r->x = 0xFF1C;
    r->y = 0x1DF3;
    ram16_set(0x1CD7, 0x7E65);
    ram16_set(0x1CD9, 0x5543);
    ram16_set(0xFE00, (uint16_t) (ram16(0xFE00) + 1));
  } else if(!(ram16(0x1CD7) == 0x7E65 && ram16(0x1CD9) == 0x5543)) {
    for(int i = 0; i < 0x11C; i++) g_ram[0x1CD7 + i] = rd8(0x9F83E7 + i);
    Tg2KeepStandings(r);
    r->y = 0x1DF3;
    ram16_set(0x1CD7, 0x7E65);
    ram16_set(0x1CD9, 0x5543);
    r->a = 0x5543;
    r->n = false;
    r->z = false;
    r->c = true;
    return;
  }
  uint16_t a = ram16(0x1CE3), m = ram16(0x1CE1);
  uint16_t d = (uint16_t) (a - m);
  r->n = (d & 0x8000) != 0;
  r->z = d == 0;
  if(a < m) ram16_set(0x1CE1, a);
  r->a = a;
  r->c = false;
}

// $8320: starts the menus' music (A: the tune, kept at $01C1) and resets the sound state
// at $01CF-$01DA.
void Tg2MenuMusic(TgRegs* r) {
  ram16_set(0x01C1, r->a);
  tg_call_jsr(r, 0xF0A3, 0x8326, TG_MX_16);
  g_ram[0x01D0] = 0xFF;
  g_ram[0x01D1] = 0xFF;
  g_ram[0x01D2] = 0x01;
  g_ram[0x01D3] = 0;
  g_ram[0x01D5] = 0;
  g_ram[0x01D7] = 0;
  g_ram[0x01D8] = 0;
  g_ram[0x01D9] = 0;
  g_ram[0x01DA] = 0;
  g_ram[0x01CF] = 0;
  r->a = r->a & 0xFF00;
  r->x &= 0xFF;
  r->y &= 0xFF;
  r->n = false;
  r->z = true;
}

// ---- $1F:DA09 / $1F:DA3C / $1F:DA69: the two drivers' standings ----
//
// $1D03 (9 words) and $1D2C (9 words) hold each player's points per country, $1D19 and
// $1D42 their totals; $1D45-$1D6F keeps a copy to go back to.

// $DA09: a new season: clears $1DC5-$1DEC, the points from $9F:8413 / $9F:843C, and the
// totals, race and country.
void Tg2NewSeason(TgRegs* r) {
  for(uint16_t x = 0x28; x != 0; x -= 2) ram16_set(0x1DC3 + x, 0);
  uint16_t a = 0;
  for(uint16_t x = 0; x < 0x12; x += 2) {
    ram16_set(0x1D03 + x, rd(0x9F8413 + x));
    a = rd(0x9F843C + x);
    ram16_set(0x1D2C + x, a);
  }
  ram16_set(0x1D19, 0);
  ram16_set(0x1D42, 0);
  ram16_set(0x1CE5, 0);
  ram16_set(0x1CE1, 0);
  r->a = a;
  r->x = 0x12;
  r->n = false;
  r->z = true;
  r->c = true;
}

// $DA3C: keeps a copy of the standings.
void Tg2KeepStandings(TgRegs* r) {
  for(uint16_t x = 0; x < 0x12; x += 2) ram16_set(0x1D45 + x, ram16(0x1D03 + x));
  ram16_set(0x1D59, ram16(0x1D19));
  for(uint16_t x = 0; x < 0x12; x += 2) ram16_set(0x1D5B + x, ram16(0x1D2C + x));
  r->a = ram16(0x1D42);
  ram16_set(0x1D6F, r->a);
  r->x = 0x12;
  r->n = (r->a & 0x8000) != 0;
  r->z = r->a == 0;
  r->c = true;
}

// $DA69: goes back to the copy.
void Tg2RestoreStandings(TgRegs* r) {
  for(uint16_t x = 0; x < 0x12; x += 2) ram16_set(0x1D03 + x, ram16(0x1D45 + x));
  ram16_set(0x1D19, ram16(0x1D59));
  for(uint16_t x = 0; x < 0x12; x += 2) ram16_set(0x1D2C + x, ram16(0x1D5B + x));
  r->a = ram16(0x1D6F);
  ram16_set(0x1D42, r->a);
  r->x = 0x12;
  r->n = (r->a & 0x8000) != 0;
  r->z = r->a == 0;
  r->c = true;
}

// ---- $1F:8B4A: the name entry's sprites ----
//
// Draws the letter grid's pieces for screen $A5 (sprite list $9E:8225, offset $9C:96E6 plus
// $B1) into the sprite table from $AF, the "END" key, the name typed so far ($1CF3 + $A3)
// and, on the frames where it shows, the cursor; then packs the table.
static uint32_t dbx(const TgRegs* r, uint16_t a) { return (uint32_t) r->db << 16 | a; }

void Tg2NameEntrySprites(TgRegs* r) {
  uint16_t i = (uint16_t) (dp(r, 0xA5) << 1);
  uint16_t base = (uint16_t) (rd(0x9C96E6 + i) + dp(r, 0xB1));
  dp_set(r, 0x00, base);
  uint16_t x = rd(0x9E8225 + i);
  uint16_t y = dp(r, 0xAF);
  for(uint16_t a; (a = rd(0x9E0000 + x)) != 0; x += 4, y += 4) {
    wr(dbx(r, 0x0502 + y), a);
    wr8(dbx(r, 0x0500 + y), (uint8_t) (rd8(0x9E0002 + x) + (uint8_t) base));
    wr8(dbx(r, 0x0501 + y), (uint8_t) (rd8(0x9E0003 + x) + (uint8_t) (base >> 8)));
    wr(dbx(r, 0x0301 + y), 0);
  }
  dp_set(r, 0xAF, y);
  r->y = 0x8127;
  r->x = (uint16_t) (dp(r, 0xB1) + 0x0883);
  r->a = 0x309E;
  Tg2DrawSmallString(r);
  y = dp(r, 0xAF);
  for(uint16_t k = 0; k < 4; k++) wr(dbx(r, 0x0502 + y + k * 4), (uint16_t) (0x31E6 + k));
  static const uint16_t kStep[4] = {0x48BF, 0x0010, 0x0010, 0x0008};
  uint32_t a = dp(r, 0xB1), c = 0;
  for(uint16_t k = 0; k < 4; k++) {
    a = (a & 0xFFFF) + kStep[k] + c;
    c = a >> 16;
    wr(dbx(r, 0x0500 + y + k * 4), (uint16_t) a);
  }
  for(uint16_t k = 0; k < 4; k++) wr(dbx(r, 0x0301 + y + k * 4), 0);
  dp_set(r, 0xAF, (uint16_t) (y + 0x10));
  r->y = (uint16_t) (dp(r, 0xA3) + 0x1CF3);
  r->x = (uint16_t) (dp(r, 0xB1) + 0x5890);
  r->a = 0x3000;
  Tg2DrawSmallString(r);
  if(!((rd(dbx(r, 0x0014)) - dp(r, 0xAB)) & 0x0008)) {
    y = dp(r, 0xAF);
    wr(dbx(r, 0x0500 + y), (uint16_t) (dp(r, 0x00) + 0x0100));
    wr(dbx(r, 0x0502 + y), 0xB1E5);
    wr(dbx(r, 0x0302 + y), 0);
    y += 4;
    dp_set(r, 0xAF, y);
    r->y = y;
  }
  Tg2PackSpritesSwap(r);
}

// ---- The menu screens' button handlers ----
//
// The menu script runner ($1F:8650) calls these for each button pressed; carry set on
// return leaves the screen (A and X then say where to). They play the menu click
// ($1F:A117) when something changes.

void Tg2SetFadeIn(TgRegs* r);

static void nz16(TgRegs* r, uint16_t v) { r->n = (v & 0x8000) != 0; r->z = v == 0; }
static void nz8(TgRegs* r, uint8_t v) { r->n = (v & 0x80) != 0; r->z = v == 0; }
static uint8_t adc8(TgRegs* r, uint8_t a, uint8_t b) {
  unsigned s = (unsigned) a + b + r->c;
  uint8_t res = (uint8_t) s;
  r->c = s > 0xFF;
  r->v = (~(a ^ b) & (a ^ res) & 0x80) != 0;
  nz8(r, res);
  return res;
}

// $88C0 / $88C7: the cursor up or down a line.
void Tg2MenuUp(TgRegs* r) {
  Tg2SetFadeIn(r);
  uint16_t v = (uint16_t) (dp(r, 0x8D) - 1);
  dp_set(r, 0x8D, v);
  nz16(r, v);
  r->c = false;
}
void Tg2MenuDown(TgRegs* r) {
  Tg2SetFadeIn(r);
  uint16_t v = (uint16_t) (dp(r, 0x8D) + 1);
  dp_set(r, 0x8D, v);
  nz16(r, v);
  r->c = false;
}

// $88E0: back out of the screen, unless it's still busy ($9F).
void Tg2MenuBack(TgRegs* r) {
  Tg2SetFadeIn(r);
  r->a = dp(r, 0x9F);
  if(r->a != 0) {
    nz16(r, r->a);
    r->c = false;
    return;
  }
  r->a = 0;
  r->x = 0x86DA;
  nz16(r, r->x);
  r->c = true;
}

// The name entry's letter grid: $A5 is the key under the cursor, 8 to a row in 5 rows
// ($00-$27, where $26 is the 2-wide END key and $27 isn't used). $A9 counts down while a
// direction is held, so a held direction repeats.
static void name_right(TgRegs* r) {
  Tg2SetFadeIn(r);
  uint16_t a = dp(r, 0xA5);
  uint16_t col = (a + 1) & 7;
  dp_set(r, 0xA5, col);
  a = (a & 0xFFF8) | col;
  cmp16(r, a, 0x27);
  if(a == 0x27) {
    a = 0x20;
    nz16(r, a);
  }
  dp_set(r, 0xA5, a);
  r->a = a;
  r->c = false;
}
static void name_left(TgRegs* r) {
  Tg2SetFadeIn(r);
  uint16_t a0 = dp(r, 0xA5), a = a0;
  if(a == 0x27) a = 0x26;
  uint16_t col = (uint16_t) (a - 1) & 7;
  dp_set(r, 0xA5, col);
  a = (a0 & 0xFFF8) | col;
  cmp16(r, a, 0x27);
  if(a == 0x27) {
    a = 0x26;
    nz16(r, a);
  }
  dp_set(r, 0xA5, a);
  r->a = a;
  r->c = false;
}
static void name_down(TgRegs* r) {
  Tg2SetFadeIn(r);
  r->c = false;
  uint16_t a = adc16(r, dp(r, 0xA5), 0x0008);
  cmp16(r, a, 0x28);
  if(r->c) a = sbc16(r, a, 0x28);
  dp_set(r, 0xA5, a);
  r->a = a;
  r->c = false;
}
static void name_up(TgRegs* r) {
  Tg2SetFadeIn(r);
  r->c = true;
  uint16_t a = sbc16(r, dp(r, 0xA5), 0x0008);
  if(!r->c) a = adc16(r, a, 0x28);
  dp_set(r, 0xA5, a);
  r->a = a;
  r->c = false;
}
// $8A1F / $8A26 / $8A2D / $8A34: a direction held, once its repeat delay has run out.
static void name_held(TgRegs* r, void (*move)(TgRegs*)) {
  r->a = dp(r, 0xA9);
  if(r->a != 0) {
    nz16(r, r->a);
    r->c = false;
    return;
  }
  move(r);
}
void Tg2NameHeldRight(TgRegs* r) { name_held(r, name_right); }
void Tg2NameHeldLeft(TgRegs* r) { name_held(r, name_left); }
void Tg2NameHeldUp(TgRegs* r) { name_held(r, name_up); }
void Tg2NameHeldDown(TgRegs* r) { name_held(r, name_down); }
// $8A3D: each frame: the repeat delay counts down, then repeats every 10 frames.
void Tg2NameRepeat(TgRegs* r) {
  uint16_t v = (uint16_t) (dp(r, 0xA9) - 1);
  dp_set(r, 0xA9, v);
  nz16(r, v);
  if(v & 0x8000) {
    r->a = 0x000A;
    dp_set(r, 0xA9, r->a);
    nz16(r, r->a);
  }
  r->c = false;
}
// $8A48 / $8A50 / $8A58 / $8A60: a direction pressed: moves at once, then waits 24 frames
// before repeating.
static void name_press(TgRegs* r, void (*move)(TgRegs*)) {
  r->a = 0x0018;
  dp_set(r, 0xA9, r->a);
  move(r);
}
void Tg2NamePressRight(TgRegs* r) { name_press(r, name_right); }
void Tg2NamePressLeft(TgRegs* r) { name_press(r, name_left); }
void Tg2NamePressUp(TgRegs* r) { name_press(r, name_up); }
void Tg2NamePressDown(TgRegs* r) { name_press(r, name_down); }

// $8AD3: the key pressed. Letters A-Z ($00-$19) and the digits and signs ($1A-$23) add a
// character (up to 11), $24 a space, $25 deletes one, END ($26) leaves. A name still marked
// as the default ($1D1B bit 0) is cleared first.
void Tg2NameKey(TgRegs* r) {
  uint16_t a3 = dp(r, 0xA3);
  r->x = (uint16_t) (dp(r, 0xA7) + a3);
  uint16_t key = dp(r, 0xA5);
  if(key >= 0x26) {
    Tg2MenuBack(r);
    return;
  }
  uint16_t flags = rd(dbx(r, 0x1D1B + a3));
  if(flags & 1) {
    wr(dbx(r, 0x1D1B + a3), flags & 0xFFFE);
    for(uint16_t i = 0; i < 12; i += 2) wr(dbx(r, 0x1CF3 + a3 + i), 0);
    dp_set(r, 0xA7, 0);
    r->x = a3;
  }
  r->a = key;
  uint8_t k = (uint8_t) key;
  if(k == 0x25) {
    r->y = dp(r, 0xA7);
    nz16(r, r->y);
    if(r->y != 0) {
      Tg2SetFadeIn(r);
      r->x--;
      wr8(dbx(r, 0x1CF3 + r->x), 0);
      uint8_t n = (uint8_t) (g_ram[(uint16_t) (r->d + 0xA7)] - 1);
      dp8_set(r, 0xA7, n);
      nz8(r, n);
    }
  } else {
    r->y = dp(r, 0xA7);
    r->c = r->y >= 0x0B;
    nz16(r, (uint16_t) (r->y - 0x0B));
    if(r->y != 0x0B) {
      Tg2SetFadeIn(r);
      r->c = k >= 0x1A;
      uint8_t ch;
      if(k < 0x1A) ch = adc8(r, k, 0x41);
      else if(k == 0x24) ch = 0x20;
      else {
        r->c = k >= 0x24;
        ch = adc8(r, k, 0x16);
      }
      r->a = (uint16_t) ((r->a & 0xFF00) | ch);
      wr8(dbx(r, 0x1CF3 + r->x), ch);
      uint8_t n = (uint8_t) (g_ram[(uint16_t) (r->d + 0xA7)] + 1);
      dp8_set(r, 0xA7, n);
      nz8(r, n);
    }
  }
  r->c = false;
}

// $8D6C: the cursor's height for line $8D (table $9F:8D78) into $3A.
void Tg2MenuCursorY(TgRegs* r) {
  r->x = (uint16_t) (dp(r, 0x8D) << 1);
  r->a = rd(0x9F8D78 + r->x);
  dp_set(r, 0x3A, r->a);
  nz16(r, r->a);
  r->c = false;
}

// $8D7C: leaves the gears choice ($1D01 bit 15 marks it open).
void Tg2GearsClose(TgRegs* r) {
  r->x = dp(r, 0xA3);
  uint16_t v = rd(dbx(r, 0x1D01 + r->x));
  r->v = (v & 0x4000) != 0;
  if(v != 0) Tg2SetFadeIn(r);
  r->a = v & 0x7FFF;
  wr(dbx(r, 0x1D01 + r->x), r->a);
  nz16(r, r->a);
  dp_set(r, 0x8D, 0);
  r->c = false;
}

// $8F11 / $8F26: the control type ($1D01, 0-4) one up or down.
void Tg2ControlNext(TgRegs* r) {
  r->x = dp(r, 0xA3);
  uint32_t adr = dbx(r, 0x1D01 + r->x);
  r->a = rd(adr) & 0x7FFF;
  cmp16(r, r->a, 4);
  if(r->a != 4) {
    Tg2SetFadeIn(r);
    uint16_t v = (uint16_t) (rd(adr) + 1);
    wr(adr, v);
    nz16(r, v);
  }
  r->c = false;
}
void Tg2ControlPrev(TgRegs* r) {
  r->x = dp(r, 0xA3);
  uint32_t adr = dbx(r, 0x1D01 + r->x);
  r->a = rd(adr) & 0x7FFF;
  nz16(r, r->a);
  if(r->a != 0) {
    Tg2SetFadeIn(r);
    uint16_t v = (uint16_t) (rd(adr) - 1);
    wr(adr, v);
    nz16(r, v);
  }
  r->c = false;
}

// Starts the video queue's transfer of $0380 bytes from $7F:(src) to VRAM (dest).
static void queue_screen(TgRegs* r, uint16_t src, uint16_t dest) {
  dp_set(r, 0x7C, src);
  dp_set(r, 0x7E, 0x007F);
  dp_set(r, 0x80, dest);
  r->a = 0x0380;
  dp_set(r, 0x7A, r->a);
  r->n = false;
  r->z = false;
  r->c = false;
}

// $8F38: the control type's picture: control type ($1D01) x $380 bytes into the screens
// unpacked at $60, from $0984 on.
void Tg2ControlPicture(TgRegs* r) {
  r->x = dp(r, 0xA3);
  uint16_t a0 = rd(dbx(r, 0x1D01 + r->x)) & 0x7FFF;
  dp_set(r, 0x00, a0);
  r->c = (a0 & 0x8000) != 0;
  uint16_t a = (uint16_t) (a0 << 1);
  a = adc16(r, a, a0);
  r->c = (a & 0x8000) != 0;
  a = (uint16_t) (a << 1);
  a = adc16(r, a, a0);
  a = (uint16_t) (a << 8 | a >> 8);
  r->c = a & 1;
  a >>= 1;
  a = adc16(r, a, 0x0984);
  a = adc16(r, a, dp(r, 0x60));
  queue_screen(r, a, dp(r, 0x62));
}

// $9064: the speed units' picture (km/h when $1CFF is set) into the screens at $66.
void Tg2UnitsPicture(TgRegs* r) {
  r->y = 0x0380;
  r->x = dp(r, 0xA3);
  if(rd(dbx(r, 0x1CFF + r->x)) != 0) r->y = 0x0700;
  r->c = false;
  uint16_t a = adc16(r, r->y, dp(r, 0x60));
  queue_screen(r, a, dp(r, 0x66));
}

// $909A: picks km/h and moves down a line.
void Tg2UnitsKmh(TgRegs* r) {
  r->x = dp(r, 0xA3);
  uint32_t adr = dbx(r, 0x1CFF + r->x);
  if((uint16_t) (rd(adr) - 1) != 0) Tg2SetFadeIn(r);
  r->a = 1;
  wr(adr, 1);
  Tg2MenuDown(r);
}

// $CB08: the 1P/2P choice ($1CDF) from left or right on the pad ($2A: new presses).
static bool take_press(TgRegs* r, uint16_t bit) {
  uint16_t t = dp(r, 0x2A);
  dp_set(r, 0x2A, t & (uint16_t) ~bit);
  r->z = (t & bit) == 0;
  return !r->z;
}
void Tg2PlayersPick(TgRegs* r) {
  r->a = 0x0800;
  if(take_press(r, 0x0800)) {
    Tg2SetFadeIn(r);
    wr(dbx(r, 0x1CDF), 0);
  }
  r->a = 0x0400;
  r->n = false;
  if(take_press(r, 0x0400)) {
    Tg2SetFadeIn(r);
    r->a = 1;
    wr(dbx(r, 0x1CDF), 1);
    nz16(r, 1);
  }
}

// $CB26: the difficulty ($1CDD, 0-2): keeps the current one's best-lap settings
// ($1CE1/$1CE3) in its slot ($1CED/$1CE7), moves on a left or right press, and loads the
// new one's.
void Tg2DifficultyPick(TgRegs* r) {
  uint32_t level = dbx(r, 0x1CDD);
  r->x = (uint16_t) (rd(level) << 1);
  wr(dbx(r, 0x1CED + r->x), rd(dbx(r, 0x1CE1)));
  wr(dbx(r, 0x1CE7 + r->x), rd(dbx(r, 0x1CE3)));
  if(take_press(r, 0x0800)) {
    Tg2SetFadeIn(r);
    if(rd(level) != 0) wr(level, (uint16_t) (rd(level) - 1));
  }
  if(take_press(r, 0x0400)) {
    Tg2SetFadeIn(r);
    if(rd(level) != 2) wr(level, (uint16_t) (rd(level) + 1));
  }
  uint16_t a = rd(level);
  r->c = (a & 0x8000) != 0;
  r->x = (uint16_t) (a << 1);
  wr(dbx(r, 0x1CE1), rd(dbx(r, 0x1CED + r->x)));
  r->a = rd(dbx(r, 0x1CE7 + r->x));
  wr(dbx(r, 0x1CE3), r->a);
  nz16(r, r->a);
}

// $D326: the password screen's two pictures into VRAM.
void Tg2PasswordPictures(TgRegs* r) {
  r->a = 0x009E; r->x = 0xEDC0; r->y = 0x6000;
  Tg2UnpackToVram(r);
  r->a = 0x0098; r->x = 0x926A; r->y = 0x6A00;
  Tg2UnpackToVram(r);
}

// $A4F4: the cursor's height for line $8D (table $9F:A5EB) into $3C.
void Tg2OptionsCursorY(TgRegs* r) {
  uint16_t line = dp(r, 0x8D);
  r->c = (line & 0x8000) != 0;
  r->x = (uint16_t) (line << 1);
  r->a = rd(0x9FA5EB + r->x);
  dp_set(r, 0x3C, r->a);
  nz16(r, r->a);
}

// $A4FF / $A519 / $A533 / $A54D: open a sub-screen (script X), unless the screen is busy.
static void open_screen(TgRegs* r, uint16_t scroll, uint16_t script) {
  r->a = dp(r, 0x9F);
  if(r->a != 0) {
    nz16(r, r->a);
    r->c = false;
    return;
  }
  Tg2SetFadeIn(r);
  dp_set(r, 0x8F, scroll);
  dp_set(r, 0xA1, dp(r, 0x8D));
  r->x = script;
  r->a = 0;
  nz16(r, 0);
  r->c = true;
}
void Tg2OpenScreen1(TgRegs* r) { open_screen(r, 0x4000, 0x874E); }
void Tg2OpenScreen2(TgRegs* r) { open_screen(r, 0x6000, 0x87A6); }
void Tg2OpenScreen3(TgRegs* r) { open_screen(r, 0xA000, 0x87C6); }
void Tg2OpenScreen4(TgRegs* r) { open_screen(r, 0xA000, 0x87F0); }

// $AAF2: copies the shop's 4x4-tile panel at $60 + $05D8 into the map at $0D80.
void Tg2CopyMapBlock(TgRegs* r);
void Tg2ShopPanel(TgRegs* r) {
  r->c = false;
  r->a = adc16(r, dp(r, 0x60), 0x05D8);
  dp_set(r, 0x00, r->a);
  dp_set(r, 0x04, 0x0004);
  dp_set(r, 0x06, 0x0004);
  dp_set(r, 0x08, 0);
  dp_set(r, 0x0A, 0x0038);
  dp_set(r, 0x0C, 0x7F01);
  r->a = 0x0D80;
  dp_set(r, 0x0E, r->a);
  r->n = false;
  r->z = false;
  Tg2CopyMapBlock(r);
}

// $AB43: a line of shop text as sprites at x = $00 from the sprite table's Y: a dot
// first, then each character ($9F:X, ending with 0) 4 pixels apart ('4', $34, takes 5).
void Tg2ShopText(TgRegs* r) {
  uint8_t ch = rd8(0x9F0000 + r->x);
  if(ch == 0) {
    r->a = 0;
    nz16(r, 0);
    return;
  }
  wr(dbx(r, 0x0502 + r->y), 0x3172);
  wr(dbx(r, 0x0500 + r->y), dp(r, 0x00));
  r->c = false;
  dp_set(r, 0x00, adc16(r, dp(r, 0x00), 0x0006));
  wr(dbx(r, 0x0301 + r->y), 0);
  r->y += 4;
  while((ch = rd8(0x9F0000 + r->x)) != 0) {
    r->c = false;
    wr(dbx(r, 0x0502 + r->y), adc16(r, ch, 0x3143));
    wr(dbx(r, 0x0500 + r->y), dp(r, 0x00));
    r->c = false;
    dp_set(r, 0x00, adc16(r, dp(r, 0x00), 0x0004));
    cmp16(r, ch, 0x34);
    if(ch == 0x34) dp_set(r, 0x00, (uint16_t) (dp(r, 0x00) + 1));
    wr(dbx(r, 0x0301 + r->y), 0);
    r->y += 4;
    r->x++;
  }
  r->a = 0;
  nz16(r, 0);
}

// $B6AC: the low digit of $00 as a character ('0' + n, hex), and $00 shifted down a digit.
void Tg2NextDigit(TgRegs* r) {
  uint16_t v = dp(r, 0x00);
  r->c = false;
  r->a = adc16(r, v & 0x000F, 0x0030);
  r->c = (v >> 3) & 1;
  v >>= 4;
  dp_set(r, 0x00, v);
  r->n = false;
  r->z = v == 0;
}

// $B6BE: a box's frame in the map at $7F:0700 (row width $40 bytes): the top and bottom
// edges' corners and middle pieces from the tiles at $7F:0F20-$7F:0F32, for a box from
// $02 to $0A (in pixels) placed at $00.
void Tg2BoxEdges(TgRegs* r) {
  r->c = true;
  uint16_t a = (uint16_t) (sbc16(r, dp(r, 0x0A), 0x000A) & 0x00F8);
  dp_set(r, 0x04, a);
  a >>= 2;
  r->c = false;
  a = adc16(r, a, dp(r, 0x00));
  dp_set(r, 0x00, a);
  uint16_t x = a;
  r->c = true;
  a = sbc16(r, dp(r, 0x02), dp(r, 0x04));
  r->c = false;
  a = adc16(r, a, dp(r, 0x02));
  r->c = false;
  a = (uint16_t) (adc16(r, a, 0x000A) & 0xFFF8);
  r->c = true;
  a = (uint16_t) (sbc16(r, a, dp(r, 0x04)) >> 3);
  r->c = true;
  uint16_t y = sbc16(r, a, 0x0005);
  wr(0x7F0700 + x, rd(0x7F0F20));
  wr(0x7F0740 + x, rd(0x7F0F2A));
  wr(0x7F0702 + x, rd(0x7F0F22));
  wr(0x7F0742 + x, rd(0x7F0F2C));
  for(; y != 0; y--, x += 2) {
    wr(0x7F0704 + x, rd(0x7F0F24));
    wr(0x7F0744 + x, rd(0x7F0F2E));
  }
  wr(0x7F0704 + x, rd(0x7F0F26));
  wr(0x7F0744 + x, rd(0x7F0F30));
  wr(0x7F0706 + x, rd(0x7F0F28));
  r->a = rd(0x7F0F32);
  wr(0x7F0746 + x, r->a);
  nz16(r, r->a);
  r->x = x;
  r->y = 0;
}
