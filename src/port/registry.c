#include "port.h"

void RaceRevEngines(TgRegs* r);
void RaceBuildSprites(TgRegs* r);
void RaceAddSprite(TgRegs* r);
void RaceAddSpriteSplit(TgRegs* r);
void RaceAddSpriteP2(TgRegs* r);
void RaceAddSpriteP2Split(TgRegs* r);
void RaceDrawHud(TgRegs* r);
void RaceBuildWindowTable(TgRegs* r);
void RaceUpdateSegmentValues(TgRegs* r);
void RaceUpdateMessages(TgRegs* r);
void RaceMoveCars(TgRegs* r);
void RaceSortCars(TgRegs* r);
void RaceListVisibleCars(TgRegs* r);
void RaceFinishOthers(TgRegs* r);
void RaceBuildSplitWindowTable(TgRegs* r);
void RaceUploadBank3Tiles(TgRegs* r);
void RaceUploadMessageTiles(TgRegs* r);
void DecompressBegin(TgRegs* r);
void DecompressNext(TgRegs* r);
void RaceDrivePlayers(TgRegs* r);

// Every translated routine, sorted by address.
const TgPortFunc tg_port_funcs[] = {
  { 0x0088A1, "RaceRevEngines", RaceRevEngines, TG_CALL_JSR, TG_MX_16, TG_MX_ANY, 0 },
  { 0x008910, "RaceUpdateSegmentValues", RaceUpdateSegmentValues, TG_CALL_JSR, TG_MX_I16, TG_MX_16, 0 },
  { 0x008FC0, "RaceUploadBank3Tiles", RaceUploadBank3Tiles, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x009381, "RaceBuildWindowTable", RaceBuildWindowTable, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, 0 },
  { 0x00A175, "RaceMoveCars", RaceMoveCars, TG_CALL_JSR, TG_MX_16, TG_MX_ANY, TG_DEAD_FLAGS },
  { 0x00A1BD, "RaceSortCars", RaceSortCars, TG_CALL_JSR, TG_MX_16, TG_MX_ANY, TG_DEAD_FLAGS },
  { 0x00AB7A, "RaceFinishOthers", RaceFinishOthers, TG_CALL_JSR, TG_MX_16, TG_MX_ANY, TG_DEAD_Y | TG_DEAD_FLAGS },
  { 0x00ABFC, "RaceListVisibleCars", RaceListVisibleCars, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x00B18C, "RaceBuildSplitWindowTable", RaceBuildSplitWindowTable, TG_CALL_JSR, TG_MX_16, TG_MX_ANY, TG_DEAD_FLAGS },
  { 0x00CE61, "RaceDrawHud", RaceDrawHud, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x00D86A, "RaceUploadMessageTiles", RaceUploadMessageTiles, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x00DA75, "RaceUpdateMessages", RaceUpdateMessages, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x00EA8E, "DecompressBegin", DecompressBegin, TG_CALL_JSR, TG_MX_ANY, TG_MX_A8_I16, 0 },
  { 0x00EA9B, "DecompressNext", DecompressNext, TG_CALL_JSR, TG_MX_ANY, TG_MX_A8_I16, 0 },
  { 0x018000, "RaceBuildSprites", RaceBuildSprites, TG_CALL_JSL, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x018A87, "RaceAddSprite", RaceAddSprite, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_A | TG_DEAD_X | TG_DEAD_Y | TG_DEAD_NVZ },
  { 0x018C5C, "RaceAddSpriteSplit", RaceAddSpriteSplit, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_A | TG_DEAD_X | TG_DEAD_Y | TG_DEAD_NVZ },
  { 0x018E33, "RaceAddSpriteP2", RaceAddSpriteP2, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_A | TG_DEAD_X | TG_DEAD_Y | TG_DEAD_NVZ },
  { 0x019008, "RaceAddSpriteP2Split", RaceAddSpriteP2Split, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_A | TG_DEAD_X | TG_DEAD_Y | TG_DEAD_NVZ },
  { 0x05D2E0, "RaceDrivePlayers", RaceDrivePlayers, TG_CALL_JSL, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS, 0x001D },
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
