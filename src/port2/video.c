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
