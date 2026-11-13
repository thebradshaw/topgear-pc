// Top Gear 3000: the race view's objects, section by section (bank $02).
//
// $02:CBD4 starts the DSP-4's object pass: the two players' markers on the course map
// (02:D8C6), then for each road section built this frame ($14: its slot, $16: the track
// position in the scenery list at $7E:8CB3, 4 bytes an entry) it places the section's
// scenery through its kind's handler (the table at $02:80CE), and every car standing in
// that section: the player's own car (with its exhaust and the gauges) or another car,
// whose sprite is picked by how far it is turned against the road. The race order
// ($1E6A list, from $28) is walked alongside, by section ($2C).

#include <stddef.h>

#include "../port/port.h"

#define REC 0xFAB4u

void Tg3SendShapeRom(TgRegs* r);
void Tg3DrawBar(TgRegs* r);

static uint16_t z(const TgRegs* r, uint8_t o) { return ram16((uint16_t) (r->d + o)); }
static void zs(const TgRegs* r, uint8_t o, uint16_t v) { ram16_set((uint16_t) (r->d + o), v); }
static uint16_t w(uint16_t a) { return ram16(a); }
static void ws(uint16_t a, uint16_t v) { ram16_set(a, v); }
static uint16_t l7e(uint32_t a) { return ram16((a - 0x7E0000) & 0x1FFFF); }
static void l7e_set(uint32_t a, uint16_t v) { ram16_set((a - 0x7E0000) & 0x1FFFF, v); }
static uint16_t car(uint16_t y, uint16_t o) { return w((uint16_t) (REC + o + y)); }

static uint32_t g_bank;
static void dput(uint16_t v) { tg_bus_write16(g_bank | 0x8000, v); }
static uint16_t dget(void) { return (uint16_t) (tg_bus_read8(g_bank | 0x8000) | tg_bus_read8(g_bank | 0x8001) << 8); }
// LDA $C000 until bit 15.
static uint16_t dwait(void) {
  for(;;) {
    uint16_t s = (uint16_t) (tg_bus_read8(g_bank | 0xC000) | tg_bus_read8(g_bank | 0xC001) << 8);
    if(s & 0x8000) return s;
  }
}
static bool minus(uint16_t a, uint16_t b) { return ((uint16_t) (a - b) & 0x8000) != 0; }
static uint16_t sext8(uint16_t v) { v &= 0xFF; return (v & 0x80) ? (uint16_t) (v | 0xFF00) : v; }

// $02:D8C6: a player's marker on the course map (A: its colour, X: its place on the map,
// Y: its tile and flags, $14: its section). The chip's sprite goes after $0184.
static void map_marker(TgRegs* r, uint16_t a, uint16_t x, uint16_t y) {
  if(tg_read16(0x82DC68 + w(0x047B)) & 0x8000) {
    // Courses whose map wraps: the start's marker at either end.
    uint16_t sec = z(r, 0x14);
    if(sec != 0) x = (sec & 0x8000) ? 0 : (uint16_t) (((uint16_t) (w(0x019A) - 1) >> 2) & 0xFFFC);
  }
  r->c = false;
  uint16_t p = adc16(r, a, l7e(0x7E4F32 + x)) & 0xFF;
  dput(0x000B);
  r->c = false;
  dput(adc16(r, l7e(0x7E4F31 + x) & 0xFF, 0x00BE));
  dput(p);
  dput(y);
  // One sprite at most.
  y = w(0x0184);
  dwait();
  uint16_t v = dget();
  r->a = v;
  if(v != 0) {
    tg_bus_write16(g_bank | (uint16_t) (0x1576 + y), dget());
    r->a = dget();
    tg_bus_write16(g_bank | (uint16_t) (0x1578 + y), r->a);
    y = (uint16_t) (y + 4);
    ws(0x0184, y);
  }
  r->x = x;
  r->y = y;
  flags_nz16(r, v != 0 ? y : 0);
}

// $02:D267: the start of a car's sprite request (X: the slot after the section's, Y: the
// car): its place on the road, at its height plus $34 and across by $32. $2A: the chip's
// first answer (how far it is turned on screen).
static void car_request(TgRegs* r, uint16_t x, uint16_t y) {
  dput(l7e(0x7E386A + x));
  dput(0x9000);
  dput(car(y, 0x00));
  uint16_t s14 = z(r, 0x14);
  r->c = false;
  uint16_t a = adc16(r, sext8(w(0x0243)), 0x0033);
  r->c = false;
  a = adc16(r, a, z(r, 0x34));
  dput(a);
  dput(a);
  dput(0);
  dput(z(r, 0x32));
  r->c = false;
  uint16_t t = adc16(r, (uint16_t) ((car(y, 0x01) >> 1) & 0x7E), l7e(0x7E39EC + s14));
  dput(tg_read16(0x83ACEC + t));
  dput(0);
  zs(r, 0x2A, dget());
  r->x = y;
}

static void send_rom_shape(TgRegs* r, uint16_t x) {
  r->x = x;
  Tg3SendShapeRom(r);
}

// The section's scenery handler (X: its kind), in C when there is one. Returns carry.
static bool scenery(TgRegs* r, uint16_t kind, uint16_t site) {
  r->x = kind;
  uint16_t target = tg_read16(0x8280CE + kind);
  const TgPortFunc* fn = tg_port_find(0x020000 | target);
  if(fn != NULL && fn->call == TG_CALL_JSR) {
    fn->fn(r);
    if(!tg_port_take_declined()) return r->c;
  }
  tg_call_jsr(r, target, (uint16_t) (site + 3), TG_MX_16);
  return r->c;
}

void Tg3SceneryLoop(TgRegs* r) {
  r->db = 0x30;  // PEA $8330; PLB
  g_bank = 0x300000;
  ws(0x0184, w(0x0186));
  dput(3);
  zs(r, 0x1A, (uint16_t) (z(r, 0x8E) + 0x200));
  r->c = false;
  adc16(r, z(r, 0x8E), 0x200);
  zs(r, 0x3C, z(r, 0xC8) == 0 ? 0x1000 : 0);
  dwait();
  dput(5);
  // The first car in the order at or behind the view's place: $28.
  uint16_t y = 0x28, x = w(0x1E90);
  r->c = w(0x0225) >= car(x, 0x01);
  sbc16(r, w(0x0227), car(x, 0x03));
  if(!r->c) {
    for(y = 0x26; y != 0; y = (uint16_t) (y - 2)) {
      x = w((uint16_t) (0x1E68 + y));
      r->c = w(0x0225) >= car(x, 0x01);
      sbc16(r, w(0x0227), car(x, 0x03));
      if(r->c) break;
    }
  }
  zs(r, 0x28, y);
  zs(r, 0x2C, w(0x0226));
  dwait();
  // The two players' markers on the course map.
  x = (uint16_t) ((w(0x0226) >> 2) & 0xFFFC);
  if(w(0x1E66) & 0x8000) x = (uint16_t) (x + 2);
  zs(r, 0x14, w(0x0235));
  map_marker(r, 0x1E, x, l7e(0x7EFAD1) | 0x30FD);
  x = (uint16_t) ((w(0x02D9) >> 2) & 0xFFFC);
  if(w(0x1E68) & 0x8000) x = (uint16_t) (x + 2);
  zs(r, 0x14, w(0x02E8));
  map_marker(r, 0x1E, x, l7e(0x7EFAD1 + 0x35) | 0x30FD);
  static const uint16_t kStart[] = { 9, 0x80, 0xA9, 0, 0, 0x100, 4 };
  for(unsigned i = 0; i < sizeof(kStart) / sizeof(kStart[0]); i++) dput(kStart[i]);
  uint16_t h = w(0x1EF2);
  dput((h & 0x8000) ? (uint16_t) (h + 0xDC) : 0xDC);
  if(h & 0x8000) {
    r->c = false;
    adc16(r, h, 0xDC);
  }

  // $CDB7: the sections.
  zs(r, 0x14, 0);
  zs(r, 0x16, (uint16_t) (w(0x0226) << 2));
  zs(r, 0x18, (uint16_t) (w(0x019A) << 2));
  zs(r, 0x30, (uint16_t) (w(0x1EDE) << 2));
  r->c = false;
  x = adc16(r, w(0x019C), w(0x1E5E));
  r->c = false;
  zs(r, 0x2E, (uint16_t) (adc16(r, tg_read16(0x840000 + x), 0x003F) << 2));
  uint16_t a = (uint16_t) (tg_read16(0x840010 + x) << 2);
  if(w(0x1E66) & 0x8000) a = 0;
  zs(r, 0x36, a);
  ws(0x1DF6, 0);
  ws(0x1DF8, 0);
  r->c = true;
  l7e_set(0x7E34EC, sbc16(r, l7e(0x7E34EE), w(0x020C)));
  r->c = true;
  l7e_set(0x7E36EC, adc16(r, (uint16_t) ~sext8(w(0x0209)), l7e(0x7E36EE)));
  r->c = false;
  l7e_set(0x7E356C, adc16(r, l7e(0x7E34EC), l7e(0x7E356C)));
  r->c = false;
  l7e_set(0x7E376C, adc16(r, l7e(0x7E36EC), l7e(0x7E376C)));
  l7e_set(0x7E386C, 0x0033);
  r->c = true;
  l7e_set(0x7E39EC, sbc16(r, l7e(0x7E39EE), 0x0080));
  goto cars;

section:
  // $CE4E: the section's scenery.
  x = z(r, 0x16);
  r->c = false;
  a = adc16(r, x, 4);
  r->c = a >= z(r, 0x18);
  if(r->c) a = sbc16(r, a, z(r, 0x18));
  if(a == z(r, 0x30)) {
    // The lap's join: the scenery list goes on from the start.
    zs(r, 0x16, a);
    r->c = false;
    zs(r, 0x2C, adc16(r, w(0x1EE0), z(r, 0x2C)));
    uint16_t j = w(0x1EE0);
    r->c = (j >> 14) & 1;
    a = adc16(r, (uint16_t) (j << 2), z(r, 0x16));
  }
  if(a == z(r, 0x2E)) {
    r->c = false;
    a = adc16(r, a, z(r, 0x36));
    r->c = false;
    zs(r, 0x2C, adc16(r, (uint16_t) (z(r, 0x36) >> 2), z(r, 0x2C)));
  }
  zs(r, 0x16, a);
  {
    uint16_t entry = l7e(0x7E8CB3 + x);
    if(entry == 0) {
      // Nothing here: a blank request keeps the chip in step.
      dput(l7e(0x7E386C + z(r, 0x14)));
      dput(0);
    } else {
      zs(r, 0x1C, x);
      uint16_t kind = (uint16_t) (((entry >> 8) & 0x7F) << 1);
      zs(r, 0x1E, kind);
      zs(r, 0x3A, z(r, 0x3C));
      uint16_t far = l7e(0x7E39EC + z(r, 0x14));
      uint16_t site;
      if(far < 0x0082) {
        // Close: the near palette, and remembered for the overlay.
        zs(r, 0x26, l7e(0x7E45EC));
        ws(0x1DF6, kind);
        site = 0xCEE7;
        r->c = false;
      } else if(far >= 0x0442) {
        zs(r, 0x26, l7e(0x7E495C + tg_read16(0x86AD66 + kind)));
        site = 0xCED0;
        r->c = true;
      } else {
        zs(r, 0x26, l7e(0x7E45EC + tg_read16(0x86AD66 + kind)));
        site = 0xCEB7;
        r->c = false;
      }
      if(scenery(r, kind, site)) goto done;
    }
  }

cars:
  // $CEFD: the cars standing in this section, in race order.
  for(;;) {
    x = w((uint16_t) (0x1E6A + z(r, 0x28)));
    a = car(x, 0x02);
    if(a == z(r, 0x2C)) break;
    if(!minus(a, z(r, 0x2C))) goto next_section;
    zs(r, 0x28, (uint16_t) (z(r, 0x28) + 2));
  }
car_here:
  // $CF1A: car x (record offset) is in this section.
  y = x;
  r->y = y;
  x = (uint16_t) (z(r, 0x14) + 2);
  if(x == z(r, 0x90)) goto next_section;
  if(y != 0 || (w(0x0244) & 1)) goto other_car;
  {
    // The player's car: the exhaust flame, the car, its shadow and the gauge.
    uint16_t gear = (uint16_t) ((w(0x023A) >> 8) & 0xFF);
    if(w(0x021A) == 0) {
      ws(0x1F24, w(0x1F18));
      if(!(w(0x1F18) & 0x8000)) {
        zs(r, 0x32, w(0x1F1C));
        zs(r, 0x34, w(0x1F20));
        car_request(r, x, y);
        dput(car(r->x, 0x0B));
        r->c = false;
        a = adc16(r, z(r, 0x3C), w(0x1F34));
        r->c = false;
        dput(adc16(r, a, 0x0138));
        ws(0x1F24, w(0x1F18) & 0xFFF8);
        send_rom_shape(r, tg_read16(0x86C8B2 + w(0x1F24)));
        r->x = x;
        r->y = y;
      }
    }
    if(w(0x021D) & 0x8000) {
      zs(r, 0x32, 0);
      zs(r, 0x34, 0);
      car_request(r, x, y);
      dput(car(r->x, 0x0B));
      r->c = false;
      dput(adc16(r, z(r, 0x3C), 0x0F00));
      send_rom_shape(r, tg_read16(0x86CBEC + (uint16_t) (gear << 1)));
      r->x = x;
      r->y = y;
    }
    zs(r, 0x32, 0);
    zs(r, 0x34, 0);
    car_request(r, x, y);
    dput(car(r->x, 0x0B));
    r->c = false;
    dput(adc16(r, z(r, 0x3C), 0x0F00));
    send_rom_shape(r, tg_read16(0x86C97A + (uint16_t) (gear << 3)));
    r->x = x;
    r->y = y;
    car_request(r, x, y);
    dput(0);
    dput(z(r, 0x3C));
    uint16_t i = (uint16_t) (gear << 3);
    r->x = tg_read16(0x86CC66 + i);
    r->y = tg_read16(0x86CC68 + i);
    r->a = tg_read16(0x86CC64 + i);
    Tg3DrawBar(r);
    if(w(0x014C) == 2) {
      dput(l7e(0x7E386A + z(r, 0x14)));
      static const uint16_t kRest[] = { 0x9000, 0, 0x33, 0x33, 0, 0, 0x7FFF };
      for(unsigned k = 0; k < sizeof(kRest) / sizeof(kRest[0]); k++) dput(kRest[k]);
      dput(w(0x028B));
      dget();
      dput(w(0x028D));
      dput(z(r, 0x3C));
      send_rom_shape(r, 0xAD4A);
    }
    ws(0x0281, 0xFFFF);
    r->c = false;
    goto after_car;
  }

other_car:
  // $D062: another car. The first one seen is remembered (its far distance in $0293).
  if(w(0x0281) & 0x8000) {
    ws(0x0281, y);
    ws(0x0283, x);
    r->c = false;
    uint16_t t = adc16(r, (uint16_t) ((car(y, 0x01) >> 1) & 0x7E), l7e(0x7E39EC + z(r, 0x14)));
    ws(0x0293, tg_read16(0x83ACEC + t));
    x = w(0x0283);
  }
  {
    dput(l7e(0x7E386A + x));
    dput(0x9000);
    dput(car(y, 0x00));
    uint16_t s = z(r, 0x14), d;
    bool jumping = (car(y, 0x2A) & 0x22) == 0x20;
    if(jumping) {
      dput(l7e(0x7E376E + s));
      dput(l7e(0x7E376C + s));
      dput(l7e(0x7E356E + s));
      dput(l7e(0x7E356C + s));
    } else {
      dput(l7e(0x7E36EE + s));
      dput(l7e(0x7E36EC + s));
      dput(l7e(0x7E34EE + s));
      dput(l7e(0x7E34EC + s));
    }
    r->c = false;
    uint16_t t = adc16(r, (uint16_t) ((car(y, 0x01) >> 1) & 0x7E), l7e(0x7E39EC + s));
    dput(tg_read16(0x83ACEC + t));
    dput(car(y, 0x07));
    zs(r, 0x2A, dget());
    dput(car(y, 0x0B));
    r->c = true;
    if(jumping) {
      dput(0x1000 | car(y, 0x1D));
      d = sbc16(r, l7e(0x7E356C + s), l7e(0x7E356E + s));
    } else {
      dput(z(r, 0x3C) | car(y, 0x1D));
      d = sbc16(r, l7e(0x7E34EC + s), l7e(0x7E34EE + s));
    }
    // How far it is turned against the road picks one of seven views.
    r->c = true;
    a = adc16(r, (uint16_t) ~(uint16_t) (d << 1), z(r, 0x2A));
    static const uint16_t kTurn[6] = { 0xFFA0, 0xFFC0, 0xFFE8, 0x0018, 0x0040, 0x0060 };
    uint16_t v = 0;
    while(v < 12 && !minus(a, kTurn[v >> 1])) v = (uint16_t) (v + 2);
    a = tg_read16(0x7F0000 + v);
    r->c = false;
    a = adc16(r, a, tg_read16(0x849180 + t));
    send_rom_shape(r, tg_read16(0x7F0000 + a));
  }

after_car:
  // $D1B5
  if(r->c) goto done;
  zs(r, 0x28, (uint16_t) (z(r, 0x28) + 2));
  x = w((uint16_t) (0x1E6A + z(r, 0x28)));
  if(car(x, 0x02) == z(r, 0x2C)) goto car_here;

next_section:
  // $D1CD
  zs(r, 0x2C, (uint16_t) (z(r, 0x2C) + 1));
  if(z(r, 0x2C) == w(0x019A)) {
    zs(r, 0x2C, 0);
    zs(r, 0x28, 0);
  }
  zs(r, 0x14, (uint16_t) (z(r, 0x14) + 2));
  if(z(r, 0x14) != z(r, 0x90)) goto section;
  r->c = true;

done:
  // $D1E7: the end of the pass, and the near scenery's overlay.
  dput(0);
  dput(0x8000);
  ws(0x1E02, 0);
  x = w(0x1DF6);
  if(x != 0) {
    a = tg_read16(0x86AE36 + x);
    if(a != 0) {
      zs(r, 0x14, a);
      ws(0x1E04, 0x007F);
      uint16_t m = tg_read16(0x848000 + l7e(0x7E39EE));
      r->c = (m & 0x8000) != 0;
      uint16_t t = adc16(r, (uint16_t) (m << 1), a);
      ws(0x1DFE, tg_read16(0x860006 + t));
      ws(0x1E02, tg_read16(0x860008 + t));
      x = tg_read16(0x860000 + a);
      r->c = false;
      ws(0x1DFE, adc16(r, l7e(0x7E6591 + x), w(0x1DFE)));
      ws(0x1E00, (uint16_t) (l7e(0x7E45EC) << 4));
    }
  }
  ws(0x0186, w(0x0184));
  uint16_t i = (uint16_t) (((w(0x023A) >> 8) & 0xFF) << 3);
  ws(0x1E52, tg_read16(0x86C974 + i));
  ws(0x1E54, tg_read16(0x86C976 + i));
  ws(0x1E56, tg_read16(0x86C978 + i));
  r->a = w(0x1E56);
  r->c = false;  // from ASL of the gear
  r->x = i;
  r->db = 0x83;
  r->n = true;  // PLB of $83
  r->z = false;
}
