# Notes for contributors and coding agents

This project runs the 1992 SNES game Top Gear from the player's own ROM. The repository may
hold only code, tools, documentation and original art. Everything from the game stays in the
player's ROM and is read from it at run time. These rules keep it that way; please follow them
in every change, and stop and ask the maintainer when something is unclear.

## Never commit anything from the ROM

- No ROM files (`*.sfc`, `*.smc`; `.gitignore` blocks them), and no part of one.
- No graphics, palettes, tiles, fonts, music, sound samples or SPC/BRR data from the game, in
  any form: image files, C arrays, base64, hex dumps.
- No data tables copied from the ROM (car stats, track layouts, text, credits, passwords,
  lookup tables). Code reads them from the ROM at run time (`tg_rom()`, `tg_read8()`).
- No disassembly output: `disasm/symbols.txt` and `disasm/ram.txt` hold names and comments
  only, never ROM bytes or `.byte` lines.
- No game text beyond the few words needed to identify a screen in a comment or note.
- No pictures of the game: screenshots, frame dumps or recordings of what the game shows.
- No RAM, VRAM or PPU dumps, save states or traces: they contain the game's code and data.

## Tools that read the ROM write outside the repository

`tools/disasm.py`, `tools/check_disasm.py`, `tg_headless --screenshot / --shots-dir /
--dump-wram / --dump-ppu / --trace-out` and similar produce files derived from the ROM. Write
them to a `build*/` directory (ignored by git) or outside the repository, never under `src/`,
`docs/`, `assets/`, `tests/` or `disasm/`. Input recordings (`tests/inputs/*.txt`, button
presses only) and test hashes are fine.

## Art, fonts and other binary files

- Art in the repository must be original and drawn by a committed script, as
  `tools/gen_launcher_art.py` draws the launcher's logo, Start word and icon. Commit the
  script with its output.
- Fonts must be under the SIL Open Font License or a similar licence, live in `third_party/`
  with their licence file and a `SOURCE.txt` saying where they came from.
- Never use the game's logo, box art, title screen, characters or graphics, and never draw
  something meant to look like them (for example its italic block lettering with stars and
  speed stripes).
- Before committing any binary file (image, audio, font, archive), look at it and know where
  it came from. `git diff --cached --stat` lists what a commit adds.
- README screenshots may show only what contains no ROM pixels, such as the launcher rendered
  offscreen: `SDL_VIDEO_DRIVER=offscreen TG_LAUNCHER_SHOT=launcher.bmp redline-atlas-launcher`.

## Names and trademarks

- The project is called Redline Atlas. The name is set in one place for the code
  (`TG_APP_NAME` and `TG_EXE_NAME` in `CMakeLists.txt`), plus `NAME` in
  `tools/gen_launcher_art.py`, `APP`/`TITLE` in `.github/workflows/release.yml` and the
  README.
- Use "Top Gear" only to name the ROM the program needs ("Top Gear (USA)") or to say what the
  project is compatible with, and keep the README's Legal section with it. Never use the
  game's name, logo or art for this project's own branding, window titles, icons or file
  names. (The settings folder keeps its old name, `topgear-pc/Top Gear`, so existing
  settings carry over.)
- Do not suggest that the project is official, licensed or endorsed by any rights holder.

## Third-party code

- Keep each third-party component's licence file next to it (`third_party/*/LICENSE.txt` or
  `OFL.txt`) and record its origin in `SOURCE.txt`; mark local changes in its source.
- Release downloads include the licences of everything built into the programs
  (`licenses/` in the zip and tarball; see `.github/workflows/release.yml`).
- Do not copy code from projects without a licence that allows it (for example other Top
  Gear recompilations without a licence file).
- The project's own licence is the maintainer's decision.

## Checks before a pull request

- `cmake -S . -B build -DTG_TEST_ROM=<your ROM>`, build, and
  `ctest --test-dir build -LE long --output-on-failure`: all pass.
- `git status` shows no ROM, dump, screenshot or disassembly output staged.
