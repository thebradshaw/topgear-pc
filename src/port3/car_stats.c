// Top Gear 3000: the car's handling figures for its parts (bank $03).
//
// $03:EA9B copies six figures from the tables in bank $86, by X (the car and its parts),
// into $1EA0..$1EAC. A is left with the first ($1EA0), which callers store too.

#include <stddef.h>

#include "../port/port.h"

void Tg3CarStats(TgRegs* r) {
  static const struct { uint32_t table; uint16_t dst; } k[] = {
    { 0x86DB47, 0x1EA2 }, { 0x86DB99, 0x1EA4 }, { 0x86DBEB, 0x1EA6 },
    { 0x86DC3D, 0x1EAA }, { 0x86DC8F, 0x1EAC }, { 0x86DAF5, 0x1EA0 },
  };
  uint8_t db = r->db;
  if(!(db < 0x40 || db == 0x7E || (db >= 0x80 && db < 0xC0))) {
    tg_port_decline();
    return;
  }
  uint16_t a = 0;
  for(size_t i = 0; i < 6; i++) {
    a = tg_read16(k[i].table + r->x);
    ram16_set(k[i].dst, a);
  }
  r->a = a;
  r->n = (a & 0x8000) != 0;
  r->z = a == 0;
}
