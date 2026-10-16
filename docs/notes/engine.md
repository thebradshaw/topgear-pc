# How the original game is put together

Findings from the disassembly. Addresses are SNES CPU addresses; see `disasm/symbols.txt` and `disasm/ram.txt` for the names used here.

## Boot

- `Reset` ($00:8000) sets native mode, stack $01FE, direct page 0, forced blank.
- It decompresses a code blob from $0D:E000 into work RAM at $7E:2000 with `DecompressBegin`/`DecompressNext` ($00:EA8E), an LZ-style byte-stream decompressor with a 2 KiB window.
- It uploads the sound driver (`UploadSoundDriver`, $07:8000) and then checks $1F91 = $1956 and $1FAE = $5BED to tell a warm reset from power-on. $1F00-$1FFF (names, records, settings) survives a soft reset.
- `GameInit` ($0F:806D) writes `JML $0F:8000` at $0000 and `RTI` at $0004. The native NMI and IRQ vectors point at those two RAM addresses, so `NmiHandler` is $0F:8000 and IRQs are unused.

## Every frame (NMI)

`NmiHandler` starts any DMA queued in `NmiDmaChannels` ($0424), copies `InidispShadow` ($42) to INIDISP, bumps `FrameCounter8` ($08) and `FrameCounter16` ($1FFC), waits for auto-joypad read, and stores held and newly pressed buttons for both controllers ($0A-$10).

## Two engines: menus and races

The menus (bank $0F) and the races (bank $00 plus RAM code) are separate engines that reuse the same low RAM for different variables, so names in `disasm/ram.txt` are prefixed Menu/Race where they differ.

- Menus: NMI = `NmiHandler` ($0F:8000), no IRQ.
- Races: `RaceSetup` ($00:81B8) rewrites the trampolines to `JML $00:EC1A` (NMI: joypads only) and `JML $00:EC5B` (raster IRQ that calls the routine whose address is in $000A, so each IRQ chains to the next split of the screen), and enables NMI + H/V IRQ.

## Code that runs from RAM

Only races run code from RAM: one routine at $7E:2000-$7E:34AD (about 2,170 instructions, a single RTL at the end), called with `JSL $7E2000` once per player view. It writes into its own instructions during the race (self-modifying immediates, e.g. at $7E:2805, $7E:280A, $7E:2828, $7E:2838), so the C port will turn those operands into variables. It is decompressed from ROM with `DecompressBegin`; callers are at $00:802C (boot, from $0D:E000), $00:C11E, $00:C218 and $00:EA56. List it with `tools/disasm_ram.py` from a WRAM snapshot taken during a race. It computes the road; see below.

## Colour gradient

`SetupColorGradientHdma` ($0F:80BD) points HDMA channels 3 and 4 at CGRAM ($2121/$2122) with tables at $0D:C7BC and $0D:C7F3: the per-line colour changes behind the road.

## Race timing: 20 updates a second

A race advances once every three video frames (20 Hz). Tracing frames 5000-5060 of `tests/inputs/race_1p.txt` shows a fixed three-frame cycle: the RAM routine at $7E:2000 starts on the first frame and runs into the second, `$01:8000` runs on the second, and the rest of the cycle is spent in `RaceSyncWait` ($00:EC0E), which spins until the raster IRQ chain clears $0C. Raster IRQs keep running every frame. For the high-frame-rate option this means the original only produces 20 new pictures a second, so interpolation will make a large visible difference.

## How the road is drawn

`RaceSetupHdma` ($00:B639) sets up seven HDMA channels:

| Channel | Target | Table | Purpose (inferred) |
|---|---|---|---|
| 0 | $2121 CGADD, indirect | $00:0C12, data bank $08 | which colour to change on each line |
| 1-4 | $2122 CGDATA, indirect | $00:0C12, data banks $09-$0C | road, stripe and verge colours per line |
| 5 | $2111 BG3HOFS/VOFS, indirect | $00:BEFA or $00:BF07 (ROM), bank $08 | per-line BG3 scroll, read from WRAM through the bank $08 mirror |
| 6 | $2126-$2129 WH0-WH3 | $00:0E54 | window edges, a few fixed bands (patched by `F00_9381` and `F00_B18C`) |

The road is BG3 (2bpp, priority on, `BGMODE` $61). Its curves and hills come from per-line scrolling: channel 5 gives each of the 104 road lines in a view its own BG3 horizontal and vertical scroll, read as (HOFS, VOFS) word pairs from WRAM. There are two buffers, $0592/$0732 (top/bottom view) and $08D2/$0A72, and the NMI switches channel 5 between the two ROM line tables so the game can fill one while the other is shown. Channels 0-4 rewrite four CGRAM entries on each line from per-line pointers in $0C12 into ROM banks $08-$0C; with BG3 only having four colours per palette this gives the road surface, stripes and kerbs their own colours line by line, and stepping the pointers through eight phases ($8080-$808E) scrolls the stripes toward the player.

The RAM routine at $7E:2000 (`RaceRenderView`) does the perspective maths, using the PPU multiplier ($211B/$211C) and the CPU divider ($4204-$4206), and writes both tables: the scroll pairs (through self-modified store addresses at $7E:2805/$280A/$2828/$2838, set at entry from $E7/$E9) and the colour pointers at $0C12. Cars and roadside objects are sprites; `RaceBuildSprites` ($01:8000) builds them into four 544-byte OAM buffers ($0F0A, $112A, $134A, $156A), and the raster IRQ for each view DMAs its buffer to OAM and its 96-colour palette ($038E top, $044E bottom) to CGRAM $20.

For widescreen this looks promising for the road: it is a scrolled background, so a wider view mostly needs the BG3 tilemap to cover the extra columns (still to be checked against the tilemap layout), while the scenery on BG1/BG2 and the sprites need the new art and clipping rules described in the plan.

`tg_headless --trace-frame N --trace-count K --trace-out FILE` writes every instruction and bus write for those frames.

## Race main loop and driving

After `RaceSetup`, a race runs three stages, all paced by `RaceSyncWait`:

1. `RaceIntro` ($00:8504): each view's camera position ($46 top, $59 bottom) eases toward its car's track position ($1ED7, $1EDA) in steps of at most 12, rendering a frame each time.
2. `RaceCountdown` ($00:8636): four start-light steps of 60 frames. `RaceRevEngines` lets players rev (RPM in $1E5E/$1E60 rises by $80 per frame while accelerate is held, falls by $20 otherwise).
3. `RaceMainLoop` ($00:86C3): per update, in order: `F00_DA75`, `F00_CE61`, `F00_90CC`, `RaceDrivePlayers` ($05:D2E0), camera follow, `RaceMoveCars` ($00:A175), `F00_ABFC`, then `jsl $7E2000` (render), `F00_9381`, `RaceBuildSprites`, `F00_8910`, `F00_B33B`, `F00_B18C`.

Every per-update step scales by `RaceFramesElapsed` ($1E90), the number of video frames since the previous update, so the game logic is written for a variable step even though in practice it is always 3. For the C port this means the step must be reproduced exactly; for the high-frame-rate option it means the logic is already structured around elapsed time, which helps interpolation.

`RaceDrivePlayers` handles gears (manual buttons $1F7A/$1F7C, or automatic using per-car thresholds at $05:99DB/$05:9A15), speed ($1E5A/$1E5C) and the slope of the track under the car (table $7E:4000, indexed by the high byte of the track position). Per-car tables are selected by $1EE8. Cars live in 18-byte records at $0200, walked by `RaceMoveCars` with DP set to $0200.
