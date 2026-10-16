#ifndef TG_RACE_COMMON_H
#define TG_RACE_COMMON_H

#include <stdint.h>

// Helpers shared by the translated race routines.

// $00:A89E with A = 0: the next random byte (also left in $1EF7). In drive.c.
uint16_t race_random(void);

// Race set-up (setup.c) and the wait for the next race update (waits.c), called directly
// by RaceLoadAll.
typedef struct TgRegs TgRegs;
void RaceLoadCourse(TgRegs* r);
void RaceLoadSkyline(TgRegs* r);
void RaceLoadRoadside(TgRegs* r);
void RaceLoadCars(TgRegs* r);
void RaceSetupGrid(TgRegs* r);
void RacePreloadTileCache(TgRegs* r);
void RaceDrawHudFrame(TgRegs* r);
void RaceSetupNames(TgRegs* r);
void RaceClearHdmaTables(TgRegs* r);
void RaceSyncWait(TgRegs* r);

#endif
