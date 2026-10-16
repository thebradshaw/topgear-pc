#include "port.h"

void RaceRevEngines(TgRegs* r);

// Every translated routine, sorted by address.
const TgPortFunc tg_port_funcs[] = {
  { 0x0088A1, "RaceRevEngines", RaceRevEngines, TG_CALL_JSR, TG_MX_16 },
};
const int tg_port_func_count = sizeof(tg_port_funcs) / sizeof(tg_port_funcs[0]);

const TgPortFunc* tg_port_find(uint32_t addr) {
  int lo = 0, hi = tg_port_func_count - 1;
  while(lo <= hi) {
    int mid = (lo + hi) / 2;
    if(tg_port_funcs[mid].addr == addr) return &tg_port_funcs[mid];
    if(tg_port_funcs[mid].addr < addr) lo = mid + 1; else hi = mid - 1;
  }
  return 0;
}
