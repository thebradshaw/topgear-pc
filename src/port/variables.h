#ifndef TG_VARIABLES_H
#define TG_VARIABLES_H

// Work RAM addresses used by translated code, named as in disasm/ram.txt.

#define RaceJoy1Held 0x0010  // direct page offset
#define RaceJoy2Held 0x0012  // direct page offset
#define RaceP1Rpm 0x1E5E
#define RaceP2Rpm 0x1E60
#define RaceFramesElapsed 0x1E90  // video frames since the last race update (normally 3)
#define RaceP1AccelButton 0x1F76  // button mask from the control setup
#define RaceP2AccelButton 0x1F93  // as read by RaceRevEngines (tentative)

#endif
