# The race loop in C

Everything translated before the race loop is a routine that runs start to finish inside
one call from emulated code, and is checked call by call (lockstep.c). The race loop
doesn't fit that shape: it waits for interrupts, and its pace comes from the CPU time the
original takes.

## Waiting: the game code as a fiber

Routines marked `TG_WAITS` (registry.c) run on their own stack, a fiber (src/fiber.c:
Windows fibers, `ucontext` elsewhere), in native mode only. Where the original spins until
an interrupt handler changes something, the C version calls `tg_port_wait()`, which
switches back to the emulator with the CPU idling (WAI) where it is. Each time an interrupt
returns there, the fiber carries on and checks again. Original code the fiber calls
(`tg_call_*`) runs on the machine itself, so it can take frames: the menus (GameInit), the
sound driver upload, and the start-up delay loops all run that way.

The race program, from the end of the menus to the end of a race and back into them, is
`RaceProgram` ($00:812A, program.c). With it, RaceLoadAll, RaceSyncWait and RacePauseCheck
are C too (waits.c).

## Pace

Chris chose (2026-10-16) to drop the original's slowdown and loading pauses. The original
takes about two frames of CPU time per race update, so it normally runs one update every
three frames, and four or five when a race is busy. C takes no CPU time, so it would run
an update every two frames; `tg_port_race_frames` (default 3, `--race-frames` in
tg_headless) holds it to the original's normal pace. Loading is instant.

## Checking

Verify mode can't follow a routine across frames, so `TG_WAITS` routines run emulated
there, and the recording tests check everything they call as before.

The C loop itself is checked by tools/shift_compare.py. It runs a recording in native mode
twice: once with the waiting routines emulated (`TG_EMULATE_WAITING=1`), once in C with
`--race-frames 2`, the pace the emulated loop has when the routines it calls take no time.
The only difference should be that the C run gets each race going a few frames sooner. The
tool compares every frame's picture, and where the C run gets ahead it moves the
recording's later input earlier by as much and runs again, until every frame matches.

## The raster interrupts

The race's IRQ handler ($00:EC5B) calls a stage through $0A each time, and each stage sets
the PPU up for the next band of the screen. They're C now (src/port/irq.c): the vblank
stage ($00:B6D1) with the top-of-frame work it calls (RaceIrqTop, $00:B75A), and the
stages for each view's bands. The IRQ comes at dot 124 or later, past the point LakeSnes
draws the line (dot 128) by the time a stage runs, so writing at once has the same effect
as the original's writes a little later.

The bottom view's stages end in $00:BE78, which waits for horizontal blank, turns the
screen off for a palette and OAM DMA mid-screen, and turns it back on. Its writes have to
land on particular dots, so it still runs emulated, called from the C stages.

RaceIrqTop reads the sound driver's answer on APU port 1 some way into the routine. C reads
it in no time, when the driver may not have answered yet, so a sound command can go a frame
later. To check the logic, verify mode hands C the values the original read from the APU
ports (`tg_apu_read8`).

## Still emulated

- The mid-screen DMA, $00:BE78 (above).
- RaceBuildGapTable ($00:A8E1), which reads junk past its table, including APU and PPU
  ports.
- The menus (bank $0F).
