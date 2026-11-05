// Top Gear 2's translated routines, in address order (see src/port/registry.c).

#include "../port/port.h"

void Tg2UploadQueue0(TgRegs* r);
void Tg2BuildRoad(TgRegs* r);
void Tg2DrawObjects1P(TgRegs* r);
void Tg2DrawObjects2P(TgRegs* r);
void Tg2UploadQueue1(TgRegs* r);
void Tg2AddToWords(TgRegs* r);
void Tg2VramDma7E(TgRegs* r);
void Tg2ClearVram(TgRegs* r);
void Tg2VramDma7F(TgRegs* r);
void Tg2VramDma(TgRegs* r);
void Tg2SetFadeIn(TgRegs* r);
void Tg2FillWords(TgRegs* r);

const TgPortFunc tg2_port_funcs[] = {
  { 0x01A4D1, "UploadQueue0", Tg2UploadQueue0, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x01A505, "UploadQueue1", Tg2UploadQueue1, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x01DAD6, "AddToWords", Tg2AddToWords, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x01EF37, "VramDma7E", Tg2VramDma7E, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, 0 },
  { 0x01EF6C, "VramDma7F", Tg2VramDma7F, TG_CALL_JSR, TG_MX_ANY, TG_MX_16, 0 },
  { 0x08CB74, "BuildRoad", Tg2BuildRoad, TG_CALL_JSL, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x0B8000, "DrawObjects1P", Tg2DrawObjects1P, TG_CALL_JSL, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x0B818A, "DrawObjects2P", Tg2DrawObjects2P, TG_CALL_JSL, TG_MX_ANY, TG_MX_16, TG_DEAD_AXY_FLAGS },
  { 0x1F9167, "VramDma", Tg2VramDma, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1F9EDE, "ClearVram", Tg2ClearVram, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1FA117, "SetFadeIn", Tg2SetFadeIn, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x1FA13D, "FillWords", Tg2FillWords, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
};
const int tg2_port_func_count = sizeof(tg2_port_funcs) / sizeof(tg2_port_funcs[0]);
