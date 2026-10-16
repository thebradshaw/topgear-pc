# Bank $0F (menus) translation map

Source: build/disasm/topgear.s lines 47273-53434 (bank $0F). 4,135 executed instructions in the
bank (all of $0F:8000-$0F:B481); everything after $0F:B482 and $0F:A6A9-$0F:B2F5 is data (strings,
screen tables, compressed graphics). Instruction counts below are "reachable from the entry"
as reported by `tools/routines.py --bank 0F` (they include fall-through code and code the entry
JMPs to, so they overlap); block counts for big inline sections are counted by address range.
"Guess" marks interpretation not proven by the code alone. Strings were read from the ROM.

## 0. Entry, exit, conventions

- GameInit $0F:806D is entered by JSL with A16/X16: from $00:8126 (power-on, A=0) and from
  $00:886F (after every race, A=1; returns to $00:8873). program.c already calls it with
  `tg_call_jsl(r, 0x0F806D, 0x8873, TG_MX_16)`.
- Single exit: RTL at $0F:820D, after the course preview. Before returning it sets
  NMITIMEN=$01 (NMI off, auto-joypad on), INIDISP=$80, OBSEL=$68, clears VRAM/CGRAM/OAM
  (A644) and uploads the HUD course map (82F0).
- DB=$0F throughout (PHK/PLB). Text readers use `lda $0000,x` with DB=$0F, so a pointer below
  $2000 reads WRAM through the low mirror: X=$0066 is a string built in DP $66-$75.
- NMI trampoline at $0000 = JML $0F:8000; IRQ trampoline at $0004 = RTI (no IRQs in menus).
- NMI-shared variables: $08 frame counter, $0A/$0C joypad 1/2 held, $0E/$10 newly-pressed
  (never read by bank $0F), $42 INIDISP shadow, $0424 DMA channel mask (NMI starts it, then
  clears it), $90 palette-upload request ($4C = request, NMI clears it to 0).
- Variable meanings found here (add to ram.txt): $1F04 2-player flag; $1F06 course 0-31
  (country*4+race; ram.txt calls it CarTypeIndex, variables.h already says "course");
  $1F08 saved course; $1F0A[3] highest country unlocked per skill; $1F10 skill level 0-2;
  $1F12.. finishing order; $1F26[] race times; $1F4E[20] championship points; $1F62[20]
  standings order; $1F76.. P1 control mapping; $1F8F P1 car 0-3; $1FAC P2/rival car;
  $1FB0 speed units (0 MPH, $FFFF KPH); $1FB2[32] lap records; $1FF6 WarmBootFlag (also:
  attract-mode countdown from $04B0, $FFFF = demo race, $8000 = skip straight to the main
  menu; who sets $8000 is unknown); $1FF8/$1FFA player places; $5A menu cursor; $81 player
  being edited (0/1); $46 screen number.

## 1. Routine entry points

Leaf = calls nothing. W = waits for NMI (see section 2). Names are proposals.

| Addr | Proposed name | Insns | W | What it does |
|---|---|---|---|---|
| 8000 | NmiHandler (MenuNmi) | 53 | - | if $90=$4C: palette DMA set-up (A21A), $0424=2, $90=0; start DMA $0424; INIDISP=$42; ++$08, ++$1FFC; poll $4212 bit 0; read pads |
| 806D | GameInit | 35 own, 325 reach | W | entry; installs trampolines; default names if all zero; A=0 -> 815A, else -> 83D7 |
| 80BD | SetupColorGradientHdma | 21 | - | HDMA ch3/4 indirect to $2121/$2122 from $0D:C7BC/$0D:C7F3; HDMAEN=$18 |
| 80F4 | ResetNamesAndRecords | 25 | - | blank names, default controls $0D:C656, records $0D:C696, $1FB0=0 |
| 8133 | MenuStartMusic | 20 | - | $2141=$18, JSL $07:8000 with A=song, $2140=0, 65,536-loop delay, $2140=song+1 |
| 815A | (label) MenuFromTitle | 68 block | W | A53C, 8C95 title+menus, default names, falls into 818E |
| 818E | (label) MenuRaceIntro | in above | W | course preview screen 15 "LOCATION/STATISTICS/DISTANCE/LAP RECORD", then RTL |
| 820E | MenuSetFadeRowsIntro | 7 | - | $042C[16] fade-row mask from $0E:FDC6 |
| 821D | MenuLoadCourseMap | 92 | W | decompress map tiles ($0D:8000 or $0D:911F) to $7E:2000, pick course slice into $7E:7000, NMI-DMA to VRAM $2200, tilemap in $0C6C buffer |
| 82F0 | MenuUploadHudMap | 55 | - | VRAM $0900+: de-duplicated map tiles (836D), tilemaps at VRAM $0C21/$0DE1 (guess: race HUD map) |
| 836D | MenuUploadMapTile | 27 | - | one 16-word tile to $2118, or empty-tile index |
| 83A1 | MenuDefaultPlayerName | 20 | - | name of player $81 from $0F:B4BC/$B4C9 |
| 83D7 | (label) MenuAfterRace | 222 block | W | results, standings, qualify check (below) |
| 84F5 | (label) MenuEndScreen | 123 block | W | "SORRY YOU FAILED TO QUALIFY" / "QUALIFIED FOR NEXT COUNTRY"+password / "COMPLETING TOP GEAR" |
| 8693 | MenuDrawPlayerPlace | 73 | - | player $81's name + place ("1ST".. from $1FF8) as sprites |
| 86E7 | MenuRecordPlayerPlaces | 15 | - | $1FF8/$1FFA = standings places of players 0/1 |
| 8705 | MenuLoadPaletteRow15 | 9 | - | $064C from $0E:FD66, patch $064E |
| 871A | MenuCheckQualified | 11 | - | carry set if no human in first 5 of $1F12 (guess: top-5 rule) |
| 8733 | MenuAdvanceCourse | 12 | - | ++$1F06 (mod 32); A=1/Z clear unless a country ends (8750, unrun) |
| 8781 | MenuBlankBigRows | 22 | - | 10 big-font rows of blanks ($0D:C400) |
| 87AD | MenuBlankSmallRows | 22 | - | 20 small-font rows of blanks ($0D:C408) |
| 87D9 | MenuCheckNamesA | 11 | - | name check; recorded paths change nothing (branches unrun) |
| 8802 | MenuCheckNamesB | 16 | - | name check for '='; recorded paths change nothing |
| 882D | MenuDrawRaceResults | 129 | - | award points ($0E:8024 by place) into $1F4E, update lap record, draw "RESULTS" table with times |
| 894F | MenuDrawStandings | 138 | - | sort 20 drivers by points (8-bit index), draw "WORLD RANKING" (runs into 8B4E points column) |
| 8A09 | MenuFormatTime | 52 | - | time in A -> min/sec/hundredths text in $68-$6F; uses $4204/$4206 and PPU multiply $211B/$211C/$2134 |
| 8A81 | MenuDrawLapRecord | 91 | - | lap record of course ($1FB2) as sprite text |
| 8ADC | MenuDrawDistance | 97 | - | course distance in miles/km ($0D:C6F6 * 10 or 15) via $4202/$4203 |
| 8BB6 | MenuToDecimal | 17 | leaf | A (0-99) -> two ASCII digits in A and $7F |
| 8BD6 | MenuDrawPlaceColumn | 31 | - | 10 place labels from $0D:C42B |
| 8C16 | (label) MenuLoadCarPalettes | ~25 | - | reached by JMP from 914E: rows 2/3 from $0E:FD06, car colours $0F:A51C by $1F8F/$1FAC |
| 8C68 | MenuLoadPaletteRow9 | 7 | leaf | $058C from $0E:FCC6 |
| 8C77 | MenuLoadPaletteRow10 | 0 run | leaf | $05AC from $0E:FCE6 (credits screen only) |
| 8C86 | MenuLoadPaletteRow15b | 7 | leaf | $064C from $0E:FD66 |
| 8C95 | MenuTitle | 283 | W | title sequence, attract timer, then 904B+918B main menu |
| 8F80 | MenuClearFadeRows | 6 | leaf | $042C[16]=0 |
| 8F8B | MenuTitleLogoSprites | 21 | leaf | OAM entries for the logo |
| 8FB5 | MenuOamHighFill | 7 | leaf | $0402-$040E = $AAAA |
| 8FC3 | MenuCreditsSprites | 0 run | leaf | credits screen sprites |
| 8FFB | MenuTitleTextSprites | 34 | leaf | sprites from $7E:7F00 |
| 904B | MenuLoadMenuScreen | 57 | W | blank, clear VRAM, decompress fonts/frames ($0D:A44D, $0F:BBA7, $0E:8038, $0E:F45E, $0D:A27C) |
| 90D7 | MenuLoadPaletteRow1 | 7 | leaf | $048C from $0E:FCA6 |
| 90E6 | MenuStartDemo | 10 | leaf | save course, random course and RaceRandomSeed from FrameCounter16 |
| 9102 | MenuDrawCarPortraitAt | 31 | - | variant of 911C |
| 911C | MenuDrawCarPortrait | 26 | leaf | 8x19 tile block from $7E:2000 into $0C6C buffer + $8E |
| 914E | MenuDrawCarPortraits | 67 | - | both players' cars, then JMP 8C16 |
| 918B | MenuMain | 565 reach / 103 own | W | main menu (section 3) |
| 929E | (label) MenuCountrySelect | 106 block | W | "COUNTRY", 8 countries, locked -> password entry |
| 93A3 | MenuDrawUnlockMarks | 30 | leaf | sprites for unlocked countries (guess) |
| 93DC | MenuCheckPassword | 41 | - | match DP $68 against $0D:C513 per skill; sets $1F10, $1F0A |
| 9426 | MenuComparePassword | 12 | leaf | 8-byte compare |
| 943D | MenuTextEntry | 141 | W | on-screen keyboard for names and passwords (pad $81) |
| 95AC | MenuRedrawEntry | 4 | - | 9F93 of DP string + 9DFC |
| 95B6 | MenuSecretCredits | 3 run | W | 2P only: if both names match $0D:C41B/$C423, "PROGRAMMED BY" screen (unrun) |
| 9652 | (label) MenuSkillLevel | 54 block | W | AMATEUR/PROFESSIONAL/CHAMPIONSHIP -> $1F10 |
| 96DE | (label) MenuPlayerSetup | 302 block | W | 2P chooser (unrun) or name, gear, control method, car type |
| 9AD6 | MenuDrawCarStats | 36 | leaf | stat values for KPH ($0D:C642/$C64E) |
| 9B3B | MenuLoadNameForEntry | 11 | - | 9B7B, cursor at end |
| 9B56 | MenuPlaceEntryCursor | 14 | leaf | OAM 0 at letter $5E |
| 9B70 | MenuDrawPlayerLabel | 35 | - | blank label B4AD, then 9B7B |
| 9B7B | MenuLoadPlayerName | 31 | leaf | name of player $81 to DP $68 |
| 9BC4 | MenuStorePlayerName | 13 | leaf | DP $68 -> name of player $81 |
| 9BF9 | MenuDrawControlPad | 27 | leaf | pad picture tiles from $7E:2000 into tilemap |
| 9C31 | MenuDrawScreen | 13 | - | A=screen: boxes, small text, big text, base palette, row 1 |
| 9C42 | MenuClearVramMap | 13 | - | $2118 x $400 at VRAM 0, then 9C5B |
| 9C5B | MenuClearTilemapBuffer | 6 | leaf | $0C6C-$1C6B = 0 |
| 9C66 | MenuClearFadeWork | 8 | leaf | $066C/$146C work areas, $0426 |
| 9C77 | MenuFadeSetup | 33 | - | per-component colour steps for rows in $042C |
| 9CBC | MenuFadeSplitRow | 29 | leaf | split 16 colours to r/g/b |
| 9CF2 | (label) MenuFadeRun | 24 | W | 32 steps: 9D31 for each row, $90=$4C, wait for $90=0 |
| 9D31 | MenuFadeStepRow | 66 | leaf | add one step to a palette row |
| 9DB2 | MenuFadeIn | 36 | W | CGADSUB=$20, $42=$0F, zero rows (9E5E), 32-frame fade up |
| 9DC5 | MenuFadeOut | 33 | W | 32-frame fade down |
| 9DDD | MenuQueueOamAndMap | 23 | leaf | DMA ch7 OAM $0202 x$220, falls into 9DFC |
| 9DFC | MenuQueueTilemap | 12 | leaf | DMA ch2 $0C6C -> VRAM 0, $1000 bytes (caller sets $0424) |
| 9E1C | MenuClearBg | 31 | W | fixed-source DMA from $0F:9E2B (a $00 inside an instruction) to VRAM, clear buffer, wait 1 |
| 9E5E | MenuFadeZeroRows | 23 | leaf | zero palette rows being faded in |
| 9E88 | MenuWaitButton512 | 21 | W | wait <=$200 frames for a button, then for release |
| 9EAC | MenuWaitButton1536 | 21 | W | same, $600 frames |
| 9ED0/9ED3 | MenuDrawSpriteText | 47 | - | strings as OAM entries at Y |
| 9F35 | MenuDrawScreenText | 40 | - | small-font list for screen $46 ($0D:C716) |
| 9F3E | MenuDrawText | 35 | - | 8x8 font strings into $0C6C buffer |
| 9F88 | MenuDrawScreenBigText | 96 | - | big-font list for screen $46 ($0D:C738) |
| 9F93 | MenuDrawBigText | 90 | - | 16x16 font, $FF bumps palette $44 |
| A045 | MenuDrawCursor | 112 | - | highlight bar sprites at $48 size $4A, clears OAM from X |
| A131 | MenuDrawScreenBoxes | 81 | - | frame boxes for screen $46 ($0D:C75A) |
| A1C4 | MenuSpriteOrigin | 8 | leaf | $58 from $48 |
| A1D0 | MenuTilemapAddr | 17 | leaf | $4C = $0C6C + 2*(row*32+col) |
| A1EB | MenuClearPalette | 6 | leaf | $046C-$066B = 0 |
| A1F6 | MenuSetPaletteDma | 14 | leaf | DMA ch1 $046C -> CGRAM $200 bytes |
| A21A | MenuSetPaletteDmaNmi | 14 | leaf | copy of A1F6 used by the NMI |
| A23E | MenuLoadBasePalette | 7 | leaf | $046C[512] from $0E:FB86 |
| A24D | MenuWaitFrames | 6 | W | $08=0, spin until $08==A |
| A258 | MenuWriteMode7Regs | 35 | leaf | M7A-M7Y, BG1HOFS/VOFS from DP $1E-$2D |
| A2AD | MenuComputeMode7Matrix | 90 | - | matrix from angle $12 (sine $0D:AC51) and scales $14/$16/$1A |
| A360 | MenuMulSigned | 77 | - | signed 16x16 via A39B |
| A39B | MenuMulUnsigned | 53 | leaf | 16x16 -> 32 via $4202/$4203 |
| A402 | MenuConvertMode7Tiles | 51 | leaf | $D0 4bpp tiles at $7E:6000+Y -> 8bpp at $7E:2000 |
| A462 | MenuUploadMode7Tiles | 45 | W | 4 frames of NMI DMA $7E:2000 -> VRAM high bytes |
| A4D2 | MenuWriteMode7Map | 33 | W | wait 2, then $2118 writes (VMAIN=0) from $7E:6000+X |
| A53C | MenuInitPpu | 138 reach | - | all PPU regs, HTIME/VTIME, MEMSEL=1, CLI; falls into A644 |
| A644 | MenuClearPpuMemory | 40 | - | immediate DMA: OAM, all 64 KiB VRAM, CGRAM from $0F:A67D (=$00); falls into A691 |
| A691 | MenuClearOam | 12 | leaf | OAM buffer Y=$F0, high table 0 |
| B2F6 | MenuDecompressToVramAttr | 31 | - | B306 with tile attribute from $44 |
| B306 | MenuDecompressToVram | 22 | - | A=VMAIN/bank, X=src, Y=VRAM |
| B334 | MenuDecompressToRam | 20 | - | to $7E:2000+Y |
| B35A/B367 | MenuDecompressBegin/Next | 158 | leaf | same LZ as $00:EA8E/EA9B (decompress.c), but DP $2E-$37 and ring at $044C-$0C4B |

Unrun code worth knowing: 8558-8591 (end-screen texts for A=0 and A>=2), 8750 (country end),
925F (SPEED MPH/KPH toggle), 95BC-9651 (credits), 96F2-9777 (2P "PLAYER SETUP" chooser),
994F-99A3 (control method left/right), most player-2 branches ("ldy $81; bne").
Decoded with tools/decode.py: none of it calls outside the routines above.

## 2. Routines that wait (TG_WAITS) and what they wait on

No WAI, no $4212/$4210 loops outside the NMI, no IRQ. All waits are spins on RAM the NMI writes:

| Where | Flag | Notes |
|---|---|---|
| A24D MenuWaitFrames | $08 (NMI increments) | $08=0 then `cmp $08; bne`: A frames |
| 9CF2 loop at 9D27 (MenuFadeIn/Out) | $90 (NMI clears after setting palette DMA) | one wait per fade step, 32 per fade |
| 918B 91D0 (with A24D), 922F | $0A | menu cursor; 922F waits for release |
| 929E 92E4, 9327 | $0A | spins with no frame wait |
| 943D top, 952E, 9541 | $0A+2*$81 | keyboard; release waits |
| 9652 967B, 96BE | $0A | skill level |
| 9781.. 9856, 9889, 9898, 993E, 99A4, 9A5B, 9AAB, 9AC4 | $0A+2*$81 | gear, control, car |
| 96F2 (unrun) 9716/9761 | $0A or $0C | 2P chooser |
| 9E88/9EAC | $0A, $0C (with A24D) | timeout or button |
| 8C95 8F17 | $0A, $0C (with A24D) | attract timer, 1200 frames |

Routines that wait only through callees: GameInit body, 8C95, 904B, 918B and its submenus,
821D, 9E1C, A462, A4D2, 9DB2/9DC5, 9E88/9EAC. (9C31 and the drawing routines do not wait.)
Every routine in the call tree above them is a fiber candidate; the non-waiting ones can be
verified one call at a time first.

## 3. Call graph and menu flow

```
GameInit 806D (A=0 power-on / A=1 after race)
 |- ResetNamesAndRecords (only if all name bytes zero)
 |- A=0: 815A MenuFromTitle
 |   |- A53C MenuInitPpu -> A644 -> A691
 |   |- 8C95 MenuTitle
 |   |   |- $1FF6=$8000: skip to 8F5E (main menu)
 |   |   |- load: B306 x2, B334 x3, A402 x2, A462, A4D2 (mode-7 logo)
 |   |   |- not after demo: "KEMCO PRESENTS" sprites (9ED3 B022), 9DB2, 9E88, 9DC5
 |   |   |- 8DA3: BGMODE 7, SetupColorGradientHdma, brightness 0->14 (2 frames each),
 |   |   |   zoom loops 8E14/8E98: A2AD, A24D, A258 per frame; second map: A462, A4D2
 |   |   |- copyright sprites (9ED3 AFC6), attract wait 1200 frames (8F17)
 |   |   |    timeout -> 90E6 MenuStartDemo ($1FF6=$FFFF) -> fade -> RTS (demo race)
 |   |   |    button -> fade, HDMA off -> 904B MenuLoadMenuScreen -> 918B MenuMain
 |   |- 83A1 default names; then 818E MenuRaceIntro:
 |       820E, A691, 9C31-like (A131/9F35 screen 15), 821D course map, 9ED0 course name,
 |       8ADC distance, 8A81 lap record, 9DDD; if $1FF6=0: 9DB2, 9E88, 9DC5;
 |       9E1C, NMITIMEN=$01, A644, 82F0 -> RTL (race starts)
 |- A=1: 83D7 MenuAfterRace
     |- A53C...; demo ($1FF6!=0): restore $1F06 from $1F08 -> 815A
     |- unlock $1F0A[$1F10]; 904B; 9F93 "RESULTS"; 87D9; 882D; fade in
     |- 9F93 "WORLD RANKING"; 894F standings; 9E88; 8802
     |- scroll BG1VOFS 0->$100 by 3 per frame (845D); 9E88; 9DC5; 9E1C
     |- 871A not qualified -> zero points -> 84F5 end screen (A=1)
     |- else 86E7, 8733: same country -> 818E next race intro
     |                   country done (unrun) -> 84F5 (A=0, or 3 after course 31)
     '- 84F5 MenuEndScreen: B306/B334 graphics, 914E car portraits, message
        (A=1 SORRY.. / A=0 QUALIFIED + 8133 song 5 / A>=2 COMPLETING TOP GEAR + 8133 song 6),
        8693 per player, password ($0D:C513) for A=0, fade, 9EAC, fade;
        A=0 -> 818E; else $1FF6=A, 8133 song 0, -> 83D7 -> 815A (title)

918B MenuMain (screen 0 "OPTIONS": 1 PLAYER/2 PLAYERS, SKILL LEVEL, COUNTRY, SPEED MPH, PLAY GAME)
 |- 9C31(0), 9C31($1F04), A045 cursor, 9DB2; loop on $0A: up/down moves $5A (0-4);
 |   left/right on item 0 toggles $1F04 (2P)
 |- item 0 -> 96DE MenuPlayerSetup: 2P -> 96F2 chooser (unrun); 1P -> 9781:
 |     name (screen 3, 9B3B, 943D, 9BC4), 95B6, gear select (screen 4, $87),
 |     control method (screen 5, 9BF9, 4 layouts $0D:C656 -> $1F76..),
 |     car type (screen 6, 9AD6 stats, 2x2 grid -> $1F8F) -> 9778 -> 918B
 |- item 1 -> 9652 MenuSkillLevel (screen 2) -> $1F10, $1F06 -> 918B
 |- item 2 -> 929E MenuCountrySelect (screen 7, 8 countries): unlocked -> $1F06=c*4 -> 918B;
 |     locked -> PASSWORD (screen 8, 943D, 93DC) -> 92B5 again
 |- item 3 -> 925F toggle $1FB0 MPH/KPH (unrun)
 '- item 4 PLAY GAME -> 1P: pick $1FAC != $1F8F from FrameCounter16 -> RTS (to 8F7A)
```

## 4. Calls out of bank $0F, indirect jumps, tables

- Only one call out: `JSL $07:8000` UploadSoundDriver in 8133 (callers: 8675 song 0; unrun
  8563 song 6, 858D song 5). The race program does the same sequence at $00:8175 and
  program.c runs it with tg_call_jsl plus tg_call_jmp for the delay loops; copy that.
- Returns: RTL to $00:8873 (after race) or $00:812A (power-on).
- No `JMP (abs)`, `JMP (abs,X)`, `JSR (abs,X)` or JML in executed or decoded-unrun code.
  Dispatch is by compare chains on $5A. Data pointer tables (16-bit, bank $0F strings):
  $0D:C716 / $0D:C738 / $0D:C75A (per-screen small text, big text, boxes), $0D:C77C
  (course names), $0D:C632/$C63A (screen per control/car choice), $0F:B4D4/$0D:C5F8/$C5FA
  (fade row ranges). Long data reads from banks $0D, $0E, $7E.
- Code-as-data: 9E1C and A644 use DMA sources inside instructions ($0F:9E2B and $0F:A67D,
  both $00). 8ADC uses `beq *+5` into the operand of `bit $0AA9` to run a hidden
  `lda #$0A` (MPH multiplier 10 vs KPH 15).

## 5. Timing-sensitive points

- NMI handler: reads $4210, polls $4212 bit 0 until auto-joypad read ends, starts DMA from
  $0424 (OAM/tilemap/palette uploads). Keep it emulated, or port it like the race IRQ stages.
- 8133: writes $2141/$2140 around the sound upload with a 65,536-iteration `dex/bne`
  delay (~7 frames of CPU time) before $2140=song+1. The delay matters to the APU, so run
  $0F:814A-$0F:8150 emulated (tg_call_jmp) as program.c does. No APU port reads in bank $0F.
- A4D2 writes ~$1C0 bytes to $2118 directly two frames after a wait. At $0F:8E90 the
  screen is on ($42=$0F), so the loop relies on fitting in vblank; at slow-ROM speed it
  probably runs past vblank (guess), and the hardware would drop late writes. Check what
  LakeSnes does before deciding C behaviour.
- SetupColorGradientHdma (HDMAEN=$18) and HDMA off at 8F50 are written just after a frame
  wait; mode-7 registers (A258) and BG1VOFS (845D loop, 8E14/8E98) likewise. Fine if C writes
  them at the same point after tg_port_wait.
- 8A09 uses the PPU multiplier: writes $211B twice/$211C, reads $2134; it changes the M7A/M7B
  latches, so C must make the same bus writes even though it can compute the product.
- $4202/$4203 and $4204/$4206 (A39B, 8ADC, 8A09) read results after NOPs; C computes directly
  (lockstep ignores these writes).
- No raster IRQ: A53C sets HTIME=$84/VTIME=1 and CLI, but NMITIMEN is $81 or $01 (no IRQ
  enable), and $0004 holds RTI. No reads of $2137/$213C/$213D.
- Decompressor ring buffer $044C-$0C4B overlaps the palette shadow ($046C) and fade work
  ($066C), so callers reload palettes after decompressing. C must keep the overlap.

## 6. Proposed translation order

Register the non-waiting routines one at a time (JSR, M/X 16 unless noted) so verify mode
checks them, then write the waiting flow as one fiber.

Batch 1, pure leaves (~330): A1C4, A1D0, A1EB, A1F6, A21A, A23E, 90D7, 820E, 8C68, 8C86,
8705, 8F80, 8FB5, 9C5B, 9C66, A691, 8BB6, 9B7B, 9BC4, 9B56, 83A1, ResetNamesAndRecords 80F4,
90E6, 9426, 911C/9102, 9BF9, 9AD6, 8F8B, 8FFB, 9DDD/9DFC, A258, 87D9, 8802, 86E7, 871A, 8733.

Batch 2, decompressor and VRAM loaders (~330): B35A/B367 (reuse decompress.c with DP and
ring as parameters), B306, B2F6, B334, A402, 9C42, 82F0+836D, A53C+A644 (A644's 64 KiB DMA
runs at once in C).

Batch 3, text and screen drawing (~450): 9F3E, 9F35, 9F93, 9F88, 9ED3/9ED0, A131, A045,
9C31, 8781, 87AD, 8BD6, 93A3, 9B3B, 9B70, 95AC, 914E (+8C16), 8693.

Batch 4, results, timing maths, fade arithmetic (~520): 8A09 (keep the PPU multiplier writes),
8ADC, 8A81, 882D, 894F (8-bit index quirk: high bytes of X/Y cleared), 93DC, A39B, A360,
A2AD, 9C77, 9CBC, 9D31, 9E5E.

Batch 5, waiting helpers as fiber functions (~250): A24D, 9CF2/9DB2/9DC5, 9E1C, 9E88, 9EAC,
A462, A4D2, 821D, 904B, 8133 (delay emulated).

Batch 6, the flows (~1,200 own): 943D text entry; 918B main menu with 929E, 9652, 96DE/9781
submenus; 8C95 title; GameInit with 815A/818E/83D7/84F5. Register GameInit as TG_WAITS
($0F:806D, JSL); check with tools/shift_compare.py. Each `lda $0A; bne/beq` spin becomes
a loop around tg_port_wait(). Unrun branches (2P, KPH, credits, country end) can be
translated from tools/decode.py output but no recording checks them yet; a recording that
plays 2P and clears a country would cover most of them.

Status (2026-10-16): batches 1-6 are translated. Batches 1-4 are registered one routine at
a time and checked by verify mode (src/port/menu_leaves.c, menu_text.c, menu_results.c).
Batches 5-6 are src/port/menu_flow.c: GameInit ($0F:806D) runs as one TG_WAITS fiber and
the race-end code in program.c calls it directly with K set to $0F. tools/shift_compare.py
matches the emulated menus on attract, race_2p, country_uk and country_japan; race_1p and a
few others differ by two frames at race end, a pre-existing flaw of the race-end check.
Still emulated from the C flow: 8C77 and 8FC3 (secret credits).
