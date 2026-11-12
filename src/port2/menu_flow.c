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
void Tg2ShopPanel(TgRegs* r);
void Tg2ShopText(TgRegs* r);
void Tg2PackSpritesKeep(TgRegs* r);
void Tg2OptionsCursorY(TgRegs* r);
void Tg2UnpackSend(TgRegs* r);
void Tg2UnpackSend2(TgRegs* r);
void Tg2SetFadeIn(TgRegs* r);
void Tg2ShopDraw(TgRegs* r);
void Tg2RaceInfoScreen(TgRegs* r);
static void title(TgRegs* r);
static void opening(TgRegs* r);
static bool results(TgRegs* r);
static void standings(TgRegs* r);
static void country_password(TgRegs* r);

static uint16_t w(uint16_t adr) { return ram16(adr); }
static void ws(uint16_t adr, uint16_t v) { ram16_set(adr, v); }

static void wr16_9f(uint16_t adr, uint16_t v) { tg_bus_write16(0x9F0000 | adr, v); }
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
  if(f != NULL && f->call == TG_CALL_JSR) {
    f->fn(r);
    if(!tg_port_take_declined()) return;
  }
  {
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
    Tg2RaceInfoScreen(r);
    regs(r);
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
  title(r);
  regs(r);
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
    opening(r);
    regs(r);
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
  if(results(r)) {  // the race's results; true: not in the top ten, back to the menus
    menus(r);
    return;
  }
  g_ram[0x01D2] = 0x02;
  standings(r);
  if(w(0x1CE5) == 0) country_password(r);
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

// Has the NMI send bytes from $7F:src to VRAM dest (the $7A queue) and waits for it.
static void send_7a_n(TgRegs* r, uint16_t src, uint16_t dest, uint16_t n) {
  dp_set_d(r, 0x7C, src);
  dp_set_d(r, 0x7E, 0x007F);
  dp_set_d(r, 0x80, dest);
  dp_set_d(r, 0x7A, n);
  while(dp_d(r, 0x7A) != 0) tg_port_wait();
  r->a = 0;
  flags_nz16(r, 0);
}
static void send_7a(TgRegs* r, uint16_t src, uint16_t dest) { send_7a_n(r, src, dest, 0x0380); }

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

// $A7A1: the shop: the panel's frame, the four item boxes, the item's name, its picture
// (sent by the $7A queue), the level sprites, the price list and the money (as a
// number, $1D19 x 1000, without leading zeros); then swaps the sprite tables ($BB/$BD)
// and waits for the NMI to take them. The paint shop (item 9) isn't in C yet: declined.
static uint16_t rom9f(uint16_t adr) { return tg_read16(0x9F0000 | adr); }
static uint32_t db9f(uint16_t adr) { return 0x9F0000 | adr; }

void Tg2ShopDraw(TgRegs* r) {
  uint16_t item = dp_d(r, 0xA5);
  if(item == 0x0009) {
    tg_port_decline();
    return;
  }
  uint16_t b1 = dp_d(r, 0xB1);
  dp_set_d(r, 0x00, (uint16_t) (dp_d(r, 0x60) + 0x0380));
  dp_set_d(r, 0x02, (uint16_t) (dp_d(r, 0x60) + 0x000E));
  dp_set_d(r, 0x04, 0x0019);
  dp_set_d(r, 0x06, 0x000C);
  dp_set_d(r, 0x08, 0);
  dp_set_d(r, 0x0A, 0x000E);
  dp_set_d(r, 0x0C, 0x7F01);
  r->a = 0x0D80;
  dp_set_d(r, 0x0E, r->a);
  Tg2CopyMapBlock(r);
  static const uint16_t kBoxes[4] = {0x006C, 0x0076, 0x01EC, 0x01F6};
  for(int i = 0; i < 4; i++) {
    dp_set_d(r, 0x02, (uint16_t) (dp_d(r, 0x60) + kBoxes[i]));
    Tg2ShopPanel(r);
  }
  uint16_t i2 = (uint16_t) (item << 1);
  r->y = rom9f((uint16_t) (0xAE90 + i2));
  r->x = (uint16_t) ((b1 & 0xFF00) + 0x0870);
  r->a = 0x309F;
  Tg2DrawSmallString(r);
  dp_set_d(r, 0x7C, (uint16_t) (dp_d(r, 0x60) + 0x0780 + rom9f((uint16_t) (0xAF17 + i2))));
  dp_set_d(r, 0x7E, 0x007F);
  uint16_t bd = dp_d(r, 0xBD);
  dp_set_d(r, 0x80, (uint16_t) (rom9f((uint16_t) (0xAF2B + bd)) + dp_d(r, 0x62)));
  dp_set_d(r, 0x7A, 0x0800);
  // The level marker and the four level boxes.
  uint16_t y = dp_d(r, 0xAF);
  uint16_t base = rom9f((uint16_t) (0xAC31 + i2));
  dp_set_d(r, 0x00, base);
  uint16_t level = (uint16_t) (tg_read16(db9f((uint16_t) (0x1D03 + i2 + dp_d(r, 0xA3)))) << 2);
  r->c = false;
  uint16_t a = adc16(r, level, rom9f((uint16_t) (0xAF2F + bd)));
  r->c = false;
  a = adc16(r, a, base);
  wr16_9f(y + 0x0502, a);
  r->c = false;
  wr16_9f(y + 0x0500, adc16(r, 0x2060, b1));
  wr16_9f(y + 0x0301, 0x0100);
  r->c = false;
  a = adc16(r, rom9f((uint16_t) (0xAF2F + bd)), base);
  wr16_9f(y + 0x0506, a);
  r->c = false;
  a = adc16(r, a, 4);
  wr16_9f(y + 0x050A, a);
  a = adc16(r, a, 4);
  wr16_9f(y + 0x050E, a);
  a = adc16(r, a, 4);
  wr16_9f(y + 0x0512, a);
  r->c = false;
  a = adc16(r, 0x08B0, b1);
  wr16_9f(y + 0x0504, a);
  a = adc16(r, a, 0x0028);
  wr16_9f(y + 0x0508, a);
  a = adc16(r, 0x38B0, b1);
  wr16_9f(y + 0x050C, a);
  a = adc16(r, a, 0x0028);
  wr16_9f(y + 0x0510, a);
  for(uint16_t k = 0; k < 4; k++) wr16_9f(y + 0x0305 + k * 4, 0x0100);
  r->c = false;
  y = adc16(r, y, 0x0014);
  dp_set_d(r, 0xAF, y);
  // The item's own sprites.
  uint16_t sel = (uint16_t) (dp_d(r, 0xA7) << 1);
  r->c = false;
  dp_set_d(r, 0x00, adc16(r, rom9f((uint16_t) (0xAF33 + sel)), b1));
  uint16_t x = rom9f((uint16_t) (0xAF3F + sel));
  y = dp_d(r, 0xAF);
  while((a = rom9f((uint16_t) (0xAF4B + x))) != 0) {
    wr16_9f(y + 0x0502, a);
    r->c = false;
    wr16_9f(y + 0x0500, adc16(r, rom9f((uint16_t) (0xAFAF + x)), dp_d(r, 0x00)));
    wr16_9f(y + 0x0301, 0);
    x += 2;
    y += 4;
  }
  dp_set_d(r, 0xAF, y);
  // The price list.
  x = rom9f((uint16_t) (0xB0FB + i2));
  r->y = y;
  while((a = rom9f(x)) != 0) {
    r->c = false;
    dp_set_d(r, 0x00, adc16(r, a, b1));
    r->x = rom9f((uint16_t) (x + 2));
    Tg2ShopText(r);
    x += 4;
  }
  dp_set_d(r, 0xAF, r->y);
  wait1(r);
  r->c = false;  // $A627
  // The money: $1D19 x 1000 in 24 bits (x 1024 plus x -24 on the multiplier).
  uint16_t money = tg_read16(db9f((uint16_t) (0x1D19 + dp_d(r, 0xA3))));
  uint16_t d = r->d;
  ram16_set((uint16_t) (d + 1), (uint16_t) (money << 2));
  tg_bus_write8(0x211B, (uint8_t) money);
  tg_bus_write8(0x211B, (uint8_t) (money >> 8));
  tg_bus_write8(0x211C, 0xE8);
  g_ram[d] = tg_bus_read8(0x2134);
  g_ram[(uint16_t) (d + 3)] = 0;
  uint16_t mid = (uint16_t) (tg_bus_read8(0x2135) | tg_bus_read8(0x2136) << 8);
  ram16_set((uint16_t) (d + 1), (uint16_t) (mid + ram16((uint16_t) (d + 1))));
  uint32_t n = g_ram[d] | g_ram[(uint16_t) (d + 1)] << 8 | (uint32_t) g_ram[(uint16_t) (d + 2)] << 16;
  int k;
  for(k = 0; k < 6; k++) {
    uint32_t p = tg_bus_read8(0x9FAAE0 + k) | tg_bus_read8(0x9FAAE6 + k) << 8 | (uint32_t) tg_bus_read8(0x9FAAEC + k) << 16;
    uint8_t digit = 0;
    while(n >= p) {
      n -= p;
      digit++;
    }
    g_ram[(uint16_t) (d + 3 + k)] = (uint8_t) (digit + 0x30);
  }
  g_ram[d] = (uint8_t) n;
  g_ram[(uint16_t) (d + 1)] = (uint8_t) (n >> 8);
  g_ram[(uint16_t) (d + 2)] = (uint8_t) (n >> 16);
  g_ram[(uint16_t) (d + 3 + 6)] = (uint8_t) (n + 0x30);
  g_ram[(uint16_t) (d + 4 + 6)] = 0;
  uint16_t lead = 0;
  while(lead < 6 && g_ram[(uint16_t) (d + 3 + lead)] == 0x30) lead++;
  dp_set_d(r, 0x00, lead);
  r->x = (uint16_t) (d + 3 + lead);
  r->c = false;
  dp_set_d(r, 0x00, adc16(r, 0x5878, b1));
  r->y = dp_d(r, 0xAF);
  Tg2ShopText(r);
  dp_set_d(r, 0xAF, r->y);
  while(dp_d(r, 0x7A) != 0) tg_port_wait();
  dp_set_d(r, 0x7C, dp_d(r, 0x60));
  dp_set_d(r, 0x7E, 0x007F);
  dp_set_d(r, 0x80, dp_d(r, 0x62));
  r->a = 0x0380;
  Tg2PackSpritesKeep(r);
  dp_set_d(r, 0x93, dp_d(r, 0x93) | 0x0008);
  uint16_t bb = dp_d(r, 0xBB);
  r->x = dp_d(r, 0xBD);
  dp_set_d(r, 0xBB, r->x);
  dp_set_d(r, 0xBD, bb);
  r->a = 0x0008;
  for(;;) {
    uint16_t m = dp_d(r, 0x93);
    r->n = (m & 0x8000) != 0;
    r->v = (m & 0x4000) != 0;
    r->z = (m & 0x0008) == 0;
    if(r->z) break;
    tg_port_wait();
  }
}

// $A7A1 from C: the original when the C version declines (the paint shop).
static void shop_draw(TgRegs* r, uint16_t ret) {
  Tg2ShopDraw(r);
  if(tg_port_take_declined()) {
    uint16_t d = r->d;
    tg_call_jsr(r, 0xA7A1, ret, TG_MX_16);
    r->d = d;
    r->db = 0x9F;
  }
}

// $ACC7 / $ACEF / $AD60: the cursor moves between the shop's item levels (tables
// $9F:ADAF, $9F:ADBB, $9F:ADD3 give the level to go to from each; in $AD60's a negative
// one moves on to the next item instead, skipping item 5). Not yet for the paint shop.
static void shop_move(TgRegs* r, uint16_t table, uint16_t ret, bool next_item) {
  if(dp_d(r, 0xA5) == 0x0009) {
    tg_port_decline();
    return;
  }
  Tg2SetFadeIn(r);
  uint16_t a = rom9f((uint16_t) (table + (dp_d(r, 0xA7) << 1)));
  r->x = (uint16_t) (dp_d(r, 0xA7) << 1);
  if(next_item && (a & 0x8000)) {
    dp_set_d(r, 0xA5, (uint16_t) (dp_d(r, 0xA5) + 1));
    if(dp_d(r, 0xA5) == 0x0005) dp_set_d(r, 0xA5, 0x0006);
    dp_set_d(r, 0xA7, 0);
  } else {
    dp_set_d(r, 0xA7, a);
  }
  shop_draw(r, ret);
  r->c = false;
}
void Tg2ShopLeft(TgRegs* r) { shop_move(r, 0xADAF, 0xACDE, false); }
void Tg2ShopRight(TgRegs* r) { shop_move(r, 0xADBB, 0xAD06, false); }
void Tg2ShopDown(TgRegs* r) { shop_move(r, 0xADD3, 0xAD8F, true); }

static void unpack_send2_at(TgRegs* r, uint16_t y, uint16_t bank, uint16_t src) {
  r->y = y;
  r->a = bank;
  r->x = src;
  Tg2UnpackSend2(r);
}

// The options screen's pictures, shared by $A24C and $A3B8 (send: $910D or $90E9).
static void options_pictures(TgRegs* r, void (*send)(TgRegs*, uint16_t, uint16_t, uint16_t), bool title) {
  unpack_send_at(r, (uint16_t) (0x2000 + dp_d(r, 0x6E)), 0x009E, 0xEDC0);
  if(title) send(r, dp_d(r, 0x6A), 0x009C, 0x85E9);
  uint16_t i = dp_d(r, 0x6C);
  send(r, dp_d(r, 0x64), tg_read16(0x9FA202 + i), tg_read16(0x9FA200 + i));
  send(r, (uint16_t) (dp_d(r, 0x6A) + 0x1000), 0x009C, 0x9796);
  send(r, (uint16_t) (dp_d(r, 0x6A) + 0x10C0), 0x009C, 0x9817);
  unpack_7f(r, 0x7F9C, 0x983D, dp_d(r, 0x60));
  send_7a(r, dp_d(r, 0x60), dp_d(r, 0x62));
  send_7a_n(r, (uint16_t) (dp_d(r, 0x60) + 0x0700), dp_d(r, 0x66), 0x0800);
  r->y = 0xA5F5;
  r->x = (uint16_t) (dp_d(r, 0xB1) + 0x10A4);
  r->a = 0x309F;
  Tg2DrawSmallString(r);
  Tg2PackSpritesSwap(r);
}

// The options screen's layers, maps and window, and the cursor.
static void options_regs(TgRegs* r) {
  dp_set_d(r, 0x4C, 0x0017);
  uint16_t a = (uint16_t) ((dp_d(r, 0x62) >> 8) & 0x00FC);
  dp_set_d(r, 0x00, a);
  dp_set_d(r, 0x43, (uint16_t) ((dp_d(r, 0x64) & 0xFC00) | a));
  dp_set_d(r, 0x45, (uint16_t) ((dp_d(r, 0x66) >> 8) & 0x00FC));
  uint16_t v = (uint16_t) ((dp_d(r, 0x6A) >> 8) & 0x00F0);
  dp_set_d(r, 0x47, v);
  v = (uint16_t) (((v >> 4) + 1) | v);
  dp_set_d(r, 0x47, v);
  dp_set_d(r, 0x47, (uint16_t) (((v & 0x000F) << 8) | v));
  uint8_t b = g_ram[(uint16_t) (r->d + 0x6F)];
  g_ram[(uint16_t) (r->d + 0x42)] = (uint8_t) (((uint8_t) (b + 0x20) & 0xF0) >> 5);
  g_ram[(uint16_t) (r->d + 0x3E)] = 0x09;
  Tg2OptionsCursorY(r);
}

static void options_start(TgRegs* r) {
  wait1(r);
  dp_set_d(r, 0x7A, 0);
  dp_set_d(r, 0x8F, 0);
  dp_set_d(r, 0x8D, dp_d(r, 0xA1));
}

// $A24C: the options screen from the title: its palettes straight in, then a scroll in
// ($2C/$2E) unless the second player's view is to be closed.
void Tg2OptionsFromTitle(TgRegs* r) {
  dp_set_d(r, 0x30, 0x8000);
  options_start(r);
  options_pictures(r, unpack_send2_at, true);
  uint16_t p = dp_d(r, 0x70);
  for(uint16_t i = 0; i < 0x20; i++) {
    g_ram[(uint16_t) (p + 0x40 + i)] = tg_bus_read8(0x9E8020 + i);
    g_ram[(uint16_t) (p + 0x60 + i)] = tg_bus_read8(0x9C9736 + i);
    g_ram[(uint16_t) (p + 0x100 + i)] = tg_bus_read8(0x9C9756 + i);
  }
  wr16_9f((uint16_t) (p + 0x0022), 0x14BF);
  options_regs(r);
  if((dp_d(r, 0x93) & 0x4000) && w(0x1CDB) == 0) {
    uint16_t d = r->d;
    tg_call_jsr(r, 0x9185, 0xA397, TG_MX_16);
    regs(r); r->d = d;
    tg_call_jsr(r, 0x9185, 0xA39A, TG_MX_16);
    regs(r); r->d = d;
    r->a = 0x8000;
    flags_nz16(r, r->a);
    r->c = true;
    return;
  }
  dp_set_d(r, 0x30, 0);
  dp_set_d(r, 0x2E, 0x0F00);
  dp_set_d(r, 0x2C, 0x0040);
  dp_set_d(r, 0x9F, 0x0040);
  wait1(r);
  r->c = false;
}

// $A3B8: the options screen, faded in.
void Tg2OptionsScreen(TgRegs* r) {
  dp_set_d(r, 0x4C, 0x0002);
  options_start(r);
  options_pictures(r, unpack_send_at, false);
  fade_row(r, 0x0062, 0x009C, 0x9738);
  fade_row(r, 0x0102, 0x009C, 0x9758);
  fade_row(r, 0x0022, 0x009C, 0x9778);
  options_regs(r);
  screen_end(r, 0x0017);
}

// ---- $1F:B265: the race info screen (country, track map, length, laps, record) ----

void Tg2PackSprites(TgRegs* r);
void Tg2SetScreenRegs(TgRegs* r);
void Tg2FillWords(TgRegs* r);
void Tg2UnpackToVram(TgRegs* r);
void Tg2PixelsToTiles(TgRegs* r);
void Tg2VramDma(TgRegs* r);
void Tg2NextDigit(TgRegs* r);
void Tg2BoxEdges(TgRegs* r);

// MVN: n bytes, one at a time (the blocks may overlap), from sb:x to db:y.
static void move_bytes(uint8_t sb, uint16_t x, uint8_t db, uint16_t y, uint16_t n) {
  for(uint16_t i = 0; i < n; i++)
    tg_bus_write8((uint32_t) db << 16 | (uint16_t) (y + i), tg_read8((uint32_t) sb << 16 | (uint16_t) (x + i)));
}

static void copy_block(TgRegs* r, uint16_t src, uint16_t dest, uint16_t w4, uint16_t h6, uint16_t a) {
  dp_set_d(r, 0x00, src);
  dp_set_d(r, 0x02, dest);
  dp_set_d(r, 0x04, w4);
  dp_set_d(r, 0x06, h6);
  dp_set_d(r, 0x08, 0);
  dp_set_d(r, 0x0A, a);
  dp_set_d(r, 0x0C, 0x7F01);
  dp_set_d(r, 0x0E, 0);
  Tg2CopyMapBlock(r);
}

static void vram_dma(TgRegs* r, uint16_t n, uint16_t src, uint16_t dest) {
  dp_set_d(r, 0x00, n);
  r->a = 0x007F;
  r->x = src;
  r->y = dest;
  Tg2VramDma(r);
}

static void small_string(TgRegs* r, uint16_t a, uint16_t x, uint16_t y) {
  r->a = a;
  r->x = x;
  r->y = y;
  Tg2DrawSmallString(r);
}

static void box_edges(TgRegs* r, uint16_t x, uint16_t y) {
  dp_set_d(r, 0x00, x);
  dp_set_d(r, 0x02, y);
  Tg2BoxEdges(r);
}

// One digit's two sprites (top and bottom half) at $0500 + x, tile t, position p.
static void time_digit(uint16_t x, uint16_t t, uint16_t p) {
  ws(0x0502 + x, t);
  ws(0x0506 + x, (uint16_t) (t + 0x000C));
  ws(0x0500 + x, (uint16_t) (0xB000 | p));
  ws(0x0504 + x, (uint16_t) (0xB800 | p));
  ws(0x0301 + x, 0);
  ws(0x0305 + x, 0);
}

// $B4DC: the country's flag, the length and laps, the record time's sprites and the
// country and track names, then sent to VRAM.
static void race_info_text(TgRegs* r) {
  uint16_t c = w(0x1CE1), k = w(0x1CE5);
  uint16_t a = rom9f((uint16_t) (0xB855 + c * 2));
  for(uint16_t n = k; n != 0; n--) a >>= 4;
  copy_block(r, rom9f((uint16_t) (0xBDDD + (a & 0x000F) * 2)), 0x0C64, 5, 5, 0x0036);
  uint16_t i = (uint16_t) (c << 2 | k);
  dp_set_d(r, 0x00, rom9f((uint16_t) (0xB975 + i)));
  Tg2NextDigit(r);
  g_ram[0x17A4] = (uint8_t) r->a;
  Tg2NextDigit(r);
  g_ram[0x17A2] = (uint8_t) r->a;
  i = (uint16_t) (i * 2);
  uint8_t div = (uint8_t) rom9f((uint16_t) (0xB875 + i));
  tg_bus_write16(0x4204, 0x0040);
  tg_bus_write8(0x4206, div);
  uint16_t q = div ? (uint16_t) (0x40 / div) : 0xFFFF;
  g_ram[0x17B5] = (uint8_t) (q + 1 + 0x30);
  uint16_t t = rom9f((uint16_t) (0xB8F5 + i));
  dp_set_d(r, 0x00, t);
  uint16_t x = dp_d(r, 0xAF);
  if((t >> 12) & 7) {
    time_digit(x, (uint16_t) (((t >> 12) & 7) | 0x3300), 0xC3);
    x = (uint16_t) (x + 8);
  }
  time_digit(x, (uint16_t) (((t >> 8) & 0x0F) | 0x3300), 0xCB);
  time_digit((uint16_t) (x + 8), (uint16_t) (((t >> 4) & 0x0F) | 0x3300), 0xD3);
  time_digit((uint16_t) (x + 16), (uint16_t) ((t & 0x0F) | 0x3300), 0xDB);
  uint16_t s = (t & 0x8000) ? 0x330A : 0x3316;
  ws(0x051A + x, s);
  ws(0x051E + x, (uint16_t) (s + 1));
  ws(0x0518 + x, 0xC0CE);
  ws(0x051C + x, 0xC0D6);
  ws(0x0319 + x, 0);
  ws(0x031D + x, 0);
  dp_set_d(r, 0xAF, (uint16_t) (x + 0x20));
  small_string(r, 0x309F, 0x4CB4, rom9f((uint16_t) (0xDB01 + c * 2)));
  box_edges(r, 0x0240, 0x00B4);
  small_string(r, 0x309F, 0x5CB4, rom9f((uint16_t) (0xB9B5 + ((c << 2 | k) << 1))));
  box_edges(r, 0x02C0, 0x00B4);
  small_string(r, 0x3000, 0x6CB4, 0x1797);
  Tg2PackSprites(r);
  ws(0x0022, w(0x0022) | 0x0002);
  wait1(r);
  vram_dma(r, 0x0700, 0x0700, 0x0000);
}

static void scroll_frame(TgRegs* r) {
  wait1(r);
  dp_set_d(r, 0xA5, (uint16_t) (dp_d(r, 0xA5) + 2));
}

void Tg2RaceInfoScreen(TgRegs* r) {
  regs(r);
  dp_set_d(r, 0xAF, 0);
  dp_set_d(r, 0xB9, 0x0200);
  Tg2PackSprites(r);
  r->x = 0xB809;
  r->a = 0x009F;
  Tg2SetScreenRegs(r);
  Tg2ClearVram(r);
  r->a = 0xE0E0; r->x = 0x0500; r->y = 0x0200;
  Tg2FillWords(r);
  r->a = 0x0000; r->x = 0x0700; r->y = 0x0020;
  Tg2FillWords(r);
  static const uint16_t pics[4][3] = {
    { 0x009E, 0xEDC0, 0x6000 }, { 0x009F, 0xBDE7, 0x7000 },
    { 0x009C, 0xA1B0, 0x1000 }, { 0x009D, 0x95F7, 0x2040 } };
  for(int p = 0; p < 4; p++) {
    r->a = pics[p][0]; r->x = pics[p][1]; r->y = pics[p][2];
    Tg2UnpackToVram(r);
  }
  // The track's entry in the bank $87 tables.
  uint16_t e = tg_read16(0x870000 | (uint16_t) (w(0x1CE5) * 2 + tg_read16(0x870000 | (uint16_t) (w(0x1CE1) * 2 + tg_read16(0x878000)))));
  dp_set_d(r, 0x20, e);
  r->y = tg_read16(0x870000 | e);
  r->a = (uint16_t) (tg_read16(0x870000 | (uint16_t) (e + 2)) | 0x7F00);
  r->x = r->y;
  r->y = 0;
  Tg2Unpack(r);
  dp_set_d(r, 0x00, 0x1640);
  r->y = tg_read16(0x870000 | (uint16_t) (e + 6));
  r->x = tg_read16(0x870000 | (uint16_t) (e + 4));
  r->a = 0x007F;
  Tg2PixelsToTiles(r);
  r->x = tg_read16(0x878004); r->a = 0x7F87; r->y = 0;
  Tg2Unpack(r);
  // The track picture's palette, 24-bit colours to the SNES's 15.
  uint16_t x = tg_read16(0x870000 | (uint16_t) (e + 0x0A));
  for(uint16_t y = 0x1440; y != 0x1460; y += 2, x += 3) {
    uint16_t v = (uint16_t) ((g_ram[0x10000 + (uint16_t) (x + 2)] & 0xF8) << 7);
    v |= (uint16_t) ((g_ram[0x10000 + (uint16_t) (x + 1)] & 0xF8) << 2);
    v |= (uint16_t) ((g_ram[0x10000 + x] & 0xF8) >> 3);
    ws(y, v);
  }
  ws(0x145E, tg_read16(0x870000 | (uint16_t) (e + 0x10)));
  r->x = tg_read16(0x878002); r->a = 0x7F87; r->y = 0;
  Tg2Unpack(r);
  uint16_t m = tg_read16(0x870000 | (uint16_t) (e + 8));
  for(uint16_t n = 0; n < 0x200; n++) {
    uint32_t adr = 0x7F0000 | (uint16_t) (m + n * 2);
    tg_bus_write16(adr, (uint16_t) (tg_read16(adr) + 0x1864));
  }
  move_bytes(0x7F, m, 0x7F, 0x0000, 0x0200);
  move_bytes(0x7F, (uint16_t) (m + 0x0200), 0x7F, 0x1000, 0x0200);
  move_bytes(0x7F, 0x1000, 0x7F, 0x0700, 0x0200);
  vram_dma(r, 0x0700, 0x0700, 0x0800);
  r->a = 0x7F9E; r->x = 0xED01; r->y = 0x0700;
  Tg2Unpack(r);
  move_bytes(0x7F, 0x0900, 0x7F, 0x0200, 0x0500);
  r->a = 0x7F80; r->x = 0x8800; r->y = 0x0700;
  Tg2Unpack(r);
  copy_block(r, 0x0E00, 0x0300, 0x000C, 0x000C, 0x0028);
  vram_dma(r, 0x0700, 0x0000, 0x0400);
  move_bytes(0x9F, 0xBD3D, 0x00, 0x1380, 0x00A0);
  move_bytes(0x9F, 0xBD3D, 0x00, 0x1480, 0x0040);
  move_bytes(0x9D, 0xB152, 0x00, 0x1460, 0x0020);
  move_bytes(0x9F, 0xBCDC, 0x00, 0x1797, 0x0021);
  ws(0x0022, w(0x0022) | 0x0001);
  race_info_text(r);
  // HDMA channel 1 on, then the picture scrolls in, waits for a button and scrolls out.
  tg_bus_write16(0x4310, 0x0F42);
  tg_bus_write16(0x4312, 0xB84D);
  tg_bus_write8(0x4317, 0x9F);
  tg_bus_write8(0x4314, 0x9F);
  wait_frames(1);
  tg_bus_write8(0x420C, 0x02);
  dp_set_d(r, 0xA5, 0x0080);
  ws(0x012E, 0x0F00);
  ws(0x012C, 0x0080);
  do scroll_frame(r); while(w(0x012C) != 0);
  for(;;) {
    scroll_frame(r);
    uint16_t b = w(0x012A);
    ws(0x012A, b & (uint16_t) ~0xD0C0);
    if(b & 0xD0C0) break;
  }
  Tg2SetFadeIn(r);
  ws(0x012E, 0);
  ws(0x012C, 0xFF80);
  do scroll_frame(r); while(w(0x012C) != 0);
  tg_bus_write16(0x420B, 0);
  r->a = 0;
  r->x = 0x0700;
  r->y = 0;
  r->n = false;
  r->z = true;
  r->c = true;
}

// ---- $1F:C492: the title screen and its menu ----

void Tg2NewSeason(TgRegs* r);

// Takes a press of any of `mask` from $012A (TRB): true when one was there.
static bool take(uint16_t mask) {
  uint16_t b = w(0x012A);
  ws(0x012A, b & (uint16_t) ~mask);
  return (b & mask) != 0;
}

// The picture scrolls out ($012C counts down in the NMI).
static void scroll_out(void) {
  ws(0x012E, 0);
  ws(0x012C, 0xFF80);
  while(w(0x012C) != 0) tg_port_wait();
}


// ---- $1F:CC35: the password screen ----

void Tg2MakePassword(TgRegs* r);
void Tg2PasswordPictures(TgRegs* r);

enum { PW_GO_ON, PW_TITLE, PW_PRACTICE, PW_DONE };

static void pw_put(uint16_t i, uint8_t v) { g_ram[0x17D3 + rom9f((uint16_t) (0xD228 + i * 2))] = v; }

static void pw_clear(uint16_t i) {
  uint16_t x = rom9f((uint16_t) (0xD228 + i * 2));
  g_ram[0x17D3 + x] = 0;
  g_ram[0x17D9 + x] = 0;
}

// The cursor's moves over the letters ($A5): right $CE6B, left $CEA6, down $CEE8, up $CF00.
static void pw_right(TgRegs* r) {
  uint16_t a = (uint16_t) (dp_d(r, 0xA5) + 1);
  if(a == 0x0E) a = 0;
  else if(a == 0x1C) a = 0x0E;
  else if(a == 0x27) a = 0x28;
  else if(a == 0x29 || a == 0x2A) a = 0x1C;
  else if(a == 0x20) a = 0x26;
  dp_set_d(r, 0xA5, a);
}
static void pw_left(TgRegs* r) {
  uint16_t a = (uint16_t) (dp_d(r, 0xA5) - 1);
  if(a == 0xFFFF) a = 0x0D;
  else if(a == 0x0D) a = 0x1B;
  else if(a == 0x26) a = 0x1F;
  else if(a == 0x1B) a = 0x29;
  else if(a == 0x28) a = 0x27;
  else if(a == 0x25) a = 0x1F;
  dp_set_d(r, 0xA5, a);
}
static void pw_down(TgRegs* r) {
  uint16_t a = (uint16_t) (dp_d(r, 0xA5) + 0x0E);
  if(a < 0x20 || (a >= 0x26 && a < 0x2A)) dp_set_d(r, 0xA5, a);
}
static void pw_up(TgRegs* r) {
  uint16_t a = dp_d(r, 0xA5);
  if(a >= 0x0E) dp_set_d(r, 0xA5, (uint16_t) (a - 0x0E));
}

// The entered letters' blinking cursor ($CE4D / $D047).
static void pw_blink(TgRegs* r) {
  pw_put(dp_d(r, 0xA9), (g_ram[0x01AB] & 0x10) ? 0x5F : 0x00);
  g_ram[0x01AB]++;
}

static void pw_text(TgRegs* r, uint16_t y2, uint16_t x2, uint16_t a2) {
  wait1(r);
  dp_set_d(r, 0xAF, 0);
  r->y = 0x8000; r->x = 0x0880; r->a = 0x3098;
  Tg2DrawBigString(r);
  r->y = y2; r->x = x2; r->a = a2;
  Tg2DrawBigString(r);
}

static void pw_letters(TgRegs* r) {
  r->y = 0x17D3; r->x = 0x7E36; r->a = 0x3000;
  Tg2DrawBigString(r);
  Tg2PackSprites(r);
  ws(0x0022, 0x0002);
}

// $CFB9: the password given: when it is good, the season goes on from it; when not, the
// screen says so ($D027) until a button (back to the letters) or START (the title).
static int pw_enter(TgRegs* r) {
  for(uint16_t i = 0; i < 0x16; i++) g_ram[0x1780 + i] = g_ram[0x17D7 + i * 6];
  call(r, 0xD4B3, 0xCFD7);
  if(!r->c) {
    if(w(0x01E3) & 0x8000) {  // from practice: that country's first race
      uint16_t c = w(0x1CE1);
      tg_bus_write8(0x211B, (uint8_t) c);
      tg_bus_write8(0x211B, (uint8_t) (c >> 8));
      tg_bus_write8(0x211C, 0x0A);
      uint16_t x = tg_read16(0x2134);
      ws(0x1D19, x);
      if(w(0x1CDB)) ws(0x1D42, x);
    }
    ws(0x1CE5, 0);
    for(uint16_t x = 0x28; x != 0; x -= 2) ws(0x1DC3 + x, 0);
    scroll_out();
    ws(0x1DF1, 0);
    return PW_DONE;
  }
  for(;;) {
    pw_text(r, 0xA879, 0x3E80, 0x309D);
    pw_blink(r);
    pw_letters(r);
    if(!(w(0x0130) & 0x7FFF)) return PW_TITLE;
    if(take(0x1000)) {
      ws(0x012E, 0);
      ws(0x012C, 0xFF80);
    }
    if(take(0xE0C0)) {
      Tg2SetFadeIn(r);
      dp_set_d(r, 0xAB, 0x0010);
      return PW_GO_ON;
    }
  }
}

// $CF0B: a button on the cursor's letter: END enters the password, the arrows take a
// letter back, the others add theirs.
static int pw_select(TgRegs* r) {
  uint16_t a = dp_d(r, 0xA5);
  if(a == 0x28 || a == 0x29) {
    Tg2SetFadeIn(r);
    return pw_enter(r);
  }
  if(dp_d(r, 0x1E) & 0x8000) {  // the first letter clears the old password
    dp_set_d(r, 0x1E, 0);
    dp_set_d(r, 0xA9, 0);
    for(uint16_t i = 0; i < 0x15; i++) pw_clear(i);
  }
  uint16_t n = dp_d(r, 0xA9);
  if(a == 0x26 || a == 0x27) {
    if(n != 0) {
      Tg2SetFadeIn(r);
      dp_set_d(r, 0xA9, --n);
      pw_clear(n);
    }
  } else if(n != 0x16) {
    Tg2SetFadeIn(r);
    pw_put(n, (uint8_t) rom9f((uint16_t) (0xD202 + a)));
    dp_set_d(r, 0xA9, (uint16_t) (n + 1));
  }
  dp_set_d(r, 0xAB, 0x0010);
  return PW_GO_ON;
}

static int password(TgRegs* r) {
  r->x = 0xD2E2; r->a = 0x009F;
  Tg2SetScreenRegs(r);
  r->a = 0x009C; r->x = 0xA9D0; r->y = 0x0800;
  Tg2UnpackToVram(r);
  Tg2PasswordPictures(r);
  r->y = 0; r->a = 0x7F9D; r->x = 0xA8C4;
  Tg2Unpack(r);
  vram_dma(r, 0x0700, 0x0000, 0x0000);
  vram_dma(r, 0x0700, 0x0E00, 0x0400);
  r->a = 0x009D; r->x = 0xA8AC; r->y = 0x5000;
  Tg2UnpackToVram(r);
  move_bytes(0x9D, 0xB0B2, 0x00, 0x1380, 0x00C0);
  move_bytes(0x9D, 0xB012, 0x00, 0x1480, 0x0020);
  dp_set_d(r, 0xAF, 0);
  dp_set_d(r, 0xB9, 0x0200);
  Tg2PackSprites(r);
  ws(0x0022, 0x0003);
  ws(0x0130, 0);
  ws(0x012E, 0x0F00);
  ws(0x012C, 0x0080);
  dp_set_d(r, 0xA7, 0);
  dp_set_d(r, 0xA5, 0x0028);
  move_bytes(0x9F, 0xD256, 0x00, 0x17D3, 0x008C);
  dp_set_d(r, 0xA9, 0);
  dp_set_d(r, 0xAD, 0);
  if(!(dp_d(r, 0xE3) & 0x8000)) {  // the season so far as a password
    Tg2MakePassword(r);
    dp_set_d(r, 0x1E, 0xFFFF);
    for(uint16_t i = 0; i < 0x16; i++) g_ram[0x17D7 + i * 6] = g_ram[0x1780 + i];
    dp_set_d(r, 0xA9, 0x16);
    dp_set_d(r, 0xAD, 0x16);
  }
  for(;;) {
    pw_text(r, 0xF72E, 0x2725, 0x309E);
    // The cursor's sprites.
    uint16_t y = dp_d(r, 0xAF), i = (uint16_t) (dp_d(r, 0xA5) * 4);
    uint16_t off = rom9f((uint16_t) (0xD0A2 + i)), x = rom9f((uint16_t) (0xD0A0 + i));
    uint16_t t = rom9f((uint16_t) (0xD158 + x));
    do {
      ws(0x0502 + y, t);
      ws(0x0500 + y, (uint16_t) (rom9f((uint16_t) (0xD15A + x)) + off));
      ws(0x0301 + y, rom9f((uint16_t) (0xD15C + x)));
      x = (uint16_t) (x + 6);
      y = (uint16_t) (y + 4);
    } while((t = rom9f((uint16_t) (0xD158 + x))) != 0);
    dp_set_d(r, 0xAF, y);
    pw_letters(r);
    int res = PW_GO_ON;
    if(take(0x0100)) { Tg2SetFadeIn(r); pw_right(r); dp_set_d(r, 0xA7, 0x10); }
    if(take(0x0200)) { Tg2SetFadeIn(r); pw_left(r); dp_set_d(r, 0xA7, 0x10); }
    if(take(0x0400)) { Tg2SetFadeIn(r); pw_down(r); dp_set_d(r, 0xA7, 0x10); }
    if(take(0x0800)) { Tg2SetFadeIn(r); pw_up(r); dp_set_d(r, 0xA7, 0x10); }
    if(take(0xE0C0)) {
      Tg2SetFadeIn(r);
      if((res = pw_select(r)) != PW_GO_ON) return res;
      dp_set_d(r, 0xA7, 0x10);
    }
    if(take(0x1000)) {
      Tg2SetFadeIn(r);
      scroll_out();
      return (w(0x01E3) & 0x8000) ? PW_PRACTICE : PW_TITLE;
    }
    uint16_t held = dp_d(r, 0x28);
    if(held & 0xEFC0) {  // held: repeats
      if(dp_d(r, 0xA7)) {
        dp_set_d(r, 0xA7, (uint16_t) (dp_d(r, 0xA7) - 1));
      } else {
        if(held & 0x0100) { Tg2SetFadeIn(r); pw_right(r); }
        if(held & 0x0200) { Tg2SetFadeIn(r); pw_left(r); }
        if(held & 0x0400) { Tg2SetFadeIn(r); pw_down(r); }
        if(held & 0x0800) { Tg2SetFadeIn(r); pw_up(r); }
        if(held & 0xE0C0) {
          if((res = pw_select(r)) != PW_GO_ON) return res;
        }
        dp_set_d(r, 0xA7, 0x0004);
      }
    }
    pw_blink(r);
  }
}

// ---- $1F:C738: practice: pick a country (those reached so far) ----

static void copy_block_e(TgRegs* r, uint16_t src, uint16_t dest, uint16_t w4, uint16_t h6, uint16_t a,
                         uint16_t e) {
  dp_set_d(r, 0x0E, e);
  dp_set_d(r, 0x00, src);
  dp_set_d(r, 0x02, dest);
  dp_set_d(r, 0x04, w4);
  dp_set_d(r, 0x06, h6);
  dp_set_d(r, 0x08, 0);
  dp_set_d(r, 0x0A, a);
  dp_set_d(r, 0x0C, 0x7F01);
  Tg2CopyMapBlock(r);
}

// $1D19 (and $1D42 with two players) = country * 10: its first race.
static void first_race_of(uint16_t c) {
  tg_bus_write8(0x211B, (uint8_t) c);
  tg_bus_write8(0x211B, (uint8_t) (c >> 8));
  tg_bus_write8(0x211C, 0x0A);
  uint16_t x = tg_read16(0x2134);
  ws(0x1D19, x);
  if(w(0x1CDB)) ws(0x1D42, x);
}

// A country's name: those not reached yet in another colour.
static void country_name(TgRegs* r, uint16_t c, uint16_t x, uint16_t open, uint16_t shut, bool big) {
  r->y = rom9f((uint16_t) (0xDB01 + c * 2));
  r->x = x;
  r->a = c > w(0x1CE3) ? shut : open;
  if(big) Tg2DrawBigString(r);
  else Tg2DrawSmallString(r);
}

static int practice(TgRegs* r) {
  r->x = 0xCA35; r->a = 0x009F;
  Tg2SetScreenRegs(r);
  dp_set_d(r, 0x30, 0x8000);
  r->a = 0x009C; r->x = 0xA9D0; r->y = 0x0800;
  Tg2UnpackToVram(r);
  r->a = 0x009E; r->x = 0xEDC0; r->y = 0x6000;
  Tg2UnpackToVram(r);
  r->y = 0; r->a = 0x7F9D; r->x = 0xAE80;
  Tg2Unpack(r);
  vram_dma(r, 0x0700, 0x0000, 0x0000);
  r->a = 0x7F9D; r->x = 0x8000; r->y = 0x8000;
  Tg2Unpack(r);
  r->x = 0x82E9; r->y = 0x2000; r->a = 0x7E9E;
  Tg2Unpack(r);
  // $7F:0000-06FF cleared: DMA from the multiplier (0 * 0).
  tg_bus_write8(0x211B, 0);
  tg_bus_write8(0x211B, 0);
  tg_bus_write8(0x211C, 0);
  tg_bus_write8(0x4304, 0x7F);
  tg_bus_write16(0x4300, 0x3481);
  tg_bus_write16(0x4302, 0x0000);
  tg_bus_write16(0x4305, 0x0700);
  tg_bus_write8(0x420B, 0x01);
  move_bytes(0x9D, 0xB0B2, 0x00, 0x1380, 0x00C0);
  move_bytes(0x9E, 0x80A0, 0x00, 0x1460, 0x0020);
  move_bytes(0x9D, 0xB032, 0x00, 0x1480, 0x0020);
  move_bytes(0x9D, 0xB052, 0x00, 0x14A0, 0x0020);
  move_bytes(0x9D, 0xB072, 0x00, 0x14C0, 0x0020);
  move_bytes(0x9D, 0xB092, 0x00, 0x14E0, 0x0020);
  dp_set_d(r, 0xAF, 0);
  dp_set_d(r, 0xB9, 0x0200);
  Tg2PackSprites(r);
  wait1(r);
  ws(0x0022, 0x0003);
  ws(0x0130, 0);
  ws(0x012E, 0x0F00);
  ws(0x012C, 0x0080);
  dp_set_d(r, 0xA5, w(0x1CE1));
  for(;;) {
    wait1(r);
    dp_set_d(r, 0xAF, 0);
    uint16_t c = dp_d(r, 0xA5);
    // The country's map: row (map number * $F2) of the maps at $7F:8520.
    tg_bus_write8(0x211B, 0xF2);
    tg_bus_write8(0x211B, 0x00);
    tg_bus_write8(0x211C, tg_read8(0x9FDB21 + c));
    copy_block_e(r, (uint16_t) (tg_read16(0x2134) + 0x8520), 0x0148, 0x000B, 0x000B, 0x002A, 0x2C00);
    r->y = 0xDAEC; r->x = 0x0880; r->a = 0x309F;
    Tg2DrawBigString(r);
    country_name(r, c, 0x48AC, 0x309F, 0x329F, true);
    uint16_t x = 0x3CAC;  // the three before it, above
    for(uint16_t k = c, n = 3; n != 0 && k != 0; n--) {
      k--;
      country_name(r, k, x, 0x349F, 0x369F, false);
      x = (uint16_t) (x - 0x0900);
    }
    x = 0x5CAC;  // and the three after, below
    for(uint16_t k = c, n = 3; n != 0 && k + 1 != 0x10; n--) {
      k++;
      country_name(r, k, x, 0x349F, 0x369F, false);
      x = (uint16_t) (x + 0x0900);
    }
    Tg2PackSprites(r);
    ws(0x0022, 0x0002);
    wait1(r);
    dp_set_d(r, 0x00, 0x0800);
    r->a = 0x007E; r->x = tg_read16(0x9C8000 + c * 2); r->y = 0x5000;
    Tg2VramDma(r);
    vram_dma(r, 0x0700, 0x0000, 0x0400);
    if(take(0x0800) && dp_d(r, 0xA5) != 0) {
      dp_set_d(r, 0xA5, (uint16_t) (dp_d(r, 0xA5) - 1));
      Tg2SetFadeIn(r);
    }
    if(take(0x0400) && dp_d(r, 0xA5) != 0x0F) {
      dp_set_d(r, 0xA5, (uint16_t) (dp_d(r, 0xA5) + 1));
      Tg2SetFadeIn(r);
    }
    if(take(0x1000)) {
      Tg2SetFadeIn(r);
      scroll_out();
      return PW_TITLE;
    }
    if(take(0xE0C0)) {
      Tg2SetFadeIn(r);
      c = dp_d(r, 0xA5);
      if(c <= w(0x1CE3)) {  // reached: a season from that country
        ws(0x1CE1, c);
        Tg2NewSeason(r);
        ws(0x1CE1, c);
        first_race_of(c);
        scroll_out();
        ws(0x1DF1, 0);
        return PW_DONE;
      }
      ws(0x1CE1, c);  // not yet: its password
      scroll_out();
      g_ram[0x01E4] = 0xFF;
      return PW_GO_ON;
    }
  }
}

// After the title's menu: practice (`at` PW_PRACTICE) or the password (PW_GO_ON), which
// lead to each other, until the title again (PW_TITLE) or a race (PW_DONE).
static int after_title(TgRegs* r, int at) {
  for(;;) {
    int res = at == PW_PRACTICE ? practice(r) : password(r);
    if(res == PW_TITLE || res == PW_DONE) return res;
    if(res == PW_PRACTICE) ws(0x1CE1, w(0x1CE3));
    at = res;
  }
}

static void title_done(TgRegs* r) {
  r->a = 0;
  r->n = false;
  r->z = true;
}

// $CBA8: the title's settings: difficulty and music.
static void settings_text(TgRegs* r) {
  r->y = 0xCC1B; r->a = 0x309F; r->x = 0x0000;
  Tg2DrawBigString(r);
  r->y = rom9f((uint16_t) (0xCBDE + w(0x1CDD) * 2)); r->a = 0x309F; r->x = 0x98AC;
  Tg2DrawBigString(r);
  r->y = rom9f((uint16_t) (0xCC0C + w(0x1CDF) * 2)); r->a = 0x309F; r->x = 0x9844;
  Tg2DrawBigString(r);
  Tg2PackSprites(r);
}

static void fade_rows(TgRegs* r, uint16_t bank, bool both) {
  r->x = 0xB034; r->y = 0x1482; r->a = bank;
  Tg2QueueFadeRow(r);
  if(both) {
    r->x = 0xAFF4; r->y = 0x13C2; r->a = bank;
    Tg2QueueFadeRow(r);
  }
  ws(0x0193, w(0x0193) | 0x0004);
}

// $CA79: the settings over the title: left and right pick music or difficulty, up and down
// change it; a button goes back to the title's menu ($CB70).
static void title_settings(TgRegs* r) {
  dp_set_d(r, 0xA5, 0);
  settings_text(r);
  ws(0x0022, w(0x0022) | 0x0003);
  wait_frames(2);
  fade_rows(r, 0x009D, true);
  for(;;) {
    ws(0x0022, w(0x0022) | 0x0002);
    wait_frames(1);
    vram_dma(r, 0x0700, dp_d(r, 0xA5) ? 0x0E00 : 0x0700, 0x0400);
    if(take(0x0100)) dp_set_d(r, 0xA5, 1);
    if(take(0x0200)) dp_set_d(r, 0xA5, 0);
    if(take(0xF0C0) && !(w(0x0193) & 0x0004)) {
      Tg2SetFadeIn(r);
      break;
    }
    if(dp_d(r, 0xA5) == 0) {  // $CB08: music on or off
      if(take(0x0800)) { Tg2SetFadeIn(r); ws(0x1CDF, 0); }
      if(take(0x0400)) { Tg2SetFadeIn(r); ws(0x1CDF, 1); }
    } else {  // $CB26: difficulty, each with its own country and progress
      uint16_t x = (uint16_t) (w(0x1CDD) * 2);
      ws(0x1CED + x, w(0x1CE1));
      ws(0x1CE7 + x, w(0x1CE3));
      if(take(0x0800)) { Tg2SetFadeIn(r); if(w(0x1CDD) != 0) ws(0x1CDD, w(0x1CDD) - 1); }
      if(take(0x0400)) { Tg2SetFadeIn(r); if(w(0x1CDD) != 2) ws(0x1CDD, w(0x1CDD) + 1); }
      x = (uint16_t) (w(0x1CDD) * 2);
      ws(0x1CE1, w(0x1CED + x));
      ws(0x1CE3, w(0x1CE7 + x));
    }
    settings_text(r);
  }
  fade_rows(r, 0x809D, true);
  while(w(0x0193) & 0x0004) tg_port_wait();
  fade_rows(r, 0x009D, false);
}

static void title(TgRegs* r) {
restart:
  regs(r);
  ws(0x0010, 0x92B8);
  dp_set_d(r, 0x99, 0);
  dp_set_d(r, 0x97, 0);
  dp_set_d(r, 0xAF, 0);
  dp_set_d(r, 0xB9, 0x0200);
  Tg2PackSprites(r);
  r->x = 0xC6F4; r->a = 0x009F;
  Tg2SetScreenRegs(r);
  r->a = 0x0098; r->x = 0x801C; r->y = 0x1000;
  Tg2UnpackToVram(r);
  r->a = 0x009E; r->x = 0xEDC0; r->y = 0x6000;
  Tg2UnpackToVram(r);
  r->y = 0; r->a = 0x7F9D; r->x = 0xAAFB;
  Tg2Unpack(r);
  vram_dma(r, 0x0700, 0x0000, 0x0000);
  vram_dma(r, 0x0700, 0x0700, 0x0400);
  move_bytes(0x9D, 0xAF94, 0x00, 0x1380, 0x0060);
  move_bytes(0x9D, 0xB032, 0x00, 0x1480, 0x0020);
  move_bytes(0x9D, 0xB152, 0x00, 0x1460, 0x0020);
  ws(0x0022, w(0x0022) | 0x0001);
  ws(0x0130, 0);
  ws(0x012E, 0x0F00);
  ws(0x012C, 0x0080);
  dp_set_d(r, 0xA5, 0x0258);
  for(;;) {
    uint16_t t = w(0x0014);
    while(w(0x0014) == t) tg_port_wait();
    dp_set_d(r, 0xAF, 0);
    dp_set_d(r, 0xA5, (uint16_t) (dp_d(r, 0xA5) - 1));  // counts, but nothing reads it
    // 1 player or 2 players.
    r->a = 0x309F; r->x = 0x5480; r->y = w(0x1CDB) ? 0xDAA2 : 0xDA96;
    Tg2DrawBigString(r);
    // The cursor's two sprites.
    uint16_t item = dp_d(r, 0x8D), y = dp_d(r, 0xAF);
    ws(0x0500 + y, rom9f((uint16_t) (0xC6E0 + item * 2)));
    ws(0x0502 + y, 0x30E2);
    ws(0x0301 + y, 0x0100);
    ws(0x0504 + y, rom9f((uint16_t) (0xC6EA + item * 2)));
    ws(0x0506 + y, 0x70E2);
    ws(0x0305 + y, 0x0100);
    dp_set_d(r, 0xAF, (uint16_t) (y + 8));
    Tg2PackSprites(r);
    ws(0x0022, w(0x0022) | 0x0002);
    if(take(0x0400)) {  // down
      dp_set_d(r, 0x8D, item + 1 == 5 ? 0 : item + 1);
      Tg2SetFadeIn(r);
      dp_set_d(r, 0xA5, 0x0258);
    }
    if(take(0x0800)) {  // up
      item = dp_d(r, 0x8D);
      dp_set_d(r, 0x8D, item == 0 ? 4 : item - 1);
      Tg2SetFadeIn(r);
      dp_set_d(r, 0xA5, 0x0258);
    }
    if(take(0x0200) && dp_d(r, 0x8D) == 0) {  // left: 1 player
      Tg2SetFadeIn(r);
      ws(0x1CDB, 0);
      dp_set_d(r, 0xA5, 0x0258);
    }
    if(take(0x0100) && dp_d(r, 0x8D) == 0) {  // right: 2 players
      Tg2SetFadeIn(r);
      ws(0x1CDB, 1);
      dp_set_d(r, 0xA5, 0x0258);
    }
    if(!take(0xF0C0)) continue;
    Tg2SetFadeIn(r);
    switch(dp_d(r, 0x8D)) {
    case 0:  // players: toggles
      ws(0x1CDB, w(0x1CDB) ^ 1);
      continue;
    case 1:  // start: a new season
      scroll_out();
      Tg2NewSeason(r);
      ws(0x1DF1, 0);
      title_done(r);
      return;
    case 2:  // practice: the country select
      scroll_out();
      if(after_title(r, PW_PRACTICE) == PW_TITLE) goto restart;
      title_done(r);
      return;
    case 3:  // options
      if(w(0x0193) & 0x0004) continue;
      r->x = 0xB034; r->y = 0x1482; r->a = 0x809D;
      Tg2QueueFadeRow(r);
      ws(0x0193, w(0x0193) | 0x0004);
      while(w(0x0193) & 0x0004) tg_port_wait();
      title_settings(r);
      dp_set_d(r, 0xA5, 0x0258);
      continue;
    default:  // password
      scroll_out();
      g_ram[0x01E4] = 0;
      if(after_title(r, PW_GO_ON) == PW_TITLE) goto restart;
      title_done(r);
      return;
    }
  }
}

// ---- $1F:C369: the opening screen, three seconds with its music ----

static void opening(TgRegs* r) {
  regs(r);
  ws(0x0010, 0x92B8);
  dp_set_d(r, 0x99, 0);
  dp_set_d(r, 0x97, 0);
  dp_set_d(r, 0xAF, 0);
  dp_set_d(r, 0xB9, 0x0200);
  Tg2PackSprites(r);
  r->x = 0xC6F4; r->a = 0x009F;
  Tg2SetScreenRegs(r);
  r->a = 0x0098; r->x = 0x801C; r->y = 0x1000;
  Tg2UnpackToVram(r);
  r->a = 0x009E; r->x = 0xEDC0; r->y = 0x6000;
  Tg2UnpackToVram(r);
  r->y = 0; r->a = 0x7F9D; r->x = 0xAAFB;
  Tg2Unpack(r);
  move_bytes(0x7F, 0x00C0, 0x7F, 0x1700, 0x0140);
  tg_bus_write16(0x7F173C, tg_read16(0x7F1C00));
  tg_bus_write16(0x7F173E, tg_read16(0x7F1C02));
  vram_dma(r, 0x0700, 0x1500, 0x0000);
  move_bytes(0x9D, 0xAF94, 0x00, 0x1380, 0x0020);
  move_bytes(0x9D, 0xA88C, 0x00, 0x13A0, 0x0020);
  move_bytes(0x9D, 0xB032, 0x00, 0x1480, 0x0020);
  small_string(r, 0x309D, 0x0080, 0xB172);
  Tg2PackSprites(r);
  ws(0x0022, w(0x0022) | 0x0003);
  tg_bus_write8(0x212C, 0x11);
  ws(0x0130, 0);
  ws(0x012E, 0x0F00);
  ws(0x012C, 0x0080);
  dp_set_d(r, 0xA5, 0x00B4);
  wait1(r);
  ws(0x01C1, 0);
  call(r, 0xF0A3, 0xC443);
  g_ram[0x01D0] = 0xFF;
  g_ram[0x01D1] = 0xFF;
  g_ram[0x01D2] = 0;
  g_ram[0x01D3] = 0;
  g_ram[0x01D5] = 0;
  g_ram[0x01D7] = 0;
  g_ram[0x01D8] = 0;
  g_ram[0x01D9] = 0;
  g_ram[0x01DA] = 0;
  g_ram[0x01CF] = 0;
  do {
    wait1(r);
    dp_set_d(r, 0xA5, (uint16_t) (dp_d(r, 0xA5) - 1));
  } while(dp_d(r, 0xA5) != 0);
  scroll_out();
}

// ---- $1F:EB7C: the race's results ----

void Tg2KeepStandings(TgRegs* r);

static void big_string(TgRegs* r, uint16_t a, uint16_t x, uint16_t y) {
  r->a = a;
  r->x = x;
  r->y = y;
  Tg2DrawBigString(r);
}

// Shows the players' places ($2D, $2F); true (carry) when no player made the top ten:
// the race's points are taken back and the menus come again. Else the season goes on:
// the next race, or the next country after four, or the end after the last.
static bool results(TgRegs* r) {
  dp_set_d(r, 0xAF, 0);
  dp_set_d(r, 0xB9, 0x0200);
  Tg2PackSprites(r);
  for(uint16_t x = 0; x < 0x28; x += 2) {
    uint16_t car = w(0x1D75 + x);
    if(car == 0) ws(0x002D, x >> 1);
    else if(car == 1) ws(0x002F, x >> 1);
  }
  bool out = w(0x002D) >= 10 && (!w(0x1CDB) || w(0x002F) >= 10);
  ws(0x0031, out ? 0xFFFF : 0);
  g_ram[0x01D2] = out ? 4 : 3;  // the music
  r->x = 0xB2BB; r->a = 0x009D;
  Tg2SetScreenRegs(r);
  r->a = 0x0098; r->x = 0x952F; r->y = 0x1000;
  Tg2UnpackToVram(r);
  r->a = 0x009E; r->x = 0xEDC0; r->y = 0x6000;
  Tg2UnpackToVram(r);
  r->y = 0; r->a = 0x7F98; r->x = 0xAA55;
  Tg2Unpack(r);
  vram_dma(r, 0x0700, (uint16_t) ((w(0x1CDB) ? 0 : 0x0E00) + (out ? 0x0700 : 0)), 0x0000);
  move_bytes(0x9C, 0xFAF5, 0x00, 0x1380, 0x0040);
  move_bytes(0x9F, 0xBD3D, 0x00, 0x1480, 0x0020);
  big_string(r, 0x2000, 0x1828, 0x1CF3);
  big_string(r, 0x209C, 0x18C0, tg_read16(0x9CFB53 + w(0x002D) * 2));
  big_string(r, 0x209C, 0x3080, tg_read16(0x9CFBF5 + w(0x002D) * 2));
  if(w(0x1CDB)) {
    big_string(r, 0x2000, 0x6028, 0x1D1C);
    big_string(r, 0x209C, 0x60C0, tg_read16(0x9CFB53 + w(0x002F) * 2));
    big_string(r, 0x209C, 0x7880, tg_read16(0x9CFBF5 + w(0x002F) * 2));
  }
  Tg2PackSprites(r);
  ws(0x0022, w(0x0022) | 0x0003);
  ws(0x0130, 0);
  ws(0x012E, 0x0F00);
  ws(0x012C, 0x0080);
  // Up to 200 frames, or a button from either player.
  for(uint16_t n = 0x00C8; n != 0; n--) {
    wait_frames(1);
    if(take(0xD0C0)) break;
    if(w(0x1CDB)) {
      uint16_t b = w(0x022A);
      ws(0x022A, b & (uint16_t) ~0xD0C0);
      if(b & 0xD0C0) break;
    }
  }
  scroll_out();
  if(out) {
    for(uint16_t x = 0; x < 0x28; x += 2) {
      uint16_t y = (uint16_t) (0x1DC5 + w(0x1D75 + x) * 2);
      ws(y, (uint16_t) (w(y) - rom9f((uint16_t) (0xE35D + x))));
    }
    return true;
  }
  ws(0x1D19, (uint16_t) (w(0x1D19) + rom9f((uint16_t) (0xEF46 + w(0x002D) * 2))));
  if(w(0x1CDB)) ws(0x1D42, (uint16_t) (w(0x1D42) + rom9f((uint16_t) (0xEF46 + w(0x002F) * 2))));
  uint16_t k = (uint16_t) (w(0x1CE5) + 1);
  if(!(k & 4)) {
    ws(0x1CE5, k);
    return false;
  }
  ws(0x1CE5, 0);  // the country's four races done
  Tg2KeepStandings(r);
  ws(0x1CE1, (uint16_t) (w(0x1CE1) + 1));
  if(w(0x1CE1) >= w(0x1CE3)) ws(0x1CE3, w(0x1CE1));
  if(w(0x1CE1) != 0x10) return false;
  call(r, 0xE52E, 0xED74);  // the last: the end
  return r->c;
}

// ---- $1F:DED5: the season's standings after a race ----

void Tg2ClearVram(TgRegs* r);
void Tg2DrawTime(TgRegs* r);
void Tg2NarrowText(TgRegs* r);

// A driver's name (car c) in the narrow font: with one player, the other cars' names come
// one later in the table (the second player's name is not shown).
static void driver_name(TgRegs* r, uint16_t car, uint16_t map) {
  uint16_t x = (uint16_t) (car * 2);
  if(!w(0x1CDB) && x != 0) x = (uint16_t) (x + 2);
  dp_set_d(r, 0x14, map);
  r->y = rom9f((uint16_t) (0xEF7D + x));
  r->a = 0x009F;
  dp_set_d(r, 0x0A, 0);
  Tg2NarrowText(r);
}

static void wait_press(void) {
  while(!take(0xD0C0)) tg_port_wait();
}

static void standings(TgRegs* r) {
  regs(r);
  dp_set_d(r, 0xAF, 0);
  dp_set_d(r, 0xB9, 0x0200);
  Tg2PackSprites(r);
  r->x = 0xB1EF; r->a = 0x009D;
  Tg2SetScreenRegs(r);
  Tg2ClearVram(r);
  static const uint16_t pics[4][3] = {
    { 0x009C, 0xA1B0, 0x1000 }, { 0x009E, 0xED01, 0x0C00 },
    { 0x009C, 0xF2CD, 0x1640 }, { 0x009D, 0xB343, 0x0000 } };
  for(int p = 0; p < 4; p++) {
    r->a = pics[p][0]; r->x = pics[p][1]; r->y = pics[p][2];
    Tg2UnpackToVram(r);
  }
  move_bytes(0x9D, 0xB152, 0x00, 0x1460, 0x0020);
  move_bytes(0x9C, 0xFE94, 0x00, 0x1480, 0x0020);
  move_bytes(0x9F, 0xBD3D, 0x00, 0x1380, 0x0020);
  r->a = 0x009E; r->x = 0xEDC0; r->y = 0x6000;
  Tg2UnpackToVram(r);
  g_ram[0x1CFE] = 0;
  g_ram[0x1D27] = 0;
  // The top ten's times (two the same are told apart by a frame).
  dp_set_d(r, 0x16, 0);
  dp_set_d(r, 0x14, 0);
  for(uint16_t x = 0, y = 0x00D5; x != 0x14; x += 2, y += 0x40) {
    uint16_t a = (uint16_t) (w(0x1D9D + x) + dp_d(r, 0x16));
    if(a == dp_d(r, 0x14)) {
      a++;
      dp_set_d(r, 0x16, (uint16_t) (dp_d(r, 0x16) + 1));
    }
    dp_set_d(r, 0x14, a);
    r->a = a;
    r->y = y;
    Tg2DrawTime(r);
  }
  // The race's points to each car, then the cars sorted by their points.
  for(uint16_t x = 0; x != 0x28; x += 2) {
    uint16_t y = (uint16_t) (0x1DC5 + w(0x1D75 + x) * 2);
    ws(y, (uint16_t) (rom9f((uint16_t) (0xE35D + x)) + w(y)));
  }
  for(uint16_t x = 0; x != 0x28; x += 2) {
    ws(0x17D7 + x, x >> 1);
    ws(0x17FF + x, w(0x1DC5 + x));
  }
  for(uint16_t x = 0; x != 0x26;) {
    uint16_t a = w(0x17FF + x), b = w(0x1801 + x);
    if(a < b) {
      ws(0x1801 + x, a);
      ws(0x17FF + x, b);
      uint16_t c = w(0x17D7 + x);
      ws(0x17D7 + x, w(0x17D9 + x));
      ws(0x17D9 + x, c);
      if(x != 0) {
        x -= 2;
        continue;
      }
    }
    x += 2;
  }
  // The points, straight to VRAM.
  dp_set_d(r, 0x00, 0x04BA);
  for(uint16_t y = 0; y != 0x28; y += 2) {
    tg_bus_write16(0x2116, dp_d(r, 0x00));
    uint16_t a = w(0x17FF + y), x;
    if(a >= 100) {
      uint16_t h = 0;
      while(a >= 100) {
        h++;
        a -= 100;
      }
      tg_bus_write16(0x2118, rom9f((uint16_t) (0xE295 + h * 2)));
      x = (uint16_t) (a * 2);
      tg_bus_write16(0x2118, rom9f((uint16_t) (0xE1CD + x)));
    } else {
      tg_bus_read8(0x2139);  // a blank first digit
      tg_bus_read8(0x213A);
      x = (uint16_t) (a * 2);
      uint16_t t = rom9f((uint16_t) (0xE1CD + x));
      if(t == 0x0064) {
        tg_bus_read8(0x2139);
        tg_bus_read8(0x213A);
      } else {
        tg_bus_write16(0x2118, t);
      }
    }
    tg_bus_write16(0x2118, rom9f((uint16_t) (0xE295 + x)));
    dp_set_d(r, 0x00, (uint16_t) (dp_d(r, 0x00) + 0x20));
  }
  // The names: the race's top ten, then the season's twenty.
  dp_set_d(r, 0x12, 0x2000);
  dp_set_d(r, 0x16, 0x0100);
  for(uint16_t x = 0, y = 0x00C4; x != 0x14; x += 2, y += 0x40) driver_name(r, w(0x1D75 + x), y);
  for(uint16_t x = 0, y = 0x04A2; x != 0x28; x += 2, y += 0x20) driver_name(r, w(0x17D7 + x), y);
  Tg2PackSprites(r);
  ws(0x0022, w(0x0022) | 0x0003);
  ws(0x0130, 0);
  ws(0x012E, 0x0F00);
  ws(0x012C, 0x0080);
  while(w(0x012C) != 0) tg_port_wait();
  wait_press();
  // A button scrolls down to the season's standings.
  for(uint16_t x = 3, n = 0x40; n != 0; n--, x += 4) {
    wait_frames(1);
    tg_bus_write8(0x210E, (uint8_t) x);
    tg_bus_write8(0x210E, (uint8_t) (x >> 8));
  }
  wait_press();
  scroll_out();
}

// ---- $1F:D904: after a country's four races, its password ----

static void country_password(TgRegs* r) {
  regs(r);
  r->x = 0xD2E2; r->a = 0x009F;
  Tg2SetScreenRegs(r);
  r->a = 0x009C; r->x = 0xA9D0; r->y = 0x0800;
  Tg2UnpackToVram(r);
  Tg2PasswordPictures(r);
  r->y = 0; r->a = 0x7F9D; r->x = 0xA8C4;
  Tg2Unpack(r);
  vram_dma(r, 0x0700, 0x0700, 0x0000);
  vram_dma(r, 0x0700, 0x1500, 0x0400);
  r->a = 0x009D; r->x = 0xA8AC; r->y = 0x5000;
  Tg2UnpackToVram(r);
  move_bytes(0x9D, 0xB0B2, 0x00, 0x1380, 0x00C0);
  move_bytes(0x9D, 0xB012, 0x00, 0x1480, 0x0020);
  dp_set_d(r, 0xAF, 0);
  dp_set_d(r, 0xB9, 0x0200);
  Tg2PackSprites(r);
  ws(0x0022, 0x0003);
  ws(0x0130, 0);
  ws(0x012E, 0x0F00);
  ws(0x012C, 0x0080);
  dp_set_d(r, 0xA7, 0);
  dp_set_d(r, 0xA5, 0x0028);
  move_bytes(0x9F, 0xD256, 0x00, 0x17D3, 0x008C);
  Tg2MakePassword(r);
  for(uint16_t i = 0; i < 0x16; i++) g_ram[0x17D7 + i * 6] = g_ram[0x1780 + i];
  dp_set_d(r, 0xA9, 0x0016);
  do {
    wait1(r);
    dp_set_d(r, 0xAF, 0);
    big_string(r, 0x3098, 0x1880, 0x8011);
    big_string(r, 0x3000, 0x3636, 0x17D3);
    Tg2PackSprites(r);
    ws(0x0022, 0x0002);
  } while(!take(0xF0C0));
  scroll_out();
}
