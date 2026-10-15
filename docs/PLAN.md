# Top Gear (SNES) Windows port: plan

Goal for phase one: a Windows build that plays exactly like the original cartridge. Widescreen, 120 fps and other extras come after, but the plan is shaped so they are possible later.

## 1. What we're starting from

**Your ROM** (`/mnt/project-files/Top Gear (USA).sfc`), checked:

- 524,288 bytes, LoROM, SlowROM, no enhancement chip, no battery save (progress is password based).
- SHA-256 `ca9889f17f184b3d99a2eaaa82af73e366f03ed00313fdd369e5e023b208e788`, internal checksum valid (`$C15C`). This is the clean USA release.
- Reset vector `$00:8000`. Native NMI and IRQ vectors point into work RAM (`$0000`, `$0004`), so the game installs its own interrupt trampolines at runtime. Any translator has to resolve those jumps; it's a known wrinkle, not a blocker.
- No coprocessor means the only hardware we must reproduce is the standard CPU, PPU, DMA/HDMA and the SPC700 audio unit. That makes Top Gear one of the easier SNES games to port.

**Existing work found:**

| Project | What it is | Can we use it? |
|---|---|---|
| [Junior-Jones/Top-Gear-SNES-Static-Recomp](https://github.com/Junior-Jones/Top-Gear-SNES-Static-Recomp) (and a fork by PabloVSouza) | A finished-looking static recompilation of exactly this ROM (same SHA-256) for Windows 10/11. Fullscreen, integer scaling, SDL gamepads, NTSC frame lock. No widescreen or high frame rate. 3 stars, 16 commits, Oct 2026. | **No license file**, so legally all rights reserved. We can try it as a behaviour reference, but we can't copy its code. Its output is machine-generated C (8,685 contexts in 29 shards), which is hard to extend for widescreen/120 fps. |
| [snesrecomp](https://github.com/mstan/snesrecomp) | General SNES static recompiler (65816 to C) plus a hardware runtime. Used for Super Mario World, Mega Man X (with experimental true widescreen), Zelda 3. Has lockstep comparison against an interpreter and against bsnes/Snes9x. | Usable for a non-commercial project (PolyForm Noncommercial 1.0.0). Alpha quality. Fastest route to a first running .exe. |
| [zelda3](https://github.com/snesrev/zelda3) / smw by snesrev | Hand-written, readable C reimplementation of the game logic, run in lockstep against an embedded emulator that compares RAM every frame. Widescreen and other enhancements were added on top. | The proven method for a faithful port that can later gain widescreen. We'd follow the method, not the code. |
| [DiztinGUIsh](https://github.com/IsoFrieze/DiztinGUIsh) | SNES disassembler that uses emulator trace logs to separate code from data and track 65816 register widths. | Yes, for building the disassembly. |
| [TCRF page](https://tcrf.net/Top_Gear) | Notes on unused content. | Background only. I found no public Top Gear disassembly or RAM map. |

## 2. Recommended approach: readable C, verified frame by frame

Translate the game into readable, named C (the zelda3 method), with an emulator running the original ROM alongside it as a referee.

Why this over just recompiling:

- **Faithfulness is testable.** Every frame, the port's work RAM, video RAM and hardware register writes are compared against the original running in an embedded emulator. Any mismatch stops with the exact frame and address.
- **The game always runs.** Functions not yet translated fall back to the emulated original, so we have a playable Windows build from week one and replace code piece by piece.
- **Widescreen and 120 fps need understanding, not just translation.** Widescreen means drawing more road, scenery and cars past the original 256-pixel edge; 120 fps means separating simulation (kept at 60 Hz for identical physics) from rendering and interpolating car and road positions. Both are far easier with named variables like `player1_speed` than with machine-generated code.

snesrecomp can be used as an accelerator in step 3 to produce a first-draft C translation that we then clean up, if its licence suits you.

## 3. Phases

**Phase 0. Repository and harness**
- Private GitHub repo, CMake + SDL3, MSVC and MinGW builds, CI on Windows.
- ROM is never committed. The build asks for your ROM, checks the SHA-256 above, and extracts assets at first run.
- Embed a small accurate SNES core (e.g. LakeSnes, MIT) as the reference and fallback.
- Done when: a Windows .exe boots the ROM through the fallback and plays.

**Phase 1. Disassembly**
- Record emulator trace logs across every mode (championship, 1P/2P, all 32 tracks, passwords, options) and feed them to DiztinGUIsh to mark code vs data and register widths.
- Export assembly that rebuilds a byte-identical ROM; CI checks the hash.
- Build a RAM map and label functions (car physics, road renderer, AI, HUD, menus, sound driver).
- Done when: the rebuilt ROM matches byte for byte and the main loop, NMI handler and road renderer are labelled.

**Phase 2. Input recordings as the test suite**
- Record input movies covering every track, both players, pit stops, nitro, crashes, passwords, every menu, attract mode.
- Replayed on both the original and the port, with frame-by-frame RAM comparison and final screenshot/audio hashes.
- Done when: the suite runs in CI and passes on the pure-fallback build.

**Phase 3. Translate the game to C**
- Order: frame loop and NMI, then menus, HUD, race logic, car physics, AI, road renderer, sound driver commands.
- Each function is ported, then must pass the full recording suite in lockstep before it's merged.
- The SPC700 sound program keeps running in an accurate audio emulator (as zelda3 does); only the CPU side is translated. That keeps music and effects exact.
- Done when: the whole game runs with no fallback calls and every recording matches.

**Phase 4. Windows polish (still original behaviour)**
- Fullscreen and windowed, integer and aspect-correct scaling, 8:7 or 4:3 pixel aspect.
- Keyboard and XInput/SDL gamepads, remapping, two-player on one PC (Top Gear is always split-screen).
- Exact NTSC timing (60.0988 Hz) with vsync handling, low audio latency.
- Done when: a full championship plays through on Windows with no visible or audible difference.

**Phase 5. Enhancements (after fidelity is locked)**
- Widescreen: wider viewports for each player's half of the split screen; extend road and backgrounds drawn by line-scroll.
- 120 fps (and arbitrary rates): physics at 60 Hz, interpolated rendering.
- Each enhancement is an option that defaults off, so the original-accuracy mode keeps passing the test suite.

## 4. What I need from you

1. OK to create a private GitHub repo (name suggestion: `topgear-pc`) and push to it.
2. Try the existing recomp linked above if you'd like a quick feel for what "running on Windows" looks like; it's a useful reference but we can't build on its code.
