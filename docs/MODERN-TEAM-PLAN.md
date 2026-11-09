# Modern look: the team plan

The 3D mod (`src/mod3d`, `src/gl3d.c`, the menu redraws in `main.c`) is to be finished
and polished by a team of roles, each working in turn on what the role before handed on.
The producer (the coordinating session) writes each role's brief, reviews its result,
installs the build and keeps this file up to date.

## Rules every role keeps

- **The game stays the game.** Only the drawing changes: no change to what the game does
  (its RAM, its timing, its inputs). Both regressions must pass after every change:
  `build/tg_headless --rom ROM --port verify --frames 11000 --input tests/inputs/race_1p.txt
  --expect-wram 08f94c7b…2a8a --expect-frame 94766e6c…37c9` and the same with
  `race_2p.txt`, 14000 frames, `af867184…8bc6bf5f8592` / `ca027930…4ba7`.
- **Nothing from the ROM in the repository.** Pictures taken from the game (exports,
  upscales) stay in the player's `art3d` folder.
- **The game is not started on the player's screen.** Check with `tg_headless` renders
  (`--shots3d WxH`: `_3dx.ppm` is the modern look, `_sel.ppm` a redrawn menu; a frame with
  neither shows the original picture).
- **Research first.** Before a task, look up how the profession does it today (web search
  for current techniques, references, tools) and say what was used.
- **Small, reviewed steps.** Each change built (`cmake --build build --target tg_headless`),
  looked at in renders, regressions run, committed on `mod/3d-renderer` with a message
  saying what and why.

## Paths

`$TG_DIR` is the player's game folder (the ROM, `art3d/`, `recordings/`; the QA tools take it
from `TG_QA_TOP`, default `~/Top Gear`); `$SCRATCH` a scratch folder outside the repository.

- ROM: `$TG_DIR/Top Gear (USA).sfc`
- Player's art folder: `$TG_DIR/art3d/`
  (cars in `cars/`, earlier cars in `cars-previous/`)
- Player's recordings: `$TG_DIR/recordings/`
  (`session-*.txt`, replay with `--port native`)
- Recorded inputs: `tests/inputs/` (`race_1p`, `race_2p`, `country_*` with
  `--port verify --native-from 6000`, `chris_session_*`)
- Concept art and models: the player's concept folder, outside the repository
  (the current cars' design, renders and gameplay concept)
- Blender: `$SCRATCH/blender-4.2.3-linux-x64/blender`; car exporter
  `tools/export_cars_specialist_blender.py`
- Headless only: `--set-course FRAME:N` sets the course number (track and map follow; the
  skyline graphics do not), to look at courses no recording reaches.

## The roles, in order

1. **Test engineer (QA)** builds `tools/qa/`: one command that replays every recording,
   checks the regressions, finds frames where the original graphics show through, cars
   that change model or colour between frames, skyline or scenery that pops, frames too
   slow for 60 a second, and makes contact sheets of every screen it passes. Output: the
   bug list (`docs/qa/BUGS.md`) the other roles work from, and the harness they rerun.
2. **Retro gaming expert** audits fidelity: every screen and state of the original
   (power-on, intro, attract demo, every menu, passwords, results, standings, pit stops,
   2-player split, night, end of championship, game over) against the modern look: is it
   redrawn, is anything the player needs to see lost, do cars and road line up with the
   original's positions. Output: a coverage matrix and must-fix list.
3. **Art director / game designer** writes the style guide (palette, type, motion,
   lighting per country, the target references) and reviews every screen against it.
   Output: `docs/STYLE.md` and a prioritised list of what to change.
4. **3D modeller / technical artist** brings the models and materials to the guide: cars
   (all five, levels of detail, wheels, lamps, liveries), roadside objects, start gate,
   pits, landmarks, skylines, trees; materials and lighting; triangle budgets.
5. **Engine / graphics programmer** fixes what the renderer does: popping (levels of
   detail, model or light car, things learned from the screen), fades and transitions,
   frame pacing and speed, night, the 2-player split.
6. **UI / UX designer** polishes the menus and the HUD: layout, readability, motion and
   transitions, the hints for keys and pads.
7. **Test engineer again**: the whole harness, the regressions, a sign-off report with the
   contact sheets.

Each role keeps `docs/TRACKER.md` up to date after every step: what it implemented, what it
proved (and with which check), what it found missing. A role's work is done only when its
items there are `tested`. Each role ends by updating the status below.

## Status

- Producer: plan written; known open items from play-testing: car colour/model changes in
  a bunched pack (fixed: paint shared, models for the 12 nearest), the title fading in as
  the original picture (fixed), skyline popping in on a first visit (fades in now), scenery
  popping in on a first visit (open).
- Art director + 3D modeller (2026-11-03): style guide `docs/STYLE.md`; stages with soul (a
  kit per city in `src/mod3d/view3d_stages.inc`, START gantry, national flags, the original's
  cyan sky, a landmark on every course's skyline); R-09, R-10, R-11 fixed, R-15 decided. Open:
  authored far skylines per course (R-05), 3D hero landmarks near the road, the QA popping
  check counts motion of large near objects (a motion test is suggested to QA).
- Engine (2026-11-03, pass 2): the start-of-race slowdown measured at the player's
  3440x1440 and fixed (car levels of detail by screen size with clustered far meshes,
  the HUD upscale only when drawn, partial texture uploads, no file writes in the race's
  first frame, buffer orphaning, textures warmed on the loading card); a frame-time log
  in the game (Shift+F2) and `tools/qa/perf_report.py`; every race message in the modern
  HUD (taunt bubbles, name banners), the pit sign modelled, props near the camera faded,
  skies checked; the QA popping check is motion-compensated (72 -> 12). Open: a
  frame-time log from the player's PC; KEMCO and other non-solid sponsor pictures still
  pixel pictures; warm skies.
- Engine / audio (2026-11-03): the player's modern music in the game (Music option: ORIGINAL,
  FAITHFUL default, MODERN = each city's D, CITY BAND, LOCAL BAND, POSTCARD, SYNTHWAVE,
  ORCHESTRAL; docs/AUDIO.md), the chip's music voices masked, effects kept; the modern car
  select's Left / Right go through the cars in their line (src/select_input.c). Open: modern
  sound effects.
- Engine, part D (2026-11-03): classic title with the car, gradient sky and neon grid; cars
  point along the road (the track frame's heading now follows the drawn road: it was 12 deg
  off); a level chase camera that frames the car like the original off the road; no ghosts
  (short eased near fade, ground bands not hung in the air), what hides the player's car
  fades, roadside objects at the game's own distance; launcher and game agree on
  full_screen / wide_screen; no night flash in day races (QA-026); no freeze leaving the
  title (QA-025); the light car levels whole, with a switch margin; the skyline's colour
  switch eased (QA-009). QA-024 is the original's own fade. Open: QA-007 (the landmark
  needs a place in the world), the remaining 1-2 block skyline pops.
- Art director (2026-11-03, pass 2): QA-007 fixed - every city's landmark has a fixed direction
  in the world (the start straight's view, to the pits' side), props kept out of its sightline,
  the hillside sign in block letters (ROLLYWOOD); all 32 checked on the grid. QA-009: skyline windows,
  column joins, learned-scenery picks, props' draw range and near fades smoothed; headless now
  draws the modern look first (its fades never moved in QA shots). Open: the popping check
  flags layered motion (thin trunks, a tree before a sign) as pops - QA to refine.
- UI / UX (2026-11-03, country select): the country screen redrawn (two columns of four
  cards, flags drawn in code to their construction sheets, the four cities from the ROM, the
  game's own cursor with an eased frame; Left / Right go across); "Country select" option
  PASSWORD / FREE (default FREE): a locked country's password is read from the ROM and typed
  into the game's own password screen as presses (src/country_input.c), so the game is
  unchanged and recordings replay as received. Open: the end-of-country password screen is
  still not reached by any recording.
- Engine / platform (2026-11-03, two-player-black): the player's 2-player race was black on his
  PC (QA-034). The game's own frontend now replays offscreen with its picture saved (`--window`,
  `--shots`, `--frames`, `--fake-hz`; SDL's offscreen driver, no window); it draws the race on
  every Mesa driver and setting tried, so the cause is by elimination: only 2-view pictures had
  SDL's renderer draw between and over gl3d's views. Now every picture is the renderer's clear,
  gl3d's views in one go, the present; recordings note the GL driver, the race's views and GL
  errors; recordings reach the disk every frame and note a crash. Open: confirmation from the
  player's next 2-player session.
- 3D vehicle artist + graphics (2026-11-03, car-models): the cars' models and textures (owner: "You
  were supposed to improve the models, make new textures too."): the five rebuilt in Blender from
  the specialist's design with an open greenhouse and interior, detailed wheels and fixed calipers,
  clean levels of detail; a baked and painted 1024 atlas per car (liveries, invented sponsors,
  tyre lettering, discs, lamps, carbon, interior) in tgcar version 3, mipmapped in GL and the
  software renderer, paint left to the game through a tint mask; the rivals' paint vivid and
  varied (owner: "more vivid colors to the computer cars"): a 12-paint palette by the game's
  colour and the grid place, metallic flake, decals in contrasting ink. Pipeline `tools/cars/`,
  docs/CARS.md. Open: a look at the game's GL window on the player's PC; no normal map.
- Environment art / graphics (2026-11-03, landmarks): the player's "the Cristo Redentor looks
  horrible" fixed (QA-037): Rio's Christ the Redeemer on Corcovado, with Sugarloaf, modelled in
  3D and baked for the one side a far landmark is seen from (`tools/make_landmarks.py` ->
  `landmark_meshes.h`, `lm_mesh`: the scene's light, haze, no new batch; floodlit at night); the
  Eiffel Tower, Tokyo Tower and the Leaning Tower the same way; every landmark rated from the
  grid (`tools/qa/landmark_sheet.py`). Open: 12 landmarks still crude (TRACKER), the night ones
  invisible.
- Brand / UI artist (2026-11-03, launcher-logo): copyright audit - the launcher's built-in logo (the
  original's title logo) replaced by our own, drawn by `tools/gen_launcher_art.py` from the OFL fonts
  (the edition's name in chrome, a 3D tag; since replaced by the Race Masters logo, see TRACKER "Brand and copyright"), with the START word (it was in the original logo's
  style and colours) and the launcher icon (a crop of the box art); the README's screenshots are the
  modern look with nothing from the game's pictures, proven per shot by `tools/qa/readme_shots.py`.
  Open (TRACKER, "Brand and copyright"): the in-game MODERN title imitates the original logo's look;
  the rivals' taunts are the game's text.
- Graphics / UI (2026-11-03, mode-transition): F7 between the original picture and 3D as time
  passing (owner: "a 0.5 loading screen transitions when we press 2d or 3d mode, make it look like
  something as time passing"): a clock's hand of light sweeps the 3D view in (clockwise, the years
  1992 -> 2026 on a clock face) or rewinds to the original (anticlockwise, back to 1992), 0.5 s,
  turning back from where it is on a new press; presentation only, the GL picture's order as
  QA-034 left it (`src/mod3d/transition.c`, TRACKER). Open: a look on the player's PC; Ctrl+F7
  stays instant.
- Gameplay presentation (2026-11-04, pit-stop): the pit stop (owner: "I NEED TO IMPROVE THE PITSTOP,
  BOTH THE MARKING ON THE SCREEN + THE ANIMATION"): a real pit lane where the game has its pits,
  laid out from the game's pit sign and divider boards (13 courses; `src/mod3d/view3d_pits.inc`):
  the game's line painted, gore, wall and catch fence, lanes, boxes, speed-limit lines and boards,
  PIT IN/OUT, a gantry, eight garages with a canopy and a glass floor (the players' in their colours
  and names, invented teams), all on the ground's slope; the stop animated from the game's state
  (the car into its box when the game's scene stops, a 12-strong crew: jacks, wheel guns, tyres, the
  fuel hose while the game's fuel rises, the lollipop BRAKE/GO; out at the limit and eased back onto
  the game's place), the camera rising over the box; a pit panel in steps with a big fuel gauge and
  the lollipop's word, a pit entry cue; no black frame or darkening round the stop (QA-024, QA-038).
  Open: no recording has a 2-player stop (both boxes are drawn; player 2's stop untested in a
  render); the computer rival's stop (1-player) not seen in a recording.
- Release and compliance (2026-11-05, public-release): the project renamed **Race Masters** (the name
  in one place, `TG_APP_NAME` in CMakeLists.txt; programs `race-masters`, `race-masters-launcher`),
  a new logo, icon, Start word and launcher header (a tour map; the road with kerbs gone), the MODERN
  title and splash restyled, the classic title's "3D" badge removed; the rivals' taunts and the
  title's licence lines drawn from the player's ROM at run time (none typed in the source); the
  coverage file without opcode bytes; game text, personal data and private paths out of the docs;
  `LEGAL.md`, `AGENTS.md`. TRACKER "Brand and copyright".
