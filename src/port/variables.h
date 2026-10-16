#ifndef TG_VARIABLES_H
#define TG_VARIABLES_H

// Work RAM addresses used by translated code, named as in disasm/ram.txt.

#define RaceJoy1Held 0x0010  // direct page offset
#define RaceJoy2Held 0x0012  // direct page offset
#define RaceWindowMode 0x0100  // $C0DF in 1-player races: per-view window shapes (tentative)
#define RaceP1SegmentValue 0x0112  // $7E:3E00 entry for each view's track segment (tentative)
#define RaceP2SegmentValue 0x0114
#define RaceLeaderSegmentValue 0x0116  // same for the car furthest along
#define RaceWindowTablePtr 0x00EB  // direct page offset; pointer in the data bank
#define RaceWindowShape1 0x182E  // bits 9-11 pick view 1's window shape (tentative)
#define RaceWindowShape2 0x1830
#define RaceWindowHidden1 0x1EDB  // word whose bit 15 hides view 1's window (tentative)
#define RaceWindowHidden2 0x1EDC
#define RaceCars 0x0200  // car records, 18 bytes each
#define RaceCarSize 0x12
#define RaceP1Car 0x0204  // offset of player 1's record in RaceCars
#define RaceP2Car 0x0206
#define RaceSegmentTable 0x3E00  // $7E:3E00, one word per track segment
#define RaceP1Messages 0x0118  // HUD message state, 18 bytes per player (see messages.c)
#define RaceP2Messages 0x012A
#define RaceLaps 0x1E56  // laps in the race
#define RaceP1Speed 0x1E5A
#define RaceP2Speed 0x1E5C
#define RaceP1Rpm 0x1E5E
#define RaceP2Rpm 0x1E60
#define RaceP1Fuel 0x1E62  // fuel gauge, high byte / 8 = segments (tentative)
#define RaceP2Fuel 0x1E64
#define RaceP1Nitros 0x1E6C  // nitro boosts left, shown as a digit
#define RaceP2Nitros 0x1E6E
#define RaceP1Gear 0x1E72  // 0-4
#define RaceP1Lap 0x1E76
#define RaceP2Lap 0x1E78
#define RaceP2Gear 0x1E74
#define RaceP1Position 0x1E7A  // 0 = first
#define RaceP2Position 0x1E7C
#define RaceP1TrackPos 0x1ED7  // high byte is the track segment
#define RaceP2TrackPos 0x1EDA
#define RacePitEntry 0x1EF0  // track position the pit message is keyed to (tentative)
#define RaceP1PitFlags 0x1EF4  // word; $4000 = pit message shown (P2's word overlaps at $1EF5)
#define RaceP2PitFlags 0x1EF5
#define RaceP1Bumped 0x1EF5  // bit 7: player 1 bumped a car (byte inside the pit flag words)
#define RaceP2Bumped 0x1EF6
#define RaceFramesElapsed 0x1E90  // video frames since the last race update (normally 3)
#define RaceTime 0x1EAE  // race clock in frames
#define RaceFinishCount 0x1EB0  // byte: cars finished so far
#define RaceP1PositionShown 0x1F00  // shown instead of RaceP1Position when not negative (tentative)
#define RaceP2PositionShown 0x1F02
#define RaceFinishOrder 0x1F12  // bytes: car ids in finishing order
#define RaceFinishTimes 0x1F26  // words by car id; negative until the car finishes
#define RaceP1FinishTime 0x1F26  // player 1 is car 0
#define RaceP2FinishTime 0x1F28
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
