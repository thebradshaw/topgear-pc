// Top Gear 3000: roadside scenery (bank $02). Each kind of scenery has a handler, called
// with X = its entry in the frame table at $86:AF06 and $1C = its record in the track's
// scenery list at $7E:8CB3 (byte +0: side offset in 1/16ths, +1 bits 8-10: palette,
// +2 bits 4-5: layout, +3: a phase). They place the object one or more times through the
// DSP-4 senders, stopping as soon as the sprite buffer is full (carry set).

#include "../port/port.h"

void Tg3SendObject(TgRegs* r);
void Tg3SendObjectFlat(TgRegs* r);

static uint16_t z(const TgRegs* r, uint8_t o) { return ram16((uint16_t) (r->d + o)); }
static void zs(const TgRegs* r, uint8_t o, uint16_t v) { ram16_set((uint16_t) (r->d + o), v); }
static uint16_t l7e(uint32_t a) { return ram16((a - 0x7E0000) & 0x1FFFF); }

// The side offset: the record's first byte times 16, sign-extended from 12 bits.
static uint16_t side(uint16_t rec) {
  uint16_t a = (uint16_t) ((l7e(0x7E8CB3 + rec) & 0xFF) << 4);
  return (a & 0x0800) ? (uint16_t) (a | 0xF000) : a;
}
static void palette(TgRegs* r, uint16_t rec) {
  zs(r, 0x26, (uint16_t) (((l7e(0x7E8CB4 + rec) & 0x0700) << 1) | z(r, 0x26)));
}
static void negate20(TgRegs* r) { zs(r, 0x20, (uint16_t) -z(r, 0x20)); }

// Sends with one of the two senders; true when the buffer filled.
static bool put(TgRegs* r, bool flat) {
  if(flat) Tg3SendObjectFlat(r);
  else Tg3SendObject(r);
  return r->c;
}

// $02:DA48: an object with a bobbing height (from the phase and the frame counter $0024,
// through the wave at $84:A200) and its shadow ($BC30) on the ground.
void Tg3SceneryBobbing(TgRegs* r) {
  zs(r, 0x24, tg_read16(0x86AF06 + r->x));
  uint16_t rec = z(r, 0x1C);
  zs(r, 0x20, side(rec));
  palette(r, rec);
  uint16_t t = (uint16_t) (((uint16_t) (tg_bus_read8((uint32_t) r->db << 16 | 0x0024) | tg_bus_read8((uint32_t) r->db << 16 | 0x0025) << 8) - l7e(0x7E8CB6 + rec)) & 0x3E);
  zs(r, 0x22, (uint16_t) (tg_read16(0x84A200 + t) + 0xFFC0));
  bool flat = (l7e(0x7E8CB5 + t) & 0x10) != 0;  // indexed by the wave step, as the original
  if(put(r, false)) return;
  zs(r, 0x22, 0);
  zs(r, 0x24, 0xBC30);
  put(r, flat);
}

// $02:DB87: copies mirrored across the road; the layout bits (+2 bits 4-5) pick them.
// Layouts 0 and 3: four standing, the second pair $80 further out. Layout 1: standing and
// flat at each side. Layout 2: four flat, like layout 0.
void Tg3SceneryFour(TgRegs* r) {
  zs(r, 0x24, tg_read16(0x86AF06 + r->x));
  uint16_t rec = z(r, 0x1C);
  palette(r, rec);
  uint16_t layout = (uint16_t) ((l7e(0x7E8CB5 + rec) & 0x30) >> 4);
  zs(r, 0x20, side(rec));
  zs(r, 0x22, 0);
  if(layout == 1) {
    if(put(r, false) || put(r, true)) return;
    negate20(r);
    if(put(r, false)) return;
    put(r, true);
    return;
  }
  bool flat = layout == 2;
  if(put(r, flat)) return;
  negate20(r);
  if(put(r, flat)) return;
  zs(r, 0x20, (uint16_t) (z(r, 0x20) - 0x80));
  if(put(r, flat)) return;
  negate20(r);
  put(r, flat);
}
