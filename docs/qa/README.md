# QA harness for the modern look

One command, rerun after every change:

```
python3 tools/qa/run_qa.py --quick    # after every small change: ~2 min
python3 tools/qa/run_qa.py            # everything: see "Timings"
```

Useful flags: `--only NAME` / `--skip NAME` (substring of a recording name, repeatable),
`--no-regressions`, `--no-build`, `--keep-frames` (keep the PPM shots), `--jobs N` (at most
3 `tg_headless` at once, for memory), `--update-baselines` (pin the native-mode replay
hashes after a change that is *meant* to change them, which should be never).

It builds `tg_headless`, runs the two regressions and replays the recordings with
`--shots3d 640x360 --art3d <player art>`, analysing each shot while the replay is still
running and deleting it straight after (a full race at every 2nd frame is ~1 GB of PPMs).
Everything it writes goes to the QA folder (default `build/qa/`, ignored by git; override
with `TG_QA_OUT`), never into tracked files:

- `runs/<mode>-<time>/report.md` — every check with frames, per recording stats
- `runs/<mode>-<time>/summary.json` — the same, machine readable
- `runs/<mode>-<time>/sheets/<rec>_NN.png` — contact sheets (8×8 thumbnails)
- `runs/<mode>-<time>/sheets/<rec>_events_NN.png` — before/after pairs of the strongest
  popping / flicker / car-colour events, the flagged blocks boxed in red
- `runs/<mode>-<time>/logs/` — tg_headless stdout/stderr per recording
- `latest-quick`, `latest-full` — links to the last run; `history/` — pass/fail per check
  per run, so the console says what newly fails or newly passes since the last run

Exit code: **1** a regression or replay hash failed (the game changed: never commit),
**2** only image/speed checks failed, **0** all passed.

Other paths: `TG_QA_TOP` (the player's game folder, default `~/Top Gear`), and under it by
default the ROM (`TG_QA_ROM`, `Top Gear (USA).sfc`), the art folder (`TG_QA_ART`, `art3d`) and
the recordings (`TG_QA_RECORDINGS`, `recordings`).

## What is replayed

| Set | Recordings | Port | Shots every |
|---|---|---|---|
| quick | regressions + the first 3600 frames of `session-20261102-173420` (title, menus, start and 20 s of Las Vegas) | native | 2 |
| full | `race_1p`, `race_2p`, `attract` (no input, like the CMake test) | verify | 4 |
| full | `country_*` | verify, `--native-from 6000` | 4 |
| full | `chris_session_01/02/03` | verify (see below) | 30 / 4 / 60 |
| full | every non-empty `recordings/session-*.txt` of the player, length from its `# end frame N` | native | 2 |

Chris's sessions were recorded against the original's timing. Replayed with
`--native-from 2000` they desync (in the first run `chris_session_03` sat in name entry for
~100 000 frames instead of racing), so the harness replays them in verify mode, against
their CMake hashes. The `country_*` recordings do reach their race with `--native-from 6000`.

## The checks

Each prints `PASS`/`FAIL` per recording with the frame numbers.

- **regression/** — `race_1p` 11000 frames and `race_2p` 14000 frames with
  `--port verify`, pinned wram and frame hashes (as in the plan). Fatal.
- **replay-hash/** — the end-of-replay wram hash *of the replay with the modern look on*
  (`--shots3d` turns on interpolation and the full view, which must not change the game):
  against the player's recorded `# end frame N wram H`, the CMake verify hash, or — for
  runs with `--native-from`, whose faster C loading changes RAM timing so the verify
  hashes do not apply — the hash pinned in `tools/qa/baselines.json`. Fatal.
- **leaks/** — a shot with no `_sel` and no `_3dx` whose original picture is not black
  (>2 % of pixels with a channel above 40): the player would see the original graphics.
  Limit: only sampled frames are seen (every 2–4 frames; 30/60 in the two long chris
  sessions), so a one-frame leak can slip between shots in the sparse runs.
- **blank/** — a modern frame that is one flat colour (std < 3) while the original shows
  something.
- **popping/** — the modern picture at 320×180 is split into 10-pixel blocks; the HUD
  panels, the race-message area (countdown, GO, 19TH — they blink by design) and the
  player's car are masked out. Two bands: *skyline* (10–43 % of the view height) and
  *roadside* (43–84 %). Per block, the mean grey change between consecutive shots `d(t)`.
  In the skyline band the previous frame is first shifted by the best ±8 px / ±4 px
  offset (camera yaw and pitch). A block **pops** when `d(t) ≥ 18` and
  `d(t) ≥ 3 × max(d(t-1), d(t+1), 3)` — a temporal spike: steady motion changes a block
  every frame and is not flagged, something appearing or vanishing at once is. A block
  **flickers** when it changes twice (`≥ 18`) and frame t+1 is back within 6 of frame
  t-1 (A-B-A), over a 2×2 patch of blocks or a run of 4 in a row (thin roadside posts
  passing close by are seen in one shot only — temporal aliasing of the sampling — and
  are not counted). **Motion compensation** (block matching, as video codecs and
  frame-interpolation checks do): a spiking or flickering block whose new content is found
  within ±32 px across / ±12 px up-down in the shot before *and* whose old content is
  still found nearby in this shot is something that moved — a near prop sweeping by in
  perspective, a car — not something that appeared or vanished, and is dropped (counted as
  `pop_blocks_moved` in the summary). Within 32 px of the picture's sides and 12 px of its
  bottom, where a thing can leave the picture in one step, either of the two is enough.
  One skyline block or two roadside blocks make an event; more than 30 % of
  blocks changing is a cut or fade and is skipped. Roadside events that cover a strip
  ≥ half the width and ≤ 5 blocks tall are labelled `:wide-band` (a whole stretch of
  ground changing at once). **Explained by the original:** the original picture's change
  at the same step is measured too; if the original has the same spike (or A-B-A), or a
  cut (mean change ≥ 30) one shot before or after (the modern view can trail the original
  by a frame or two), the event is kept in the report marked `*` but does not fail the
  check (the modern look
  follows the game there, e.g. the camera kick when the car hits the kerb).
  Limits: fixed bands (the horizon moves on hills, so a hill can put ground in the
  skyline band); a large near object leaving the frame in one step (the start gate
  overhead) is a true spike and is flagged; objects that pop in very small (a few pixels
  at the horizon) are under the threshold; the comparison with the original is
  whole-picture, not per object. Needs consecutive shots, so it runs only where shots are
  every 2–4 frames.
- **cars/** — in the zone where cars are drawn, each block gets the hue (12 bins) of its
  saturated pixels if they cover ≥35 % of it. An event: two adjacent blocks whose hue
  jumps by ≥60° between consecutive shots, where the new colour was nowhere within 2
  blocks before and the old colour is nowhere within 2 blocks after (a car that moved
  would still be nearby) — i.e. a car that turned another colour in place. Limits: cars
  in grey/white/black liveries are invisible to it; a car passing in front of a board
  of another colour can match; a model-change at the same colour (shape only) is not
  seen.
- **speed/** — `tg_headless` prints, per view of each modern shot (`TG_V3_TIME=1`), the
  scene-build time (`tg_view3d_draw_view`, CPU, the same code the game runs per frame),
  the triangles and batches (≈ draw calls on the GPU), and the software raster time (the
  headless rasterizer only, so a relative number). Reported: median, p95, p99, max and the
  "1 % low" as the mean of the worst 1 % (CapFrameX's *low average*), frames over the
  16.7 ms budget, hitches (> 2× the median and > 8 ms) and the first frame of each race
  separately. Fails when the steady p99 is over 16.7 ms or any frame stalls > 100 ms.
  Limits: this is not the GL frame time on the player's PC (no GPU, no present); three
  replays share the CPU, so single frames over budget are listed but do not fail.
- **sheets** — contact sheets of what the player sees (`_sel`, else `_3dx`, else the
  original), a tile every ~60 frames (more spread in long recordings) plus one at every
  change of what is shown and at every leak; red border = original leak, orange = blank.

## Timings (this machine, 12 cores, 3 jobs)

- quick: 2–3 min (117–171 s; the 2-player regression is the longest part).
- full: ~62 min (3702 s): `chris_session_03` (304 552 frames in verify mode) alone takes
  that long; everything else is done in ~32 min, so `--skip chris_session_03` for a
  faster full run.

## Research: what was adopted

- **Visual regression testing in games** ([Bugnet: screenshot comparison testing](https://bugnet.io/blog/how-to-automate-screenshot-comparison-testing),
  [Nordeus: screenshot tests for Top Eleven](https://engineering.nordeus.com/screenshot-tests-for-top-eleven/),
  SSIM-based tools such as AutoControl): deterministic replays first (we have them: the
  recordings and pinned hashes), masks for regions that change by design (our HUD and
  message masks), tolerant rather than pixel-exact comparison for 3D renders, diff images
  with boxes as evidence, and comparison against the previous run. We do not keep
  golden images (they would be ROM-derived and the look is still changing); instead the
  checks are *temporal* (a frame against its neighbours) and *structural* (original
  graphics showing, flat frames), which need no baseline, and the pass/fail history per
  check gives the run-to-run comparison.
- **Popping / LOD detection** (research on game visual-bug detection, e.g.
  [arXiv 2510.03591](https://arxiv.org/pdf/2510.03591) and LOD validation
  [arXiv 2208.12674](https://arxiv.org/pdf/2208.12674)): popping is an abrupt change
  between consecutive frames that motion does not explain. Learned detectors need labelled
  data we do not have, so we use the classic temporal-spike / A-B-A test with camera
  compensation for the skyline, and the original game's own picture as the reference for
  "is this change explained".
- **Frame pacing** ([CapFrameX: performance metrics](https://www.capframex.com/blog/post/Explanation%20of%20different%20performance%20metrics),
  PresentMon): judge frame *times*, not FPS; report median, percentiles and the worst-1 %
  average, and list single hitches. We measure the CPU scene build per frame, the one
  part the headless tool shares with the game; the GL frame time on the player's PC still
  needs a PresentMon/CapFrameX capture (not done: the game is not started here).

## Frame-time log (the player's PC)

The game can write what each picture cost, so a slowdown on the player's machine can be
measured rather than guessed (headless runs have no GPU, no driver, no vsync).

- **Record:** in the game press **Shift+F2** (a toast says "Frame-time log on"), play a
  race, press **Shift+F2** again (or quit). The log is
  `recordings/perf-YYYYMMDD-HHMMSS.csv` next to the game, beside that session's
  `session-*.txt`. Or start with `race-masters.exe --perf-log C:\path\perf.csv`, or set the
  environment variable `TG_PERF_LOG=C:\path\perf.csv`, to log from power-on.
- **Columns** (one line per picture shown): `t_ms`, the game `frame`, `in_race`,
  `game_frames` run before this picture and their `game_ms` (the emulated game), the 3D
  view's `capture_ms` (learning from the game's picture), `build_ms` (the scene's
  triangles), `gl_ms` (OpenGL submit: texture and vertex upload, draw calls, CPU side),
  `present_ms` (SDL_RenderPresent: mostly the vsync wait), `frame_ms` (picture to picture,
  what the player feels), `triangles`, `draws`, `vertex_kb`, `upload_kb` / `uploads`
  (texture data sent), `gpu_ms` (the GPU's own time for the 3D views from GL timer
  queries; it lags a frame or two, -1 when no result came back yet), the window size.
- **Read:** `python3 tools/qa/perf_report.py perf-....csv` prints the percentiles (median,
  p95, p99, max, worst-1 % average as CapFrameX's *1 % low*) of each column, the first
  10 s of each race against the rest, and the 15 worst pictures with their breakdown.
  `frame_ms` steady near the display's refresh with `present_ms` large = fine (waiting
  for vsync); `gpu_ms` near or over the refresh = the GPU is the limit (window size,
  antialiasing); `build_ms` / `capture_ms` large = CPU; `upload_kb` spikes = textures.
- **The same from a replay:** `tg_headless ... --shots3d WxH --perf-log FILE` builds the
  modern look every race frame at WxH (no raster) and logs `capture_ms`, `build_ms`,
  triangles, batches, vertex bytes and the texture bytes a renderer would send
  (`tg_v3_list_upload_bytes`); this is how the start-of-race slowdown was found
  (TRACKER, "Frame time ... start of race").
- **Headless logs:** `perf_report.py` reads a `tg_headless --perf-log` file too: `frame_ms` is
  then capture + build (CPU only), GL/present/GPU are shown as not measured, `t_ms` is the game
  frame at 60 a second.
- **Research used:** PresentMon / CapFrameX (frame times not FPS, percentiles, 1 % low,
  present vs GPU busy), GL texture streaming (`glTexSubImage2D` of only what changed
  instead of re-creating a texture; a vertex buffer that grows and is orphaned and refilled
  each frame rather than reallocated), GPU timer queries read back without stalling,
  warming resources before they are needed (the loading card), screen-size based levels
  of detail and vertex-clustering simplification (Rossignac & Borrel) for meshes far away.

## Course sheets (all 32 courses)

`python3 tools/qa/course_sheets.py [--jobs N] [--from F --to T --every E] [courses...]` replays
`session-20261030-081901` with `--set-course 1900:N` for each course (numbers as in
`docs/retro/CITIES.md`) and writes `$TG_QA_OUT/courses/<time>/sheet_cNN.png` (modern shots, the
original's picture of the same frame as an inset top right) and `all.png`. ~50 s a course with
one job. Limit: the learned skyline is the recording's course's, not course N's.

## Landmarks (look development)

The modelled skyline landmarks (Rio's Corcovado and Sugarloaf, the Eiffel Tower, Tokyo Tower,
the Leaning Tower) are baked by `tools/make_landmarks.py` into `src/mod3d/landmark_meshes.h`
(run it from the repository root; deterministic). `--only LmEiffel --preview DIR` bakes one and
writes previews (day at 1440p and 1080p, night, a close-up) lit as the game lights it;
`--debug DIR` writes where the baked painter's order disagrees with a depth buffer (red).
`TG_LM_INFO=1` makes `tg_headless` print each course's landmark, its direction and the scene's
light in its view frame (the light to preview a model with); `TG_LANDMARK=N` puts landmark kind N
(the `LM_*` order in `view3d.c`, Christ the Redeemer = 5) on any course, e.g. at night on New
York (`--set-course 1900:2`). `python3 tools/qa/landmark_sheet.py [--frame F] [--size WxH]
[courses...]`: every course's landmark from the grid (frame 2700, `--set-course 1900:N`), one
full-size shot each and a labelled sheet of their top halves (`$TG_QA_OUT/landmarks/<time>/`).

## Per-car check (cars keep their colour and level)

`TG_CAR_TRACE=1 build/tg_headless ... --shots3d WxH --shots-every 2 2> trace.txt` writes a line
per car drawn (`CT frame id paint … depth … pal … lvl … brake … view …`) and per rear picture
learned (`CR`). `python3 tools/qa/car_trace_check.py trace.txt` then checks, per car id and
per race (no gap over 120 frames): the paint never changes (except once from the neutral
silver drawn while the car palettes are still black at the race's fade-in), and no level of
detail flips back within 10 frames (A-B-A) in a view. Stricter than `cars/`: it follows each
car by its id, sees grey and white cars and model changes, and is not fooled by a car passing
a board. Exit 1 on a failure.

Room between cars (drawn only, `step_separation`): the same `TG_CAR_TRACE=1` run also writes, once
a game frame, `CS frame id game ALONG ACROSS drawn ALONG ACROSS` for each rival within 8 m along
and 3 m across of a player's car (its gaps in metres, as the game has them and as drawn), `CP frame
id id game ... drawn ...` for two rivals as close, and `CK frame player P rival ID ahead|behind`
when the game's knock ($1EF5 / $1EF6 bit 7) rises with a rival in reach (the sparks). The models
(4.3 x 1.9 m) intersect where `|along| < 4.3` and `|across| < 1.85`; count those in the game's and
the drawn gaps, and the drawn gap jumping more in a frame than the game's.

Cars seen through (`car_cover`): the same run writes, per drawn view, `CF frame view id cover C alpha A
door D` for each car that hides part of that view's player's car from the chase camera, or is still
fading back: `C` the share of the player's car *as seen* (its faces turned to the eye, sampled and
weighed by solid angle) whose sight line from the eye passes through this car's body (lower body and
cabin) first; it fades from 0.3 and is let go under 0.2. `A` the opacity it is drawn with (eased),
`D` its screen door (of 16 pixels this many left out, 10 at the most). A car beside, ahead, or behind
in another lane should stay at door 0.

## GL shader check (the game's GL renderer, without its window)

`python3 tools/qa/gl_check.py [--build build]` pulls the GLSL out of `src/gl3d.c` (the shaded
materials' program and the basic fallback), builds `tools/qa/gl_check.c` against it and runs
it on an offscreen EGL context (Mesa: the GPU's own driver - on WSL the D3D12 one - and
llvmpipe), on the 25 showcase pictures of the five cars (`--showcase` with
`TG_SHOWCASE_DUMP=1`: their triangles as drawn, batch by batch with their pictures - the cars'
textures with their mip levels and tint masks, tgcar version 3, and the race number's font -
and each batch's screen door), and the same 25 seen through by screen door
(`TG_SHOWCASE_DOOR=7`, `door_*`: the ordered dither a rival hiding the player's car is drawn
with; GL must leave out the very pixels `tg_v3_raster` does). Each is compared with
`tg_v3_raster`'s picture of the same triangles (the headless renders' per-pixel lighting,
`shade_mat`, and trilinear texture sampling, `sample_mip`, which the shader and the GPU
repeat); differences per picture and `$TG_QA_OUT/gl_check/sheet.png`. Exit 1 if a shader
doesn't compile or link, a material-shader picture differs (mean over 3 / 255, or over 2 % of
pixels by more than 24), or a `door_*` picture is the whole car's. The basic fallback is
reported, not judged, for textured cars: it lights the paint at the corners and draws the rest
of the texture unlit (mean 1.7-4 / 255). Run it after changing `shade_mat` / `env_light`, the
texture sampling, the screen door or the shader. Limits: no MSAA and no anisotropic filtering
(the game uses up to 8x on the cars' textures; the software renderer has none); it proves the
shader and the vertex layout, not the game's window on the player's PC. WSL's D3D12 driver
sometimes crashes on exit after the picture is written (counted as a pass).

## Self-test

`python3 tools/qa/selftest.py` (also run by the harness as `selftest/analyze`) feeds
synthetic frames — no ROM — to the checks: smooth motion (nothing found), a skyline pop,
a roadside A-B-A flicker, a car turning blue in place, a pop the original does too (not a
failure) and a leak. Run it after changing `analyze.py`.

## tg_headless change

The `TG_V3_TIME` line in `tools/tg_headless.c` now also prints the look, the view (`view k
of n`) and the raster time, so the speed check can pick the modern look's lines out of the
three looks each shot renders and add up both views in 2-player. Nothing else changed.

## Results figures check

`python3 tools/qa/check_results.py NAME INPUT PORT FRAME [args]` replays a recording to a
frame with the RESULTS page up and compares, row by row, the original's printed figure (read
off BG1), the game's tables in WRAM (`$1F12` order, `$1F26 + 2·driver` times in frames) and
what the modern table prints (TIME, GAP). Exit 1 if any row differs, the times are not
non-decreasing in finishing order, or fewer than 10 rows are read. Checked: session-20261102-195339
native 8400, session-20261030-095942 native 7400, chris_session_02 verify 8880 (30/30 rows).

## README screenshots (nothing from the game's pictures)

`python3 tools/qa/readme_shots.py [--write] [names...]` renders the README's shots of the modern
look (Rio's grid, New York's grid at night, Las Vegas a second after GO) at 1920x1080, averages
them down to 1600x900 (full colour, under ~600 KB) and proves each: the modern view's draw list
(`TG_V3_PICS=1`) uses only the pictures the mod makes (`TEX_*`, `FONT_PIC`, `CAR_TEX`,
`LIVERY_PIC`), and no skyline had been learned when the run ended at the shot (`--export-art`:
no `sky-*.bmp`). The skyline matters although it is untextured: its outline layer traces the
heights of the game's skyline picture, and it is learned only while the car moves past the
start gantry, so the shots are on the grid or in the first second after GO (Rio, with many
banners, learns as soon as the car moves). Only the art folder's `cars/` is given (a menu
photo or picture upscaled from the game that is in the folder is never loaded), no
`--v3-cache`. The original picture and the picture looks' shots are deleted at once. Exit 1
on a failure; `--write` copies the shots to `docs/screenshots`. Kept apart from what is
checked: the road and the HUD's map follow the course data the game has in RAM, and sky and
ground colours its palette (facts drawn by the mod's code, as in every race).

The launcher's screenshot is the launcher's own: build it for Linux against an SDL with only
its offscreen and dummy video drivers (no window can open):
`cmake -S . -B build-lnx -DTG_BUILD_FRONTEND=ON -DFETCHCONTENT_SOURCE_DIR_SDL3=<SDL3 source>
-DSDL_UNIX_CONSOLE_BUILD=ON -DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_KMSDRM=OFF`, then
`SDL_VIDEO_DRIVER=offscreen SDL_RENDER_DRIVER=software XDG_DATA_HOME=<scratch> TG_LAUNCHER_SHOT=out.bmp
TG_LAUNCHER_FOCUS=18 build-lnx/race-masters-launcher` (`<scratch>/race-masters/Race Masters/launcher.txt`
holding `rom <path>` shows the ROM verified; 18 = Start focused).
