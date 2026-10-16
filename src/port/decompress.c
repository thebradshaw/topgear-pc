// The byte-stream decompressor (bank $00), used for the RAM code at boot, track data and
// graphics.
//
// An LZ scheme with a 2 KiB ring buffer at $07B2 (absolute, in the data bank) that fills
// downwards. Each call returns one byte, so the state lives in direct page:
//   $1A  word  bytes left in the current run or literal block, or copy length
//   $1C  word  ring index the next copied byte is read from (its high byte overlaps $1D)
//   $1D  byte  the byte a run repeats, or the first byte of a copy
//   $1E  word  ring index the next output byte is written to
//   $20  word  offset into the stream
//   $22  byte  0: read a command; $FF: in a run; $FE: in a literal block; 1: copying
//   $23  long  start of the stream (DecompSrc)
// Commands, by the first byte c (n is the low 13 bits of c and the byte after it):
//   $00        end of stream
//   $01-$1F    c literal bytes follow
//   $20-$3F    n + $20 literal bytes follow
//   $40-$5F    one byte follows, repeated (c & $1F) + 3 times
//   $60-$7F    one byte follows, repeated n + $24 times
//   $80-$BF    copy ((c >> 5) & 1) + 2 bytes from earlier output, distance c & $1F
//   $C0-$FF    copy ((c >> 2) & $F) + 4 bytes, distance from the low 10 bits of n
// The registers on return are kept exactly, including the high byte of A.

#include "port.h"

typedef struct Decomp {
  TgRegs* r;
  uint16_t a;  // full 16-bit accumulator; the routine runs with 8-bit A most of the time
} Decomp;

static uint8_t dp8(TgRegs* r, uint16_t o) { return g_ram[(uint16_t) (r->d + o)]; }
static void dp8_set(TgRegs* r, uint16_t o, uint8_t v) { g_ram[(uint16_t) (r->d + o)] = v; }
static uint16_t dp16(TgRegs* r, uint16_t o) { return (uint16_t) (dp8(r, o) | dp8(r, (uint16_t) (o + 1)) << 8); }
static void dp16_set(TgRegs* r, uint16_t o, uint16_t v) {
  dp8_set(r, o, (uint8_t) v);
  dp8_set(r, (uint16_t) (o + 1), (uint8_t) (v >> 8));
}

static uint8_t stream(TgRegs* r, uint16_t y) {
  uint32_t base = (uint32_t) (dp8(r, 0x23) | dp8(r, 0x24) << 8 | dp8(r, 0x25) << 16);
  return tg_read8((base + y) & 0xffffff);
}

static uint32_t ring_adr(TgRegs* r, uint16_t i) { return (uint32_t) r->db << 16 | (uint16_t) (0x07B2 + i); }
static uint8_t ring(TgRegs* r, uint16_t i) { return tg_read8(ring_adr(r, i)); }
static void ring_set(TgRegs* r, uint16_t i, uint8_t v) { tg_bus_write8(ring_adr(r, i), v); }

static uint16_t ring_prev(uint16_t i) { return i == 0 ? 0x07FF : (uint16_t) (i - 1); }

static void set_lo(Decomp* s, uint8_t v) { s->a = (uint16_t) ((s->a & 0xff00) | v); }

// 16-bit ADC with the carry given, setting V and C as the original's ADC does.
static uint16_t adc(TgRegs* r, uint16_t a, uint16_t b, bool carry) {
  r->c = carry;
  return adc16(r, a, b);
}

// $00:EB3A-$00:EB4D: reads a literal byte from the stream, then outputs it.
static void put_byte(Decomp* s, uint8_t v);
static void literal(Decomp* s, uint16_t y) {
  TgRegs* r = s->r;
  set_lo(s, stream(r, y));
  y++;
  dp16_set(r, 0x20, y);
  r->y = y;
  put_byte(s, (uint8_t) s->a);
}

// $00:EB3F: writes the output byte into the ring and returns it with carry clear.
static void put_byte(Decomp* s, uint8_t v) {
  TgRegs* r = s->r;
  uint16_t x = dp16(r, 0x1E);
  ring_set(r, x, v);
  x = ring_prev(x);
  dp16_set(r, 0x1E, x);
  r->x = x;
  flags_nz16(r, x);
  r->c = false;
}

// $00:EB65-$00:EBB5: a copy command. Copies the whole match into the ring now and
// returns its first byte; the following calls read the rest back out of the ring.
static void copy(Decomp* s, uint16_t y, uint16_t distance) {
  TgRegs* r = s->r;
  dp16_set(r, 0x20, y);
  uint16_t a = adc(r, distance, dp16(r, 0x1A), true);
  a = adc(r, a, dp16(r, 0x1E), false);
  a &= 0x07FF;
  uint16_t x = a;
  y = dp16(r, 0x1E);
  s->a = a;
  set_lo(s, ring(r, x));
  ring_set(r, y, (uint8_t) s->a);
  dp8_set(r, 0x1D, (uint8_t) s->a);
  for(;;) {
    x = ring_prev(x);
    y = ring_prev(y);
    uint8_t left = (uint8_t) (dp8(r, 0x1A) - 1);  // 8-bit DEC
    dp8_set(r, 0x1A, left);
    if(left & 0x80) break;
    set_lo(s, ring(r, x));
    ring_set(r, y, (uint8_t) s->a);
  }
  set_lo(s, dp8(r, 0x1D));
  x = ring_prev(dp16(r, 0x1E));
  dp16_set(r, 0x1C, x);
  dp16_set(r, 0x1E, y);
  r->x = x;
  r->y = y;
  flags_nz16(r, x);
  r->c = false;
}

// $00:EA9B. Returns the next byte in A (8-bit; index registers 16-bit), carry set at the
// end of the stream.
void DecompressNext(TgRegs* r) {
  Decomp s = { r, r->a };
  uint8_t state = dp8(r, 0x22);
  if(state == 0) {
    uint16_t y = dp16(r, 0x20);
    uint8_t c = stream(r, y);
    set_lo(&s, c);
    r->y = y;
    if(c == 0) {
      r->n = false;
      r->z = true;
      r->c = true;
    } else if(c & 0x80) {
      dp8_set(r, 0x22, 1);
      y++;
      uint16_t distance;
      if(c >= 0xC0) {
        uint8_t c2 = stream(r, y);
        y++;
        uint16_t w = (uint16_t) (c << 8 | c2);
        dp16_set(r, 0x1A, adc(r, (c >> 2) & 0x000F, 0x0003, false));
        distance = w & 0x03FF;
      } else {
        uint16_t w = s.a;
        dp16_set(r, 0x1A, (uint16_t) (((w << 3) >> 8 & 0x0001) + 1));
        distance = w & 0x001F;
      }
      r->y = y;
      copy(&s, y, distance);
    } else {
      dp8_set(r, 0x22, 0xFF);
      y++;
      if(c >= 0x40) {
        uint16_t n;
        if(c >= 0x60) {
          s.a = (uint16_t) (c << 8 | stream(r, y));
          y++;
          n = adc(r, s.a & 0x1FFF, 0x0022, false);
        } else {
          n = (uint16_t) ((s.a & 0x001F) + 1);
        }
        s.a = n;
        dp16_set(r, 0x1A, n);
        set_lo(&s, stream(r, y));
        y++;
        dp16_set(r, 0x20, y);
        r->y = y;
        dp8_set(r, 0x1D, (uint8_t) s.a);
        put_byte(&s, (uint8_t) s.a);
      } else {
        dp8_set(r, 0x22, 0xFE);
        if(c >= 0x20) {
          s.a = (uint16_t) (c << 8 | stream(r, y));
          y++;
          s.a = adc(r, s.a & 0x1FFF, 0x001E, false);
          dp16_set(r, 0x1A, s.a);
        } else {
          s.a = (uint16_t) ((s.a & 0x001F) - 2);
          if(s.a & 0x8000) dp8_set(r, 0x22, 0);
          else dp16_set(r, 0x1A, s.a);
        }
        literal(&s, y);
      }
    }
  } else if(state & 0x80) {
    // In a run or a literal block.
    uint16_t left = (uint16_t) (dp16(r, 0x1A) - 1);
    dp16_set(r, 0x1A, left);
    if(left & 0x8000) dp8_set(r, 0x22, 0);
    set_lo(&s, (uint8_t) (state ^ 0xFF));
    if((uint8_t) s.a != 0) {
      literal(&s, dp16(r, 0x20));
    } else {
      set_lo(&s, dp8(r, 0x1D));
      put_byte(&s, (uint8_t) s.a);
    }
  } else {
    // Copying: the next byte of the match, already in the ring.
    uint16_t x = dp16(r, 0x1C);
    set_lo(&s, ring(r, x));
    x = ring_prev(x);
    r->x = x;
    uint16_t diff = (uint16_t) (x - dp16(r, 0x1E));
    if(diff != 0) {
      dp16_set(r, 0x1C, x);
      r->n = diff >> 15;
      r->z = false;
    } else {
      dp8_set(r, 0x22, 0);
      r->n = false;
      r->z = true;
    }
    r->c = false;
  }
  r->a = s.a;
}

// $00:EA8E. Starts decompressing the stream at DecompSrc and returns its first byte, as
// DecompressNext.
void DecompressBegin(TgRegs* r) {
  dp16_set(r, 0x20, 0);
  dp16_set(r, 0x1E, 0x07FF);
  dp8_set(r, 0x22, 0);
  r->a = 0x07FF;
  DecompressNext(r);
}
