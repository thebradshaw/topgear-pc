// Top Gear 2: DMA and fill helpers (banks $01 and $1F).

#include "../port/port.h"

static inline uint16_t dp16(const TgRegs* r, uint8_t o) { return ram16((uint16_t) (r->d + o)); }
static inline void dp16_set(const TgRegs* r, uint8_t o, uint16_t v) { ram16_set((uint16_t) (r->d + o), v); }

// $01:EF6C. DMA channel 7: A words from $7F:X to VRAM word address Y. Keeps the count in
// $A2. Leaves A = (count * 2) high byte : $80.
void Tg2VramDma7F(TgRegs* r) {
  uint16_t count = r->a;
  dp16_set(r, 0xA2, count);
  tg_bus_write8(0x420B, 0x00);
  tg_bus_write8(0x4374, 0x7F);
  tg_bus_write8(0x2115, 0x80);
  tg_bus_write16(0x2116, r->y);
  tg_bus_write16(0x4370, 0x1801);
  tg_bus_write16(0x4372, r->x);
  uint16_t bytes = (uint16_t) (count << 1);
  tg_bus_write16(0x4375, bytes);
  tg_bus_write8(0x420B, 0x80);
  r->a = (uint16_t) ((bytes & 0xFF00) | 0x80);
  r->n = true;
  r->z = false;
  r->c = (count & 0x8000) != 0;
}

// $1F:9167. DMA channel 0: the word at $00 (bytes) from A's low byte (bank):X to VRAM word
// address Y. Leaves X = the byte count, A low byte = 1.
void Tg2VramDma(TgRegs* r) {
  tg_bus_write16(0x2116, r->y);
  tg_bus_write16(0x4302, r->x);
  tg_bus_write16(0x4300, 0x1801);
  r->x = dp16(r, 0x00);
  tg_bus_write16(0x4305, r->x);
  tg_bus_write8(0x4304, (uint8_t) r->a);
  tg_bus_write8(0x420B, 0x01);
  r->a = (uint16_t) ((r->a & 0xFF00) | 0x01);
  r->n = false;
  r->z = false;
}

// $1F:A117. Sets $01DB = $20 and $01D7 = $FF11; registers and flags as they were.
void Tg2SetFadeIn(TgRegs* r) {
  (void) r;
  ram16_set(0x01DB, 0x0020);
  ram16_set(0x01D7, 0xFF11);
}

// $1F:A13D. Fills Y bytes at $7E:X with the word A, by DMA from the multiplier's product
// (A x 1) in channel 0. Leaves A = $3401.
void Tg2FillWords(TgRegs* r) {
  tg_bus_write8(0x211B, (uint8_t) r->a);
  tg_bus_write8(0x211B, (uint8_t) (r->a >> 8));
  tg_bus_write8(0x211C, 0x01);
  tg_bus_write8(0x420B, 0x00);
  tg_bus_write8(0x4304, 0x7E);
  tg_bus_write16(0x4300, 0x3481);
  tg_bus_write16(0x4302, r->x);
  tg_bus_write16(0x4305, r->y);
  tg_bus_write8(0x420B, 0x01);
  r->a = 0x3401;
  r->n = false;
  r->z = false;
}

// $01:DAD6. Adds the word at $A2 to the $A0 words at the long pointer $98. Leaves X = 0,
// Y = 2 x count and A = the last sum.
void Tg2AddToWords(TgRegs* r) {
  uint16_t count = dp16(r, 0xA0);
  if(count == 0) {  // would wrap and run 65536 times; never seen
    tg_port_decline();
    return;
  }
  uint32_t base = (uint32_t) dp16(r, 0x98) | (uint32_t) g_ram[(uint16_t) (r->d + 0x9A)] << 16;
  uint16_t add = dp16(r, 0xA2), y = 0, a = 0;
  bool c = false;
  for(uint16_t x = count; x != 0; x--, y = (uint16_t) (y + 2)) {
    uint32_t adr = (base + y) & 0xFFFFFF;
    uint32_t sum = (uint32_t) tg_read16(adr) + add;
    a = (uint16_t) sum;
    c = sum > 0xFFFF;
    tg_bus_write16(adr, a);
  }
  r->a = a;
  r->x = 0;
  r->y = y;
  r->c = c;
  r->n = false;
  r->z = true;
}

// $01:EF37. As VramDma7F, but from bank $7E.
void Tg2VramDma7E(TgRegs* r) {
  uint16_t count = r->a;
  dp16_set(r, 0xA2, count);
  tg_bus_write8(0x420B, 0x00);
  tg_bus_write8(0x4374, 0x7E);
  tg_bus_write8(0x2115, 0x80);
  tg_bus_write16(0x2116, r->y);
  tg_bus_write16(0x4370, 0x1801);
  tg_bus_write16(0x4372, r->x);
  uint16_t bytes = (uint16_t) (count << 1);
  tg_bus_write16(0x4375, bytes);
  tg_bus_write8(0x420B, 0x80);
  r->a = (uint16_t) ((bytes & 0xFF00) | 0x80);
  r->n = true;
  r->z = false;
  r->c = (count & 0x8000) != 0;
}

// DMA channel 7 of the byte count at dp+cnt from $7F:(dp+src) to VRAM word (dp+dst), then
// clears the count. Leaves A = count high byte : $80.
static void queued_dma(TgRegs* r, uint8_t dst, uint8_t src, uint8_t cnt) {
  tg_bus_write8(0x420B, 0x00);
  tg_bus_write8(0x4374, 0x7F);
  tg_bus_write8(0x2115, 0x80);
  tg_bus_write16(0x2116, dp16(r, dst));
  tg_bus_write16(0x4370, 0x1801);
  tg_bus_write16(0x4372, dp16(r, src));
  uint16_t bytes = dp16(r, cnt);
  tg_bus_write16(0x4375, bytes);
  tg_bus_write8(0x420B, 0x80);
  dp16_set(r, cnt, 0);
  r->a = (uint16_t) ((bytes & 0xFF00) | 0x80);
  r->n = true;
  r->z = false;
}

// $01:A4D1. Uploads the first queued block: $70 bytes from $7F:$68 to VRAM $6C.
void Tg2UploadQueue0(TgRegs* r) { queued_dma(r, 0x6C, 0x68, 0x70); }

// $01:A505. Uploads the second queued block: $72 bytes from $7F:$6A to VRAM $6E.
void Tg2UploadQueue1(TgRegs* r) { queued_dma(r, 0x6E, 0x6A, 0x72); }

// $1F:9EDE. Fills 64 KB of VRAM (from the address already set) with the fixed word at
// $00:9EFD, by DMA channel 0. Leaves A = 1.
void Tg2ClearVram(TgRegs* r) {
  tg_bus_write16(0x4300, 0x1809);
  tg_bus_write16(0x4302, 0x9EFD);
  tg_bus_write16(0x4305, 0x0000);
  tg_bus_write8(0x4304, 0x00);
  tg_bus_write8(0x420B, 0x01);
  r->a = 0x0001;
  r->n = false;
  r->z = false;
}

// --- Bank $01's clears and fills, all by DMA channel 7 ---

static void ch7(uint8_t bank, uint16_t mode, uint16_t src, uint16_t len) {
  tg_bus_write8(0x4374, bank);
  tg_bus_write16(0x4370, mode);
  tg_bus_write16(0x4372, src);
  tg_bus_write16(0x4375, len);
  tg_bus_write8(0x420B, 0x80);
}

// The multiplier's product, read by a B-to-A DMA, is the fill pattern.
static void pattern(uint16_t v) {
  tg_bus_write8(0x211B, (uint8_t) v);
  tg_bus_write8(0x211B, (uint8_t) (v >> 8));
  tg_bus_write8(0x211C, 0x01);
}

static void leaves(TgRegs* r, uint16_t a) {
  r->a = a;
  r->n = true;
  r->z = false;
}

// $01:F28F / $01:F2BF. Fills Y bytes at $7E:X (or $7F:X) with the word A.
static void fill(TgRegs* r, uint8_t bank) {
  pattern(r->a);
  tg_bus_write8(0x420B, 0x00);
  ch7(bank, 0x3481, r->x, r->y);
  leaves(r, 0x3480);
}
void Tg2Fill7E(TgRegs* r) { fill(r, 0x7E); }
void Tg2Fill7F(TgRegs* r) { fill(r, 0x7F); }

// $01:E794. Clears the $200 bytes of the palette buffer at $D8:$D6 (zero times one).
void Tg2ClearPaletteBuffer(TgRegs* r) {
  tg_bus_write8(0x211B, 0x00);
  tg_bus_write8(0x211B, 0x00);
  tg_bus_write8(0x211C, 0x00);
  tg_bus_write8(0x420B, 0x00);
  ch7(g_ram[(uint16_t) (r->d + 0xD8)], 0x3481, dp16(r, 0xD6), 0x0200);
  leaves(r, 0x0280);
}

// $01:E867. Fills the $220 bytes of the sprite buffer at $DE:$DC with $55 (all sprites
// off screen).
void Tg2ClearSpriteBuffer(TgRegs* r) {
  tg_bus_write8(0x211B, 0x55);
  tg_bus_write8(0x211B, 0x55);
  tg_bus_write8(0x211C, 0x01);
  tg_bus_write8(0x420B, 0x00);
  ch7(g_ram[(uint16_t) (r->d + 0xDE)], 0x3481, dp16(r, 0xDC), 0x0220);
  leaves(r, 0x0280);
}

// $01:E7C6. Clears CGRAM (from the zero word at $81:E7F3).
void Tg2ClearCgram(TgRegs* r) {
  tg_bus_write8(0x2121, 0x00);
  tg_bus_write8(0x420B, 0x00);
  ch7(0x81, 0x220A, 0xE7F3, 0x0200);
  leaves(r, 0x0280);
}

// $01:E89F. Clears OAM (from the word at $81:E8CF).
void Tg2ClearOam(TgRegs* r) {
  tg_bus_write8(0x420B, 0x00);
  tg_bus_write8(0x4374, 0x81);
  tg_bus_write16(0x2102, 0x0000);
  tg_bus_write16(0x4370, 0x040A);
  tg_bus_write16(0x4372, 0xE8CF);
  tg_bus_write16(0x4375, 0x0220);
  tg_bus_write8(0x420B, 0x80);
  leaves(r, 0x0280);
}

// $01:EF05. Clears all of VRAM (from the zero word at $81:EF35).
void Tg2ClearVram1(TgRegs* r) {
  tg_bus_write8(0x420B, 0x00);
  tg_bus_write8(0x4374, 0x81);
  tg_bus_write16(0x2116, 0x0000);
  tg_bus_write16(0x4370, 0x1809);
  tg_bus_write16(0x4372, 0xEF35);
  tg_bus_write16(0x4375, 0x0000);
  tg_bus_write8(0x420B, 0x80);
  leaves(r, 0x0080);
}

// --- Colours and tiles from the track data ---

static uint8_t long8(const TgRegs* r, uint8_t ptr, uint16_t y) {
  uint32_t a = (uint32_t) g_ram[(uint16_t) (r->d + ptr + 2)] << 16 | dp16(r, ptr);
  return tg_bus_read8((a + y) & 0xFFFFFF);
}
static void long16_set(const TgRegs* r, uint8_t ptr, uint16_t y, uint16_t v) {
  uint32_t a = (uint32_t) g_ram[(uint16_t) (r->d + ptr + 2)] << 16 | dp16(r, ptr);
  tg_bus_write8((a + y) & 0xFFFFFF, (uint8_t) v);
  tg_bus_write8((a + y + 1) & 0xFFFFFF, (uint8_t) (v >> 8));
}

// One colour channel: the byte (0-255) times the brightness at `scale` ($20 is full),
// rounded, less the offset at `bias`, clamped to 0-$1F. *v gets the subtraction's overflow.
static uint16_t channel(TgRegs* r, uint8_t byte, uint8_t scale, uint8_t bias, bool* v) {
  tg_bus_write8(0x211B, byte);
  tg_bus_write8(0x211B, 0x00);
  uint8_t s = g_ram[(uint16_t) (r->d + scale)];
  tg_bus_write8(0x211C, s);
  unsigned sum = (unsigned) (s >> 1) + tg_bus_read8(0x2134) + (s & 1);
  bool carry = sum > 0xFF;
  uint16_t a = (uint16_t) (tg_bus_read8(0x2135) | tg_bus_read8(0x2136) << 8);
  a = (uint16_t) (a + 1);
  uint16_t m = dp16(r, bias);
  uint16_t res = (uint16_t) (a - m - !carry);
  *v = ((a ^ m) & (a ^ res) & 0x8000) != 0;
  if(res & 0x8000) return 0;
  return res >= 0x20 ? 0x1F : res;
}

// $01:DBA1. A colours, three bytes (red, green, blue) each from the long pointer $94, to
// SNES colours at the long pointer $98, lit by the brightness at $B2-$B6 less $B8-$BC.
void Tg2Colours(TgRegs* r) {
  uint16_t count = r->a;
  uint16_t y = 0;
  dp16_set(r, 0xA6, 0);
  uint16_t c = 0;
  bool v = false;
  do {
    uint16_t red = channel(r, long8(r, 0x94, y++), 0xB2, 0xB8, &v);
    dp16_set(r, 0xA2, red);
    uint16_t green = channel(r, long8(r, 0x94, y++), 0xB4, 0xBA, &v);
    dp16_set(r, 0xA2, (uint16_t) (green << 5 | red));
    uint16_t blue = channel(r, long8(r, 0x94, y++), 0xB6, 0xBC, &v);
    c = (uint16_t) (blue << 10 | dp16(r, 0xA2));
    dp16_set(r, 0xA4, y);
    long16_set(r, 0x98, dp16(r, 0xA6), c);
    dp16_set(r, 0xA6, (uint16_t) (dp16(r, 0xA6) + 2));
  } while(--count);
  r->a = c;
  r->x = 0;
  r->y = y;
  r->n = false;
  r->z = true;
  r->c = false;
  r->v = v;
}

// $01:DAEA. $A0 tiles of 4-bit pixels (two to a byte, 32 bytes a tile) from the long
// pointer $94 to SNES 4-bitplane tiles at the long pointer $98.
void Tg2PlanarTiles(TgRegs* r) {
  dp16_set(r, 0x9C, 0);
  dp16_set(r, 0x9E, 0);
  dp16_set(r, 0xA2, dp16(r, 0xA0));
  uint16_t w1 = 0, y = 0, sum = 0;
  bool c = false, v = false;
  do {
    dp16_set(r, 0xA4, 8);
    do {
      uint16_t s = dp16(r, 0x9C);
      w1 = (uint16_t) (long8(r, 0x94, s) | long8(r, 0x94, (uint16_t) (s + 1)) << 8);
      uint16_t w2 = (uint16_t) (long8(r, 0x94, (uint16_t) (s + 2)) | long8(r, 0x94, (uint16_t) (s + 3)) << 8);
      dp16_set(r, 0x9C, (uint16_t) (s + 4));
      uint8_t* plane = &g_ram[(uint16_t) (r->d + 0xA6)];
      const uint8_t bytes[4] = { (uint8_t) w1, (uint8_t) (w1 >> 8), (uint8_t) w2, (uint8_t) (w2 >> 8) };
      for(int i = 0; i < 4; i++)
        for(int bit = 7; bit >= 0; bit--) {
          int p = 3 - (7 - bit) % 4;  // planes $A9, $A8, $A7, $A6 in turn
          plane[p] = (uint8_t) (plane[p] << 1 | (bytes[i] >> bit & 1));
        }
      y = dp16(r, 0x9E);
      long16_set(r, 0x98, y, dp16(r, 0xA6));
      y = (uint16_t) (y + 0x10);
      long16_set(r, 0x98, y, dp16(r, 0xA8));
      dp16_set(r, 0x9E, (uint16_t) (dp16(r, 0x9E) + 2));
      dp16_set(r, 0xA4, (uint16_t) (dp16(r, 0xA4) - 1));
    } while(dp16(r, 0xA4) != 0);
    uint16_t e = dp16(r, 0x9E);
    sum = (uint16_t) (e + 0x10);
    c = (uint32_t) e + 0x10 > 0xFFFF;
    v = (~(e ^ 0x10) & (e ^ sum) & 0x8000) != 0;
    dp16_set(r, 0x9E, sum);
    dp16_set(r, 0xA2, (uint16_t) (dp16(r, 0xA2) - 1));
  } while(dp16(r, 0xA2) != 0);
  r->a = sum;
  r->x = w1;
  r->y = y;
  r->n = false;
  r->z = true;
  r->c = c;
  r->v = v;
}
