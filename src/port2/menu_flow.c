// Top Gear 2: the menus' flow (bank $1F). From power-on and after each race, the front end
// is set up, the menu screens run, and the next race starts. The screens wait for the NMI,
// so this runs as a fiber (TG_WAITS); the screens not yet in C run as the original, called
// from here. The NMI ($1F:9194) and the two players' threads it switches between stay the
// original.

#include <stddef.h>

#include "../port/port.h"

void Tg2ClearVram(TgRegs* r);
void Tg2FrontEndInit(TgRegs* r);
void Tg2LoadSave(TgRegs* r);
void Tg2MenuMusic(TgRegs* r);
void Tg2RestoreStandings(TgRegs* r);
void Tg2KeepStandings(TgRegs* r);
void Tg2LoadRecords(TgRegs* r);
void Tg2SaveRecords(TgRegs* r);
void Tg2MenuStart(TgRegs* r);
void Tg2Unpack(TgRegs* r);
void Tg2QueueFadeRow(TgRegs* r);
void Tg2CopyMapBlock(TgRegs* r);
void Tg2DrawSmallString(TgRegs* r);
void Tg2DrawBigString(TgRegs* r);
void Tg2PackSpritesSwap(TgRegs* r);
void Tg2NameEntrySprites(TgRegs* r);
void Tg2MenuCursorY(TgRegs* r);
void Tg2UnitsPicture(TgRegs* r);

static uint16_t w(uint16_t adr) { return ram16(adr); }
static void ws(uint16_t adr, uint16_t v) { ram16_set(adr, v); }

static uint16_t dp_d(const TgRegs* r, uint8_t o) { return ram16((uint16_t) (r->d + o)); }
static void dp_set_d(const TgRegs* r, uint8_t o, uint16_t v) { ram16_set((uint16_t) (r->d + o), v); }

static void regs(TgRegs* r) {
  r->d = 0x0100;
  r->db = 0x9F;
}

static void call(TgRegs* r, uint16_t target, uint16_t ret) {
  regs(r);
  tg_call_jsr(r, target, ret, TG_MX_16);
  regs(r);
}

static uint16_t rom(uint16_t adr) { return tg_read16(0x9F0000 | adr); }

// $1F:9EFF: waits for n more NMIs ($0014 counts them).
static void wait_frames(uint16_t n) {
  uint16_t t = (uint16_t) (w(0x0014) + n);
  while(w(0x0014) != t) tg_port_wait();
}

// JSR ($0000,X) to a screen's handler: the C version when there is one (waiting or not:
// this is the fiber), else the original. X (and A) hold the handler's entry in the script.
static void handler(TgRegs* r, uint16_t entry, uint16_t offset, uint16_t ret) {
  r->a = r->x = entry;
  uint16_t target = rom((uint16_t) (entry + offset));
  const TgPortFunc* f = tg_port_find(0x1F0000 | target);
  if(f != NULL && f->call == TG_CALL_JSR) f->fn(r);
  else {
    uint16_t d = r->d;
    tg_call_jsr(r, target, ret, TG_MX_16);
    r->d = d;
    r->db = 0x9F;
  }
}

// $1F:8650: runs a menu script (X) for the player whose page D is. A script names its
// handlers: on entry ([0] -> +0), on leaving (+2), on going back (+4); then per cursor line
// (from [$8D * 2 + 2]) a frame handler, followed by button entries (bits, handler) up to
// a 0: bit 0 set means held buttons ($28), else new presses ($2A, which it takes). A handler
// that returns carry set ends the script: A < 0 returns, A = 0 or 1 runs the leaving or
// going-back handler first, and X is the next script.
static void run_script(TgRegs* r, uint16_t script) {
  for(;;) {
    dp_set_d(r, 0x9D, script);
    handler(r, rom(script), 0, 0x865E);
    while(!r->c) {
      wait_frames(1);
      uint16_t x = (uint16_t) ((dp_d(r, 0x8D) << 1) + dp_d(r, 0x9D));
      uint16_t line = rom((uint16_t) (x + 2));
      handler(r, line, 0, 0x8678);
      r->c = false;
      for(x = (uint16_t) (line + 2);; x += 4) {
        uint16_t bits = rom(x);
        if(bits == 0) break;
        if(bits & 1) {
          if(!(bits & 0xFFF0 & dp_d(r, 0x28))) continue;
        } else {
          uint16_t pad = dp_d(r, 0x2A);
          dp_set_d(r, 0x2A, pad & (uint16_t) ~bits);
          if(!(bits & pad)) continue;
        }
        uint16_t entry = rom((uint16_t) (x + 2));
        handler(r, entry, 0, 0x869E);
        if(r->c) {
          r->y = x;
          break;
        }
        r->x = x;
      }
    }
    uint16_t a = r->a;
    r->n = (a & 0x8000) != 0;
    r->z = a == 0;
    r->c = true;
    if(r->n) return;
    script = r->x;
    if(a <= 1) {
      handler(r, rom(dp_d(r, 0x9D)), (uint16_t) (a == 0 ? 2 : 4), a == 0 ? 0x86BB : 0x86C8);
    }
  }
}

// $1F:820D / $1F:8296: the menus with both players' threads. The NMI ($1F:92CC) gives each
// frame to the other thread until a mid-frame IRQ hands it back; this one (on the main
// stack) runs the second player's page ($0200), the other one (started from the frame
// built at $1F71, resuming at `start`) the first player's. When the script ends, it waits
// checks the second player is done ($0293 bit 14; if not, it never carries on, as
// the original), and goes back to one thread.
static void two_threads(TgRegs* r, uint16_t start, uint16_t script, bool after_race) {
  regs(r);
  Tg2MenuStart(r);
  regs(r);
  if(after_race) {
    wait_frames(1);
    tg_bus_write8(0x2133, 0x00);
  } else {
    tg_bus_write8(0x4200, 0x81);
    wait_frames(1);
    tg_bus_write8(0x4200, 0xB1);
  }
  ws(0x0029, 0x1F71);
  ws(0x1F72, 0x0100);
  ws(0x1F74, 0);
  ws(0x1F76, 0);
  ws(0x1F78, 0);
  g_ram[0x1F7A] = 0x9F;
  g_ram[0x1F7B] = 0x00;
  ws(0x1F7C, start);
  g_ram[0x1F7E] = 0x9F;
  if(after_race) {
    wait_frames(1);
    ws(0x0010, 0x92CC);
    tg_bus_write8(0x4200, 0x81);
    r->d = 0x0200;
    tg_bus_read8(0x4210);
    tg_cpu_cli();
    wait_frames(1);
  } else {
    ws(0x0010, 0x92CC);
    r->d = 0x0200;
    wait_frames(1);
    tg_cpu_cli();
    ws(w(0x0270), 0);
  }
  run_script(r, script);
  if(!(w(0x0293) & 0x4000)) for(;;) tg_port_wait();
  wait_frames(1);
  tg_cpu_sei();
  ws(0x0010, 0x9274);
  wait_frames(1);
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
    two_threads(r, 0x82F5, 0x881A, true);
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
  two_threads(r, 0x8269, 0x86CC, false);
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

// ---- The screens' handlers that wait for the NMI ----
//
// They run in the fiber only (the runner above calls them); in verify mode, and for the
// other thread, the originals run.

// $1F:9EFF from a handler, with the registers it leaves.
static void wait1(TgRegs* r) {
  wait_frames(1);
  r->a = w(0x0014);
  r->n = (r->a & 0x8000) != 0;
  r->z = true;
  r->c = true;
}

// Queues a palette row (A = bank | fade-in bit, X = source) for the CGRAM place $70 + y.
static void fade_row(TgRegs* r, uint16_t y, uint16_t bank, uint16_t src) {
  r->c = false;
  r->y = adc16(r, dp_d(r, 0x70), y);
  r->x = src;
  r->a = bank;
  flags_nz16(r, bank);
  Tg2QueueFadeRow(r);
}

// Asks the NMI to run the fade ($93 bit 2) and waits until it is done, calling `each`
// every frame.
static void run_fade(TgRegs* r, void (*each)(TgRegs*)) {
  dp_set_d(r, 0x93, dp_d(r, 0x93) | 0x0004);
  do {
    wait1(r);
    if(each) each(r);
    r->a = 0x0004;
    uint16_t m = dp_d(r, 0x93);
    r->n = (m & 0x8000) != 0;
    r->v = (m & 0x4000) != 0;
    r->z = (m & 0x0004) == 0;
  } while(!r->z);
}

// Has the NMI send $0380 bytes from $7F:src to VRAM dest (the $7A queue) and waits for it.
static void send_7a(TgRegs* r, uint16_t src, uint16_t dest) {
  dp_set_d(r, 0x7C, src);
  dp_set_d(r, 0x7E, 0x007F);
  dp_set_d(r, 0x80, dest);
  dp_set_d(r, 0x7A, 0x0380);
  while(dp_d(r, 0x7A) != 0) tg_port_wait();
  r->a = 0;
  flags_nz16(r, 0);
}

// $910D / $90E9: unpacks bank (A):X into $7F:($60), then has the NMI send it to VRAM Y
// through the queue at $7A (or $72), waiting for the queue to be free and then done.
static void unpack_send(TgRegs* r, uint8_t q) {
  uint16_t dest = r->y;
  r->a = (uint16_t) ((r->a & 0x00FF) | 0x7F00);
  r->y = dp_d(r, 0x60);
  Tg2Unpack(r);
  while((r->y = dp_d(r, q)) != 0) tg_port_wait();
  dp_set_d(r, (uint8_t) (q + 6), dest);
  r->x = dp_d(r, 0x60);
  dp_set_d(r, (uint8_t) (q + 2), r->x);
  r->x = 0x007F;
  dp_set_d(r, (uint8_t) (q + 4), r->x);
  dp_set_d(r, q, r->a);
  while((r->a = dp_d(r, q)) != 0) tg_port_wait();
  r->y = dest;
  flags_nz16(r, 0);
}
void Tg2UnpackSend(TgRegs* r) { unpack_send(r, 0x7A); }
void Tg2UnpackSend2(TgRegs* r) { unpack_send(r, 0x72); }

static void unpack_7f(TgRegs* r, uint16_t bank, uint16_t src, uint16_t y) {
  r->a = bank;
  r->x = src;
  r->y = y;
  Tg2Unpack(r);
}

static void unpack_send_at(TgRegs* r, uint16_t y, uint16_t bank, uint16_t src) {
  r->y = y;
  r->a = bank;
  r->x = src;
  Tg2UnpackSend(r);
}

// $8C26: the name entry fades out, its sprites kept up meanwhile.
void Tg2NameEntryFadeOut(TgRegs* r) {
  fade_row(r, 0x0062, 0x809E, 0x8042);
  fade_row(r, 0x0022, 0x809E, 0x8002);
  fade_row(r, 0x0102, 0x809D, 0xB014);
  run_fade(r, Tg2NameEntrySprites);
}

// $8DB0 / $8F61 / $90AE: the gears, control and units screens fade out.
void Tg2GearsFadeOut(TgRegs* r) {
  fade_row(r, 0x0062, 0x809E, 0x80E0);
  fade_row(r, 0x0082, 0x809E, 0x8042);
  fade_row(r, 0x0102, 0x809C, 0x9758);
  fade_row(r, 0x0022, 0x809C, 0x9778);
  run_fade(r, NULL);
  dp_set_d(r, 0x3A, 0);
}
void Tg2ControlFadeOut(TgRegs* r) {
  fade_row(r, 0x0002, 0x809C, 0x966A);
  fade_row(r, 0x0022, 0x809C, 0x96AA);
  fade_row(r, 0x0102, 0x809C, 0x9758);
  run_fade(r, NULL);
}
void Tg2UnitsFadeOut(TgRegs* r) {
  fade_row(r, 0x0002, 0x809C, 0x96C8);
  fade_row(r, 0x0102, 0x809C, 0x96C8);
  run_fade(r, NULL);
  dp_set_d(r, 0x3A, 0);
}

// The screens' common start: layers 2 only ($4C), the $7A queue free.
static void screen_start(TgRegs* r) {
  dp_set_d(r, 0x4C, 0x0002);
}

// The screens' common end: layers on ($4C), fade in ($93 bit 2), $20 frames before input.
static void screen_end(TgRegs* r, uint16_t layers) {
  dp_set_d(r, 0x4C, layers);
  dp_set_d(r, 0x93, dp_d(r, 0x93) | 0x0004);
  r->a = 0x0020;
  dp_set_d(r, 0x9F, r->a);
  r->n = false;
  r->z = false;
  r->c = false;
}

// $8C75: the gears screen.
void Tg2GearsScreen(TgRegs* r) {
  screen_start(r);
  dp_set_d(r, 0x7A, 0);
  unpack_send_at(r, (uint16_t) (0x2000 + dp_d(r, 0x6E)), 0x009E, 0xEDC0);
  unpack_send_at(r, (uint16_t) (dp_d(r, 0x6A) + 0x1000), 0x009D, 0x856B);
  unpack_send_at(r, (uint16_t) (dp_d(r, 0x6A) + 0x1410), 0x009C, 0x9817);
  unpack_7f(r, 0x7F9D, 0x8A2D, dp_d(r, 0x60));
  send_7a(r, dp_d(r, 0x60), dp_d(r, 0x62));
  send_7a(r, (uint16_t) (dp_d(r, 0x60) + 0x0380), dp_d(r, 0x66));
  dp_set_d(r, 0x8D, 0);
  r->y = 0x80FE;
  r->x = (uint16_t) (dp_d(r, 0xB1) + 0x6000);
  r->a = 0x309E;
  Tg2DrawSmallString(r);
  Tg2PackSpritesSwap(r);
  while(dp_d(r, 0x7A) != 0) tg_port_wait();
  fade_row(r, 0x0062, 0x009E, 0x80E0);
  fade_row(r, 0x0082, 0x009E, 0x8042);
  fade_row(r, 0x0102, 0x009C, 0x9758);
  fade_row(r, 0x0022, 0x009C, 0x9778);
  dp_set_d(r, 0x8D, 0);
  r->x = dp_d(r, 0xA3);
  if(tg_read16(0x9F0000 | (uint16_t) (0x1D01 + r->x)) & 0x8000) dp_set_d(r, 0x8D, 1);
  Tg2MenuCursorY(r);
  screen_end(r, 0x0017);
}

// $8E11: the control type screen.
void Tg2ControlScreen(TgRegs* r) {
  screen_start(r);
  dp_set_d(r, 0x8D, 0);
  dp_set_d(r, 0x7A, 0);
  unpack_send_at(r, (uint16_t) (dp_d(r, 0x6A) + 0x1000), 0x009D, 0x8B72);
  unpack_send_at(r, (uint16_t) (0x3000 + dp_d(r, 0x6E)), 0x009E, 0xEDC0);
  uint16_t i = dp_d(r, 0x6C);
  unpack_7f(r, (uint16_t) (tg_read16(0x9FA202 + i) | 0x7F00), tg_read16(0x9FA200 + i), dp_d(r, 0x60));
  unpack_7f(r, 0x7F9D, 0x93A4, (uint16_t) (dp_d(r, 0x60) + 0x0700));
  dp_set_d(r, 0x00, (uint16_t) (dp_d(r, 0x60) + 0x0700));
  dp_set_d(r, 0x02, (uint16_t) (dp_d(r, 0x60) + 0x0012));
  dp_set_d(r, 0x04, 0x0017);
  dp_set_d(r, 0x06, 0x000E);
  dp_set_d(r, 0x08, 0);
  dp_set_d(r, 0x0A, 0x0012);
  dp_set_d(r, 0x0C, 0x7F01);
  r->a = 0;
  dp_set_d(r, 0x0E, 0);
  Tg2CopyMapBlock(r);
  send_7a(r, dp_d(r, 0x60), dp_d(r, 0x64));
  r->y = 0x8200;
  r->x = (uint16_t) (dp_d(r, 0xB1) + 0x5860);
  r->a = 0x319E;
  Tg2DrawSmallString(r);
  Tg2PackSpritesSwap(r);
  fade_row(r, 0x0002, 0x009C, 0x966A);
  fade_row(r, 0x0022, 0x009C, 0x96AA);
  fade_row(r, 0x0102, 0x009C, 0x9758);
  uint16_t v = (uint16_t) ((dp_d(r, 0x6A) >> 8) & 0x00F0);
  dp_set_d(r, 0x47, v);
  dp_set_d(r, 0x47, (uint16_t) ((v >> 4) | v));
  screen_end(r, 0x0013);
}

// $8FAD: the speed units screen.
void Tg2UnitsScreen(TgRegs* r) {
  screen_start(r);
  unpack_send_at(r, (uint16_t) (0x3000 + dp_d(r, 0x6E)), 0x009E, 0xEDC0);
  dp_set_d(r, 0x7A, 0);
  unpack_send_at(r, (uint16_t) (dp_d(r, 0x6A) + 0x1000), 0x009C, 0x8401);
  dp_set_d(r, 0x7A, 0);
  unpack_send_at(r, (uint16_t) (dp_d(r, 0x6A) + 0x1300), 0x009C, 0x83D2);
  unpack_7f(r, 0x7F9C, 0x8315, dp_d(r, 0x60));
  dp_set_d(r, 0x8D, 0);
  r->x = dp_d(r, 0xA3);
  if(tg_read16(0x9F0000 | (uint16_t) (0x1CFF + r->x)) != 0) dp_set_d(r, 0x8D, 1);
  dp_set_d(r, 0x7C, dp_d(r, 0x60));
  dp_set_d(r, 0x7E, 0x007F);
  dp_set_d(r, 0x80, dp_d(r, 0x62));
  dp_set_d(r, 0x7A, 0x0380);
  r->y = 0x8118;
  r->x = (uint16_t) (dp_d(r, 0xB1) + 0x1884);
  r->a = 0x319E;
  Tg2DrawBigString(r);
  Tg2PackSpritesSwap(r);
  while(dp_d(r, 0x7A) != 0) tg_port_wait();
  Tg2UnitsPicture(r);
  while(dp_d(r, 0x7A) != 0) tg_port_wait();
  fade_row(r, 0x0002, 0x009C, 0x96C8);
  fade_row(r, 0x0102, 0x009C, 0x96C8);
  screen_end(r, 0x0017);
}

// $8920: the name entry screen.
void Tg2NameEntryScreen(TgRegs* r) {
  screen_start(r);
  dp_set_d(r, 0x7A, 0);
  unpack_send_at(r, (uint16_t) (dp_d(r, 0x6A) + 0x1000), 0x009C, 0x9796);
  dp_set_d(r, 0x3C, 0xFFFF);
  dp_set_d(r, 0x3A, 0);
  uint16_t i = dp_d(r, 0x6C);
  unpack_send_at(r, dp_d(r, 0x64), tg_read16(0x9FA1FE + i), tg_read16(0x9FA1FC + i));
  unpack_send_at(r, (uint16_t) (0x3000 + dp_d(r, 0x6E)), 0x009E, 0xEDC0);
  unpack_7f(r, 0x7F9C, 0x983D, dp_d(r, 0x60));
  dp_set_d(r, 0x7C, (uint16_t) (dp_d(r, 0x60) + 0x0380));
  dp_set_d(r, 0x7E, 0x007F);
  dp_set_d(r, 0x80, dp_d(r, 0x62));
  dp_set_d(r, 0x7A, 0x0380);
  while(dp_d(r, 0x7A) != 0) tg_port_wait();
  fade_row(r, 0x0062, 0x009E, 0x8042);
  fade_row(r, 0x0022, 0x009E, 0x8002);
  fade_row(r, 0x0102, 0x009D, 0xB014);
  dp_set_d(r, 0x8D, 0);
  dp_set_d(r, 0xA5, 0x0026);
  Tg2NameEntrySprites(r);
  // The name's length so far (up to 11).
  uint16_t x = dp_d(r, 0xA3), n = 0;
  while(g_ram[(uint16_t) (0x1CF3 + x)] != 0) {
    x++;
    n++;
    if(n == 0x0B) break;
  }
  r->x = x;
  r->y = n;
  dp_set_d(r, 0xA7, n);
  dp_set_d(r, 0x4C, 0x0013);
  dp_set_d(r, 0x45, (uint16_t) (dp_d(r, 0x66) >> 8));
  uint16_t v = (uint16_t) ((dp_d(r, 0x6A) >> 8) & 0x00F0);
  dp_set_d(r, 0x47, v);
  v = (uint16_t) (((v >> 4) + 1) | v);
  dp_set_d(r, 0x47, v);
  v = (uint16_t) (((v << 8 | v >> 8) & 0x0F00) | v);
  dp_set_d(r, 0x47, v);
  g_ram[(uint16_t) (r->d + 0x3E)] = 0x09;
  dp_set_d(r, 0x93, dp_d(r, 0x93) | 0x0004);
  dp_set_d(r, 0x9F, 0x0020);
  r->a = w(0x0014);
  dp_set_d(r, 0xAB, r->a);
  flags_nz16(r, r->a);
  r->c = false;
}

// $A59A: the options screen fades out.
void Tg2OptionsFadeOut(TgRegs* r) {
  fade_row(r, 0x0062, 0x809C, 0x9738);
  fade_row(r, 0x0102, 0x809C, 0x9758);
  fade_row(r, 0x0022, 0x809C, 0x9778);
  run_fade(r, NULL);
  r->a = 0xFFFF;
  dp_set_d(r, 0x3C, r->a);
  flags_nz16(r, r->a);
}
