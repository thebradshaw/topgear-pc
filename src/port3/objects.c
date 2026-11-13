// Top Gear 3000: moving the race objects (bank $01). Each object has a record in bank $7E
// at $FAB4 + Y, $35 bytes long; the code runs with DB = $7E and D = $0100. Products go
// through the DSP-4's multiply command (command 0 at $30:8000: two factors in, a 32-bit
// product out, low word first).
//
// Record fields used here (offsets from $FAB4):
//   +$01 (32 bits)  position along the track (+$02: its section number)
//   +$05 (32 bits)  side position (+$07: its whole part)
//   +$13            next object in the queue ($FAB4 + that: its record)
//   +$15 (32 bits)  speed (+$17: whole part)    +$19  turning   +$1D  speed it wants
//   +$1F  grip      +$23  the road it follows (side limits)      +$27  steering
//   +$2A  flags (bit 6: road segment known, 4/5: lane picked, 7: changed segment)
//   +$2C  the segment list pointer (bank $84, $14 bytes each)   +$2E/+$30  a jump ahead
//   +$32  boost left   +$34  slowing down left (8 a frame)    +$35  extra speed

#include "../port/port.h"

#define REC 0xFAB4u

static uint16_t w(uint16_t a) { return ram16(a); }
static void ws(uint16_t a, uint16_t v) { ram16_set(a, v); }
static uint16_t f(uint16_t y, uint16_t o) { return w((uint16_t) (REC + o + y)); }
static void fs(uint16_t y, uint16_t o, uint16_t v) { ws((uint16_t) (REC + o + y), v); }

// DSP-4 multiply.
static void dput(uint16_t v) { tg_bus_write16(0x308000, v); }
static uint16_t dget(void) { return (uint16_t) (tg_bus_read8(0x308000) | tg_bus_read8(0x308001) << 8); }
static void mul(uint16_t a, uint16_t b) {
  dput(0);
  dput(a);
  dput(b);
}

// adds the 32-bit product just made to the field (ADC pair).
static void add_product(TgRegs* r, uint16_t y, uint16_t o) {
  r->c = false;
  fs(y, o, adc16(r, f(y, o), dget()));
  fs(y, (uint16_t) (o + 2), adc16(r, f(y, (uint16_t) (o + 2)), dget()));
}

// $01:FBB1: finds the road segment the object is on, moving its pointer (+$2C) along the
// list in bank $84 while its section (+$02) is $3F or more past the segment's start. The
// first time (flag bit 6 clear) that is all; after that a new segment also picks a lane
// at random and adds the segment's jump (+$10) to the section. When the section reaches
// +$2E, it jumps by +$30.
void Tg3ObjectSegment(TgRegs* r) {
  if(r->db != 0x7E) {
    tg_port_decline();
    return;
  }
  uint16_t y = r->y;
  uint16_t a = f(y, 0x2A);
  if(!(a & 0x40)) {
    fs(y, 0x2A, a | 0x40);
    for(;;) {
      uint16_t x = f(y, 0x2C);
      r->x = x;
      r->c = true;
      a = sbc16(r, f(y, 0x02), rom16(0x840000 + x));
      r->a = a;
      if(!r->c) return;
      cmp16(r, a, 0x003F);
      if(!r->c) return;
      r->c = false;
      a = adc16(r, x, 0x0014);
      fs(y, 0x2C, a);
      r->a = a;
    }
  }
  uint16_t x = f(y, 0x2C);
  r->x = x;
  r->c = true;
  a = sbc16(r, f(y, 0x02), rom16(0x840000 + x));
  if(r->c) {
    uint16_t past = a;
    uint16_t flags = f(y, 0x2A);
    if(!(flags & 0x10)) {
      fs(y, 0x2A, flags & 0xFFDF);
      r->a = flags & 0xFFDF;
      tg_call_jsl(r, 0x8087DF, 0xFC02, TG_MX_16);  // a random number
      fs(y, 0x2A, (r->a & 0x20) | f(y, 0x2A) | 0x10);
    }
    cmp16(r, past, 0x003F);
    if(r->c) {
      r->c = false;
      a = adc16(r, x, 0x0014);
      fs(y, 0x2C, a);
      a = f(y, 0x2A);
      r->a = a;
      if(a & 0x20) {
        r->n = (a & 0x8000) != 0;  // LDA, then BIT #$20
        r->z = false;
        return;
      }
      r->c = false;
      fs(y, 0x02, adc16(r, rom16(0x840010 + x), f(y, 0x02)));
      a = (uint16_t) (0x0080 | f(y, 0x2A));
      fs(y, 0x2A, a);
      flags_nz16(r, a);
      r->a = a;
      return;
    }
  }
  a = f(y, 0x02);
  cmp16(r, a, f(y, 0x2E));
  r->a = a;
  if(!r->c) return;
  r->c = false;
  fs(y, 0x02, adc16(r, a, f(y, 0x30)));
  fs(y, 0x2E, 0x7000);
  a = (uint16_t) (0x0080 | f(y, 0x2A));
  fs(y, 0x2A, a);
  flags_nz16(r, a);
  r->a = a;
}

// Turns a speed share into the change of a value: the product of the share (clamped to
// the limit at +$25) and the frame time $1E9E, added (or taken away) at +$15.
static void steer(TgRegs* r, uint16_t y, uint16_t amount, bool away) {
  mul(amount, w(0x1E9E));
  r->c = away;
  if(away) {
    fs(y, 0x15, sbc16(r, f(y, 0x15), dget()));
    uint16_t a = sbc16(r, f(y, 0x17), dget());
    fs(y, 0x17, a);
    r->a = a;
  } else {
    fs(y, 0x15, adc16(r, f(y, 0x15), dget()));
    uint16_t a = adc16(r, f(y, 0x17), dget());
    fs(y, 0x17, a);
    r->a = a;
  }
}

// $01:F887: a computer car's move for the frame (Y: its record). It goes forward by its
// speed times the frame time, finds its road segment, moves across by its steering, keeps
// inside the road (bouncing its steering back), and its speed closes on the speed it wants
// within its grip; a boost adds to it while it lasts.
void Tg3MoveObject(TgRegs* r) {
  if(r->db != 0x7E) {
    tg_port_decline();
    return;
  }
  uint16_t y = r->y;
  fs(y, 0x13, 0xFFFF);
  uint16_t a = w(0x1ECE);
  if(a != 0) {  // paused
    r->a = a;
    flags_nz16(r, a);
    return;
  }
  // Forward: the 32-bit position at +$01 grows by speed x time.
  mul(f(y, 0x17), w(0x1E9E));
  add_product(r, y, 0x01);
  r->y = y;
  Tg3ObjectSegment(r);
  // Across: the speed times the time (in 1/256ths) times the steering.
  mul(f(y, 0x17), (uint16_t) (w(0x1E9E) << 8 | w(0x1E9E) >> 8));
  uint16_t d = r->d;
  ws((uint16_t) (d + 0x14), dget());
  ws((uint16_t) (d + 0x16), dget());
  mul(f(y, 0x27), w((uint16_t) (d + 0x15)));
  fs(y, 0x05, adc16(r, f(y, 0x05), dget()));  // carry from the segment search
  fs(y, 0x07, adc16(r, f(y, 0x07), dget()));
  // The road's edges where the object is: command $0A with its section and fraction, and
  // the road's two points from $7E:D1B3.
  dput(0x000A);
  uint16_t sec = f(y, 0x02);
  r->c = (sec & 0x8000) != 0;
  uint16_t x = (uint16_t) (sec << 1);
  dput((uint16_t) ((f(y, 0x00) & 0xFF00) >> 1));
  dput(ram16(0xD1B3u + x));
  dput(ram16(0xD1B5u + x));
  ws((uint16_t) (d + 0x18), dget());
  ws((uint16_t) (d + 0x1A), dget());
  ws((uint16_t) (d + 0x1C), dget());
  ws((uint16_t) (d + 0x1E), dget());
  // Kept between the edges, its steering turned back inwards.
  a = f(y, 0x07);
  cmp16(r, a, w((uint16_t) (d + 0x1A)));
  if(!r->n) {
    r->c = true;
    fs(y, 0x07, sbc16(r, f(y, 0x07), w(0x1E9E)));
    a = f(y, 0x27);
    r->n = (a & 0x8000) != 0;
    if(!r->n) {
      a = (uint16_t) (-a);
      fs(y, 0x27, a);
    }
  } else {
    cmp16(r, a, w((uint16_t) (d + 0x18)));
    if(r->n) {
      r->c = false;
      fs(y, 0x07, adc16(r, f(y, 0x07), w(0x1E9E)));
      a = f(y, 0x27);
      if(a & 0x8000) {
        a = (uint16_t) (-a);
        fs(y, 0x27, a);
      }
    }
  }
  // Slowing down (+$34) runs out at 8 a frame.
  a = f(y, 0x34);
  if(a != 0) {
    r->c = true;
    a = adc16(r, (uint16_t) ~(uint16_t) (w(0x1E9E) << 3), f(y, 0x34));
    if(a & 0x8000) a = 0;
    fs(y, 0x34, a);
  }
  // Speed: towards the speed it wants (+$1D, +$35 more), unless it follows the car ahead
  // in the queue at the same section.
  x = f(y, 0x19);
  r->x = x;
  a = w((uint16_t) (REC + 0x02 + x));
  cmp16(r, a, f(y, 0x02));
  if(!r->z) {
    r->c = true;
    uint16_t lo = sbc16(r, 0, f(y, 0x15));
    ws((uint16_t) (d + 0x14), lo);
    r->c = false;
    a = adc16(r, f(y, 0x35) & 0x00FF, f(y, 0x23));
    uint16_t hi = sbc16(r, a, f(y, 0x17));
    ws((uint16_t) (d + 0x16), hi);
    if(hi & 0x8000) {
      // Too fast: slow down by the grip (+$25) at most.
      r->c = true;
      lo = adc16(r, (uint16_t) ~lo, 0);
      ws((uint16_t) (d + 0x14), lo);
      hi = adc16(r, (uint16_t) ~hi, 0);
      ws((uint16_t) (d + 0x16), hi);
      uint16_t amount = hi != 0 ? f(y, 0x25) : (lo < f(y, 0x25) ? lo : f(y, 0x25));
      steer(r, y, amount, true);
      return;
    }
    uint16_t amount = hi != 0 ? f(y, 0x25) : (lo < f(y, 0x25) ? lo : f(y, 0x25));
    steer(r, y, amount, false);
  }
  // A boost (+$32) adds while it lasts.
  a = f(y, 0x32);
  r->a = a;
  if(a == 0) {
    r->n = false;
    r->z = true;
    return;
  }
  steer(r, y, a, false);
  r->c = true;
  a = sbc16(r, f(y, 0x32), w(0x1E9E));
  if(a & 0x8000) {
    a = 0;
    r->z = true;
    r->n = false;
  }
  fs(y, 0x32, a);
  r->a = a;
}
