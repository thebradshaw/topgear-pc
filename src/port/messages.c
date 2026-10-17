// Race HUD messages (bank $00): lap, position, low fuel and pit messages that blink in
// each player's view.

#include "port.h"
#include "variables.h"

// Per-player state, 18 bytes from RaceP1Messages / RaceP2Messages.
enum {
  kPending = 0x00,    // message queued by other code (added to pending_base)
  kLastPos = 0x02,    // position last announced
  kFuelWarned = 0x04, // $FFFF once the low fuel message has been shown
  kLastLap = 0x06,    // lap last announced
  kDelay = 0x08,      // frames before the message appears (counted down elsewhere)
  kCleared = 0x0A,    // set to the player's clear value when a message goes
  kId = 0x0C,         // message showing, or $FFFF
  kBlink = 0x0E,      // blink counter; bit 15 = message hidden
  kFlags = 0x10,      // from the $06:DB80 table: bit 15 blink, bit 14 stay, low bits times
};

typedef struct MessagePlayer {
  uint16_t vars;
  uint16_t fuel_low_id, fuel_empty_id;
  uint16_t lap_base, pos_base, pit_id, pending_base;
  uint16_t clear_value;
  uint16_t lap, position, shown_position, fuel, track_pos, pit_flags;
  uint16_t dp_flag;  // direct page word whose bit 15 skips the fuel and pit checks
} MessagePlayer;

static const MessagePlayer kPlayers[2] = {
  { RaceP1Messages, 0x1D, 0x1E, 0x14, 0x00, 0x7E, 0x1E, 0x0620,
    RaceP1Lap, RaceP1Position, RaceP1PositionShown, RaceP1Fuel, RaceP1TrackPos, RaceP1PitFlags, 0x40 },
  { RaceP2Messages, 0x5C, 0x5D, 0x53, 0x3F, 0x80, 0x5D, 0x0720,
    RaceP2Lap, RaceP2Position, RaceP2PositionShown, RaceP2Fuel, RaceP2TrackPos, RaceP2PitFlags, 0x53 },
};

// Message layouts: $06:DF16 points to a list of [VRAM address, word count, words...]
// ending with a zero address.
static void draw_message(uint16_t id, bool clear) {
  uint16_t y = rom16(0x06DF16 + (uint16_t) (id * 2));
  for(;;) {
    uint16_t adr = tg_read16(0x060000 | y);
    if(adr == 0) return;
    tg_bus_write16(0x2116, adr);
    uint16_t count = tg_read16(0x060000 | (uint16_t) (y + 2));
    y += 4;
    do {
      tg_bus_write16(0x2118, clear ? 0 : tg_read16(0x060000 | y));
      y += 2;
    } while(--count);
  }
}

static uint16_t var(const MessagePlayer* p, int off) { return ram16((uint16_t) (p->vars + off)); }
static void set_var(const MessagePlayer* p, int off, uint16_t v) { ram16_set((uint16_t) (p->vars + off), v); }

static void start_message(const MessagePlayer* p, uint16_t id) {
  uint16_t x = (uint16_t) (id * 2);
  set_var(p, kBlink, 0);
  set_var(p, kFlags, rom16(0x06DB80 + x));
  set_var(p, kDelay, rom16(0x06DC84 + x));
}

// Subtracts $800 from a track position, wrapping by the word at $0200 (never needed in
// the recordings).
static uint16_t back_800(uint16_t a) {
  if(a < 0x800) return (uint16_t) (a - 0x800 + ram16(0x0200));
  return (uint16_t) (a - 0x800);
}

static void update_player(TgRegs* r, const MessagePlayer* p) {
  uint16_t id = var(p, kId);
  bool fuel_message = id == p->fuel_low_id || id == p->fuel_empty_id;
  uint16_t lap = ram16(p->lap);
  bool keep_going = fuel_message && !(lap & 0x8000) && lap >= ram16(RaceLaps);
  if(!keep_going && var(p, kFlags) != 0) {
    // A message is up: wait out the delay, then blink it. Smooth motion (an enhancement)
    // runs three race updates to the original's one, so the blinking steps on every third.
    static unsigned ticks[2];
    if(tg_port_race_frames == 1 && ++ticks[p == &kPlayers[0] ? 0 : 1] % 3 != 0) return;
    if(var(p, kDelay) != 0) return;
    uint16_t blink = var(p, kBlink);
    if(blink == 0) {
      set_var(p, kBlink, 6);
      draw_message(var(p, kId), false);
    } else if(blink & 0x8000) {
      uint16_t next = (uint16_t) (blink - 1);
      if(!(next & 0x8000)) {
        if(var(p, kFlags) & 0x8000) draw_message(var(p, kId), false);
        next = 6;
      }
      set_var(p, kBlink, next);
    } else if(--blink != 0) {
      set_var(p, kBlink, blink);
    } else {
      uint16_t flags = var(p, kFlags);
      bool hide;
      if(flags & 0x4000) {
        hide = flags & 0x8000;
      } else if((flags & 0x3FFF) == 0) {
        draw_message(var(p, kId), true);
        set_var(p, kId, 0xFFFF);
        set_var(p, kFlags, 0);
        set_var(p, kPending, 0);
        set_var(p, kCleared, p->clear_value);
        return;
      } else {
        flags--;
        set_var(p, kFlags, flags);
        hide = flags & 0x8000;
      }
      if(hide) draw_message(var(p, kId), true);
      set_var(p, kBlink, 0x8007);
    }
    return;
  }

  if(!(id & 0x8000)) {
    draw_message(id, true);
    set_var(p, kDelay, 0);
    set_var(p, kCleared, p->clear_value);
    set_var(p, kId, 0xFFFF);
  }
  uint16_t diff = (uint16_t) (lap - var(p, kLastLap));
  if(diff != 0 && !(diff & 0x8000)) {
    set_var(p, kLastLap, lap);
    uint16_t left = (uint16_t) (ram16(RaceLaps) - lap - 1);
    if(left & 0x8000) {
      // Finished: announce the final position.
      uint16_t pos = ram16(p->shown_position);
      set_var(p, kLastPos, pos);
      uint16_t msg = (uint16_t) (pos + p->pos_base);
      set_var(p, kId, msg);
      set_var(p, kBlink, 0);
      set_var(p, kFlags, 0xC000);
      set_var(p, kDelay, rom16(0x06DC84 + (uint16_t) (msg * 2)));
      return;
    }
    uint16_t msg = (uint16_t) (left + p->lap_base);
    set_var(p, kId, msg);
    start_message(p, msg);
    return;
  }
  uint16_t pos = ram16(p->position);
  if(pos != var(p, kLastPos)) {
    set_var(p, kLastPos, pos);
    uint16_t msg = (uint16_t) (pos + p->pos_base);
    set_var(p, kId, msg);
    start_message(p, msg);
    return;
  }
  if(!(var(p, kFuelWarned) & 0x8000)) {
    if(ram16(p->fuel) < 0x3000) {
      set_var(p, kFuelWarned, 0xFFFF);
      set_var(p, kId, p->fuel_low_id);
      start_message(p, p->fuel_low_id);
      return;
    }
  } else if(ram16((uint16_t) (r->d + p->dp_flag)) & 0x8000) {
    if(ram16(p->fuel) >= 0x3000) set_var(p, kFuelWarned, 0);  // never happens in the recordings
  } else if(ram16(p->fuel) == 0) {
    set_var(p, kId, p->fuel_empty_id);
    start_message(p, p->fuel_empty_id);
    return;
  } else {
    // Pit message once per pass through the $800 before RacePitEntry - $800.
    uint16_t tp = ram16(p->track_pos);
    uint16_t a = back_800(ram16(RacePitEntry));
    bool near = false;
    if(a >= tp) {
      a = back_800(a);
      near = a < tp;
    }
    if(!near) {
      ram16_set(p->pit_flags, ram16(p->pit_flags) & ~0x4000);
    } else {
      uint16_t flags = ram16(p->pit_flags);
      ram16_set(p->pit_flags, flags | 0x4000);
      if(!(flags & 0x4000)) {
        uint16_t msg = (ram16((uint16_t) (r->d + 0xDF)) & 0x8000) ? p->pit_id : (uint16_t) (p->pit_id + 1);
        set_var(p, kId, msg);
        start_message(p, msg);
        return;
      }
    }
  }
  uint16_t pending = var(p, kPending);
  if(pending != 0) {
    set_var(p, kPending, 0);
    uint16_t msg = (uint16_t) (pending + p->pending_base);
    set_var(p, kId, msg);
    start_message(p, msg);
  }
}

// $00:DA75. Once per race update, for each player: starts lap, position, fuel and pit
// messages and blinks or clears the one showing. Tables are in bank $06. Leaves 16-bit A
// and index; A, X, Y and N/V/Z/C are don't-care.
void RaceUpdateMessages(TgRegs* r) {
  update_player(r, &kPlayers[0]);
  update_player(r, &kPlayers[1]);
}
