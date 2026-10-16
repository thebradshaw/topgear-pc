#include "port.h"

#include "../coverage.h"
#include "snes.h"

uint8_t* g_ram;
static bool g_declined;

void tg_port_decline(void) { g_declined = true; }

bool tg_port_take_declined(void) {
  bool d = g_declined;
  g_declined = false;
  return d;
}
static Snes* g_snes;
static const uint8_t* g_rom;

void tg_port_bind(Snes* snes, const uint8_t* rom) {
  g_snes = snes;
  g_ram = snes->ram;
  g_rom = rom;
}

uint8_t tg_bus_read8(uint32_t adr) {
  return snes_read(g_snes, adr);
}

void tg_bus_write8(uint32_t adr, uint8_t val) {
  snes_write(g_snes, adr, val);
}

const uint8_t* tg_rom(uint32_t adr) {
  return g_rom + tg_lorom_offset(adr);
}

uint8_t tg_read8(uint32_t adr) {
  int32_t off = tg_lorom_offset(adr);
  if(off >= 0) return g_rom[off];
  uint8_t bank = (uint8_t) (adr >> 16);
  if(bank == 0x7e || bank == 0x7f) return g_ram[adr & 0x1ffff];
  if((bank & 0x40) == 0 && (adr & 0xffff) < 0x2000) return g_ram[adr & 0x1fff];
  return snes_read(g_snes, adr);
}
