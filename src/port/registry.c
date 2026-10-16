#include "port.h"

void RaceRevEngines(TgRegs* r);
void RaceDrawHud(TgRegs* r);
void RaceBuildWindowTable(TgRegs* r);
void RaceUpdateSegmentValues(TgRegs* r);
void RaceUpdateMessages(TgRegs* r);

// Every translated routine, sorted by address.
const TgPortFunc tg_port_funcs[] = {
  { 0x0088A1, "RaceRevEngines", RaceRevEngines, TG_CALL_JSR, TG_MX_16, TG_MX_ANY, 0 },
  { 0x008910, "RaceUpdateSegmentValues", RaceUpdateSegmentValues, TG_CALL_JSR, TG_MX_I16, TG_MX_16, 0 },
  { 0x009381, "RaceBuildWindowTable", RaceBuildWindowTable, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, 0 },
  { 0x00CE61, "RaceDrawHud", RaceDrawHud, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x00DA75, "RaceUpdateMessages", RaceUpdateMessages, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
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
