// Top Gear 3000: sending race objects to the DSP-4 chip (bank $02) and collecting the
// sprite pieces it sends back.
//
// The DSP-4 projects each object (a car, a roadside sign) for the view: the CPU writes the
// object's position and its shape (a list of pieces from the ROM), and the chip answers with
// the screen position and tile of each piece that shows, two words at a time, which are
// collected in the sprite buffer at $1574. The chip's data register is $8000 and its status
// register $C000 in the data bank the callers set ($30-$3F).

#include "../port/port.h"

static uint16_t dp(const TgRegs* r, uint8_t o) { return ram16((uint16_t) (r->d + o)); }
static void dp_set(const TgRegs* r, uint8_t o, uint16_t v) { ram16_set((uint16_t) (r->d + o), v); }

static uint32_t g_dr;  // the chip's data register in the caller's data bank

static void dsp_put(uint16_t v) { tg_bus_write16(g_dr, v); }
static uint16_t dsp_get(void) { return (uint16_t) (tg_bus_read8(g_dr) | tg_bus_read8(g_dr + 1) << 8); }
// The original waits for the chip's request bit (status bit 15) before reading.
static void dsp_wait(void) {
  while(!(tg_bus_read8(g_dr + 0x4001) & 0x80)) {}
}

// Writes a word to the sprite buffer through the data bank (low RAM).
static void buf_set(uint8_t db, uint16_t y, uint16_t v) { tg_bus_write16((uint32_t) db << 16 | (uint16_t) (0x1574 + y), v); }

// One run of the shape list: each entry (three words) is sent, and the pieces the chip
// answers with are collected until it sends 0. A zero entry ends the run. Returns false
// when the buffer filled up (at the limit in $1A): the rest of the answer is read and
// dropped.
static bool send_run(TgRegs* r, uint32_t list, uint16_t* x, uint16_t* y) {
  uint16_t w = tg_read16(list + *x);
  dsp_put(w);
  while(w != 0) {
    dsp_put(tg_read16(list + (uint16_t) (*x + 2)));
    dsp_put(tg_read16(list + (uint16_t) (*x + 4)));
    dsp_wait();
    while(dsp_get() != 0) {
      *y = (uint16_t) (*y + 2);
      buf_set(r->db, *y, dsp_get());
      *y = (uint16_t) (*y + 2);
      buf_set(r->db, *y, dsp_get());
      if(*y == dp(r, 0x1A)) return false;
      dsp_wait();
    }
    r->v = *x < 0x8000 && (uint16_t) (*x + 6) >= 0x8000;  // TXA; CLC; ADC #6
    *x = (uint16_t) (*x + 6);
    w = tg_read16(list + *x);
    dsp_put(w);
  }
  return true;
}

// Drains what the chip still has to say after the buffer filled.
static void drain(void) {
  for(;;) {
    dsp_wait();
    if(dsp_get() == 0) return;
    dsp_get();
    dsp_get();
  }
}

// The two runs of a shape (the near pieces, then the far ones), from the list at
// list + X; the pieces go after Y ($84) in the buffer. Carry set: the buffer is full.
static void send_shape(TgRegs* r, uint32_t list) {
  g_dr = (uint32_t) r->db << 16 | 0x8000;
  uint16_t x = r->x, y = dp(r, 0x84);
  if(!send_run(r, list, &x, &y)) {
    drain();
    dsp_put(0);
    dsp_put(0);
    r->c = true;
  } else {
    x = (uint16_t) (x + 2);
    if(!send_run(r, list, &x, &y)) {
      drain();
      dsp_put(0);
      r->c = true;
    } else {
      r->c = false;
    }
  }
  dp_set(r, 0x84, y);
  r->x = x;
  r->y = y;
  r->a = 0;
  r->n = false;
  r->z = true;
}

// $02:DDBF: a shape from the list in bank $7F (X: its offset there).
void Tg3SendShapeRam(TgRegs* r) { send_shape(r, 0x7F0000); }

// $02:DEA8: the same for a shape from the ROM (bank $86).
void Tg3SendShapeRom(TgRegs* r) { send_shape(r, 0x860000); }

// $02:D934: an object: its position and size ($14: the object's slot in the tables at
// $7E35EC-$7E39EC), the view offsets ($20, $22), its flags ($26 | $3A), then its shape.
void Tg3SendObject(TgRegs* r) {
  uint16_t x = dp(r, 0x14);
  g_dr = (uint32_t) r->db << 16 | 0x8000;
  dsp_put(tg_read16(0x7E386C + x));
  dsp_put(tg_read16(0x7E38EC + x));
  dsp_put(tg_read16(0x7E35EC + x));
  dsp_put(tg_read16(0x7E37EC + x));
  dsp_put(dp(r, 0x20));
  dsp_put(dp(r, 0x22));
  dsp_put(dp(r, 0x26) | dp(r, 0x3A));
  // The shape: frame $24's entry, through the object's model table.
  uint16_t frame = dp(r, 0x24);
  uint16_t base = tg_read16(0x7E6591 + tg_read16(0x860000 + frame));
  tg_push16(base);
  uint16_t model = tg_read16(0x848000 + tg_read16(0x7E39EC + x));
  uint16_t i = (uint16_t) (model + frame);
  base = tg_pull16();
  r->c = false;
  r->x = adc16(r, base, tg_read16(0x860002 + i));
  send_shape(r, 0x7F0000);
}
