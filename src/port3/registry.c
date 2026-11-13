// Top Gear 3000's translated routines, in address order (see src/port/registry.c).

#include "../port/port.h"

void Tg3DrawBar(TgRegs* r);
void Tg3RoadWindows(TgRegs* r);
void Tg3CarsTurn(TgRegs* r);
void Tg3MoveObject(TgRegs* r);
void Tg3ObjectSegment(TgRegs* r);
void Tg3RacePlaces(TgRegs* r);
void Tg3SceneryLoop(TgRegs* r);
void Tg3SendObject(TgRegs* r);
void Tg3MergeSplits(TgRegs* r);
void Tg3BuildRoad(TgRegs* r);
void Tg3RoadHdmaTables(TgRegs* r);
void Tg3BuildRoadCar(TgRegs* r);
void Tg3SendShapeRam(TgRegs* r);
void Tg3SendObjectFlat(TgRegs* r);
void Tg3SceneryBobbing(TgRegs* r);
void Tg3SceneryFour(TgRegs* r);
void Tg3SendShapeRom(TgRegs* r);
void Tg3PlayerEngine(TgRegs* r);
void Tg3PlayerSetup(TgRegs* r);
void Tg3TrackPosition(TgRegs* r);
void Tg3RoadAhead(TgRegs* r);
void Tg3RoadEdges(TgRegs* r);
void Tg3CameraFollow(TgRegs* r);
void Tg3SpriteFinish(TgRegs* r);
void Tg3TileScroll(TgRegs* r);
void Tg3Countdown(TgRegs* r);
void Tg3PlayerRecord(TgRegs* r);
void Tg3RoadPieces(TgRegs* r);
void Tg3EngineSound(TgRegs* r);
void Tg3HorizonScroll(TgRegs* r);
void Tg3PlayerFrame(TgRegs* r);
void Tg3CarStats(TgRegs* r);
void Tg3SceneryLoopTop(TgRegs* r);
void Tg3SceneryLoopBottom(TgRegs* r);
void Tg3CameraFollowBottom(TgRegs* r);
void Tg3SpriteFinishBottom(TgRegs* r);
void Tg3RoadWindowsTop(TgRegs* r);
void Tg3RoadWindowsBottom(TgRegs* r);
void Tg3BuildRoadTop(TgRegs* r);
void Tg3BuildRoadBottom(TgRegs* r);
void Tg3BuildRoadCarTop(TgRegs* r);
void Tg3BuildRoadCarBottom(TgRegs* r);

const TgPortFunc tg3_port_funcs[] = {
  { 0x018487, "DrawBar", Tg3DrawBar, TG_CALL_JSL, TG_MX_16, TG_MX_16, 0 },
  { 0x018564, "RoadWindows", Tg3RoadWindows, TG_CALL_JSL, TG_MX_16, TG_MX_16, 0 },
  { 0x01DBAC, "RoadWindowsTop", Tg3RoadWindowsTop, TG_CALL_JSL, TG_MX_16, TG_MX_16, 0 },
  { 0x01E0F8, "RoadWindowsBottom", Tg3RoadWindowsBottom, TG_CALL_JSL, TG_MX_16, TG_MX_16, 0 },
  { 0x01E812, "CarsTurn", Tg3CarsTurn, TG_CALL_JSL, TG_MX_16, TG_MX_16, 0 },
  { 0x01F887, "MoveObject", Tg3MoveObject, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x01FA8F, "PlayerRecord", Tg3PlayerRecord, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x01FBB1, "ObjectSegment", Tg3ObjectSegment, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x02A9B7, "CameraFollow", Tg3CameraFollow, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_A | TG_DEAD_FLAGS },
  { 0x02AA7B, "CameraFollowBottom", Tg3CameraFollowBottom, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_A | TG_DEAD_FLAGS },
  { 0x02AB9D, "Countdown", Tg3Countdown, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x02AC52, "EngineSound", Tg3EngineSound, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_A | TG_DEAD_FLAGS },
  { 0x02AD2C, "RacePlaces", Tg3RacePlaces, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x02AE15, "SpriteFinish", Tg3SpriteFinish, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x02AEA4, "SpriteFinishBottom", Tg3SpriteFinishBottom, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x02CBD4, "SceneryLoop", Tg3SceneryLoop, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x02CCC7, "SceneryLoopTop", Tg3SceneryLoopTop, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x02D2CC, "SceneryLoopBottom", Tg3SceneryLoopBottom, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x02D934, "SendObject", Tg3SendObject, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x02D988, "SendObjectFlat", Tg3SendObjectFlat, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x02DA48, "SceneryBobbing", Tg3SceneryBobbing, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x02DB87, "SceneryFour", Tg3SceneryFour, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x02DDBF, "SendShapeRam", Tg3SendShapeRam, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x02DEA8, "SendShapeRom", Tg3SendShapeRom, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x02DFAD, "RoadAhead", Tg3RoadAhead, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x02EC36, "RoadEdges", Tg3RoadEdges, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x02F2B3, "RoadPieces", Tg3RoadPieces, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x038989, "HorizonScroll", Tg3HorizonScroll, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x03CD70, "BuildRoadCarBottom", Tg3BuildRoadCarBottom, TG_CALL_JSL, TG_MX_16, TG_MX_16, 0 },
  { 0x03D1C1, "BuildRoadCarTop", Tg3BuildRoadCarTop, TG_CALL_JSL, TG_MX_16, TG_MX_16, 0 },
  { 0x03D612, "BuildRoadBottom", Tg3BuildRoadBottom, TG_CALL_JSL, TG_MX_16, TG_MX_16, 0 },
  { 0x03DA0A, "BuildRoadTop", Tg3BuildRoadTop, TG_CALL_JSL, TG_MX_16, TG_MX_16, 0 },
  { 0x03E241, "PlayerFrame", Tg3PlayerFrame, TG_CALL_JSL, TG_MX_16, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x03E2D7, "PlayerSetup", Tg3PlayerSetup, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x03EA9B, "CarStats", Tg3CarStats, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x03EAC6, "PlayerEngine", Tg3PlayerEngine, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_A | TG_DEAD_FLAGS },
  { 0x03F7F9, "TrackPosition", Tg3TrackPosition, TG_CALL_JSR, TG_MX_16, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x059147, "TileScroll", Tg3TileScroll, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x05B1FF, "MergeSplits", Tg3MergeSplits, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x05B3D7, "BuildRoadCar", Tg3BuildRoadCar, TG_CALL_JSL, TG_MX_16, TG_MX_16, 0 },
  { 0x05B828, "BuildRoad", Tg3BuildRoad, TG_CALL_JSL, TG_MX_16, TG_MX_16, 0 },
  { 0x05BC20, "RoadHdmaTables", Tg3RoadHdmaTables, TG_CALL_JSL, TG_MX_16, TG_MX_16, 0 },
};
const int tg3_port_func_count = sizeof(tg3_port_funcs) / sizeof(tg3_port_funcs[0]);
