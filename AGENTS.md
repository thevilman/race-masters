# Notes for contributors and coding agents

Race Masters runs the 1992 SNES game Top Gear from the player's own ROM and draws its races in
3D. The repository may hold only code, tools, documentation and original art. Everything from
the game stays in the player's ROM and is read from it at run time. These rules keep it that
way; follow them in every change, and stop and ask the maintainer when something is unclear.

## Never commit anything from the ROM

- No ROM files (`*.sfc`, `*.smc`; `.gitignore` blocks them), and no part of one.
- No graphics, palettes, tiles, logos, fonts, music, sound samples or SPC/BRR data from the
  game, in any form: image files, C arrays, base64, hex dumps, traced outlines.
- No data tables copied from the ROM (car stats, track layouts, text, credits, passwords,
  taunts, lookup tables). Code reads them from the ROM at run time (`tg_rom()`, `rom16()`,
  `tg_read8()`), as `course_name`, `taunt_ink` and the title's credit lines do.
- No game text beyond the few words needed to identify a screen or a routine in a comment or
  note. Describe screens ("the failed-to-qualify screen"); don't transcribe them.
- No disassembly output: `disasm/symbols.txt` and `disasm/ram.txt` hold names and comments
  only. `disasm/coverage.tgcov` keeps no opcode bytes: save it with
  `tools/covmerge.py --strip` (the `coverage_public` test checks it).
- No pictures of the game: screenshots, frame dumps or recordings of what the game shows.
- No RAM, VRAM or PPU dumps, save states or traces: they contain the game's code and data.

## Tools that read the ROM write outside the repository

`tools/disasm.py`, `tools/check_disasm.py`, `tools/audio/`, `tools/qa/` and `tg_headless`'s
`--screenshot`, `--shots-dir`, `--shots3d`, `--dump-wram`, `--dump-ppu`, `--trace-out`,
`--export-art` produce files derived from the ROM. Write them to a `build*/` directory
(ignored by git; the QA tools default to `build/qa`) or outside the repository, never under
`src/`, `docs/`, `assets/`, `tests/` or `disasm/`. Input recordings (`tests/inputs/*.txt`,
button presses only) and test hashes are fine.

## Art, fonts and other binary files

- Art in the repository must be original and drawn by a committed script, as
  `tools/gen_launcher_art.py` draws the launcher's logo, Start word and icon. Commit the
  script with its output.
- Fonts must be under the SIL Open Font License or a similar licence, live in `third_party/`
  with their licence file and a `SOURCE.txt` saying where they came from.
- Never use the game's logo, box art, title screen, characters or graphics in this
  project's own art, and never draw something meant to look like them (for example heavy
  italic block lettering with stars and speed stripes).
- The cars' `.tgcar` files, the player's `art3d/` pictures and any remade music are not
  committed and not bundled with releases unless the maintainer decides so.
- Before committing any binary file (image, audio, font, archive), look at it and know where
  it came from. `git diff --cached --stat` lists what a commit adds.
- README screenshots may show only what contains no ROM pixels: the launcher rendered
  offscreen, or the modern 3D look proven by `tools/qa/readme_shots.py`.

## Names and trademarks

- The project is called Race Masters. The name is set in one place for the code
  (`TG_APP_NAME` and `TG_EXE_NAME` in `CMakeLists.txt`, passed on by `src/brand.h.in`); the
  launcher's art is drawn from that line by `tools/gen_launcher_art.py`; `APP`/`TITLE` in
  `.github/workflows/release.yml`, `BRAND` in `tools/cars/paint_cars.py` and the README follow
  it.
- Use "Top Gear" only to name the ROM the program needs ("Top Gear (USA)") or to say what the
  project works with. Never use the game's name, logo or art for this project's own
  branding, window titles, icons or file names, and never suggest the project is official,
  licensed or endorsed by any rights holder. Keep `LEGAL.md` with the README.

## Personal data

- No personal names, e-mail addresses, user names, home or user-profile paths, machine
  names, private repository URLs, session links or scratch paths in code, docs, tools,
  defaults or commit messages. Use environment variables, relative paths and placeholders
  (`$TG_DIR` for the player's game folder, `$SCRATCH` for a scratch folder).

## Third-party code

- Keep each third-party component's licence file next to it (`third_party/*/LICENSE.txt` or
  `OFL.txt`) and record its origin in `SOURCE.txt`; mark local changes in its source.
- Release downloads include the licences of everything built into the programs
  (`tools/release_licenses.sh`, `licenses/` in the zip and tarball).
- Do not copy code from projects without a licence that allows it.

## The game stays the game

- The 3D mod changes presentation only: no change to the game's RAM, timing or inputs. The
  regressions must pass after every change:
  `ctest --test-dir build -R "^(core|coverage_public|race_1p|race_2p|rear_push|curve_push|attract_demo)$"`
  (configure with `-DTG_TEST_ROM=<your ROM>`).
- `git status` shows no ROM, dump, screenshot or disassembly output staged.
