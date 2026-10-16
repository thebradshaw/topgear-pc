// The race's raster IRQ (RaceIrqHandler, $00:EC5B), which calls the stage at $0A through
// JSR ($000A) with A and the index registers 16-bit, D = 0 and DB = 0. Each stage sets
// the PPU up for the next band of the screen, and the next stage and the scanline it
// starts on.
//
// A stage's writes land later in the original, after its code has run, but the IRQ comes
// at dot 124 or later (HTIME), past the point where the line is drawn (dot 128 in
// LakeSnes) by the time the handler gets to the stage, and the original's writes are done
// well before the next line is. So writing at once has the same effect. The bottom view's
// stages end in a wait for horizontal blank and a mid-screen DMA ($00:BE78), which runs
// emulated: its writes have to land on particular dots.

#include <stddef.h>

#include "port.h"

#include "race_common.h"
#include "variables.h"

void RacePitScrollP1(TgRegs* r);
void RacePitScrollP2(TgRegs* r);
void RaceUpdateTileCache(TgRegs* r);
void RaceUploadMessageTiles(TgRegs* r);
void RacePitReloadP1(TgRegs* r);
void RacePitReloadP2(TgRegs* r);
void RaceUploadBank3Tiles(TgRegs* r);
void RaceUploadBank3TilesP2(TgRegs* r);
void RaceIrqTop(TgRegs* r);

static uint16_t z(uint8_t o) { return ram16(o); }
static uint8_t z8(uint8_t o) { return g_ram[o]; }
static void z_set(uint8_t o, uint16_t v) { ram16_set(o, v); }
static void z8_set(uint8_t o, uint8_t v) { g_ram[o] = v; }
static void w8(uint16_t adr, uint8_t v) { tg_bus_write8(adr, v); }
static void w16(uint16_t adr, uint16_t v) { tg_bus_write16(adr, v); }

// The end of every stage: the next stage, the line it starts on, and for some the dot.
static void next_stage(TgRegs* r, uint16_t stage, uint16_t line) {
  z_set(0x0A, stage);
  w16(0x4209, line);
  r->a = line;
}

// A view's sky: colour math off ($B3FE) or on ($B42F, $B462), the sprite size, and the
// BG2 scroll from direct page (top view $B0-$B3).
static void sky_obsel(uint8_t obsel, uint16_t windows, uint8_t math, uint8_t hofs_lo, uint8_t hofs_hi, uint8_t vofs_lo,
                      uint8_t vofs_hi) {
  w16(0x2123, windows);
  w8(0x2131, math);
  w8(0x2101, obsel);
  w8(0x210F, hofs_lo);
  w8(0x210F, hofs_hi);
  w8(0x2110, vofs_lo);
  w8(0x2110, vofs_hi);
}

static void sky(uint16_t windows, uint8_t math, uint8_t hofs_lo, uint8_t hofs_hi, uint8_t vofs_lo, uint8_t vofs_hi) {
  sky_obsel(0x01, windows, math, hofs_lo, hofs_hi, vofs_lo, vofs_hi);
}

// $00:B3FE: the top view when it's off.
void RaceIrqTopViewOff(TgRegs* r) {
  sky(0x0000, 0x00, z8(0xA6), z8(0xA7), 0x8F, 0x03);
  next_stage(r, z(0xC0), 0x006C);
}

// $00:B42F: the top view's sky with no road band (the renderer's second choice).
void RaceIrqTopViewSky(TgRegs* r) {
  sky(0x0230, z8(0x66), z8(0xB0), z8(0xB1), z8(0xB2), z8(0xB3));
  next_stage(r, z(0xC0), 0x006C);
}

// $00:B462: the top view's sky, then the road band ($00:B4C4) from line $AE at dot
// $013D.
void RaceIrqTopViewHorizon(TgRegs* r) {
  sky(0x0230, z8(0x66), z8(0xB0), z8(0xB1), z8(0xB2), z8(0xB3));
  next_stage(r, 0xB4C4, z(0xAE));
  w16(0x4207, ram16(0x013D));
  r->a = ram16(0x013D);
}

// $00:B49B: the top view with the road from the top: windows on, BG2 scrolled away.
void RaceIrqTopViewRoad(TgRegs* r) {
  w16(0x2123, 0x0320);
  w8(0x2110, 0x8F);
  w8(0x2110, 0x03);
  w8(0x2131, z8(0x67));
  w8(0x2101, 0x01);
  next_stage(r, z(0xC0), 0x006C);
}

// The bottom view's stages ($B511-$B5B6) are the same with its own sprite size and
// scroll ($BA-$C7), and end in $00:BE78: wait for the next horizontal blank to end, then
// with the screen off for a few dots, the bottom view's palette and the second half of the
// object list. Those writes have to land on particular dots, so $00:BE78 runs emulated.
static void mid_screen_dma(TgRegs* r) {
  tg_call_jsr(r, 0xBE78, 0xEC75, TG_MX_16);  // returns to RaceIrqHandler
}

// $00:B511: the bottom view when it's off.
void RaceIrqBottomViewOff(TgRegs* r) {
  sky_obsel(0x09, 0x0000, 0x00, z8(0xBA), z8(0xBB), 0x1F, 0x03);
  next_stage(r, 0xB6D1, 0x00E0);
  mid_screen_dma(r);
}

// $00:B545: the bottom view's sky with no road band.
void RaceIrqBottomViewSky(TgRegs* r) {
  sky_obsel(0x09, 0x0230, z8(0x66), z8(0xC4), z8(0xC5), z8(0xC6), z8(0xC7));
  next_stage(r, 0xB6D1, 0x00E0);
  mid_screen_dma(r);
}

// $00:B57B: the bottom view's sky, then the road band ($00:B5E2) from line $C2 at dot
// $013D.
void RaceIrqBottomViewHorizon(TgRegs* r) {
  sky_obsel(0x09, 0x0230, z8(0x66), z8(0xC4), z8(0xC5), z8(0xC6), z8(0xC7));
  next_stage(r, 0xB5E2, z(0xC2));
  w16(0x4207, ram16(0x013D));
  r->a = ram16(0x013D);
  mid_screen_dma(r);
}

// $00:B5B6: the bottom view with the road from the top.
void RaceIrqBottomViewRoad(TgRegs* r) {
  w16(0x2123, 0x0320);
  w8(0x2110, 0x1F);
  w8(0x2110, 0x03);
  w8(0x2131, z8(0x67));
  w8(0x2101, 0x09);
  next_stage(r, 0xB6D1, 0x00E0);
  mid_screen_dma(r);
}

// $00:B4C4 / $00:B5E2: from the horizon down, the road: windows on, and either BG2 scrolled
// away or (bit 7 of $0101) colour math from $67. Through D = $2100 in the original.
static void road_band(TgRegs* r, uint8_t vofs, uint16_t stage, uint16_t line) {
  if(!(g_ram[0x0101] & 0x80)) {
    w8(0x2110, vofs);
    w8(0x2110, 0x03);
    w16(0x2123, 0x0320);
  } else {
    uint8_t math = g_ram[0x0067];
    w16(0x2123, 0x0320);
    w8(0x2131, math);
  }
  r->d = 0;
  next_stage(r, stage, line);
  w16(0x4207, 0x007C);
  r->a = 0x007C;
}

void RaceIrqTopRoadBand(TgRegs* r) { road_band(r, 0x8F, z(0xC0), 0x006C); }
void RaceIrqBottomRoadBand(TgRegs* r) { road_band(r, 0x1F, 0xB6D1, 0x00E0); }

// $00:B6D1: line $E0, the start of vblank. Screen off, count the frame for the race loop
// ($0C), OAM and palette uploads when asked ($0590, $038C), then the top-of-frame work at
// $08 (RaceIrqTop during a race).
void RaceIrqVblank(TgRegs* r) {
  w8(0x2100, 0x80);
  z_set(0x0C, (uint16_t) (z(0x0C) + 1));
  if(ram16(0x0590) & 0x8000) {
    ram16_set(0x0590, 0);
    w16(0x2102, 0x0000);
    w8(0x420B, 0x00);
    w8(0x4374, 0x00);
    w16(0x4370, 0x0402);
    w16(0x4372, 0x0592);
    w16(0x4375, 0x0220);
    w8(0x420B, 0x80);
  }
  uint16_t oam = ram16(0x058E);
  w16(0x2102, oam);
  ram16_set(0x058E, (uint16_t) ((oam + 0x40) & 0x80C0));
  if(ram16(0x038C) & 0x8000) {
    ram16_set(0x038C, 0);
    w8(0x2121, 0x00);
    w8(0x420B, 0x00);
    w8(0x4374, 0x00);
    w16(0x4370, 0x2202);
    w16(0x4372, 0x038E);
    w16(0x4375, 0x0200);
    w8(0x420B, 0x80);
  }
  r->x = 0;
  uint16_t next = z(0x08);
  if(next == 0xB75A) {
    RaceIrqTop(r);
  } else if(next != 0xEC7E) {  // EC7E: RTS
    tg_call_jsr(r, next, 0xB759, TG_MX_16);
  }
}

// --- RaceIrqTop ($00:B75A) ---

// $00:B850-$00:B95C: the next command for the sound driver, once it has taken the last one
// (it echoes it on port 1). $3D is the last command, $3E one sent twice in a row, which
// is sent again next time, and $3F counts the commands, alternating between the
// players. Each player's events (bits of the drive flags, the crash and skid flags at
// $1E6D/$1E67 and their last values) come first, then their engine (port 2: RPM).
static void sound_commands(void) {
  uint8_t last = z8(0x3D);
  if(!(last & 0x80) && last != tg_apu_read8(0x2141)) return;
  uint8_t cmd;
  if(z8(0x3E) != 0) {
    cmd = z8(0x3E);
    goto send;
  }
  static const struct {
    uint16_t flags;                 // drive flags
    uint16_t crash, crash_last;     // $1E6D / $1E70
    uint16_t skid, skid_last;       // $1E67 / $1E6A
    uint16_t sent, rpm;             // $1E62 / $1E5E
    uint8_t c_pit, c_bump, c_crash, c_skid, c_engine;
  } kPlayers[2] = {
    { 0x1EF5, 0x1E6D, 0x1E70, 0x1E67, 0x1E6A, 0x1E62, 0x1E5E, 0x07, 0x0B, 0x05, 0x0A, 0x03 },
    { 0x1EF6, 0x1E6F, 0x1E71, 0x1E69, 0x1E6B, 0x1E64, 0x1E60, 0x12, 0x16, 0x10, 0x15, 0x0E },
  };
  int p;
  if(g_ram[0x1EF5] & 0x08) {
    if(g_ram[0x1EF6] & 0x08) {
      cmd = 0x00;
      goto check;
    }
    p = 1;
  } else if(g_ram[0x1EF6] & 0x08) {
    p = 0;
  } else {
    p = (z8(0x3F) & 0x01) ? 1 : 0;
  }
  {
    const uint16_t flags = kPlayers[p].flags;
    if(g_ram[flags] & 0x04) {
      g_ram[flags] = (uint8_t) ((g_ram[flags] & ~0x04) | 0x08);
      cmd = kPlayers[p].c_pit;
      goto check;
    }
    if(z8(0x3F) & 0x02) {
      if(g_ram[flags] & 0x80) {
        g_ram[flags] &= 0x7F;
        cmd = kPlayers[p].c_bump;
        goto check;
      }
      uint8_t a = g_ram[kPlayers[p].crash];
      if((a & 0x80) && !(g_ram[kPlayers[p].crash_last] & 0x80)) {
        g_ram[kPlayers[p].crash_last] = a;
        cmd = kPlayers[p].c_crash;
        goto check;
      }
      g_ram[kPlayers[p].crash_last] = a;
      a = g_ram[kPlayers[p].skid];
      if((a & 0x80) && !(g_ram[kPlayers[p].skid_last] & 0x80)) {
        g_ram[kPlayers[p].skid_last] = a;
        cmd = kPlayers[p].c_skid;
        goto check;
      }
      g_ram[kPlayers[p].skid_last] = a;
    }
    uint16_t engine = ram16(kPlayers[p].sent);
    if(engine != 0) {
      uint16_t rpm = ram16(kPlayers[p].rpm);
      if(rpm & 0x8000) rpm = 0;
      engine = (uint16_t) (rpm + 0x0400);
    }
    w16(0x2142, engine);
    cmd = kPlayers[p].c_engine;
    goto send;
  }
check:
  if(cmd == z8(0x3D)) {  // the same again: send nothing this time, this next time
    z8_set(0x3E, cmd);
    cmd = 0x00;
    goto out;
  }
send:
  z8_set(0x3E, 0);
  z8_set(0x3F, (uint8_t) (z8(0x3F) + 1));
out:
  w8(0x2141, cmd);
  z8_set(0x3D, cmd);
}

// $00:B965-$00:B9B3: a view's fade ($E1/$E2: negative fades out, positive in) of its
// brightness ($52/$65), one step a frame.
static void fade(uint8_t flag, uint8_t level) {
  uint8_t f = z8(flag);
  if(f == 0) return;
  uint8_t a = z8(level) & 0x0F;
  if(f & 0x80) {
    if(a == 0 || --a == 0) {
      z8_set(flag, 0);
      a |= 0x80;
    }
  } else if(a == 0x0F || ++a == 0x0F) {
    z8_set(flag, 0);
  }
  z8_set(level, a);
}

// $00:B9B7: a view's sky scroll ($1E96/$1E98 += $1E9A/$1E9C), as a 9-bit BG2 offset.
static uint16_t sky_scroll(uint16_t pos, uint16_t speed) {
  uint16_t v = (uint16_t) (ram16(pos) + ram16(speed));
  ram16_set(pos, v);
  uint16_t s = (uint16_t) (v << 8 | v >> 8);
  s = (uint16_t) (s << 1 | s >> 15);
  return s & 0x01FF;
}

// $00:B9E7-$00:BAB6: a player's flash colour ($03C0 / $0480, white unless flashing). $1E6C
// negative: bit 14 clear, fading from white by $0200 a frame; bits 14 and 13 clear, red
// rising by $20; bit 13 set, a fade by $0100.
static void flash(uint16_t state, uint16_t colour) {
  ram16_set(colour, 0x7FFF);
  uint16_t a = ram16(state);
  if(!(a & 0x8000)) return;
  if(!(a & 0x4000)) {
    ram16_set(state, (uint16_t) (a + 0x0200));
    uint16_t c = (uint16_t) (((a & 0x3E00) ^ 0x3E00) << 1);
    ram16_set(colour, c);
    c = (uint16_t) (((uint16_t) (c << 8 | c >> 8) << 3) | ram16(colour) | 0x001F);
    ram16_set(colour, c);
  } else if(!(a & 0x2000)) {
    ram16_set(state, (uint16_t) (a + 0x0020));
    ram16_set(colour, 0x001F);
  } else {
    uint16_t n = (uint16_t) (a + 0x0100);
    if(!(n & 0x8000) && n != 0) n--;
    ram16_set(state, n);
    uint16_t c = (uint16_t) ((a & 0x1F00) << 2);
    ram16_set(colour, c);
    c = (uint16_t) (((uint16_t) (c << 8 | c >> 8) << 3) | ram16(colour) | 0x001F);
    ram16_set(colour, c);
  }
}

// $00:BB08-$00:BB2E: the sky colours for the course from bank $08 into CGRAM $10.
static void sky_colours(uint16_t base) {
  uint16_t src = (uint16_t) (base + rom16(0x00DFE4 + (uint16_t) (ram16(CarTypeIndex) << 1)));
  w8(0x2121, 0x10);
  w8(0x420B, 0x00);
  w8(0x4374, 0x08);
  w16(0x4370, 0x2202);
  w16(0x4372, src);
  w16(0x4375, 0x0020);
  w8(0x420B, 0x80);
}

// $00:BAB9-$00:BBBD: the sky flashes of some courses (lightning): $0100 negative, $0102
// counts down to the next, then a sequence through $0100's low byte and $0106.
static void lightning(void) {
  uint16_t a = ram16(0x0100);
  if(!(a & 0x8000)) return;
  uint16_t wait = ram16(0x0102);
  if(wait & 0x8000) goto colour;
  if(wait != 0) {
    ram16_set(0x0102, (uint16_t) (wait - 1));
    goto colour;
  }
  uint16_t y = (uint16_t) (ram16(CarTypeIndex) << 1);
  if(a & 0x4000) {
    uint16_t next = ram16(0x0106);
    if(next & 0x8000 || next < 1) goto colour;
    if(next != 1) {
      ram16_set(0x0106, rom16(0x00E024 + y));
      goto colour;
    }
    a--;
    if(a == 0xC0DE) {
      ram16_set(0x0100, a);
      z_set(0x66, 0xB6B6);
      sky_colours(0x8A5A);
      goto colour;
    }
    if(a == 0xC000) {
      ram16_set(0x0106, (uint16_t) (ram16(0x0106) + 1));
      ram16_set(0x0102, (uint16_t) (ram16(0x0102) - 1));
      a = 0x8000;
    }
    ram16_set(0x0100, a);
    goto colour;
  }
  if(a == 0x80DF) {
    uint16_t next = ram16(0x0106);
    if(next & 0x8000 || next < 1) goto colour;
    if(next != 1) {
      ram16_set(0x0106, rom16(0x00E064 + y));
      goto colour;
    }
    z_set(0x66, 0xB494);
    sky_colours(0x8E5A);
    ram16_set(0x0106, (uint16_t) (ram16(0x0106) + 1));
    ram16_set(0x0102, (uint16_t) (ram16(0x0102) - 1));
    a = 0xC0DE;
  }
  ram16_set(0x0100, (uint16_t) (a + 1));
colour:
  w8(0x2132, (uint8_t) (((g_ram[0x0100] >> 4) & 0x1F) | 0xE0));
}

// $00:BBC2-$00:BC33: a player's blinking HUD lights: $010A/$010C count up with the speed,
// flipping a bit of $0110 at $2000; $010E flips bit 13 every other frame.
static void blink(uint16_t speed, uint16_t count, uint16_t bit) {
  uint16_t s = ram16(speed);
  if(s == 0) {
    ram16_set(count, 0);
    ram16_set(0x0110, ram16(0x0110) & (uint16_t) ~bit);
    return;
  }
  uint16_t t = (uint16_t) (s + ram16(count));
  if(t < 0x2000) {
    ram16_set(count, t);
  } else {
    ram16_set(count, t & 0x1FFF);
    ram16_set(0x0110, ram16(0x0110) ^ bit);
  }
}

static void cgram_colour(uint8_t index, uint16_t colour) {
  w8(0x2121, index);
  w8(0x2122, (uint8_t) colour);
  w8(0x2122, (uint8_t) (colour >> 8));
}

// $00:BCD6-$00:BE2D: a player's car on the pit lane (pit state $FFF8-$FFFC): braking
// into the pit, then pulling away, scrolling the pit scene by the speed.
static void pit_lane(TgRegs* r, uint16_t state, uint16_t speed, uint8_t scroll, void (*pit_scroll)(TgRegs*),
                     uint8_t light_a, uint8_t light_b) {
  uint16_t st = ram16(state);
  if(st < 0xFFF8 || st >= 0xFFFD) return;
  if(st == 0xFFFB || st == 0xFFFC) {
    uint16_t a = z(scroll) & 0x03FF;
    if(a >= 0x0188) {
      ram16_set(speed, (uint16_t) (ram16(speed) + 0x0029));  // ADC with the carry set
    } else {
      uint16_t v = 0;
      bool stop = a < 0x0080;
      if(!stop) {
        v = (uint16_t) (((uint16_t) (((uint16_t) (a - 0x0200)) >> 2) | 0xC000) + ram16(speed));
        stop = (v & 0x8000) != 0;
      }
      if(stop) {
        ram16_set(speed, 0);
        ram16_set(state, (uint16_t) (ram16(state) - 1));
      } else {
        ram16_set(speed, v);
      }
    }
  } else if(st == 0xFFF9) {
    uint16_t a = (uint16_t) (0x1B00 - ram16(speed));
    a = (uint16_t) (a << 8 | a >> 8);
    a = (uint16_t) (a << 1 | a >> 15);
    a = (uint16_t) (a << 1 | a >> 15);
    a &= 0x03FF;
    if(a >= 0x0200) a |= 0xFC00;
    ram16_set(speed, (uint16_t) (a + ram16(speed)));
  }
  z_set(scroll, (uint16_t) (z(scroll) - ((ram16(speed) >> 8) >> 3)));
  r->a = z(scroll);  // the pit scroll routines take the scroll in A
  pit_scroll(r);
  cgram_colour(light_a, 0x0C63);
  cgram_colour(light_b, 0x0C63);
}

// $00:B75A, from RaceIrqVblank. Every frame: the next object list goes to OAM; sound
// commands; fades, sky scroll, flashes and lightning; the HUD's blinking lights; cars on
// the pit lane; the palette; then the tile streaming and uploads, and the first raster
// stage of the frame ($AC). After a race update ($E3 negative), the views' new buffers
// and HDMA tables come in first.
uint32_t tg_port_race_irqs;

void RaceIrqTop(TgRegs* r) {
  tg_port_race_irqs++;
  if(z(0xE3) & 0x8000) {
    z_set(0xE3, 0);
    if(ram16(0x0106) == 0) ram16_set(0x0106, 1);
    if(!(z(0x40) & 0x8000)) {
      uint16_t t = z(0xCE); z_set(0xCE, z(0xC8)); z_set(0xC8, t);
      t = z(0xD2); z_set(0xD2, z(0xCC)); z_set(0xCC, t);
    }
    if(!(z(0x53) & 0x8000)) {
      uint16_t t = z(0xDA); z_set(0xDA, z(0xD4)); z_set(0xD4, t);
      t = z(0xDE); z_set(0xDE, z(0xD8)); z_set(0xD8, t);
    }
    z_set(0xD0, z(0xCA));
    z_set(0xDC, z(0xD6));
    z_set(0xE5, (uint16_t) ~z(0xE5));
    uint16_t scroll, window;
    if(z(0xE5) & 0x8000) {
      scroll = 0xBF07; window = 0x0EAF;
      z_set(0xE7, 0x0592); z_set(0xE9, 0x0C12); z_set(0xEB, 0x0E54);
    } else {
      scroll = 0xBEFA; window = 0x0E54;
      z_set(0xE7, 0x08D2); z_set(0xE9, 0x0D33); z_set(0xEB, 0x0EAF);
    }
    for(int c = 0; c < 5; c++) w16((uint16_t) (0x4302 + 0x10 * c), z(0xA0));
    w16(0x4352, scroll);
    w16(0x4362, window);
    z_set(0xC6, z(0xBE));
    z_set(0xC2, z(0xB8));
    z_set(0xC0, z(0xB6));
    z_set(0xB2, z(0xAA));
    z_set(0xAE, z(0xA4));
    z_set(0xAC, z(0xA2));
    z_set(0x0A, z(0xA2));
  }
  // Sprites: the object list through channel 7.
  w16(0x2102, 0x0000);
  w8(0x420B, 0x00);
  w8(0x4374, 0x00);
  w16(0x4370, 0x0402);
  w16(0x4372, z(0xD2));
  w16(0x4375, 0x0220);
  w8(0x420B, 0x80);
  w16(0x2102, z(0xD0) | 0x8000);

  if(ram16(0x1E58) & 0x8000) {  // paused or over: the sound driver's pause command
    w8(0x2141, 0x19);
    z8_set(0x3D, 0x19);
  } else {
    if(ram16(WarmBootFlag) == 0) sound_commands();
    ram16_set(0x1EAE, (uint16_t) (ram16(0x1EAE) + 1));
    fade(0xE1, 0x52);
    fade(0xE2, 0x65);
    z_set(0xB0, sky_scroll(0x1E96, 0x1E9A));
    z_set(0xC4, sky_scroll(0x1E98, 0x1E9C));
    if(ram16(0x1E58) == 0) {
      flash(0x1E6C, 0x03C0);
      flash(0x1E6E, 0x0480);
      lightning();
      blink(0x1E5A, 0x010A, 0x8000);
      blink(0x1E5C, 0x010C, 0x4000);
      ram16_set(0x010E, (uint16_t) (ram16(0x010E) - 1));
      if(ram16(0x010E) & 0x8000) {
        ram16_set(0x010E, 1);
        ram16_set(0x0110, ram16(0x0110) ^ 0x2000);
      }
      uint16_t f = ram16(0x0108);
      cgram_colour(0x8E, (f & 0x8000) ? 0x14BF : 0x0C6F);
      cgram_colour(0x9E, (f & 0x4000) ? 0x14BF : 0x0C6F);
      uint16_t b = ram16(0x0110);
      cgram_colour(0x87, (b & 0x8000) ? 0x0421 : 0x394A);
      cgram_colour(0x97, (b & 0x4000) ? 0x0421 : 0x394A);
      uint16_t c = (b & 0x2000) ? 0x0421 : 0x394A;
      cgram_colour(0xA7, c);
      cgram_colour(0xB7, c);
      pit_lane(r, 0x1E86, 0x1E5A, 0xA8, RacePitScrollP1, 0x87, 0x8E);
      pit_lane(r, 0x1E88, 0x1E5C, 0xBC, RacePitScrollP2, 0x97, 0x9E);
    }
  }
  // $00:BE32: the palette's first $C0 bytes from $038E, the brightness, and the uploads.
  w8(0x2121, 0x20);
  w8(0x420B, 0x00);
  w8(0x4374, 0x00);
  w16(0x4370, 0x2202);
  w16(0x4372, 0x038E);
  w16(0x4375, 0x00C0);
  w8(0x420B, 0x80);
  w8(0x2100, z8(0x52));
  RaceUpdateTileCache(r);
  RaceUploadMessageTiles(r);
  RacePitReloadP1(r);
  RacePitReloadP2(r);
  RaceUploadBank3Tiles(r);
  RaceUploadBank3TilesP2(r);
  // JMP ($00AC): the first raster stage.
  // Its RTS returns from RaceIrqVblank's JSR ($0008,X), so an emulated stage is called
  // with that return address.
  uint16_t stage = z(0xAC);
  if(stage == 0xEC7E) return;  // RTS
  const TgPortFunc* f = tg_port_find(stage);
  if(f != NULL) f->fn(r);
  else tg_call_jsr(r, stage, 0xB759, TG_MX_16);
}
