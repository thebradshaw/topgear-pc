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
void Tg2WindowsNone(TgRegs* r);
void Tg2WindowsTunnel(TgRegs* r);
void Tg2WindowsWall(TgRegs* r);
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

const TgPortFunc tg2_port_funcs[] = {
  { 0x008A41, "DriveCar1", Tg2DriveCar1, TG_CALL_JSL, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x00A2A5, "DriveCar2", Tg2DriveCar2, TG_CALL_JSL, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x00D726, "Cars", Tg2Cars, TG_CALL_JSL, TG_MX_16, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x019261, "RaceColours1P", Tg2RaceColours1P, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, TG_DEAD_A | TG_DEAD_Y },
  { 0x019284, "RaceColours2PTop", Tg2RaceColours2PTop, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, TG_DEAD_A | TG_DEAD_Y },
  { 0x0192A7, "RaceColours2PBottom", Tg2RaceColours2PBottom, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, TG_DEAD_A | TG_DEAD_Y },
  { 0x01A4D1, "UploadQueue0", Tg2UploadQueue0, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x01A505, "UploadQueue1", Tg2UploadQueue1, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x01DAD6, "AddToWords", Tg2AddToWords, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x01EF37, "VramDma7E", Tg2VramDma7E, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, 0 },
  { 0x01EF6C, "VramDma7F", Tg2VramDma7F, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, 0 },
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
  { 0x0EB250, "WindowsBand", Tg2WindowsBand, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1F9167, "VramDma", Tg2VramDma, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1F9EDE, "ClearVram", Tg2ClearVram, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1FA117, "SetFadeIn", Tg2SetFadeIn, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1FA13D, "FillWords", Tg2FillWords, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
};
const int tg2_port_func_count = sizeof(tg2_port_funcs) / sizeof(tg2_port_funcs[0]);
