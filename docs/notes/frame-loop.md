# Moving the frame loop to C (design)

Status: proposal. Everything translated so far is a routine that runs start to finish
inside one call from emulated code. What's left in races doesn't fit that shape:

- Routines that wait for an interrupt: RaceSyncWait (`$00:EC0E`), the palette upload in
  RaceLoadPalettes (`$00:CCDB`), the pause loop (`$00:8878`), and through them the main
  race loop and RaceLoadAll (`$00:BF14`).
- The raster IRQ stages reached through `JSR ($000A)`, whose PPU writes have to land on
  the right scanline.

## Waiting: the game code as a coroutine

The C game code gets its own stack (a fiber on Windows, `ucontext` elsewhere, behind a
small `tg_fiber` API). A translated routine that waits calls `tg_wait_until(cond)`, which
switches back to the emulator. The emulator keeps running frames, interrupts and the
frontend as usual; on each NMI or IRQ it checks the condition and switches back into the
fiber when it holds. Nothing else changes for routines that don't wait.

In the end state the whole main loop runs in the fiber and the emulated CPU only idles,
so the C code paces itself on the same interrupts the original does.

## Checking routines that wait

Per-call lockstep can't compare these: they span frames, and the isolated check (with
interrupts held off) would never finish. They are checked per frame instead, as zelda3
does: at the end of each frame, work RAM, VRAM, CGRAM and OAM from the C run are
compared with the original run from the same state at the start of the frame. The
per-call checks stay for everything else.

## IRQ stages

Each stage's PPU writes must take effect on the same line as the original's. LakeSnes
draws a line at dot 128, so for each stage we record (from the original, once) whether
its writes land before or after that point, and the C version runs the stage at the
matching moment: at IRQ time, or deferred until just after the line is drawn.
