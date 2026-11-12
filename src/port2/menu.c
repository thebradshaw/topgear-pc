// Top Gear 2: the menus and the screens around them (bank $1F).

#include <stddef.h>

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
