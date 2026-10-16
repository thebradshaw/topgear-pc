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
void RaceListGridCars(TgRegs* r);
void RaceFinishOthers(TgRegs* r);
void RaceBuildSplitWindowTable(TgRegs* r);
void RaceUploadBank3Tiles(TgRegs* r);
void RaceUploadMessageTiles(TgRegs* r);
void DecompressBegin(TgRegs* r);
void DecompressNext(TgRegs* r);
void RaceDrivePlayers(TgRegs* r);
void RaceRenderViews(TgRegs* r);
void RaceCallOvertakes(TgRegs* r);
void RaceUpdatePitStops(TgRegs* r);
void RaceLoadCourse(TgRegs* r);
void RaceLoadSkyline(TgRegs* r);
void RaceLoadRoadside(TgRegs* r);
void DecompressToVram(TgRegs* r);
void RaceLoadCars(TgRegs* r);
void RaceSetupGrid(TgRegs* r);
void RaceUpdateTileCache(TgRegs* r);
void RacePreloadTileCache(TgRegs* r);
void RaceDrawHudFrame(TgRegs* r);
void RaceClearHdmaTables(TgRegs* r);
void RaceSetupNames(TgRegs* r);
void RacePitReloadP1(TgRegs* r);
void RacePitReloadP2(TgRegs* r);
void RacePitScrollP1(TgRegs* r);
void RacePitScrollP2(TgRegs* r);
void RaceUploadBank3TilesP2(TgRegs* r);
void RacePauseCheck(TgRegs* r);
void RaceSyncWait(TgRegs* r);
void RaceLoadAll(TgRegs* r);
void RaceProgram(TgRegs* r);

// Every translated routine, sorted by address.
const TgPortFunc tg_port_funcs[] = {
  { 0x00812A, "RaceProgram", RaceProgram, TG_CALL_NONE, TG_MX_ANY, TG_MX_ANY, 0, 0, TG_WAITS },
  { 0x008878, "RacePauseCheck", RacePauseCheck, TG_CALL_JSR, TG_MX_16, TG_MX_ANY, TG_DEAD_AXY_FLAGS, 0, TG_WAITS },
  { 0x0088A1, "RaceRevEngines", RaceRevEngines, TG_CALL_JSR, TG_MX_16, TG_MX_ANY, 0 },
  { 0x008910, "RaceUpdateSegmentValues", RaceUpdateSegmentValues, TG_CALL_JSR, TG_MX_I16, TG_MX_16, 0 },
  { 0x008966, "RacePitScrollP1", RacePitScrollP1, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, TG_DEAD_FLAGS },
  { 0x008A2C, "RacePitScrollP2", RacePitScrollP2, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, TG_DEAD_FLAGS },
  { 0x008DA8, "RacePitReloadP1", RacePitReloadP1, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x008EB4, "RacePitReloadP2", RacePitReloadP2, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x008FC0, "RaceUploadBank3Tiles", RaceUploadBank3Tiles, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x009046, "RaceUploadBank3TilesP2", RaceUploadBank3TilesP2, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x0090CC, "RaceUpdatePitStops", RaceUpdatePitStops, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x009381, "RaceBuildWindowTable", RaceBuildWindowTable, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, 0 },
  { 0x00A175, "RaceMoveCars", RaceMoveCars, TG_CALL_JSR, TG_MX_16, TG_MX_ANY, TG_DEAD_FLAGS },
  { 0x00A1BD, "RaceSortCars", RaceSortCars, TG_CALL_JSR, TG_MX_16, TG_MX_ANY, TG_DEAD_FLAGS },
  { 0x00A558, "RaceSetupGrid", RaceSetupGrid, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x00AB7A, "RaceFinishOthers", RaceFinishOthers, TG_CALL_JSR, TG_MX_16, TG_MX_ANY, TG_DEAD_Y | TG_DEAD_FLAGS },
  { 0x00ABFC, "RaceListVisibleCars", RaceListVisibleCars, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x00AEF3, "RaceListGridCars", RaceListGridCars, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x00B18C, "RaceBuildSplitWindowTable", RaceBuildSplitWindowTable, TG_CALL_JSR, TG_MX_16, TG_MX_ANY, TG_DEAD_FLAGS },
  { 0x00B33B, "RaceCallOvertakes", RaceCallOvertakes, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x00BF14, "RaceLoadAll", RaceLoadAll, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_AXY_FLAGS, 0, TG_WAITS },
  { 0x00BF44, "RaceLoadCourse", RaceLoadCourse, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x00C182, "RaceLoadSkyline", RaceLoadSkyline, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x00C230, "RaceLoadRoadside", RaceLoadRoadside, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x00C5B2, "RaceLoadCars", RaceLoadCars, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x00C6BB, "RacePreloadTileCache", RacePreloadTileCache, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x00C722, "RaceDrawHudFrame", RaceDrawHudFrame, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x00CC9B, "RaceClearHdmaTables", RaceClearHdmaTables, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x00CE61, "RaceDrawHud", RaceDrawHud, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x00D86A, "RaceUploadMessageTiles", RaceUploadMessageTiles, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x00D913, "RaceSetupNames", RaceSetupNames, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x00DA75, "RaceUpdateMessages", RaceUpdateMessages, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x00E20D, "RaceUpdateTileCache", RaceUpdateTileCache, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x00EA40, "DecompressToVram", DecompressToVram, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, 0 },
  { 0x00EA8E, "DecompressBegin", DecompressBegin, TG_CALL_JSR, TG_MX_ANY, TG_MX_A8_I16, 0 },
  { 0x00EA9B, "DecompressNext", DecompressNext, TG_CALL_JSR, TG_MX_ANY, TG_MX_A8_I16, 0 },
  { 0x00EC0E, "RaceSyncWait", RaceSyncWait, TG_CALL_JSR, TG_MX_16, TG_MX_ANY, 0, 0, TG_WAITS },
  { 0x018000, "RaceBuildSprites", RaceBuildSprites, TG_CALL_JSL, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x018A87, "RaceAddSprite", RaceAddSprite, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_A | TG_DEAD_X | TG_DEAD_Y | TG_DEAD_NVZ },
  { 0x018C5C, "RaceAddSpriteSplit", RaceAddSpriteSplit, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_A | TG_DEAD_X | TG_DEAD_Y | TG_DEAD_NVZ },
  { 0x018E33, "RaceAddSpriteP2", RaceAddSpriteP2, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_A | TG_DEAD_X | TG_DEAD_Y | TG_DEAD_NVZ },
  { 0x019008, "RaceAddSpriteP2Split", RaceAddSpriteP2Split, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_A | TG_DEAD_X | TG_DEAD_Y | TG_DEAD_NVZ },
  { 0x05D2E0, "RaceDrivePlayers", RaceDrivePlayers, TG_CALL_JSL, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS, 0x001D },
  { 0x7E2000, "RaceRenderViews", RaceRenderViews, TG_CALL_JSL, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
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
