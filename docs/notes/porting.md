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

A call isn't compared when an interrupt lands inside the original, or when it runs on
into the next frame's vblank; the report counts these as skipped. The stack below SP is
free space, so leftover bytes from the original's pushes there aren't compared. Writes that HDMA makes while the
original runs (the race's per-line colours) are replayed after the C version, since they
come from the clock and not from the routine. Writes to the multiply/divide registers
aren't compared: C does that arithmetic directly.

## Adding one

1. Read the whole routine in the disassembly, following every branch: some routines
   carry on past an early RTS (RaceBuildWindowTable did, and the checker caught it).
   `tools/check_disasm.py --out DIR` regenerates the disassembly locally; never commit
   it. Name it and its RAM in `disasm/symbols.txt` and
   `disasm/ram.txt`.
2. Write it in `src/port/`, keeping the original's behaviour exactly, including quirks;
   comment anything that looks like a bug rather than fixing it. Add RAM names to
   `src/port/variables.h`.
3. Register it with its entry point, JSR or JSL, the M/X widths it's entered with and
   leaves with, and which registers no caller reads afterwards (`TG_DEAD_*`). Dead
   registers aren't compared, and in verify mode they're filled with junk after every
   call, so the recording hashes fail if a caller did read one. That saves reproducing
   the exact leftovers of long routines.
4. Run the tests: `ctest --test-dir build -L long` runs every recording. The report shows
   how many calls matched; check that the recordings actually reach the routine's
   branches (a routine that only ever sees zeros proves little).

## Translated so far

| Routine | Address | Checked calls (all recordings) |
|---|---|---|
| RaceRevEngines | $00:88A1 | 5,414 per full test run. Chris's session revs player 1 on the start line; no recording revs player 2 yet |
| RaceUpdateSegmentValues | $00:8910 | 30,129 |
| RaceBuildWindowTable | $00:9381 | 32,674; only Chris's session reaches the 1-player window shapes |
| RaceDrawHud | $00:CE61 | 37,099 |
| RaceUpdateMessages | $00:DA75 | 37,120; no recording shows the wrap at the track start or the dropped low-fuel flag |
