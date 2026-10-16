# Translating routines to C

The port replaces the game one routine at a time. Each translated routine lives in
`src/port/`, is listed in `src/port/registry.c`, and is checked against the original on
every call in every recording before it's merged.

## How a translated routine runs

While most of the game is still emulated, translated routines are entered from emulated
code. When the emulated CPU reaches a registered entry point, `src/lockstep.c` takes the
call. The routine gets the CPU registers (`TgRegs`), reads and writes work RAM through
`g_ram`, reads ROM tables through `tg_rom`, and touches hardware registers through
`tg_bus_write8`. It must leave registers and flags as the original would, because emulated
callers may look at them. When the callers are translated too, these register-level
signatures become ordinary C parameters.

`tg_headless --port` picks the mode:

- `off`: everything emulated (the default for the game today).
- `verify`: for each call, the original runs from a saved state, then the C version runs
  from the same state, and the results are compared: registers, work RAM, VRAM, CGRAM,
  OAM and the order of writes to hardware registers. The game carries on from the
  original's result, so the recording hashes still apply. All ROM tests run this way.
- `native`: only the C version runs.

When the original runs on into the next frame's vblank, or an interrupt lands inside it
and the handler touched PPU memory or the comparison fails, the call is checked again on
its own: from the saved state, the original runs with interrupts held off
(`blockInterrupts` in the CPU), then the C version, and those two results are compared.
The game then carries on as if that check hadn't happened. The report counts these as
matched "on their own"; "unchecked" counts calls that couldn't be checked either way.
The stack below SP is free space, so leftover bytes from pushes there aren't compared,
down to the lowest stack pointer the original (or original code the C version called)
reached; variables further down the page are. A routine can name one low-RAM byte the
original fills from open bus (`bus_noise`), where an HDMA transfer landing mid-instruction
leaves a byte C can't know; RaceDrivePlayers' `$001D` is one. Writes that HDMA makes while the original runs (the race's per-line
colours) are replayed after the C version, since they come from the clock and not from
the routine. Writes to the multiply/divide registers aren't compared: C does that
arithmetic directly. A DMA that translated code starts runs at once (`tg_bus_write8` on
`$420B`), since no CPU cycles pass inside C.

A routine that calls `tg_port_decline()` runs emulated for that call; the report counts
these as declined.

## Adding one

1. Read the whole routine in the disassembly, following every branch: some routines
   carry on past an early RTS (RaceBuildWindowTable did, and the checker caught it).
   `tools/check_disasm.py --out DIR` regenerates the disassembly locally; never commit
   it. `tools/routines.py DISASM --name NAME` lists a routine's full extent and flags
   code no recording has run (left as `.byte` lines); `tools/decode.py` decodes those
   bytes from the ROM. Name it and its RAM in `disasm/symbols.txt` and
   `disasm/ram.txt`.
2. Write it in `src/port/`, keeping the original's behaviour exactly, including quirks;
   comment anything that looks like a bug rather than fixing it. Add RAM names to
   `src/port/variables.h`.
3. If the original deliberately hangs on some input, call `tg_port_decline()` before
   changing anything; that call then runs emulated. A subroutine that isn't translated
   yet can be run from C with `tg_call_jsr` (RaceBuildSprites does this for its special
   objects), or `tg_call_jmp` for code the original jumps into and that jumps back.
   `JSR (table,X)` reads its table from the program bank, not the data bank. A
   subroutine that switches to 8-bit index registers clears the high byte of X and Y,
   even if it switches back before returning; RaceDrivePlayers' second car keeps the Y
   that $05:F79F leaves this way.
4. Register it with its entry point, JSR or JSL, the M/X widths it's entered with and
   leaves with, and which registers no caller reads afterwards (`TG_DEAD_*`). Dead
   registers aren't compared, and in verify mode they're filled with junk after every
   call, so the recording hashes fail if a caller did read one. That saves reproducing
   the exact leftovers of long routines. An interrupt handler that arrives while the junk
   is still there gets the real values back, since it saves registers on the stack
   without reading them.
5. Routines that wait for the NMI or an IRQ (a loop on a flag the handler clears) are
   marked `TG_WAITS` and call `tg_port_wait()` where the original spins; they run as a
   fiber in native mode and emulated in verify mode, and tools/shift_compare.py checks
   them (see frame-loop.md). Raster IRQ stages (reached through `JSR ($000A)`) can be C
   when their writes don't depend on the dot they land on; frame-loop.md says which.
   Reads of the APU ports go through `tg_apu_read8`, so verify mode can hand C what the
   original read.
6. Run the tests: `ctest --test-dir build -L long` runs every recording. The report shows
   how many calls matched; check that the recordings actually reach the routine's
   branches (a routine that only ever sees zeros proves little).

## Translated so far

| Routine | Address | Matching calls in a full test run |
|---|---|---|
| RaceRevEngines | $00:88A1 | 11,689; no recording revs player 2 on the start line yet |
| RaceUpdateSegmentValues | $00:8910 | 100,126 |
| RacePitScrollP1, RacePitScrollP2 | $00:8966, $00:8A2C | 2,942 and 4,862 |
| RacePitReloadP1, RacePitReloadP2 | $00:8DA8, $00:8EB4 | 308,926 each |
| RaceUploadBank3Tiles, RaceUploadBank3TilesP2 | $00:8FC0, $00:9046 | 308,926 each |
| RaceUpdatePitStops | $00:90CC | 97,714 |
| RaceBuildWindowTable | $00:9381 | 100,408 |
| RaceMoveCars | $00:A175 | 97,714; no recording bumps a car that is already being pushed |
| RaceSortCars | $00:A1BD | 7 on its own, plus every RaceMoveCars call, which runs into it |
| RaceSetupGrid | $00:A558 | 39 |
| RaceFinishOthers | $00:AB7A | 13 |
| RaceListVisibleCars | $00:ABFC | 97,753 |
| RaceListGridCars | $00:AEF3 | 840, in every recording except the two longest |
| RaceBuildSplitWindowTable | $00:B18C | 96,992 |
| RaceCallOvertakes | $00:B33B | 97,708 |
| Race set-up: RaceLoadCourse, RaceLoadSkyline, RaceLoadRoadside, RaceLoadCars, RacePreloadTileCache, RaceDrawHudFrame, RaceClearHdmaTables, RaceSetupNames | $00:BF44, $00:C182, $00:C230, $00:C5B2, $00:C6BB, $00:C722, $00:CC9B, $00:D913 | 39 each |
| RaceDrawHud | $00:CE61 | 109,403 |
| RaceUploadMessageTiles | $00:D86A | 308,926 |
| RaceUpdateMessages | $00:DA75 | 109,403 |
| RaceUpdateTileCache | $00:E20D | 312,457 |
| DecompressToVram | $00:EA40 | 39 from the original, plus every RaceLoadRoadside call |
| DecompressBegin, DecompressNext | $00:EA8E, $00:EA9B | 132 and 783,245 |
| RaceBuildSprites | $01:8000 | 82,905; its special objects still run the original code |
| RaceAddSprite and its split and player 2 copies | $01:8A87, $01:8C5C, $01:8E33, $01:9008 | 1,052,844, 78,105, 1,122,519 and 91,694 |
| RaceRenderViews | $7E:2000 (copied from $09:899A) | 93,984, checked on their own because the renderer runs past the next vblank |
| RaceDrivePlayers | $05:D2E0 | 97,714, including the computer steering ($05:E9AF / $05:EB58) and the computer-driven second car ($05:ED92); no recording reaches the track edges that hold a car, or runs a car out of fuel at a sign |
| RaceProgram, RaceLoadAll (with RaceLoadPalettes and RaceSetupHdma), RaceSyncWait, RacePauseCheck | $00:812A, $00:BF14, $00:EC0E, $00:8878 | Wait for interrupts: native mode only, checked with tools/shift_compare.py on every recording but chris_session_03 |
| RaceIrqVblank, RaceIrqTop and the raster stages | $00:B6D1, $00:B75A, $00:B3FE-$00:B5E2 | Sound commands checked with the original's APU reads; the mid-screen DMA ($00:BE78) stays emulated |
