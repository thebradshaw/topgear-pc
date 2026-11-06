// Top Gear 2: the per-frame colour updates for the race screen (bank $01, $9261-$A065).
//
// The dashboard draws its numbers and gauges with palette tricks: each digit position is
// a set of colours, and writing the right colours into the palette copy at $7E:C9C0+
// makes the right digit appear. The same pass blends the sky and scenery colours for the
// light level, and sets the flashing and fading colours (the queued CGRAM words at
// $0A40+ / $0AAC+, picked by the buffer flag $0126).
//
// Tables are read from the caller's data bank ($8C in a race).

#include "../port/port.h"

static uint16_t g_d;
static uint8_t g_db;

static uint16_t z(uint8_t o) { return ram16((uint16_t) (g_d + o)); }
static void zs(uint8_t o, uint16_t v) { ram16_set((uint16_t) (g_d + o), v); }
static uint8_t zb(uint8_t o) { return g_ram[(uint16_t) (g_d + o)]; }
static void zbs(uint8_t o, uint8_t v) { g_ram[(uint16_t) (g_d + o)] = v; }
// A word from the data bank.
static uint16_t t(uint16_t adr) { return tg_read16((uint32_t) g_db << 16 | adr); }
static uint16_t w(uint16_t adr) { return ram16(adr); }
static void ws(uint16_t adr, uint16_t v) { ram16_set(adr, v); }

// The queued CGRAM words for this frame's buffer.
static uint16_t queue(uint16_t a, uint16_t b) { return (w(0x0126) & 0x8000) ? b : a; }

// Seven colours from row k of a table (16 bytes a row).
static void copy_row(uint16_t table, uint16_t k, uint16_t dst) {
  for(int x = 12; x >= 0; x -= 2) ws((uint16_t) (dst + x), t((uint16_t) (table + k * 16 + x)));
}

// Four colours (two pairs) for a gauge: row k of $F148 / $F150.
static void gauge_row(uint16_t k, uint16_t dst_a, uint16_t dst_b) {
  for(int x = 6; x >= 0; x -= 2) {
    ws((uint16_t) (dst_a + x), t((uint16_t) (0xF148 + k * 16 + x)));
    ws((uint16_t) (dst_b + x), t((uint16_t) (0xF150 + k * 16 + x)));
  }
}

// One digit's two colours ($EE70 table, 4 bytes a digit).
static void digit(uint16_t i, uint16_t dst_a, uint16_t dst_b) {
  ws(dst_a, t((uint16_t) (0xEE70 + i)));
  ws(dst_b, t((uint16_t) (0xEE72 + i)));
}

// Finds v's step in a threshold table (words; the search starts at index `first`), and
// returns the remainder; *idx gets the step's byte index.
static uint16_t step(uint16_t v, uint16_t limits, uint16_t bases, uint16_t first, uint16_t* idx) {
  uint16_t x = first;
  while(v >= t((uint16_t) (limits + x))) x = (uint16_t) (x + 2);
  *idx = x;
  return (uint16_t) (v - t((uint16_t) (bases + x)));
}

// A three-digit number (0-999) in three digit positions (hundreds, tens, ones), each two
// colours; leading zeroes are blank ($EE26).
static void number(uint16_t v, const uint16_t dst[6]) {
  uint16_t x, y;
  if(v < 100) {
    uint16_t blank = t(0xEE26);
    ws(dst[0], blank);
    ws(dst[1], blank);
    if(v < 10) {
      ws(dst[2], blank);
      ws(dst[3], blank);
      digit((uint16_t) (v << 2), dst[4], dst[5]);
      return;
    }
    y = step(v, 0xFECB, 0xFEC9, 2, &x);
  } else {
    y = step(v, 0xFEB5, 0xFEB3, 2, &x);
    digit((uint16_t) (x << 1), dst[0], dst[1]);
    y = step(y, 0xFECB, 0xFEC9, 0, &x);
  }
  digit((uint16_t) (x << 1), dst[2], dst[3]);
  digit((uint16_t) (y << 2), dst[4], dst[5]);
}

// The time display: four digit fields from successive divisions, then the last digit
// through $FEA5. $8C9F is the largest time shown.
static void clock(uint16_t v, const uint16_t dst[5]) {
  static const uint16_t kLimits[4] = { 0xFE57, 0xFE6D, 0xFE7B, 0xFE91 };
  uint16_t x;
  if(v >= 0x8CA0) v = 0x8C9F;
  for(int i = 0; i < 4; i++) {
    v = step(v, kLimits[i], (uint16_t) (kLimits[i] - 2), 0, &x);
    ws(dst[i], t((uint16_t) (0xEE5C + x)));
  }
  x = (uint16_t) (t((uint16_t) (0xFEA5 + (v << 1))) << 1);
  ws(dst[4], t((uint16_t) (0xEE5C + x)));
}

// A flashing light: seven colours from row ($v & $F) of $EFD4, and one queued colour,
// brightness ($v >> 8) & $1F, which counts up to $1F (bit 15 set) or down to 0.
static void flasher(uint8_t o, uint16_t dst, uint16_t qa, uint16_t qb) {
  uint16_t v = z(o);
  copy_row(0xEFD4, (uint16_t) (v & 0x000F), dst);
  uint16_t q = queue(qa, qb);
  uint16_t m = v & 0x1F00;
  uint16_t b = (uint16_t) (m >> 8);
  zs(0xD4, (uint16_t) (m << 2));
  zs(0xD6, (uint16_t) (b >> 2));
  ws(q, (uint16_t) ((b >> 2) << 5 | (b >> 2) | (uint16_t) (m << 2) | 0x0318));
  v = z(o);
  if(v & 0x8000) {
    if((v & 0x1F00) != 0x1F00) zs(o, (uint16_t) (v + 0x0100));
  } else if(v & 0x1F00) {
    zs(o, (uint16_t) (v - 0x0100));
  }
}

// Seven colours from row ($v & 7) of one of three tables, by bits 12-13 of $v.
static void lamp(uint8_t o, uint16_t dst) {
  uint16_t v = z(o);
  uint16_t table = !(v & 0x3000) ? 0xEF84 : (v & 0x2000) ? 0xEEB4 : 0xEF24;
  copy_row(table, (uint16_t) (v & 0x0007), dst);
}

// A gradient of n colours (RGB888 at src, 3 bytes each, read forwards or backwards),
// lit to level $v: the first (v >> 8) & $F colours full, one blended through the $F387
// table by the fraction (v & $F8), the rest at a quarter. At or above full (n << 8) all
// are lit.
static uint16_t lit(uint16_t s, int x) {
  uint16_t r = (uint16_t) ((t((uint16_t) (s + x)) & 0xF8) >> 3);
  uint16_t g = (uint16_t) ((t((uint16_t) (s + x + 1)) & 0xF8) << 2);
  zs(0xDA, (uint16_t) (r | g));
  return (uint16_t) (r | g | ((t((uint16_t) (s + x + 2)) & 0xF8) << 8) >> 1);
}
static uint16_t dim(uint16_t s, int x) {
  uint16_t r = (uint16_t) ((t((uint16_t) (s + x)) & 0xE0) >> 5);
  uint16_t g = t((uint16_t) (s + x + 1)) & 0xE0;
  zs(0xDA, (uint16_t) (r | g));
  return (uint16_t) (r | g | ((t((uint16_t) (s + x + 2)) & 0xE0) << 8) >> 3);
}
static uint16_t blend(uint16_t s, int x) {
  for(int c = 0; c < 3; c++) {
    uint8_t v = (uint8_t) ((t((uint16_t) (s + x + c)) & 0xF8) >> 3);
    uint8_t q = (uint8_t) (v >> 2);
    zbs((uint8_t) (0xDA + 2 * c), q);
    zbs((uint8_t) (0xDB + 2 * c), (uint8_t) (v - q));
  }
  zs(0xE0, (uint16_t) x);
  uint16_t frac = z(0xD8), out = 0;
  for(int c = 0; c < 3; c++) {
    uint8_t lo = zb((uint8_t) (0xDA + 2 * c)), hi = zb((uint8_t) (0xDB + 2 * c));
    uint16_t v = (uint16_t) ((t((uint16_t) (0xF387 + ((hi & 0x1F) | frac))) + lo) & 0x1F);
    out = (uint16_t) (out | v << (5 * c));
    zs(0xDA, out);
  }
  return out;
}
static void gradient(uint8_t o, uint16_t src, int x, int dx, uint16_t y, int dy, int n) {
  uint16_t v = z(o);
  if(v >= (uint16_t) (n << 8)) {
    for(int i = 0; i < n; i++, x += dx, y = (uint16_t) (y + dy)) ws(y, lit(src, x));
    return;
  }
  uint16_t full = (uint16_t) ((v & 0x0F00) >> 8);
  zs(0xD6, (uint16_t) (n - 1 - full));
  zs(0xD8, (uint16_t) ((v & 0x00F8) << 2));
  for(uint16_t i = 0; i < full; i++, x += dx, y = (uint16_t) (y + dy)) ws(y, lit(src, x));
  zs(0xD4, 0xFFFF);
  ws(y, blend(src, x));
  x += dx;
  y = (uint16_t) (y + dy);
  for(int i = n - 1 - full; i > 0; i--, x += dx, y = (uint16_t) (y + dy)) ws(y, dim(src, x));
  zs(0xD6, 0xFFFF);
}

// Five fading colours from the levels at $o..$o+8: the first fades white to cyan, the rest
// blend red into green; level $2000 and up is black.
static void fades(uint8_t o, uint16_t y) {
  uint16_t v = z(o);
  ws(y, v >= 0x2000 ? 0 : (uint16_t) ((((v & 0x1F00) >> 8) ^ 0x1F) << 5 | 0x201F));
  for(int i = 1; i < 5; i++) {
    v = z((uint8_t) (o + 2 * i));
    uint16_t c = 0;
    if(v < 0x2000) {
      uint16_t k = (uint16_t) ((v & 0x1F00) >> 8);
      zs(0xD4, k);
      c = (uint16_t) ((0x1F - k) << 5 | k | 0x2000);
    }
    ws((uint16_t) (y + 2 * i), c);
  }
}

static const uint16_t kNumber1[6] = { 0xCA20, 0xCA60, 0xCA22, 0xCA62, 0xCA24, 0xCA64 };
static const uint16_t kNumber2[6] = { 0xC9DA, 0xCA1A, 0xC9DC, 0xCA1C, 0xC9DE, 0xCA1E };
static const uint16_t kNumber3[6] = { 0xCD5A, 0xCD9A, 0xCD5C, 0xCD9C, 0xCD5E, 0xCD9E };
static const uint16_t kClock1[5] = { 0xC9FA, 0xC9FE, 0xCA00, 0xCA04, 0xCA06 };
static const uint16_t kClock2[5] = { 0xC9C0, 0xC9C4, 0xC9C6, 0xC9CA, 0xC9CC };
static const uint16_t kClock3[5] = { 0xCD40, 0xCD44, 0xCD46, 0xCD4A, 0xCD4C };

static bool enter(TgRegs* r) {
  g_d = r->d;
  g_db = r->db;
  if(!(w(0x0514) & 0x8000)) return true;
  r->n = true;  // lda $0514
  r->z = false;
  return false;
}

// The flags the last fade leaves: lda #0 after a cmp that set the carry, or an ora of $2000
// after lsr shifted a clear bit into the carry. Nothing on the way overflows.
static void leave(TgRegs* r, uint8_t o) {
  bool black = z((uint8_t) (o + 8)) >= 0x2000;
  r->x = 0xFFFD;  // the second gradient counts X down from $24 by 3, 13 times
  r->n = false;
  r->v = false;
  r->z = black;
  r->c = black;
}

// $01:9261. One player.
void Tg2RaceColours1P(TgRegs* r) {
  if(!enter(r)) return;
  gauge_row(z(0xA0), 0xCC3C, 0xCC7C);
  digit((uint16_t) (z(0xA2) << 2), 0xCBC2, 0xCC02);
  number(z(0xA4), kNumber1);
  flasher(0xA6, 0xCAD8, 0x0A40, 0x0AAC);
  gradient(0xA8, 0xF336, 0, 3, queue(0x0A50, 0x0ABC), 2, 14);
  lamp(0xAA, 0xCB58);
  clock(z(0xAC), kClock1);
  gradient(0xAE, 0xF360, 0x24, -3, queue(0x0A8A, 0x0AF6), -2, 13);
  fades(0xB0, queue(0x0A42, 0x0AAE));
  leave(r, 0xB0);
}

// $01:9284. Two players: player 1's half.
void Tg2RaceColours2PTop(TgRegs* r) {
  if(!enter(r)) return;
  gauge_row(z(0xA0), 0xCC02, 0xCC42);
  digit((uint16_t) (z(0xA2) << 2), 0xCB88, 0xCBC8);
  number(z(0xA4), kNumber2);
  flasher(0xA6, 0xCA92, 0x0A40, 0x0AAC);
  gradient(0xA8, 0xF336, 0, 3, queue(0x0A50, 0x0ABC), 2, 14);
  lamp(0xAA, 0xCB12);
  clock(z(0xAC), kClock2);
  gradient(0xAE, 0xF360, 0x24, -3, queue(0x0A8A, 0x0AF6), -2, 13);
  fades(0xB0, queue(0x0A42, 0x0AAE));
  leave(r, 0xB0);
}

// $01:92A7. Two players: player 2's half.
void Tg2RaceColours2PBottom(TgRegs* r) {
  if(!enter(r)) return;
  gauge_row(z(0xBA), 0xCF82, 0xCFC2);
  digit((uint16_t) (z(0xBC) << 2), 0xCF08, 0xCF48);
  number(z(0xBE), kNumber3);
  flasher(0xC0, 0xCE12, 0x0B18, 0x0B84);
  gradient(0xC2, 0xF336, 0, 3, queue(0x0B28, 0x0B94), 2, 14);
  lamp(0xC4, 0xCE92);
  clock(z(0xC6), kClock3);
  gradient(0xC8, 0xF360, 0x24, -3, queue(0x0B62, 0x0BCE), -2, 13);
  fades(0xCA, queue(0x0B1A, 0x0B86));
  leave(r, 0xCA);
}
