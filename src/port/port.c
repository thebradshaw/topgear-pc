#include "port.h"

#include "../coverage.h"
#include "snes.h"

uint8_t* g_ram;
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
