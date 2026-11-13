// Top Gear 3000's translated routines, in address order (see src/port/registry.c).

#include "../port/port.h"

void Tg3MoveObject(TgRegs* r);
void Tg3ObjectSegment(TgRegs* r);
void Tg3SendObject(TgRegs* r);
void Tg3SendShapeRam(TgRegs* r);
void Tg3SendShapeRom(TgRegs* r);

const TgPortFunc tg3_port_funcs[] = {
  { 0x01F887, "MoveObject", Tg3MoveObject, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x01FBB1, "ObjectSegment", Tg3ObjectSegment, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x02D934, "SendObject", Tg3SendObject, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x02DDBF, "SendShapeRam", Tg3SendShapeRam, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
  { 0x02DEA8, "SendShapeRom", Tg3SendShapeRom, TG_CALL_JSR, TG_MX_16, TG_MX_16, 0 },
};
const int tg3_port_func_count = sizeof(tg3_port_funcs) / sizeof(tg3_port_funcs[0]);
