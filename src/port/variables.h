#ifndef TG_VARIABLES_H
#define TG_VARIABLES_H

// Work RAM addresses used by translated code, named as in disasm/ram.txt.

#define RaceJoy1Held 0x0010  // direct page offset
#define RaceJoy2Held 0x0012  // direct page offset
#define RaceP1Speed 0x1E5A
#define RaceP2Speed 0x1E5C
#define RaceP1Rpm 0x1E5E
#define RaceP2Rpm 0x1E60
#define RaceP1Fuel 0x1E62  // fuel gauge, high byte / 8 = segments (tentative)
#define RaceP2Fuel 0x1E64
#define RaceP1Nitros 0x1E6C  // nitro boosts left, shown as a digit
#define RaceP2Nitros 0x1E6E
#define RaceP1Gear 0x1E72  // 0-4
#define RaceP2Gear 0x1E74
#define RaceP1Position 0x1E7A  // 0 = first
#define RaceP2Position 0x1E7C
#define RaceFramesElapsed 0x1E90  // video frames since the last race update (normally 3)
#define RaceTime 0x1EAE  // shown as the lap time when a player's own is negative (tentative)
#define RaceP1PositionShown 0x1F00  // shown instead of RaceP1Position when not negative (tentative)
#define RaceP2PositionShown 0x1F02
#define RaceP1LapTime 0x1F26  // in frames (tentative)
#define RaceP2LapTime 0x1F28
#define RaceP1AccelButton 0x1F76  // button mask from the control setup
#define RaceP2AccelButton 0x1F93  // as read by RaceRevEngines (tentative)
#define RaceP1SpeedKph 0x1FB0  // byte: nonzero shows km/h, zero mph
#define RaceP2SpeedKph 0x1FB1
#define WarmBootFlag 0x1FF6

// Race HUD tile buffers in low RAM, copied to VRAM elsewhere (tentative)
#define RaceP1RpmBar 0x03BA
#define RaceP2RpmBar 0x047A
#define RaceP1FuelBar 0x0410
#define RaceP2FuelBar 0x04D0

#endif
