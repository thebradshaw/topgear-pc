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

Only races run code from RAM: one routine at $7E:2000-$7E:34AD (about 2,170 instructions, a single RTL at the end), called with `JSL $7E2000` once per player view. It writes into its own instructions during the race (self-modifying immediates, e.g. at $7E:2805, $7E:280A, $7E:2828, $7E:2838), so the C port will turn those operands into variables. It is decompressed from ROM with `DecompressBegin`; callers are at $00:802C (boot, from $0D:E000), $00:C11E, $00:C218 and $00:EA56. List it with `tools/disasm_ram.py` from a WRAM snapshot taken during a race. This is the prime candidate for the road/scene renderer.

## Colour gradient

`SetupColorGradientHdma` ($0F:80BD) points HDMA channels 3 and 4 at CGRAM ($2121/$2122) with tables at $0D:C7BC and $0D:C7F3: the per-line colour changes behind the road.
