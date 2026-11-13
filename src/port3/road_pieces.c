// Top Gear 3000: moving the camera on through the road's pieces (bank $02).
//
// $02:F2B3 first handles a pending jump ahead: once the camera's step ($0226) reaches the
// step it was told to jump at ($1EDE), it moves on by $1EE0 steps, rebuilds the edge at
// the new step in the table at $7E:3DEC and the scroll at $0202/$0206/$020C. Then it walks
// the track's list of pieces in bank $84 (20 bytes each, from $019C): every piece the camera
// has come within $40 steps of ($1E5E) runs its handler (from the table at $02:F374), and
// every piece it has passed ($1E60) sets bit 1 of $2A.

#include <stddef.h>

#include "../port/port.h"

static uint16_t w(uint16_t a) { return ram16(a); }
static void ws(uint16_t a, uint16_t v) { ram16_set(a, v); }
static uint16_t m(uint32_t a) { return ram16(a & 0x1FFFF); }
static void ms(uint32_t a, uint16_t v) { ram16_set(a & 0x1FFFF, v); }
static uint16_t piece(uint16_t x, uint16_t o) { return tg_read16(0x840000u + (uint16_t) (x + o)); }

// The step the camera is on, against the piece's start: lda $0226; clc; adc #2; sbc $84:0000,x.
static uint16_t ahead(uint16_t x, bool* carry, bool* over) {
  uint32_t s = (uint32_t) w(0x0226) + 2;
  uint32_t c = s >> 16;
  uint16_t p = piece(x, 0);
  uint32_t d = (uint32_t) (uint16_t) s - p - (1 - c);
  *carry = (d & 0x10000) == 0;
  *over = (((uint16_t) s ^ p) & ((uint16_t) s ^ (uint16_t) d) & 0x8000) != 0;
  return (uint16_t) d;
}

// $02:F381: a piece that starts a jump, or that moves the camera on by $1EE6.
static void piece_jump(TgRegs* r) {
  r->x = r->y;
  if(w(0x1E66) & 0x8000) {
    uint32_t lo = (uint32_t) w(0x0204) + w(0x1EC2);
    ws(0x0204, (uint16_t) lo);
    ws(0x0206, (uint16_t) (w(0x0206) + w(0x1EC4) + (lo >> 16)));
    uint32_t s = (uint32_t) piece(r->x, 0) + 0x3F;
    ws(0x1EDE, (uint16_t) (s + piece(r->x, 0x10) + (s >> 16)));
    ws(0x1EE0, piece(r->x, 0x12));
    ws(0x020A, 0);
    ws(0x020C, 0);
  } else {
    ws(0x0226, (uint16_t) (w(0x1EE6) + w(0x0226)));
    ws(0x023E, (uint16_t) (w(0x1EE6) + w(0x023E)));
    ws(0x1EE6, 0);
  }
}

static void run_piece(TgRegs* r) {
  r->y = r->x;
  r->x = piece(r->x, 2) & 0xFF;
  uint16_t target = tg_read16(0x02F374u + r->x);
  switch(target) {
    case 0xF380: break;
    case 0xF381: piece_jump(r); break;
    case 0xF3D0: ws(0x1E66, 0); break;
    default: tg_call_jsr(r, target, 0xF341, TG_MX_16); break;
  }
}

void Tg3RoadPieces(TgRegs* r) {
  uint8_t db = r->db;
  if(!(db < 0x40 || db == 0x7E || (db >= 0x80 && db < 0xC0))) {
    tg_port_decline();
    return;
  }
  uint16_t d = r->d;
  ram16_set((uint16_t) (d + 0x2A), 0);
  if(w(0x0226) >= w(0x1EDE)) {
    ws(0x0226, (uint16_t) (w(0x1EE0) + w(0x0226)));
    ws(0x023E, (uint16_t) (w(0x1EE0) + w(0x023E)));
    uint32_t x = 0x7E0000u + ((w(0x1EDE) << 1) & 0x3E);
    uint16_t t = (uint16_t) (m(x + 2) - m(x + 4));
    uint16_t a = (uint16_t) (t + m(x + 2));
    ms(x, a);
    ms(x + 0x40, a);
    a = (uint16_t) ((uint16_t) -a + w(0x0206));
    ws(0x0206, a);
    ws(0x0202, a);
    ws(0x020C, t);
    ws(0x1EC2, 0);
    ws(0x1EC4, m(x));
    ws(0x1EE0, 0);
    ws(0x1EDE, 0x7FFF);
  }
  bool c, v;
  for(;;) {
    r->x = (uint16_t) (w(0x019C) + w(0x1E5E));
    uint16_t a = ahead(r->x, &c, &v);
    if((uint16_t) (a - 0x40) & 0x8000) {
      r->x = (uint16_t) (w(0x019C) + w(0x1E60));
      a = ahead(r->x, &c, &v);
      if(a & 0x8000) {
        r->a = a;
        break;
      }
      ws(0x1E60, (uint16_t) (w(0x1E60) + 0x14));
      ram16_set((uint16_t) (d + 0x2A), ram16((uint16_t) (d + 0x2A)) | 2);
    } else {
      run_piece(r);
      ws(0x1E5E, (uint16_t) (w(0x1E5E) + 0x14));
    }
  }
  r->db = db;
  r->n = true;
  r->z = false;
  r->c = c;
  r->v = v;
}
