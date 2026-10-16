// VRAM uploads made during the race's vblank handler (bank $00).

#include "port.h"

// One DMA on channel 7 from a bank:address source to VRAM at word address vram.
static void dma_to_vram(uint8_t bank, uint16_t src, uint16_t bytes, uint16_t vram) {
  tg_bus_write8(0x420B, 0x00);
  tg_bus_write8(0x4374, bank);
  tg_bus_write16(0x4370, 0x1801);  // word writes to $2118
  tg_bus_write16(0x4372, src);
  tg_bus_write16(0x4375, bytes);
  tg_bus_write16(0x2116, vram);
  tg_bus_write8(0x420B, 0x80);
}

// Sends the next 64-byte block of a player's message tiles, from a zero-terminated list
// of bank $06 addresses; cursor_adr holds the list position (0 when done) and the next
// VRAM address follows it.
static void message_block(uint16_t cursor_adr, uint16_t x) {
  uint16_t vram = ram16((uint16_t) (cursor_adr + 2));
  ram16_set((uint16_t) (cursor_adr + 2), (uint16_t) (vram + 0x20));
  // The original sets the VRAM address before the DMA registers.
  tg_bus_write16(0x2116, vram);
  tg_bus_write8(0x420B, 0x00);
  tg_bus_write8(0x4374, 0x02);
  tg_bus_write16(0x4370, 0x1801);
  tg_bus_write16(0x4372, tg_read16(0x060000 | x));
  tg_bus_write16(0x4375, 0x0040);
  tg_bus_write8(0x420B, 0x80);
  x = (uint16_t) (x + 2);
  if(tg_read16(0x060000 | x) == 0) x = 0;
  ram16_set(cursor_adr, x);
}

// $00:D86A. Uploads one block of message tiles per vblank: player 1's when $1BB4 bit 15
// is set, otherwise player 2's first, each falling back to the other.
// Leaves 16-bit A and index; registers and flags are don't-care.
void RaceUploadMessageTiles(TgRegs* r) {
  (void) r;
  bool p2_first = !(ram16(0x1BB4) & 0x8000);
  uint16_t first = p2_first ? 0x0132 : 0x0120, second = p2_first ? 0x0120 : 0x0132;
  if(ram16(first) != 0) message_block(first, ram16(first));
  else if(ram16(second) != 0) message_block(second, ram16(second));
}

// While a player's pit state is -3 and the cache is on the other view this frame
// ($1BB4), streams 4 KiB of pit crew tiles from bank $03 into the view's VRAM, 1 KiB per
// vblank (the set chosen by the player's car), then moves the state on. Player 2's
// variables are $E above player 1's.
static void bank3_tiles(uint16_t state_adr, bool view_flag, uint16_t v, uint16_t car, uint16_t vram_base) {
  uint16_t state = ram16(state_adr);
  if(state != 0xFFFD || ((ram16(0x1BB4) & 0x8000) != 0) != view_flag) return;
  if(ram16((uint16_t) (0x0572 + v)) == 0) {
    ram16_set((uint16_t) (0x0572 + v), 1);
    ram16_set((uint16_t) (0x0574 + v), 4);
    ram16_set((uint16_t) (0x0576 + v), 0);
    ram16_set((uint16_t) (0x0578 + v), 0);
  }
  uint16_t src = (uint16_t) (((g_ram[car] & 3) << 12) + 0x99C0 + ram16((uint16_t) (0x0578 + v)));
  ram16_set((uint16_t) (0x0578 + v), (uint16_t) (ram16((uint16_t) (0x0578 + v)) + 0x0400));
  uint16_t vram = (uint16_t) (vram_base + ram16((uint16_t) (0x0576 + v)));
  ram16_set((uint16_t) (0x0576 + v), (uint16_t) (ram16((uint16_t) (0x0576 + v)) + 0x0200));
  dma_to_vram(0x03, src, 0x0400, vram);
  uint16_t left = (uint16_t) (ram16((uint16_t) (0x0574 + v)) - 1);
  ram16_set((uint16_t) (0x0574 + v), left);
  if(left == 0) ram16_set(state_adr, (uint16_t) (state - 1));
}

// $00:8FC0 (player 1: $1E86, VRAM $3000, car $1F8F) and $00:9046 (player 2: $1E88,
// $4000, car $1FAC). Leave 16-bit A and index; registers and flags are don't-care.
void RaceUploadBank3Tiles(TgRegs* r) {
  (void) r;
  bank3_tiles(0x1E86, true, 0x0000, 0x1F8F, 0x3000);
}

void RaceUploadBank3TilesP2(TgRegs* r) {
  (void) r;
  bank3_tiles(0x1E88, false, 0x000E, 0x1FAC, 0x4000);
}
