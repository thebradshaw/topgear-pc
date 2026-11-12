// Top Gear 2: the menus' flow (bank $1F). From power-on and after each race, the front end
// is set up, the menu screens run, and the next race starts. The screens wait for the NMI,
// so this runs as a fiber (TG_WAITS); the screens not yet in C run as the original, called
// from here. The NMI ($1F:9194) and the two players' threads it switches between stay the
// original.

#include "../port/port.h"

void Tg2ClearVram(TgRegs* r);
void Tg2FrontEndInit(TgRegs* r);
void Tg2LoadSave(TgRegs* r);
void Tg2MenuMusic(TgRegs* r);
void Tg2RestoreStandings(TgRegs* r);
void Tg2KeepStandings(TgRegs* r);
void Tg2LoadRecords(TgRegs* r);
void Tg2SaveRecords(TgRegs* r);

static uint16_t w(uint16_t adr) { return ram16(adr); }
static void ws(uint16_t adr, uint16_t v) { ram16_set(adr, v); }

static void regs(TgRegs* r) {
  r->d = 0x0100;
  r->db = 0x9F;
}

static void call(TgRegs* r, uint16_t target, uint16_t ret) {
  regs(r);
  tg_call_jsr(r, target, ret, TG_MX_16);
  regs(r);
}

// The front end's video set up (the stack is already at $1FFF and D = $0100).
static void front_end(TgRegs* r, uint16_t sync, uint16_t sync_end) {
  regs(r);
  ws(0x0025, 0x0005);
  Tg2ClearVram(r);
  Tg2FrontEndInit(r);
  // Waits on the IRQ timer flag either side of the PPU reset: the time is what matters.
  tg_call_jmp(r, sync, sync_end, TG_MX_16);
  regs(r);
}

// $8114 on: the race's set up screens, then the music and on to the race.
static void to_race(TgRegs* r, bool set_up) {
  if(set_up) {
    call(r, 0xB265, 0x8117);
    call(r, 0x8296, 0x811A);
  }
  ws(0x1DED, 0x809B);  // where the race comes back to
  ws(0x1DEF, 0x009F);
  uint16_t x = (uint16_t) (w(0x1CE1) << 2 | w(0x1CE5));
  ws(0x01C1, tg_read16(0x9FA20C + x) & 0x00FF);
  call(r, 0xF0A3, 0x813C);
  call(r, 0xF5B2, 0x813F);
  g_ram[0x01D0] = 0xFF;
  g_ram[0x01D1] = 0xFF;
  g_ram[0x01D2] = 0x01;
  g_ram[0x01D3] = 0;
  g_ram[0x01D5] = 0;
  g_ram[0x01D7] = 0;
  g_ram[0x01D8] = 0;
  g_ram[0x01D9] = 0;
  g_ram[0x01DA] = 0;
  g_ram[0x01CF] = 0;
  if(w(0x1CE5) == 0) {  // a new season: the results so far cleared
    for(uint16_t x2 = 0x28; x2 != 0; x2 -= 2) ws(0x1DC3 + x2, 0);
    Tg2KeepStandings(r);
  }
  Tg2SaveRecords(r);
  ws(0x0100, tg_read16(0x818000));  // JML [$0100] to the race ($81:8000)
  ws(0x0102, tg_read16(0x818002));
  tg_port_leave((uint32_t) g_ram[0x0102] << 16 | w(0x0100));
}

// $8076: the title and main menus (both players' threads), then the race set up.
static void menus(TgRegs* r) {
  Tg2RestoreStandings(r);
  call(r, 0xC492, 0x807C);
  if(tg_read16(0x001DF1) != 0) {
    to_race(r, false);
    return;
  }
  call(r, 0x820D, 0x8088);
  for(uint16_t i = 0; i < 0x14; i++) ws(0x1D75 + i * 2, i);
  to_race(r, true);
}

// $1F:8030: from power-on.
void Tg2MenuPowerOn(TgRegs* r) {
  front_end(r, 0x8036, 0x8051);
  call(r, 0xF04E, 0x8054);
  tg_bus_write8(0x4200, 0x81);
  Tg2LoadSave(r);
  if(r->c) {  // no records yet: the opening
    call(r, 0xC369, 0x8065);
    g_ram[0x01D2] = 0x01;
  } else {
    r->a = 0;
    Tg2MenuMusic(r);
  }
  regs(r);
  menus(r);
}

// $1F:80B7: after a race.
void Tg2MenuAfterRace(TgRegs* r) {
  front_end(r, 0x80BD, 0x80D8);
  r->a = 0;
  Tg2MenuMusic(r);
  tg_bus_write8(0x4200, 0x81);
  Tg2LoadRecords(r);
  regs(r);
  if(tg_read16(0x001DF1) != 0 || w(0x1D9D) == 0) {
    menus(r);
    return;
  }
  call(r, 0xEB7C, 0x80FB);  // the results; carry: back to the menus
  if(r->c) {
    menus(r);
    return;
  }
  g_ram[0x01D2] = 0x02;
  call(r, 0xDED5, 0x810C);
  if(w(0x1CE5) == 0) call(r, 0xD904, 0x8114);
  to_race(r, true);
}
