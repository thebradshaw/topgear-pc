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
| 5 | $2111 BG3HOFS/VOFS, indirect | $00:BEFA (ROM), bank $08 | per-line BG3 scroll |
| 6 | $2126-$2129 WH0-WH3 | $00:0E54 | window edges per line |

So the road surface, verges and stripes are drawn with per-line window edges and palette changes rather than with tiles. The RAM routine at $7E:2000 does the perspective maths (using the PPU multiplier at $211B/$211C and the CPU divider at $4204-$4206) and writes these tables: it writes heavily to $7E:0600-$0EFF and $7E:1800-$1AFF and to its own operands.

`tg_headless --trace-frame N --trace-count K --trace-out FILE` writes every instruction and bus write for those frames.
