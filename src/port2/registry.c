// Top Gear 2's translated routines, in address order (see src/port/registry.c).

#include "../port/port.h"

void Tg2DriveCar1(TgRegs* r);
void Tg2DriveCar2(TgRegs* r);
void Tg2Cars(TgRegs* r);
void Tg2Stream1(TgRegs* r);
void Tg2Stream2(TgRegs* r);
void Tg2UploadQueue0(TgRegs* r);
void Tg2RaceColours1P(TgRegs* r);
void Tg2RaceColours2PTop(TgRegs* r);
void Tg2RaceColours2PBottom(TgRegs* r);
void Tg2BuildRoad(TgRegs* r);
void Tg2BuildRoad2P(TgRegs* r);
void Tg2RaceVblank1P(TgRegs* r);
void Tg2RaceLoop1P(TgRegs* r);
void Tg2Decompress(TgRegs* r);
void Tg2Unpack(TgRegs* r);
void Tg2DrawBigString(TgRegs* r);
void Tg2PackSprites(TgRegs* r);
void Tg2UnpackToVram(TgRegs* r);
void Tg2MakePassword(TgRegs* r);
void Tg2MultiplyLong(TgRegs* r);
void Tg2PasswordCheck(TgRegs* r);
void Tg2PixelsToTiles(TgRegs* r);
void Tg2FrontEndInit(TgRegs* r);
void Tg2ResetPpu(TgRegs* r);
void Tg2SetScreenRegs(TgRegs* r);
void Tg2QueueFadeRow(TgRegs* r);
void Tg2CopyMapBlock(TgRegs* r);
void Tg2PackSpritesSwap(TgRegs* r);
void Tg2PackSpritesKeep(TgRegs* r);
void Tg2DrawSmallString(TgRegs* r);
void Tg2SetPpu(TgRegs* r);
void Tg2LoadTrack(TgRegs* r);
void Tg2RaceSetup(TgRegs* r);
void Tg2StreamStart1(TgRegs* r);
void Tg2StreamStart2(TgRegs* r);
void Tg2GameLoop(TgRegs* r);
void Tg2DashboardMap1P(TgRegs* r);
void Tg2DashboardMap2P(TgRegs* r);
void Tg2FogBackdrop(TgRegs* r);
void Tg2NightBackdrop(TgRegs* r);
void Tg2SendCgram(TgRegs* r);
void Tg2TrackObjects(TgRegs* r);
void Tg2DashboardGraphics(TgRegs* r);
void Tg2BackdropGraphics(TgRegs* r);
void Tg2Colours(TgRegs* r);
void Tg2PlanarTiles(TgRegs* r);
void Tg2InitVectors(TgRegs* r);
void Tg2Fill7E(TgRegs* r);
void Tg2Fill7F(TgRegs* r);
void Tg2ClearPaletteBuffer(TgRegs* r);
void Tg2ClearSpriteBuffer(TgRegs* r);
void Tg2ClearCgram(TgRegs* r);
void Tg2ClearOam(TgRegs* r);
void Tg2ClearVram1(TgRegs* r);
void Tg2RaceOptions(TgRegs* r);
void Tg2RacePlayers(TgRegs* r);
void Tg2StartingGrid(TgRegs* r);
void Tg2SendPalette(TgRegs* r);
void Tg2SendSprites(TgRegs* r);
void Tg2RunQueue(TgRegs* r);
void Tg2RaceLoop2P(TgRegs* r);
void Tg2RaceVblank2P(TgRegs* r);
void Tg2WindowsNone(TgRegs* r);
void Tg2WindowsTunnel(TgRegs* r);
void Tg2WindowsWall(TgRegs* r);
void Tg2WindowsWallRight(TgRegs* r);
void Tg2WindowsWalls(TgRegs* r);
void Tg2WindowsRoadLeft(TgRegs* r);
void Tg2WindowsRoadRight(TgRegs* r);
void Tg2WindowsRoadBoth(TgRegs* r);
void Tg2WindowsBand(TgRegs* r);
void Tg2DrawObjects1P(TgRegs* r);
void Tg2DrawObjects2P(TgRegs* r);
void Tg2DrawSprite(TgRegs* r);
void Tg2ObjectPass(TgRegs* r);
void Tg2ListVisibleCars(TgRegs* r);
void Tg2FinishOam(TgRegs* r);
void Tg2ObjectType0(TgRegs* r);
void Tg2ObjectType1(TgRegs* r);
void Tg2ObjectType2(TgRegs* r);
void Tg2ObjectType3(TgRegs* r);
void Tg2ObjectType4(TgRegs* r);
void Tg2ObjectType5(TgRegs* r);
void Tg2ObjectType6(TgRegs* r);
void Tg2ObjectType7(TgRegs* r);
void Tg2ObjectType8(TgRegs* r);
void Tg2ObjectType9(TgRegs* r);
void Tg2ObjectType10(TgRegs* r);
void Tg2ObjectType11(TgRegs* r);
void Tg2ObjectType12(TgRegs* r);
void Tg2ObjectType13(TgRegs* r);
void Tg2ObjectType14(TgRegs* r);
void Tg2ObjectType15(TgRegs* r);
void Tg2UploadQueue1(TgRegs* r);
void Tg2AddToWords(TgRegs* r);
void Tg2VramDma7E(TgRegs* r);
void Tg2ClearVram(TgRegs* r);
void Tg2VramDma7F(TgRegs* r);
void Tg2VramDma(TgRegs* r);
void Tg2SetFadeIn(TgRegs* r);
void Tg2FillWords(TgRegs* r);
void Tg2MenuStart(TgRegs* r);
void Tg2ShopDraw(TgRegs* r);
void Tg2ShopLeft(TgRegs* r);
void Tg2ShopRight(TgRegs* r);
void Tg2ShopDown(TgRegs* r);
void Tg2OptionsFadeOut(TgRegs* r);
void Tg2NameEntryScreen(TgRegs* r);
void Tg2NameEntryFadeOut(TgRegs* r);
void Tg2GearsScreen(TgRegs* r);
void Tg2GearsFadeOut(TgRegs* r);
void Tg2ControlScreen(TgRegs* r);
void Tg2ControlFadeOut(TgRegs* r);
void Tg2UnitsScreen(TgRegs* r);
void Tg2UnitsFadeOut(TgRegs* r);
void Tg2UnpackSend2(TgRegs* r);
void Tg2UnpackSend(TgRegs* r);
void Tg2MenuPowerOn(TgRegs* r);
void Tg2MenuAfterRace(TgRegs* r);
void Tg2ShopPanel(TgRegs* r);
void Tg2ShopText(TgRegs* r);
void Tg2NextDigit(TgRegs* r);
void Tg2BoxEdges(TgRegs* r);
void Tg2PlayersPick(TgRegs* r);
void Tg2DifficultyPick(TgRegs* r);
void Tg2PasswordPictures(TgRegs* r);
void Tg2OptionsCursorY(TgRegs* r);
void Tg2OpenScreen1(TgRegs* r);
void Tg2OpenScreen2(TgRegs* r);
void Tg2OpenScreen3(TgRegs* r);
void Tg2OpenScreen4(TgRegs* r);
void Tg2MenuUp(TgRegs* r);
void Tg2MenuDown(TgRegs* r);
void Tg2MenuBack(TgRegs* r);
void Tg2NameHeldRight(TgRegs* r);
void Tg2NameHeldLeft(TgRegs* r);
void Tg2NameHeldUp(TgRegs* r);
void Tg2NameHeldDown(TgRegs* r);
void Tg2NameRepeat(TgRegs* r);
void Tg2NamePressRight(TgRegs* r);
void Tg2NamePressLeft(TgRegs* r);
void Tg2NamePressUp(TgRegs* r);
void Tg2NamePressDown(TgRegs* r);
void Tg2NameKey(TgRegs* r);
void Tg2MenuCursorY(TgRegs* r);
void Tg2GearsClose(TgRegs* r);
void Tg2ControlNext(TgRegs* r);
void Tg2ControlPrev(TgRegs* r);
void Tg2ControlPicture(TgRegs* r);
void Tg2UnitsPicture(TgRegs* r);
void Tg2UnitsKmh(TgRegs* r);
void Tg2NameEntrySprites(TgRegs* r);
void Tg2SaveRecords(TgRegs* r);
void Tg2LoadRecords(TgRegs* r);
void Tg2LoadSave(TgRegs* r);
void Tg2MenuMusic(TgRegs* r);
void Tg2NewSeason(TgRegs* r);
void Tg2KeepStandings(TgRegs* r);
void Tg2RestoreStandings(TgRegs* r);

const TgPortFunc tg2_port_funcs[] = {
  { 0x008A41, "DriveCar1", Tg2DriveCar1, TG_CALL_JSL, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x00A2A5, "DriveCar2", Tg2DriveCar2, TG_CALL_JSL, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x00CD09, "RaceOptions", Tg2RaceOptions, TG_CALL_JSL, TG_MX_16, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x00CD5E, "RacePlayers", Tg2RacePlayers, TG_CALL_JSL, TG_MX_16, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x00D0DA, "StartingGrid", Tg2StartingGrid, TG_CALL_JSL, TG_MX_16, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x00D726, "Cars", Tg2Cars, TG_CALL_JSL, TG_MX_16, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x018145, "GameLoop", Tg2GameLoop, TG_CALL_NONE, TG_MX_ANY, TG_MX_ANY, 0, 0, TG_WAITS },
  { 0x01826D, "RaceLoop1P", Tg2RaceLoop1P, TG_CALL_NONE, TG_MX_16, TG_MX_ANY, 0, 0, TG_WAITS },
  { 0x018522, "RaceLoop2P", Tg2RaceLoop2P, TG_CALL_NONE, TG_MX_16, TG_MX_ANY, 0, 0, TG_WAITS },
  { 0x018717, "RaceSetup", Tg2RaceSetup, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x018FAB, "DashboardMap1P", Tg2DashboardMap1P, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x0190BD, "DashboardMap2P", Tg2DashboardMap2P, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x019261, "RaceColours1P", Tg2RaceColours1P, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, TG_DEAD_A | TG_DEAD_Y },
  { 0x019284, "RaceColours2PTop", Tg2RaceColours2PTop, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, TG_DEAD_A | TG_DEAD_Y },
  { 0x0192A7, "RaceColours2PBottom", Tg2RaceColours2PBottom, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, TG_DEAD_A | TG_DEAD_Y },
  { 0x01A4D1, "UploadQueue0", Tg2UploadQueue0, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x01A505, "UploadQueue1", Tg2UploadQueue1, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x01AC79, "RaceVblank1P", Tg2RaceVblank1P, TG_CALL_JSL, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x01B345, "RaceVblank2P", Tg2RaceVblank2P, TG_CALL_JSL, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x01D1F2, "FogBackdrop", Tg2FogBackdrop, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, 0 },
  { 0x01D24E, "NightBackdrop", Tg2NightBackdrop, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, 0 },
  { 0x01D2C2, "BackdropGraphics", Tg2BackdropGraphics, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, 0 },
  { 0x01D30F, "DashboardGraphics", Tg2DashboardGraphics, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, 0 },
  { 0x01D473, "LoadTrack", Tg2LoadTrack, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, 0 },
  { 0x01D8AF, "TrackObjects", Tg2TrackObjects, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x01DAD6, "AddToWords", Tg2AddToWords, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x01DAEA, "PlanarTiles", Tg2PlanarTiles, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, 0 },
  { 0x01DBA1, "Colours", Tg2Colours, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, 0 },
  { 0x01DE85, "Decompress", Tg2Decompress, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, 0 },
  { 0x01E74F, "SendPalette", Tg2SendPalette, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x01E794, "ClearPaletteBuffer", Tg2ClearPaletteBuffer, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, 0 },
  { 0x01E7C6, "ClearCgram", Tg2ClearCgram, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, 0 },
  { 0x01E7F5, "SendCgram", Tg2SendCgram, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, 0 },
  { 0x01E821, "SendSprites", Tg2SendSprites, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x01E867, "ClearSpriteBuffer", Tg2ClearSpriteBuffer, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, 0 },
  { 0x01E89F, "ClearOam", Tg2ClearOam, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, 0 },
  { 0x01E904, "RunQueue", Tg2RunQueue, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x01EF05, "ClearVram1", Tg2ClearVram1, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, 0 },
  { 0x01EF37, "VramDma7E", Tg2VramDma7E, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, 0 },
  { 0x01EF6C, "VramDma7F", Tg2VramDma7F, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, 0 },
  { 0x01F28F, "Fill7E", Tg2Fill7E, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, 0 },
  { 0x01F2BF, "Fill7F", Tg2Fill7F, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, 0 },
  { 0x01F5B9, "InitVectors", Tg2InitVectors, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, 0 },
  { 0x01F657, "SetPpu", Tg2SetPpu, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, 0 },
  { 0x088000, "StreamStart1", Tg2StreamStart1, TG_CALL_JSL, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x0885E5, "StreamStart2", Tg2StreamStart2, TG_CALL_JSL, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x088BCA, "Stream1", Tg2Stream1, TG_CALL_JSL, TG_MX_ANY, TG_MX_16, TG_DEAD_A | TG_DEAD_X | TG_DEAD_Y },
  { 0x08AB9F, "Stream2", Tg2Stream2, TG_CALL_JSL, TG_MX_ANY, TG_MX_16, TG_DEAD_A | TG_DEAD_X | TG_DEAD_Y },
  { 0x08CB74, "BuildRoad", Tg2BuildRoad, TG_CALL_JSL, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x08D63F, "BuildRoad2P", Tg2BuildRoad2P, TG_CALL_JSL, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x0B8000, "DrawObjects1P", Tg2DrawObjects1P, TG_CALL_JSL, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x0B818A, "DrawObjects2P", Tg2DrawObjects2P, TG_CALL_JSL, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x0B8578, "ObjectPass", Tg2ObjectPass, TG_CALL_JSL, TG_MX_16, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x0B9FB7, "ObjectType0", Tg2ObjectType0, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_FLAGS },
  { 0x0B9FC9, "ObjectType1", Tg2ObjectType1, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_FLAGS },
  { 0x0B9FDB, "ObjectType2", Tg2ObjectType2, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_FLAGS },
  { 0x0B9FED, "ObjectType3", Tg2ObjectType3, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_FLAGS },
  { 0x0B9FFF, "ObjectType4", Tg2ObjectType4, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_FLAGS },
  { 0x0BA011, "ObjectType5", Tg2ObjectType5, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_FLAGS },
  { 0x0BA023, "ObjectType6", Tg2ObjectType6, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_FLAGS },
  { 0x0BA035, "ObjectType7", Tg2ObjectType7, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_FLAGS },
  { 0x0BA047, "ObjectType8", Tg2ObjectType8, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_FLAGS },
  { 0x0BA059, "ObjectType9", Tg2ObjectType9, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_FLAGS },
  { 0x0BA06B, "ObjectType10", Tg2ObjectType10, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_FLAGS },
  { 0x0BA0D5, "ObjectType11", Tg2ObjectType11, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_FLAGS },
  { 0x0BA13F, "ObjectType12", Tg2ObjectType12, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_FLAGS },
  { 0x0BA1C6, "ObjectType13", Tg2ObjectType13, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_FLAGS },
  { 0x0BA24D, "ObjectType14", Tg2ObjectType14, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_FLAGS },
  { 0x0BA2D4, "ObjectType15", Tg2ObjectType15, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_FLAGS },
  { 0x0BA49B, "ListVisibleCars", Tg2ListVisibleCars, TG_CALL_JSL, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x0BA6EA, "DrawSprite", Tg2DrawSprite, TG_CALL_JSL, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x0BD7E7, "FinishOam", Tg2FinishOam, TG_CALL_JSL, TG_MX_ANY, TG_MX_16, TG_DEAD_A | TG_DEAD_X | TG_DEAD_Y },
  { 0x0E8BCC, "WindowsNone", Tg2WindowsNone, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_Y },
  { 0x0E8C4B, "WindowsTunnel", Tg2WindowsTunnel, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x0E8F1A, "WindowsWall", Tg2WindowsWall, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x0E9712, "WindowsWallRight", Tg2WindowsWallRight, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x0E9F2C, "WindowsWalls", Tg2WindowsWalls, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x0EAE9F, "WindowsRoadLeft", Tg2WindowsRoadLeft, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x0EAFAF, "WindowsRoadRight", Tg2WindowsRoadRight, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x0EB0BF, "WindowsRoadBoth", Tg2WindowsRoadBoth, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x0EB250, "WindowsBand", Tg2WindowsBand, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1F8030, "MenuPowerOn", Tg2MenuPowerOn, TG_CALL_NONE, TG_MX_16, TG_MX_ANY, 0, 0, TG_WAITS },
  { 0x1F80B7, "MenuAfterRace", Tg2MenuAfterRace, TG_CALL_NONE, TG_MX_16, TG_MX_ANY, 0, 0, TG_WAITS },
  { 0x1F8190, "SaveRecords", Tg2SaveRecords, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1F81D4, "LoadRecords", Tg2LoadRecords, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1F8320, "MenuMusic", Tg2MenuMusic, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1F835D, "LoadSave", Tg2LoadSave, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1F8503, "MenuStart", Tg2MenuStart, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1F88C0, "MenuUp", Tg2MenuUp, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1F88C7, "MenuDown", Tg2MenuDown, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1F88E0, "MenuBack", Tg2MenuBack, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1F8920, "NameEntryScreen", Tg2NameEntryScreen, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0, 0, TG_WAITS },
  { 0x1F8A1F, "NameHeldRight", Tg2NameHeldRight, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1F8A26, "NameHeldLeft", Tg2NameHeldLeft, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1F8A2D, "NameHeldUp", Tg2NameHeldUp, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1F8A34, "NameHeldDown", Tg2NameHeldDown, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1F8A3D, "NameRepeat", Tg2NameRepeat, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1F8A48, "NamePressRight", Tg2NamePressRight, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1F8A50, "NamePressLeft", Tg2NamePressLeft, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1F8A58, "NamePressUp", Tg2NamePressUp, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1F8A60, "NamePressDown", Tg2NamePressDown, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1F8AD3, "NameKey", Tg2NameKey, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1F8B4A, "NameEntrySprites", Tg2NameEntrySprites, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1F8C26, "NameEntryFadeOut", Tg2NameEntryFadeOut, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0, 0, TG_WAITS },
  { 0x1F8C75, "GearsScreen", Tg2GearsScreen, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0, 0, TG_WAITS },
  { 0x1F8D6C, "MenuCursorY", Tg2MenuCursorY, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1F8D7C, "GearsClose", Tg2GearsClose, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1F8DB0, "GearsFadeOut", Tg2GearsFadeOut, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0, 0, TG_WAITS },
  { 0x1F8E11, "ControlScreen", Tg2ControlScreen, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0, 0, TG_WAITS },
  { 0x1F8F11, "ControlNext", Tg2ControlNext, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1F8F26, "ControlPrev", Tg2ControlPrev, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1F8F38, "ControlPicture", Tg2ControlPicture, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1F8F61, "ControlFadeOut", Tg2ControlFadeOut, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0, 0, TG_WAITS },
  { 0x1F8FAD, "UnitsScreen", Tg2UnitsScreen, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0, 0, TG_WAITS },
  { 0x1F9064, "UnitsPicture", Tg2UnitsPicture, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1F909A, "UnitsKmh", Tg2UnitsKmh, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1F90AE, "UnitsFadeOut", Tg2UnitsFadeOut, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0, 0, TG_WAITS },
  { 0x1F90E9, "UnpackSend2", Tg2UnpackSend2, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0, 0, TG_WAITS },
  { 0x1F910D, "UnpackSend", Tg2UnpackSend, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0, 0, TG_WAITS },
  { 0x1F9131, "UnpackToVram", Tg2UnpackToVram, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1F9167, "VramDma", Tg2VramDma, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1F97A9, "QueueFadeRow", Tg2QueueFadeRow, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1F98C5, "PackSprites", Tg2PackSprites, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1F98DA, "PackSpritesSwap", Tg2PackSpritesSwap, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1F98F6, "PackSpritesKeep", Tg2PackSpritesKeep, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1F9CA4, "FrontEndInit", Tg2FrontEndInit, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1F9D7B, "ResetPpu", Tg2ResetPpu, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1F9EDE, "ClearVram", Tg2ClearVram, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1F9F13, "CopyMapBlock", Tg2CopyMapBlock, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1F9F74, "SetScreenRegs", Tg2SetScreenRegs, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, 0 },
  { 0x1FA117, "SetFadeIn", Tg2SetFadeIn, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1FA13D, "FillWords", Tg2FillWords, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1FA4F4, "OptionsCursorY", Tg2OptionsCursorY, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1FA4FF, "OpenScreen1", Tg2OpenScreen1, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1FA519, "OpenScreen2", Tg2OpenScreen2, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1FA533, "OpenScreen3", Tg2OpenScreen3, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1FA54D, "OpenScreen4", Tg2OpenScreen4, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1FA59A, "OptionsFadeOut", Tg2OptionsFadeOut, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0, 0, TG_WAITS },
  { 0x1FA7A1, "ShopDraw", Tg2ShopDraw, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0, 0, TG_WAITS },
  { 0x1FAAF2, "ShopPanel", Tg2ShopPanel, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1FAB43, "ShopText", Tg2ShopText, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1FACC7, "ShopLeft", Tg2ShopLeft, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0, 0, TG_WAITS },
  { 0x1FACEF, "ShopRight", Tg2ShopRight, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0, 0, TG_WAITS },
  { 0x1FAD60, "ShopDown", Tg2ShopDown, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0, 0, TG_WAITS },
  { 0x1FB6AC, "NextDigit", Tg2NextDigit, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1FB6BE, "BoxEdges", Tg2BoxEdges, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1FB744, "PixelsToTiles", Tg2PixelsToTiles, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1FBF14, "Unpack", Tg2Unpack, TG_CALL_JSL, TG_MX_16, TG_MX_16, 0 },
  { 0x1FCB08, "PlayersPick", Tg2PlayersPick, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1FCB26, "DifficultyPick", Tg2DifficultyPick, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1FD326, "PasswordPictures", Tg2PasswordPictures, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1FD33F, "MakePassword", Tg2MakePassword, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1FD74F, "PasswordCheck", Tg2PasswordCheck, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1FD819, "MultiplyLong", Tg2MultiplyLong, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1FDA09, "NewSeason", Tg2NewSeason, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1FDA3C, "KeepStandings", Tg2KeepStandings, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1FDA69, "RestoreStandings", Tg2RestoreStandings, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1FDBDC, "DrawBigString", Tg2DrawBigString, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_A | TG_DEAD_FLAGS },
  { 0x1FDD7A, "DrawSmallString", Tg2DrawSmallString, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_A | TG_DEAD_FLAGS },
};
const int tg2_port_func_count = sizeof(tg2_port_funcs) / sizeof(tg2_port_funcs[0]);
