#include "port.h"

void RaceRevEngines(TgRegs* r);
void MenuUploadHudMap(TgRegs* r);
void MenuUploadMapTile(TgRegs* r);
void MenuClearScreen(TgRegs* r);
void MenuTilesToMode7(TgRegs* r);
void MenuInitVideo(TgRegs* r);
void MenuClearVideo(TgRegs* r);
void MenuDecompressToVramPal(TgRegs* r);
void MenuDecompressToVram(TgRegs* r);
void MenuDecompressToRam(TgRegs* r);
void MenuDecompressBegin(TgRegs* r);
void MenuDecompressNext(TgRegs* r);
void ResetNamesAndRecords(TgRegs* r);
void MenuSetFadeRowsIntro(TgRegs* r);
void MenuDefaultPlayerName(TgRegs* r);
void MenuRecordPlayerPlaces(TgRegs* r);
void MenuLoadPaletteRow15(TgRegs* r);
void MenuCheckQualified(TgRegs* r);
void MenuAdvanceCourse(TgRegs* r);
void MenuMarkNameEnds(TgRegs* r);
void MenuUnmarkNameEnds(TgRegs* r);
void MenuToDecimal(TgRegs* r);
void MenuLoadPaletteRow9(TgRegs* r);
void MenuLoadPaletteRow15b(TgRegs* r);
void MenuClearFadeRows(TgRegs* r);
void MenuTitleLogoSprites(TgRegs* r);
void MenuOamHighFill(TgRegs* r);
void MenuTitleTextSprites(TgRegs* r);
void MenuLoadPaletteRow1(TgRegs* r);
void MenuStartDemo(TgRegs* r);
void MenuDrawCarPortraitAt(TgRegs* r);
void MenuDrawCarPortrait(TgRegs* r);
void MenuComparePassword(TgRegs* r);
void MenuDrawCarStats(TgRegs* r);
void MenuPlaceEntryCursor(TgRegs* r);
void MenuLoadPlayerName(TgRegs* r);
void MenuStorePlayerName(TgRegs* r);
void MenuDrawControlPad(TgRegs* r);
void MenuClearTilemapBuffer(TgRegs* r);
void MenuClearFadeWork(TgRegs* r);
void MenuQueueOamAndMap(TgRegs* r);
void MenuQueueTilemap(TgRegs* r);
void MenuSpriteOrigin(TgRegs* r);
void MenuTilemapAddr(TgRegs* r);
void MenuClearPalette(TgRegs* r);
void MenuSetPaletteDma(TgRegs* r);
void MenuSetPaletteDmaNmi(TgRegs* r);
void MenuLoadBasePalette(TgRegs* r);
void MenuWriteMode7Regs(TgRegs* r);
void MenuClearOam(TgRegs* r);
void RaceIrqTopViewOff(TgRegs* r);
void RaceIrqTopViewSky(TgRegs* r);
void RaceIrqTopViewHorizon(TgRegs* r);
void RaceIrqTopViewRoad(TgRegs* r);
void RaceIrqTopRoadBand(TgRegs* r);
void RaceIrqBottomRoadBand(TgRegs* r);
void RaceIrqVblank(TgRegs* r);
void RaceIrqBottomViewOff(TgRegs* r);
void RaceIrqBottomViewSky(TgRegs* r);
void RaceIrqBottomViewHorizon(TgRegs* r);
void RaceIrqBottomViewRoad(TgRegs* r);
void RaceIrqTop(TgRegs* r);
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
  { 0x00B3FE, "RaceIrqTopViewOff", RaceIrqTopViewOff, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x00B42F, "RaceIrqTopViewSky", RaceIrqTopViewSky, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x00B462, "RaceIrqTopViewHorizon", RaceIrqTopViewHorizon, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x00B49B, "RaceIrqTopViewRoad", RaceIrqTopViewRoad, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x00B4C4, "RaceIrqTopRoadBand", RaceIrqTopRoadBand, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x00B511, "RaceIrqBottomViewOff", RaceIrqBottomViewOff, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x00B545, "RaceIrqBottomViewSky", RaceIrqBottomViewSky, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x00B57B, "RaceIrqBottomViewHorizon", RaceIrqBottomViewHorizon, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x00B5B6, "RaceIrqBottomViewRoad", RaceIrqBottomViewRoad, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x00B5E2, "RaceIrqBottomRoadBand", RaceIrqBottomRoadBand, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x00B6D1, "RaceIrqVblank", RaceIrqVblank, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x00B75A, "RaceIrqTop", RaceIrqTop, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_AXY_FLAGS },
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
  { 0x0F80F4, "ResetNamesAndRecords", ResetNamesAndRecords, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_FLAGS },
  { 0x0F820E, "MenuSetFadeRowsIntro", MenuSetFadeRowsIntro, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_A | TG_DEAD_FLAGS },
  { 0x0F82F0, "MenuUploadHudMap", MenuUploadHudMap, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_A | TG_DEAD_FLAGS },
  { 0x0F836D, "MenuUploadMapTile", MenuUploadMapTile, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_A | TG_DEAD_FLAGS },
  { 0x0F83A1, "MenuDefaultPlayerName", MenuDefaultPlayerName, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_FLAGS },
  { 0x0F86E7, "MenuRecordPlayerPlaces", MenuRecordPlayerPlaces, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_FLAGS },
  { 0x0F8705, "MenuLoadPaletteRow15", MenuLoadPaletteRow15, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_FLAGS },
  { 0x0F871A, "MenuCheckQualified", MenuCheckQualified, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x0F8733, "MenuAdvanceCourse", MenuAdvanceCourse, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x0F87D9, "MenuMarkNameEnds", MenuMarkNameEnds, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_A | TG_DEAD_FLAGS },
  { 0x0F8802, "MenuUnmarkNameEnds", MenuUnmarkNameEnds, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_A | TG_DEAD_FLAGS },
  { 0x0F8BB6, "MenuToDecimal", MenuToDecimal, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_FLAGS },
  { 0x0F8C68, "MenuLoadPaletteRow9", MenuLoadPaletteRow9, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_A | TG_DEAD_FLAGS },
  { 0x0F8C86, "MenuLoadPaletteRow15b", MenuLoadPaletteRow15b, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_A | TG_DEAD_FLAGS },
  { 0x0F8F80, "MenuClearFadeRows", MenuClearFadeRows, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_FLAGS },
  { 0x0F8F8B, "MenuTitleLogoSprites", MenuTitleLogoSprites, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_FLAGS },
  { 0x0F8FB5, "MenuOamHighFill", MenuOamHighFill, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_FLAGS },
  { 0x0F8FFB, "MenuTitleTextSprites", MenuTitleTextSprites, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_FLAGS },
  { 0x0F90D7, "MenuLoadPaletteRow1", MenuLoadPaletteRow1, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_A | TG_DEAD_FLAGS },
  { 0x0F90E6, "MenuStartDemo", MenuStartDemo, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_FLAGS },
  { 0x0F9102, "MenuDrawCarPortraitAt", MenuDrawCarPortraitAt, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_FLAGS },
  { 0x0F911C, "MenuDrawCarPortrait", MenuDrawCarPortrait, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_FLAGS },
  { 0x0F9426, "MenuComparePassword", MenuComparePassword, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x0F9AD6, "MenuDrawCarStats", MenuDrawCarStats, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_FLAGS },
  { 0x0F9B56, "MenuPlaceEntryCursor", MenuPlaceEntryCursor, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_FLAGS },
  { 0x0F9B7B, "MenuLoadPlayerName", MenuLoadPlayerName, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_FLAGS },
  { 0x0F9BC4, "MenuStorePlayerName", MenuStorePlayerName, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_FLAGS },
  { 0x0F9BF9, "MenuDrawControlPad", MenuDrawControlPad, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_FLAGS },
  { 0x0F9C42, "MenuClearScreen", MenuClearScreen, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_A | TG_DEAD_FLAGS },
  { 0x0F9C5B, "MenuClearTilemapBuffer", MenuClearTilemapBuffer, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_FLAGS },
  { 0x0F9C66, "MenuClearFadeWork", MenuClearFadeWork, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_FLAGS },
  { 0x0F9DDD, "MenuQueueOamAndMap", MenuQueueOamAndMap, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_FLAGS },
  { 0x0F9DFC, "MenuQueueTilemap", MenuQueueTilemap, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_FLAGS },
  { 0x0FA1C4, "MenuSpriteOrigin", MenuSpriteOrigin, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_FLAGS },
  { 0x0FA1D0, "MenuTilemapAddr", MenuTilemapAddr, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_FLAGS },
  { 0x0FA1EB, "MenuClearPalette", MenuClearPalette, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_FLAGS },
  { 0x0FA1F6, "MenuSetPaletteDma", MenuSetPaletteDma, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_FLAGS },
  { 0x0FA21A, "MenuSetPaletteDmaNmi", MenuSetPaletteDmaNmi, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_FLAGS },
  { 0x0FA23E, "MenuLoadBasePalette", MenuLoadBasePalette, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_A | TG_DEAD_FLAGS },
  { 0x0FA258, "MenuWriteMode7Regs", MenuWriteMode7Regs, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_FLAGS },
  { 0x0FA402, "MenuTilesToMode7", MenuTilesToMode7, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_A | TG_DEAD_FLAGS },
  { 0x0FA53C, "MenuInitVideo", MenuInitVideo, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_A | TG_DEAD_FLAGS },
  { 0x0FA644, "MenuClearVideo", MenuClearVideo, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, TG_DEAD_A | TG_DEAD_FLAGS },
  { 0x0FA691, "MenuClearOam", MenuClearOam, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_FLAGS },
  { 0x0FB2F6, "MenuDecompressToVramPal", MenuDecompressToVramPal, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x0FB306, "MenuDecompressToVram", MenuDecompressToVram, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x0FB334, "MenuDecompressToRam", MenuDecompressToRam, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, 0 },
  { 0x0FB35A, "MenuDecompressBegin", MenuDecompressBegin, TG_CALL_JSR, TG_MX_ANY, TG_MX_A8_I16, 0 },
  { 0x0FB367, "MenuDecompressNext", MenuDecompressNext, TG_CALL_JSR, TG_MX_ANY, TG_MX_A8_I16, 0 },
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
