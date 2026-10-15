# How the original game is put together

Findings from the disassembly. Addresses are SNES CPU addresses; see `disasm/symbols.txt` and `disasm/ram.txt` for the names used here.

## Boot

- `Reset` ($00:8000) sets native mode, stack $01FE, direct page 0, forced blank.
- It decompresses a code blob from $0D:E000 into work RAM at $7E:2000 with `DecompressBegin`/`DecompressNext` ($00:EA8E), an LZ-style byte-stream decompressor with a 2 KiB window.
- It uploads the sound driver (`UploadSoundDriver`, $07:8000) and then checks $1F91 = $1956 and $1FAE = $5BED to tell a warm reset from power-on. $1F00-$1FFF (names, records, settings) survives a soft reset.
- `GameInit` ($0F:806D) writes `JML $0F:8000` at $0000 and `RTI` at $0004. The native NMI and IRQ vectors point at those two RAM addresses, so `NmiHandler` is $0F:8000 and IRQs are unused.

## Every frame (NMI)

`NmiHandler` starts any DMA queued in `NmiDmaChannels` ($0424), copies `InidispShadow` ($42) to INIDISP, bumps `FrameCounter8` ($08) and `FrameCounter16` ($1FFC), waits for auto-joypad read, and stores held and newly pressed buttons for both controllers ($0A-$10).

## Code that runs from RAM

About 2,150 instruction addresses execute in $7E:2000-$7E:34AD. That region is an overlay: the game decompresses different code into it for the menus and for racing (the menu and race versions differ almost entirely), and during a race it rewrites some operand bytes inside it (self-modifying code). The C port has to treat those operands as variables. Other `DecompressBegin` callers are at $00:C11E, $00:C218 and $00:EA56.

## Colour gradient

`SetupColorGradientHdma` ($0F:80BD) points HDMA channels 3 and 4 at CGRAM ($2121/$2122) with tables at $0D:C7BC and $0D:C7F3: the per-line colour changes behind the road.
