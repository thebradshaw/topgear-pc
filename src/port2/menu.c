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
