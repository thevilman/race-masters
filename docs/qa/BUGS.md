# Modern look: bug list (QA, first pass)

Produced by `tools/qa/run_qa.py` (full run, commit `45c04fa` + the QA tools, 2026-11-02)
plus a look at every contact sheet. The other roles work from this list; rerun the
harness after each fix and move the item's tracker line to `tested` when its check passes.

- Full run: 25 recordings + 2 regressions, **3702 s** (62 min; `chris_session_03` in verify
  mode alone takes 62 min, everything else finishes in ~32 min, so
  `--skip chris_session_03` gives a ~32 min full run). Quick run: **~2–3 min** (171 s with a
  busy CPU).
- Game logic: **all hashes match** — both regressions, every player recording's recorded
  end hash (12), the CMake verify hashes of `race_1p`, `race_2p`, `attract`,
  `chris_session_01/02/03`, and the native-mode baselines of the 7 `country_*` runs
  (pinned in the first run, matched again in the second). The modern look does not change
  the game.
- Images: `$QA` below is
  `$SCRATCH/qa/runs/full-20261102-185250/sheets`
  (contact sheets `<rec>_NN.png`, event pairs `<rec>_events_NN.png`; full report in
  `../report.md`). Frames are game frames of that recording.

Severity: **S1** breaks the modern look for a whole screen or mode, or freezes the game;
**S2** visible in every race / to every player; **S3** visible now and then; **S4**
cosmetic or not yet confirmed.

## Sign-off (QA, final round, 2026-11-03, HEAD `a461f23`)

`$SO` = `$SCRATCH/signoff`
(`$SO/courses/` = `.../scratchpad/qa/courses/20261103-103258`, 32 course sheets with the
original as an inset); `$QA2` = `.../scratchpad/qa/runs/full-20261103-102059/sheets`.
Summary in `docs/qa/SIGNOFF.md`. Each bug below carries a **QA sign-off** line: `verified`
(the fix seen in the current build at the cited frames), `reopened` (with evidence) or
`still open`. New bugs: QA-022 to QA-027 at the end.

## Re-sign-off (QA, 2026-11-03 evening, HEAD `f77ed95`)

`$QA3` = `.../scratchpad/qa/runs/full-20261103-170156`, `$CS3` =
`.../scratchpad/qa/courses/20261103-170233`, `$RS` = `.../scratchpad/rs` (full paths in
`docs/qa/SIGNOFF.md`). Lines marked **Re-sign-off** below. Verified: QA-007, 023, 025, 026.
Reopened: QA-022, QA-020 (both S2). Still open: QA-009. New: QA-028 to QA-033.
**NO-GO for the beta until QA-022 and QA-020 are fixed.** `tg_headless` now draws the modern
look first, so image counts are not comparable with earlier runs.

## Summary

| Severity | Count | Ids (primary owner) |
|---|---|---|
| S1 | 3 | 001 (Engine, fixed), 002 (Engine), 003 (UI) |
| S2 | 7 | 004, 005, 006, 008, 009 (Engine), 007 (Art), 010 (UI) |
| S3 | 5 | 011, 012, 014 (Engine), 013 (QA), 015 (3D) |
| S4 | 3 | 016 (Engine), 017, 018 (UI) |
| **Total** | **18** | primary: Engine 11, UI 4, Art 1, 3D 1, QA 1; Retro is second owner on 001, 003, 010; Art on 004, 006, 015 |

## Bugs

### QA-001 · S1 · 2-player races drawn as one view, with the other player's car as pixel sprites
- Check: none automatic (found on the contact sheets; confirmed against the original).
- Recordings: `chris_session_02` (2-player Las Vegas race, ~3060–8712), `session-20261030-095942`
  (2-player Los Angeles race, 8182–11160), `attract` (the attract demo is a 2-player
  split-screen race in the original, 2404–7200).
- What: the original shows the split screen; the modern look shows **one** full view. In
  `chris_session_02` frame 7656 the one view follows player 2's purple car and draws
  several copies of a car as flat, blocky **original sprites** (magenta pixel cars at left
  and in the distance). `race_2p` *is* drawn split correctly, so it depends on how the
  2-player race is entered.
- Evidence: `$QA/chris_session_02_02.png` (6952–7744), `$QA/attract_01.png` (2404+),
  `$QA/session-20261030-095942_03.png`; side by side with the original:
  `$SCRATCH/qa/look9.png`.
- Suspected area: `tg_view3d_views()` / the views decision in `view3d.c`, and the sprite
  pickup in 2-player (`tg_game_set_full_view`). Owner: **Engine** (Retro to confirm which
  2-player entries exist).
- **Status: fixed (Engine).** None of the three is a 2-player game: `$1F04` (the 2-player
  flag) is 0 throughout `chris_session_02`, `session-20261030-095942` and `attract`. The
  split in the original there is the game's 1-player screen (the COMPUTER rival below, R-15:
  the modern look's full view is a style decision) and the attract demo (`$1FF6 = $FFFF`).
  (1) The attract demo is now drawn as two modern views, one per demo car, every car a
  model (`tg_view3d_views`; what is learned from the screen still follows `$1F04`).
  (2) The flat sprite cars: in verify mode a 1-player race is drawn split by the original's
  code, while the object tags come from the C sprite builder's full-screen lists; they did
  not match the OAM and cars were learned as roadside pictures (7 roadside slots held car
  sprites at chris_session_02 7700). Nothing is learned from sprites in such a frame (as
  `capture_picture` already did). The player's game (native, full view) never had this.
  Evidence: renders chris_session_02 7600-7700 (models only), attract 2600-3400 native and
  verify (two views); quick run `quick-20261102-230526` regressions PASS, popping/cars as
  before. Limit: verify-mode 1-player replays now show no learned roadside pictures.
- **QA sign-off 2026-11-03: verified.** Full run `full-20261103-102059`: `attract` sheets 2580-7200 two modern views, every car a model; `chris_session_02` sheets 3256-8712 one full view, models only, no sprite cars; `race_2p` split. Hashes PASS.

### QA-002 · S1 · Multi-second stall on the first frames of a race
- Check: `speed/*` (stalls > 100 ms).
- Recordings: every recording with a race. First race of a session: 0.9–5.3 s on the first
  modern frame, often a second stall ~30 frames later (e.g. `chris_session_01` 2100: 2478 ms
  and 2130: 3037 ms; `session-20261029-225604` 1006: 5259 ms; `attract` 2404: 737 ms, 2436:
  2880 ms; `session-20261030-081901` 2274, 3984, 6028: 1.3–1.9 s; `chris_session_03` 284580:
  318 ms). Later races of a session stall less (the cache is warm).
- What: `tg_view3d_draw_view` (the CPU scene build the game runs every frame) spends seconds
  loading or building something (art from `art3d/`, models, learned pictures) inside the
  frame. In the game this is a freeze at the start or a few frames into the race.
- Evidence: `../report.md` speed lines; `../logs/<rec>.stderr` (`TG_V3_TIME` lines).
- Suspected area: lazy loading in the draw path; move to race load (behind the black
  frames) or a worker. Owner: **Engine**.
- **Status: fixed (Engine).** Cause: the five car models (`art3d/cars/*.tgcar`, 10 MB) were
  read the first time a car was drawn, 28 bytes per `fread` (thousands of small reads; on
  the slow `/mnt/c` disk 5.2 s), and a new course probed ~300 replacement-picture names
  in the art folder one `fopen` each. Now the models are read whole (one read per file) when
  the art folder is set, before any race, and the art folder is listed once, so a course
  only opens the pictures that exist. Evidence: first modern frame of
  `session-20261102-173420` 5218 ms -> 20 ms (`TG_V3_TIME`), `attract` race start 737 /
  2880 ms -> max 28 ms; quick run `quick-20261102-201351`: `speed/` PASS, stalls >100 ms
  none (newly passing), regressions and replay hash PASS.
- **QA sign-off: verified for the player's game (native).** Full run: `speed/` PASS on all 21 player sessions and 7 `country_*`, no stall > 100 ms in any native race start; `--perf-log` 3440x1440 (session-073935, -173420, -195339): first 10 s of each race build p99 8.1-15.6 ms, max 13.5-22.9 ms warm. Left: verify-mode first race `chris_session_01` 2100 still 377 ms (solo rerun `full-20261103-121122`; was 2478 ms) and the menu hitch QA-025.

### QA-003 · S1 · Original-graphics leak: the photo message screen before a country's first race
- Check: `leaks/*`.
- Recordings: `country_france` 6056–6156, `country_italy` 6020–6116,
  `country_scandinavia` 6096–6196 (25–26 shots each, ~1.7 s).
- What: after the password, before the race, the original's greyscale city photo with a
  blue bar (a message screen as it fades in) is shown as is.
- Evidence: `$QA/country_france_02.png` (red tiles 6056, 6060, 6120).
- Suspected area: a screen with no redraw in `main.c` / `tg_view3d_menu_up`. Owner:
  **UI** (redraw), **Retro** (identify the screen and its text).
- **Status: fixed (UI).** Between a menu that starts a race (OPTIONS with PLAY GAME under the
  cursor, NEXT RACE) and the race, a GET READY card is drawn (next course and country, race
  n of 4, a running bar), at most 12 s. Evidence: renders of country_france 5900–6700: card at
  6060–6200 where the original shows its photo, race from 6220.
- **QA sign-off: verified.** `leaks/` 0 shots over all 34 replays (94 658 shots); `country_france` sheet: GET READY card 6060-6180, race from 6420; italy/scandinavia leaks PASS.

### QA-004 · S2 · Ground strips change in one frame (the speed stripes as big patches)
- Check: `popping/*`, events labelled `:wide-band` (638 pops + 123 flickers over 23
  recordings).
- Recordings: all; e.g. `session-20261102-173420` 2530 (still standing on the grid),
  2868, 2876, 2886, 2902, 2908, 2970, 3128, 3208, 4298–4310; `session-20261029-232004`
  91 events.
- What: a band of ground across half the screen or more switches pattern/brightness at
  once (desert dunes, or pale vs dark green), every ~10–12 frames — the original's
  alternating ground stripes reproduced as large patches that jump instead of moving.
  It happens with the car standing still at the start too.
- Evidence: `$QA/session-20261102-173420_events_*.png` (`pop:wide-band/roadside`).
- Suspected area: ground fields / ground texture in `view3d.c` (learned from the screen).
  Owner: **Engine** with **Art** (how the stripes should look).
- **Status: fixed (Engine).** Cause: the ground's two colours (per 40 m stretch and for the
  course) were the histogram's rank 0 and 1 each frame; the original's alternating
  stripes are two colours seen about equally often, so they swapped back and forth and
  every verge and field of the stretch changed at once (also standing still: learning
  continues). A stretch also switched from the course's colours to its own at 12 samples.
  Now the pair is kept until another colour is clearly commoner (hysteresis) and what is
  drawn eases to what was learned (~0.5 s). Evidence: quick run `quick-20261102-202900`
  wide-band roadside events 33 -> 0 (pop/flicker frames overall 65 -> 10).
- **QA sign-off: verified.** Wide-band ground events over the full run: 2 (`flicker:wide-band`), was 638 pops + 123 flickers. A 7-block ground patch still pops at `session-20261030-095942` 10090 (see QA-009).

### QA-005 · S2 · Ground and sky of the previous course at the start of a race
- Check: contact sheets; `popping` catches the switch.
- Recordings: `country_france` 6224–6540 desert, green from 6600; `country_japan` 5944
  desert and a purple car before the grid appears; `session-20261030-095942` Los Angeles
  8182–8460 desert, then **night** sky 8520–9120, day from 9180 (the original is day);
  `session-20261102-173420` 4044 sand where the original already shows green.
- What: the ground, sky colour and time of day are learned from the screen and lag the
  original by seconds; at a race start they show the last course's look.
- Evidence: `$QA/country_france_02.png`, `$QA/session-20261030-095942_03.png`,
  `$QA/country_japan_events_01.png`.
- Suspected area: what is learned from the screen at race load (reset per course, or
  read it from the course number/tables). Owner: **Engine**.
- **Status: fixed (Engine).** Causes: (1) the race view was drawn fully lit while the
  original was still black and fading in, in made-up fallback colours (a desert) because
  colours were only learned once the camera was steady; (2) a fade-in taught dark sky
  colours, so Los Angeles was lit as night. Now the modern view follows the game's own
  fade (INIDISP shadow `$52`); colours seen in a fading picture are taken back to full
  brightness exactly (the PPU's `e*b/15` inverted; a frame is skipped if its pixels do not
  fit), the panorama only at full brightness; a course with no colours yet takes them
  from its first lit pictures (also during the fly-in, sky only if sky-like), and when
  that happens under the fade they are shown at once. The `course3d-*.bin` cache is keyed
  by the track and saved under the old key before the reset (checked: it does not carry a
  look across courses). Evidence (renders): `country_france` black with the original to
  ~6420, green from the first lit frame (was desert to 6540); `session-20261030-095942`
  Los Angeles green and day from 8460 (was desert, then night 8520–9120); Las Vegas,
  Japan right from the start; quick run `blank/` PASS.
- **QA sign-off: verified.** `country_france` green from the first lit frame (6420), `session-20261030-095942` Los Angeles day + green from 8400, Las Vegas / Paris / Rio / Tokyo / Munich starts in their own colours on every sheet; `blank/` PASS in all native runs. See QA-026 for a sky that darkens mid-race.

### QA-006 · S2 · No skyline for the first 10–35 s of a race on a first visit
- Check: contact sheets.
- Recordings: Las Vegas `session-20261102-173420` 2284–2880; Japan `country_japan`
  5908–~8520; France `country_france` until ~7500, Eiffel Tower appears at 8340; all
  `race_2p` (neither view shows a skyline in the whole race).
- What: an empty sky (clouds only) until the skyline has been learned, then it fades in.
  The fade-in works (no pop), but the wait is long and 2-player never gets one.
- Evidence: `$QA/country_japan_03.png`, `$QA/country_france_03.png`, `$QA/race_2p_03.png`.
- Owner: **Engine** (learn earlier / ship skylines per course), **Art** (per-country
  skylines).
- **Status: fixed (Engine).** The far ridge is drawn from the first frame of a race:
  learned columns give their smoothed outline, unlearned ones a guess from learned ones
  (the panorama repeats: 256/128/64 columns along, else the nearest), over a procedural
  roll (hills, or building blocks for city skylines: Tokyo, New York); each column eases
  to its new height as more is learned; the crisp learned outline still fades in on top;
  the landmark shows from the start; a cached `course3d-*.bin` panorama is used when there
  is one. 2-player views get the ridge too. Evidence (renders): Las Vegas
  `session-20261102-173420` ridge from 2400 (was empty to ~2880), France from 6440, Japan
  from 6300 (city blocks; the real skyline still not learned by 8560 there), `race_2p`
  both views. Open: learning the real outline earlier (the gantry/speed rules) and
  hand-made per-course skylines (Art).
- **QA sign-off: verified.** Ridge/skyline on the grid in every start seen (all 21 player sessions, 7 countries, 32 courses `$SO/courses/`), both `race_2p` views.

### QA-007 · S2 · Wrong landmark: Los Angeles shows the Las Vegas water tower (fixed)
- Check: contact sheets, compared with the original.
- Recording: `session-20261030-095942` ~9480–10560 (the original shows a hillside sign
  at 10440).
- Suspected area: landmark choice per course (carried over from the previous course).
  Owner: **Art/Engine**.
- **Status: fixed (Engine).** Cause: the landmark was chosen per country ($1F06 bits 2-4),
  so every US race showed the Las Vegas water tower. Now chosen per city, by the course
  name as the game spells it (`course_name`, read from the ROM at run time): Las Vegas water
  tower, Los Angeles hillside sign, New York Empire State, San Francisco Golden Gate, Rio
  Christ the Redeemer, Machu Picchu peak, Chichen Itza pyramid, Tokyo Tower, Kyoto pagoda,
  Munich onion towers, Cologne spires, Frankfurt towers, Paris Eiffel Tower, Pisa, Rome
  Colosseum, Sicily Etna, Florence Duomo, London Big Ben, Stonehenge; other cities none.
  Evidence: `session-20261030-095942` Los Angeles 9120–10440 shows the LA hill
  (renders); all 19 shapes checked in a one-frame gallery render. Simple silhouettes: Art
  may refine.
- **QA sign-off: reopened (S3, Art/3D).** The hillside sign is not visible at `session-20261030-095942` 10280-10300, where the original shows the sign on the hill: the new downtown towers of the LA stage kit stand in front of it (`$SO/la_sign.png`, `$SO/lasky.png`). The landmark code is not wrong; the stage hides it. Fix: a gap in the far towers where the landmark stands, or the landmark higher.
- **Engine (2026-11-03): still open.** The landmark's place on the skyline follows the
  camera's heading mixed with the game's own sky scroll (30 %), so it has no fixed direction
  in the world and a gap left when the props are placed would not hold. Tried: fading far
  towers that cover the letters on the screen; the towers and the landmark move at
  different rates, so a tower faded and came back within a few frames (new 15-23 block
  pops at 4468, 9684, 10584 in the harness): withdrawn. What would work: the landmark
  placed in the world (a fixed direction or position), with the far towers kept out of
  that line when they are placed; or a per-prop fade with memory (eased over ~0.5 s).
  Needs Art's call on where the sign stands.
- **Art director (2026-11-03): fixed - every landmark has a place in the world.** Each course's
  landmark now stands in one fixed direction (at the horizon, like the sun; `lm_place` in
  `view3d_stages.inc`), chosen once per course as the way the road looks most often, the start
  straight (the grid) counted twelve times, turned ~6 deg to the pits' side so it is clear of
  the cars ahead and of most of the START gantry; it is drawn where that direction falls in the
  picture (projected like the world), in front of the far ridge and behind the hills. The stage
  props are then kept out of its line of sight: a prop that would cover the landmark's key part
  (the letters, the spire, the towers: `landmark_box` per kind) from the road where the landmark
  is on screen (sampled every 2 segments, centre and both sides) is not placed (10-30 % of a
  course's props; LA 145 of 741, mostly towers and palms in the sign's sector). The sign redrawn
  as real block letters (3x5 font, a 5-wide W) on Mount Lee's slope, the hill below them;
  landmarks a fifth smaller (H 64 band units), so tall ones no longer leave the top of the
  picture on a crest. Found on the way: `tg_headless` drew the modern look *after* the two
  other looks, so the modern look's own fades (the landmark, the learned skyline columns) never
  moved in any headless shot - the landmark was never drawn in QA renders (fade 0). The modern
  look is now drawn first. Evidence: `session-20261030-095942` 8400-10600 every 40
  (`$SCRATCH/art/la_sign_session_8400-10600.png`, `la_sign_8600_10120.png`):
  the sign readable over the grid, the start straight (8880-9080) and the main straight
  (10040-10240); all 32 courses on the grid (`course_sheets.py`, frame 2800,
  `$SCRATCH/art/landmarks_32_grid.png`): the landmark in view on all 32 (night courses New York,
  Black Forest, Florence keep their deliberately faint night silhouette). Limit: a prop placed by
  the game itself (its roadside objects, learned pictures) is not moved; the far ridge and the
  learned outline still stand in front of a landmark's foot.
- **Re-sign-off (f77ed95): verified.** session-20261030-095942 every 20 frames: the sign readable on the grid and the start straight 8960-9140 and 10200-10240 (`$RS/la_9000.png`, `$RS/la_sign.png`); course sheets c01 2700/3000/4800; a landmark is in view on the grid of all 32 courses (`$CS3`).

### QA-008 · S2 · Steady frame time over budget in some sessions; very many batches
- Check: `speed/*`.
- p99 of the scene build over 16.7 ms: `session-20261102-165338` 28.8 ms,
  `session-20261029-232004` 26.3 ms, `session-20261030-081808` 20.9 ms,
  `session-20261029-225604` 18.9 ms (medians 5–8 ms). Batches per frame: median 20–2400,
  max 3000–4200 in 1-player and **7351** in `race_2p` (≈ draw calls on the GPU);
  `race_2p` median 6.1 ms (two views).
- Caveat: headless CPU scene build only, three replays sharing the CPU; relative, not the
  player's GL frame time. Batches are a real GPU-side risk.
- Owner: **Engine** (batching, culling, LOD).

- **Engine pass 2 (2026-11-03), the player's "slowdown at the start of each race"
  (3440x1440, Windows/OpenGL):** measured with `tg_headless --perf-log` at his size on
  `session-20261103-073935` with his art3d models. Causes: the 20-car grid all drawn
  from the Blender models at distance-chosen levels (75-109k triangles a frame for the
  first 10 s; now 38k mean / 53k max with screen-size levels and clustered far meshes),
  the HUD's 3.5 MB upscale made and re-sent whenever the HUD changed (70 MB in the first
  10 s; now 0 in the race), file writes in the race's first frame, a vertex buffer
  reallocated each view. Scene build median at 3440x1440 10.2 -> 3.1 ms (first 10 s).
  The game now writes a frame-time log (Shift+F2; `tools/qa/perf_report.py`) so the
  player's PC can confirm it. Status: **fixed, to confirm with a log from the PC**.
- **QA sign-off: verified headless, open on the PC.** Steady p99 under 16.7 ms on every native recording (14 sessions newly passing `speed/`); 3440x1440 perf logs: race build median 2.9-3.9 ms, p99 9-13.5 ms, no upload in the race after the first frame. Triangles in the first 10 s of session-073935 at 3440x1440: median 46k, p95/max 72.8k (the tracker's 38k mean / 53k max is lower than measured now). Needs the player's Shift+F2 log.

### QA-009 · S2 · Scenery and skyline pop in
- Check: `popping/*` (`pop` events: 1160 skyline + 849 roadside, not counting wide-band).
- Recordings: all races; e.g. `session-20261102-173420` 2970 (cacti), 2990→3000 (trees
  appear on the right), 2464/2470 skyline; `country_uk` 40, `country_japan` 44,
  `chris_session_02` 409.
- What: trees, cacti and boards appearing at once mid-distance (known tracker item, now
  measured). Part of the skyline count is clouds jumping in steps (see QA-016).
- Evidence: `$QA/<rec>_events_*.png` (`pop/roadside`, `pop/skyline`).
- Owner: **Engine** (fade/LOD distance for learned scenery).
- **Status: fixed for learned scenery (Engine).** Objects of a roadside picture fade in
  and grow from 80 % to full size over ~20 frames once the picture is learned (alpha
  cross-fade; no dither shader in the back ends); the extra scenery fades out over its
  last 60 m instead of stopping at 260 m. Objects a course's cache knew are there from the
  start. Evidence: quick run `quick-20261102-202900` pop/roadside 17 -> 4 (the 4 left are
  near marker posts leaving the screen edge); the skyline part is QA-006. Not measured
  over the full run yet.
- **Engine (2026-11-03): the worst ones fixed.** `session-20261030-095942` 8532 (22 blocks)
  and 8920 (23 blocks) were not towers but the whole sky changing colour in one frame: a
  horizon colour learned from the original's hills (green) was replaced by a pale blue on
  the *eased* colour, so the sky switched as the ease crossed the line. Now the replacement
  is made on the learned colour before the ease (a mix of sky-like colours stays sky-like).
  Also eased: each skyline column's height and colours when the panorama is read again;
  stage props are tried for road segments up to 520 m (a prop 85 m off a bend is nearer
  than its segment; it came in at full strength when the segment crossed 430 m). Harness
  on that session (alone): popping 188 -> 185 frames, skyline blocks 262 -> 213, no event
  over 7 blocks (the two 22-23 block events gone). Left: 1-3 block skyline events, a few
  roadside boards.
- **QA sign-off: reopened (S3, Engine).** Motion-compensated popping over the full run, not explained by the original: 1084 skyline pops (1026 of 1-2 blocks), 228 roadside pops (90 of 3+ blocks), 21 roadside flickers; 29 of 31 race recordings fail `popping/`. Worst seen: `session-20261030-095942` 8532 and 8920 (LA skyline towers appear at once, 6-10 blocks), 10090 (a 7-block ground patch), 3796 (`$QA2/session-20261030-095942_events_02.png`).

- **Art director (2026-11-03): the pop sources found, smoothed; the rest is motion.**
  Found and fixed: (1) `tg_headless` drew the modern look last, so its own fades never moved
  in QA shots (the learned skyline outline was never shown to the harness: what QA measured
  was not what the player sees); now drawn first (QA-007 commit). (2) Skyline windows: a
  learned column's tower test ran on its *eased* height against its neighbours' learned ones,
  so the windows and the lit roof edge flipped on and off while it eased, and appeared at once
  when the column's fade crossed 200; now judged on the learned outline, the windows' strength
  eased per column (`sky_tw`) and scaled by the column's fade; night windows ramp in over 2
  units of height; the slope/step join of neighbouring columns blends over one unit instead of
  switching. (3) Learned scenery: each place picked its picture as `keys[h % n]`, so every
  place changed kind at once when a new picture was learned (n grew); now rendezvous hashing
  (a new key takes only its share of places) with a cross-fade from the one before. (4) Stage
  props were drawn by their road segment, only while that segment was between 3 behind and
  320 ahead of the camera with its road depth over -12 m: a prop 85 m off a bend vanished
  with its segment behind the camera, and one beyond the road's 800 m (where the loop comes
  back) appeared at once; now every prop of the segments 60 behind to ~560 ahead is culled
  and faded by its own place. (5) Fades with memory (`fade_mem`): the near-camera and
  hides-the-car fades of stage props and the game's roadside objects ease over ~0.25 s
  instead of following the geometry frame by frame. Harness on `session-20261030-095942`
  (alone, 640x360 every 2): with the fixed headless order, popping 173 -> 163 frames, skyline
  1-2 block events 136 -> 124, 3-5 block 9 -> 8; quick run popping 13 -> 10 frames. Looked
  at by eye (`$SCRATCH/art/diag.py`: every remaining event's blocks before/after, 4x;
  `$SCRATCH/art/dg_lv_all.png`, `dg_la_all.png`, `dg_la_sky.png`): Las Vegas 3640-3740 and
  Los Angeles 9780-10000 are all motion the block matcher cannot explain - palm trunks and
  poles crossing a block, a tree passing in front of a sign or a tower (two depths in one
  block), tower window stripes sliding (aliased at 10 px), near trees moving > 32 px a shot at
  speed, chevron boards replacing each other in the shot spacing. No appearing or vanishing
  object found in those windows. Suggested to QA: a per-pixel test (are the new colours found
  nearby in the shot before) for layered motion. Still open: game's own ground colour change
  (sand to grass in one segment, LV 3728).
- **Re-sign-off (f77ed95): still open (S3); not comparable.** The headless tool now draws the modern look first, so the counts measure something else. Full run `$QA3` (42 recordings): 1251 skyline pops (1173 of 1-2 blocks, 78 of 3+), 249 roadside pops (83 of 3+), 42 roadside and 5 skyline flickers, 70 events explained by the original; 34/42 recordings fail `popping/`. The big ones looked at: the START gantry leaving the top (race_1p 4280-4304, known limit), the grid appearing at once at the country_uk start (6540, QA-032), and marker posts. Nothing as large as the earlier LA tower pops.

### QA-010 · S2 · Nearly empty redrawn pre-race screen
- Check: contact sheets.
- Recordings: `country_france` 6220, `session-20261030-095942` 8178 (MENU tiles with only a
  few letters, e.g. "KLM", "NIJ").
- Owner: **UI** (redraw), **Retro** (which screen this is).
- **Status: fixed (UI).** Retro's diagnosis: race tiles on BG1 (tile base `$0000`) read as
  letters. `capture_menu` now requires the menus' font base `$1000`. Evidence:
  session-20261030-095942 7900–8560 renders: no junk menu, the loading card, race from 8180.
- **QA sign-off: verified.** `session-20261030-095942` 7968-8160: NEXT RACE then GET READY, no junk menu; same on every session sheet.

### QA-011 · S3 · Cars pass through each other in a bunched pack
- Check: `cars/*` (reported as a colour change).
- Recording: `session-20261030-095942` 3298–3304 (orange and purple cars merge, the purple
  one drawn through the orange one).
- Evidence: `$SCRATCH/qa/look7.png`,
  `$QA/session-20261030-095942_events_01.png`.
- Owner: **Engine** (car placement from sprites), **3D** (car bounds).
- **QA sign-off: still open** (not fixed; not seen on the sheets this run).

### QA-012 · S3 · Horizon line flickers
- Check: `popping/*` (`flicker/skyline`, 18 events).
- Recordings: `session-20261030-095942` 2588–2590, `session-20261102-173420` 2890.
- What: a row of blocks along the ground/sky edge goes A-B-A over three shots.
- Owner: **Engine**.
- **Status: fixed (Engine), with QA-006.** The horizon line itself was steady (instrumented
  `hy` over 2880–2900: smooth); the A-B-A was the skyline band appearing (the ridge and
  outline popped in when the first panorama columns were learned). With the ridge drawn
  from the start and eased, the quick run has no flicker events (was `flicker/skyline` x20
  at 2890).
- **QA sign-off: verified.** `flicker/skyline` events over the full run: 1 (was 18).

### QA-013 · S3 · Car-colour events to triage
- Check: `cars/*`: 33 events in 17 recordings (`chris_session_02` 4044, 5572, 8284, 11584;
  `race_1p` 5860; `race_2p` 9752; `session-20261030-095942` 3302, 4996, 5000, 5010, 5024,
  6424, 9116, 10268, 10372; the rest in `../report.md`).
- Triage so far: 3302 real (QA-011); 4996 (kerb) and 10372 (chevron board) false
  positives. Each event has a before/after pair in `$QA/<rec>_events_*.png`.
- Owner: **QA** (triage and tighten the heuristic), then **Engine** for real ones.
- **QA sign-off: still open (QA).** 300 `cars/` events in 28 recordings. The before/after pairs looked at (`chris_session_02` 4180-6720, `session-20261102-173420` 3244-5282) are all on static billboards, neon signs and flags, not cars: the check needs a mask for the stage's coloured props before its count means anything.

### QA-014 · S3 · Roadside posts and boards flicker in one shot
- Check: `popping/*` (`flicker/roadside`, 66 events after the thin-post filter).
- What: near roadside posts visible in a single shot (temporal aliasing at 2-frame
  sampling, probably fine at 60 Hz) and a few boards; check at 1-frame sampling before
  fixing anything.
- Owner: **Engine** (low priority).

### QA-015 · S3 · Blocky giant cactus / chevron boards close up
- Check: contact sheets (`race_2p` 10584–11304 P2 view, `chris_session_02` 8096–8272).
- What: near roadside pictures show as large pixel-art blocks.
- Owner: **3D/Art** (models or higher-resolution art for near objects).

### QA-016 · S4 · Clouds move in steps
- Check: `popping/*` (`pop/skyline` on cloud edges, e.g. `country_japan` 7956–7976,
  `chris_session_02` 4372).
- Not confirmed: the skyline camera compensation is whole-band, and clouds sit at a
  different depth than the skyline. Look at 1-frame shots first.
- Owner: **Engine**.

### QA-017 · S4 · "DEMO" label splits across the scene in the attract demo
- Check: contact sheet `$QA/attract_01.png` 2580–2640 ("DE MO").
- Owner: **UI**.

### QA-018 · S4 · Race messages blink (masked by the harness)
- The countdown, GO and position messages (19TH, 7TH) blink every few frames. The harness
  masks that area. If it is not meant to blink in the modern style, it needs fixing.
- Owner: **UI** (decide).

### QA-019 · S2 · Pixel pictures and pixel-font messages left in the modern look
- Seen on the stage contact sheets (`$SCRATCH/stages/after/`): a PITS sign drawn as the
  original's pixel picture (Rio), a taunt bubble in the game's pixel font
  (Frankfurt), the start's name banner (the player's name) in the pixel font.
- **Status: fixed (Engine).** The bubble was the HUD's taunt messages $24-$36 and the
  banner ids $37-$3E (only $38 was known): all in the modern HUD now (speech bubbles,
  the banner in the italic); the pit sign is modelled. Still pictures: the KEMCO
  trackside banners (Tokyo, `after/sheet_c08.png` 4800) and other non-solid sponsor
  pictures (Art).
- **QA sign-off: verified for the HUD.** taunt bubble modern (Frankfurt c15 3200, `$SO/c15v.png`); name banner modern (2- and 5-letter names on the grid, session-215000 2940, session-095752 2820, session-073935 2920-2980). Still pixel pictures: KEMCO trackside banners in Hiroshima/Tokyo (`$SO/courses/sheet_c09.png` 3300).

### QA-020 · S2 · Big props and trees filling the view near the camera
- Machu Picchu c05 4000, Frankfurt c15 4000 (`$SCRATCH/stages/after/`).
- **Status: fixed (Engine):** `near_fade` (gone within 3 m, or when a tree is wider than
  60 % of the picture). Renders c05/c15 3920-4000; faint ghosts remain while fading.
- **QA sign-off: reopened (S2, Engine).** Frankfurt c15 3920-4000 fades as described, but: Florence c27 4500 a dark-green prop fills the left half of the view and hides the player's car (`$SO/odd6.png`); large translucent ghosts of fading props over the road in Stockholm c16 3900 and Copenhagen c17 3300 (`$SO/c16_19.png`).
- **Re-sign-off (f77ed95): reopened (S2, Engine).** The cited frames are fixed: Florence c27 4500 car visible, Stockholm c16 3900 and Copenhagen c17 3300 with no big ghosts (`$CS3`). Dense render of Los Angeles (`--set-course 1900:1` on session-081901, every 4 frames, `$RS/c01d/`, `$RS/s01a.png`): at 3596-3620 a roadside tree passes between the camera and the car and fills the whole picture at 3608-3620, so the car is hidden for ~0.25 s. The occluder fade does not act on it. At 3896-3904 a building fades through translucent over the left half (`$RS/s01b.png`). On the 300-frame sheets, translucent houses at San Francisco c03 3900/4200 and tree trunks across the car at Nice c21 3600 and Monaco c23 3620 (to confirm dense).
- **Status: fixed (Engine, camera-fix).** `$CAM2` = `$SCRATCH/camera2`
  (dense renders `--set-course 1900:N` on session-081901, 640x360, every 2-4 frames, the
  original as an inset). Causes: (1) the occluder test compared screen boxes at a thing's
  *foot*, so a tree whose crown (not its foot) came between the camera and the car was never
  faded (LA 3596-3620); a first world-space rewrite with 12 sight lines to the car missed a
  thin marker post between them (QA-029); (2) the learned roadside scenery (`scenery()`: the
  extra trees/cacti beyond the roadside row) had no near or occluder fade at all; (3) the near
  fade ramps were long (3.5-6.5 m from the camera, 30-42 % of the picture's width), so a
  building beside the camera stayed half see-through for 8-16 frames (LA 3896-3904, a house
  at Sheffield 4776-4790); (4) `fade_mem` faded out as slowly as in (0.18 a frame: a quarter
  second of ghost). **Fixed:** `occluder_fade` is one world-space test per thing: anything
  nearer the eye than the car's middle and within the car's width of the line from the eye
  to the car (its radius added, any height) is gone, fading over the last 0.5 m across - a
  tube, not the cone the eye sees the car in, so a thing that came over the car stays gone
  until it is past the camera instead of fading back in under the car as a ghost; anything
  standing in the car (QA-029) is gone too; one dot product rejects everything beyond the car
  or behind the eye. `scenery()` uses it (eased per place); the marker posts too. Near fade
  3.5-5 m and 36-42 % of the width; `fade_mem` fades out at 0.6 a frame (6 % left after 3
  frames), in at 0.18 as before (no pop-in). Evidence: LA 3580-3640 `final_c01_3580.png` (the
  car is never covered; the tree that filled the view is gone; 2-3 shots of near fade at the
  left edge), LA 3860-3912 `c01_3880f.png` (the building solid until 3900, one shot
  see-through, faint at 3904; before `c01_3880.png`: 3896-3904), Sheffield 4780-4824
  `c29_e.png` (car never covered; the trees that filled the view at 4800-4812 stay beside it)
  and 4760-4794 `c29_f.png` (the house solid until 4776, gone by 4786; at HEAD `c29_head.png`
  see-through 4776-4790), Nice c21 3560-3640 `final_c21_3560.png`, Monaco c23 3580-3660
  `final_c23_3580.png` (car on the grass, never covered), San Francisco c03 3860-3940 and
  4160-4240 `final_c03_3860.png`, `final_c03_4160.png`, `c03_4166_crop.png` (a house over 60 %
  of the picture fades in 2 shots, 4176-4178). race_1p / race_2p pass. Limit: something the
  car has just cleared (it bounced off a board back onto the road) fades back in at 0.18 a
  frame, ~6 frames see-through beside the car (session-195339 9960-9964); a big thing beside
  the camera still fades over 2-4 shots.

### QA-021 · S3 · Chichen Itza sky deep blue; skies against the original
- Measured (`--set-course 1900:6`, original's sky row 4 every 40 frames): the original's
  own Chichen Itza sky is scripted: deep blue (0,0,189) from ~2960 to 3160, cyan
  (0,181,255) from ~3240, then orange (~3600). The modern sky follows it, eased over
  ~1 s (so 3200 is still deep while the original is already cyan). Not a bug in the
  colour; what was wrong: a learned colour that is not sky-like (the orange, or ground
  over a crest) fell back to a made-up deep blue (0x3070E8). **Status: fixed (Engine):**
  it keeps the sky shown before (else the original's cyan). Hiroshima's night start: the
  original paints a dark red dusk; the night sky now glows with it low down. All 32
  courses looked at (`$SCRATCH/stages/engine/all.png`, `sheet_cNN.png`): skies follow
  the original's (cyan day skies; Cologne / Bordeaux deep blue where the original's is
  (0,0,255); night courses navy). Open: follow a warm (orange) sky as the original does.
- Yokohama (c10) sand at 4000-4400: the original shows the same sand there (not
  QA-005); the race start is green.
- **QA sign-off: verified.** All 32 course sheets (`$SO/courses/`): cyan day skies, Hiroshima/Black Forest/Florence night with lit windows and lamps, Cologne day (`$SO/night.png`). See QA-026.

### QA-022 · S2 · Camera pitches into the ground off the road; the player's car leaves the picture
- Found on the 32 course sheets (`tools/qa/course_sheets.py`, `$SO/courses/`), compared with the
  original inset at the same frame.
- Machu Picchu c05 3900 and Black Forest c14 4500: after the car leaves the road (against a bank
  or a tree) the modern camera tilts down and drops: no horizon, the car high in the picture
  or out of it; the original stays level behind the car (`$SO/odd2.png`, `$SO/odd5.png`).
  The player's car cut off by the bottom edge at Tokyo c08, Yokohama c10, Nice c21, Monaco
  c23 at 3300 (on a crest / after a kerb hit) and Stockholm c16 4500 (`$SO/odd4.png`,
  `$SO/c20_23.png`); Nice c21 2700 the grid with a tilted horizon.
- Suspected area: camera height/pitch from the road under the car when the car is off the
  road (terrain or grade sampled at the car's lateral offset) in `view3d.c`. Owner: **Engine**.
- **Status: fixed (Engine, 300c817).** See "QA sign-off findings (2026-11-03)" below: the
  eye's height was the road's behind the car, not the car's; now level and framed.
- **Re-sign-off (f77ed95): reopened (S2, Engine).** The cited cases are fixed: c05 3900, c14 4500, c08/c21/c23 3300 and c16 4500 have a level horizon and the whole car (`$CS3`). New failures, confirmed by dense renders (`--set-course 1900:N` on session-081901, 640x360):
  - **Camera inside the terrain.** The picture is flat green (we look out from inside a
    hill) and the player's car is not drawn. Yokohama c10 3316 to past 3400, every 4 frames
    (`$RS/c10d/`, `$RS/s10.png`). Sheffield c29 from before 3200 to 3338, every 2 frames,
    after a kerb hit at 137 km/h; at 3200-3210 the view is filled by the kerb seen from
    above (`$RS/s29o.png`, `$RS/s29a.png`). Sheffield again at 4760-4806 (`$RS/s29c.png`).
    The original shows the car on the grass, level, in all of these.
  - **Car out of the picture.** Sheffield 4476-4512: the car drops below the bottom edge
    and comes back from below (`$RS/s29b.png`).
  - **Car cut in half by the bottom edge over a crest.** Copenhagen c17 4500-4580
    (`$RS/c17d/`, `$RS/s17z.png`) and Yokohama 3200-3312.
  - Harness: a green terrain wedge across the road at country_uk 9704
    (`$QA3/sheets/country_uk_events_01.png`).
  Likely cause: the terrain beside the road is drawn higher than the game's ground under an
  off-road car, and the camera keeps its height over the car, not over the terrain. Needed:
  the eye kept above the drawn terrain (or the terrain flattened within the car's reach of
  the road), and the car kept in the frame on a crest.
- **Status: fixed (Engine, camera-fix).** `$CAM` = `$SCRATCH/camera`. Causes, measured with a camera log:
  (1) the eye stood *under the drawn ground*: on the 33-43 % descents of Sheffield 3200 and
  Yokohama 3320 the road strips 6-11 m behind the car (on the inside of a bend above all, where
  the strips bunch up) are 3.2 m higher than the car, and the eye was at the car's height +
  2.9 m, then lowered further by the old "car never lower than on a flat road" rule (the eye
  came down instead of the view pitching) - at Sheffield 3200 it was 0.45 m under the road
  (the kerb seen from above), then inside the shoulder/field strips (flat green); (2) the arm
  length came from the road's middle over 0x90 units, which runs 1 m a segment where the lap's
  closing error is spread (Sheffield 4650): a 4.5 m arm, the car filling the picture; (3) a
  stage prop faded below 2 % left `g_alpha` near 0 for every later prop **and every car**, so
  the player's car vanished for single frames (Copenhagen 4492, depending on the fade memory).
  **Fixed:** a chase-camera rig: the eye's height over the car is kept above the ground the
  renderer draws (`drawn_ground`: the strips of +-24 segments, folds included, probed at the
  eye and 1.2 m round it, and along the line from the eye to the car's roof); the car is framed
  by pitching the view (its back between 0.12 and 0.27 of the depth under the axis, hard limit 0.28,
  the foot of the picture being 0.318), not by lowering the eye; height and pitch are
  critically damped springs stepped by game time (rise fast, settle slowly), the clearances
  hard limits after them; arm 80-110 % of 0x90 units x 2.5 m; the alpha leak fixed. Evidence:
  Sheffield 3180-3340 every 4 before `$CAM/qa022_c29_3180_before.png`, after
  `$CAM/qa022_c29_3180_after.png` (car whole in all 41, never inside); Yokohama 3300-3420 before
  `$CAM/qa022_c10_3300_before.png`, 3196-3420 after `$CAM/qa022_c10_3196_after.png`; Sheffield
  4464-4560 `$CAM/qa022_c29_4464_after.png` (no drop-out), 4640-4740
  `$CAM/qa022_c29_4640_after.png` (arm length kept), Copenhagen 4488-4588
  `$CAM/qa022_c17.png`; Las Vegas 2700-3900 an ordinary race `$CAM/qa022_c00_normal_after.png` (on the flat the view is ~1 deg lower than before, the car 0.27 not 0.29 under the axis: the wheels no longer touch the picture's foot).
  Limits: over a steep descending hairpin the camera looks down at up to ~30 deg (Sheffield
  3308-3340: the drawn ground behind the car is 3.6 m above it); on the inside of the tightest
  bends the ground strips fold over each other and the car, far out at 3.2 half widths, can be
  half sunk into a strip above its own (Sheffield 4684-4716): ground geometry, not the camera.

### QA-023 · S3 · The player's car drives through a chevron board
- Rome c25 3900: a chevron board drawn across the player's car; the original has it to the
  right of the car on the verge (`$SO/odd6.png`). Owner: **Engine** (lateral placement of the
  game's roadside objects near the player), **3D**.
- **Status: fixed (Engine, 300c817).** Objects now stand at the game's own distance from the
  road (low byte x 128, its collision test); see the section below.
- **Re-sign-off (f77ed95): verified.** c25 3900: the board stands to the right of the car on the verge, as in the original (`$CS3/sheet_c25.png`). Remainder: when the car really hits a board, the board is drawn through the car (QA-029).

### QA-024 · S3 · One black modern frame in a race at a pit-lane exit (verify mode)
- `blank/chris_session_03` 41640, 72900 (the only `blank/` failure). At 72900 the modern view is
  black for a single shot between 72880 and 72920 while the original shows the race (the
  pit view closing) (`$SO/blank03.png`). Seen only in verify mode; the native pit stop
  (Rio, c04 4200-4800, 30-frame shots) did not show it. Owner: **Engine**.
- **Engine: not a bug (the original does it).** Rendered every 2nd frame 72860-72940
  (`$SCRATCH/engD/q24/s.png`): at the pit-lane exit the original fades the player's own
  view (the top half) fully to black, 72898-72908, and back by 72924; only the computer's
  view below stays lit. The modern look draws the player's view and fades with it. `blank/`
  compares with the whole original picture, which never goes black because of the other
  half. In a native 1-player game the same fade is the game's own pit exit. Suggestion for
  QA: in a split picture, judge `blank/` against the player's half.
- **Gameplay presentation (2026-11-04, pit-stop): fixed.** The modern look no longer follows the
  original's fades round its own pit scene: each view takes its own brightness ($52 the top view,
  $65 the bottom: a 2-player race's views fade apart, before both followed $52), and a view's fades
  are left out while its car is in a pit stop and for 50 frames after (the fade back in), so the
  race is drawn through the stop - the stop on the pit lane (view3d_pits.inc). Rendered: chris_session_03
  72540-73220 every 4 (verify) `$PIT/dense/chris03_final.png`: no black or dark frame; Rio 4080-4720
  every 4 `$PIT/dense/rio_final.png`: the entry's black frame (4110-4122, forced blank) and the exit's
  darkening (4555-4600) gone (`$PIT` = `$SCRATCH/pit`).

### QA-025 · S2 · Freeze of 0.5-0.7 s (6 s on a cold disk) when the title is left for the menus
- `tg_headless --perf-log` (3440x1440): `capture_ms` 461 ms at frame 841 (session-173420),
  678 ms at 844 (session-195339), 725 ms at 1021 (session-073935) and **6103 ms** on the
  first run of the day (cold `/mnt/c` cache). Every session; the screen is the change from
  the title to OPTIONS (`$SO/f841/`). Not in a race, so `speed/` does not see it.
- Suspected area: something read or built on the first menu capture (car-select models or
  menu art from the art folder) — make it part of start-up or the KEMCO splash. Owner: **Engine** (UI).
- **Status: fixed (Engine).** Timed inside `tg_view3d_capture`: all of it was `capture_menu`
  (918 ms at frame 841, session-20261102-173420, 3440x1440), reading and decoding the menu
  photograph's upscaled picture `art3d/menubg-65d3cd5e.bmp` (3.6 MB) from the art folder the
  first time the OPTIONS photograph appears. The art folder's `menubg-*.bmp` are now read
  once when the art folder is set (start-up, before the first picture: behind the KEMCO
  splash) and taken from memory on the menu. Same run: frame 841 1.8 ms, worst menu frame
  3.3 ms; the OPTIONS screen at 1000 is pixel-identical to before (photograph included).
- **Re-sign-off (f77ed95): verified (headless).** `--perf-log` at 3440x1440, session-20261102-173420 and session-20261103-073935: worst picture outside a race 27.2 ms (frame 1692) and 29.0 ms (2686), both capture; nothing near 841-1021. The cold-disk case and the player's PC are not measured.

### QA-026 · S2 (to confirm) · The sky darkens to deep blue or night for ~1 s mid-race in daytime
- `session-20261030-095942` (Las Vegas, day in the original): harness shots (every 2 frames)
  deep blue sky at 3840-3900 (`$QA2/session-20261030-095942_02.png`); a render with shots every
  20 frames shows full **night** (lit windows, dark sky) at 3900 (`$SO/lvgreen.png`); with
  shots every 6 frames, day (`$SO/lvn.png`). Los Angeles: saturated royal-blue sky at 9120
  (session-095942) and 9900 (session-195339) for one tile. So the sky/time of day depends on
  which frames the look learned from, and the game learns from every frame: what the player
  sees has to be confirmed.
- Suspected area: sky colour / night decision learned from the screen (QA-021, R-06) at a
  stretch where the original's ground turns green; make the day/night state follow the
  game's palette script only. Owner: **Engine**.
- **Status: fixed (Engine).** Measured at every frame (session-20261030-095942): the deep
  blue is the original's own palette - Las Vegas 3771-3851 sky (0,148,255), Los Angeles
  9010-9090 pure (0,0,255) right after a grey stretch at 8950-9000 - which the modern sky
  follows with its ease (a second's lag, so it is deepest just as the original turns cyan
  again). The night was not the sky: night is judged on the sky and the ground, and at
  3880-3904 the learned ground pair passes one stripe through near black (0,31,0) while it
  goes from sand to grass; drawn every frame the night level dipped to 267 (night below
  120), but one such sample eased in at once (shots 20 frames apart, or any long gap
  between pictures) made it night. Now the ground counts by the brighter of its two stripes
  (the original's night ground is dark on both): the level stays >= 360 there, also with
  20-frame shots. Scripted changes kept: Cologne night from 5880 (original ~5700-6000),
  Black Forest night to day at 4320, Florence night.
- **Re-sign-off (f77ed95): verified.** session-20261030-095942 shot every 20 frames, the spacing that showed night before: Las Vegas 3760-4000 is day throughout, and the sky follows the original's own deeper blue at 3780-3840 (`$RS/lv_sky.png`). LA 9060-9140 is royal blue exactly where the original inset is (0,0,255), then cyan (`$RS/la_9000.png`). session-20261102-195339 9820-9980 is the same (`$RS/s195339sky/`). No lit windows and no night anywhere. S4 remainder: at LV 3900 the ground band at the horizon is near black for one shot while the original's ground turns green.

### QA-027 · S4 · QA tools: car-colour check flags billboards; perf_report crashed on headless logs
- `cars/` counts static coloured props (QA-013). `tools/qa/perf_report.py` raised `KeyError:
  'width'` on a `tg_headless --perf-log` file although the README says it reads it: fixed in
  this sign-off (it now maps the headless columns; CPU only). Owner: **QA**.

### QA-028 · S3 · Car select linear: a press when the screen has just come up skips a car later
- `tg_headless --select-linear` on `tests/inputs/race_2p_native.txt` with scripted presses
  from 2580 (`$RS/sel.txt`, log in SIGNOFF, `$RS/selsheet2.png`). The select screen comes up
  at 2562. A RIGHT at 2580 reaches the game as RIGHT, but the cursor `$5A` does not move. The
  translator counted it anyway, so the next RIGHT (2600) goes ROSSO -> VIOLET, skipping
  GLACIER. Everything after that is right: the line wraps both ways, UP/DOWN are ignored, a
  fast double RIGHT works, RIGHT+START moves and then takes the car. S4 on top: a move that
  is two grid steps (ROSSO -> AZURE by LEFT, 2660-2667) shows the in-between card (GLACIER)
  for 3-5 frames.
- Suggested: aim at the next card from the cursor as read now (re-read `$5A` before each
  press), not from a count of presses; hide the card change until the move is done.
  Owner: **Engine**.

### QA-029 · S3 · A roadside board is drawn through the player's car when the car hits it
- session-20261102-195339 9940-9960 (LA, speed 83 -> 57: the car hits the chevron board,
  and the board stands inside the car's body) (`$RS/s195339sky/`); Loch Ness c30 4500 on the
  300-frame sheet (`$CS3/sheet_c30.png`). The original draws its car over the board. Owner:
  **Engine/3D** (push the board out of the car's box while they touch, or draw the car
  over it as the original does).
- **Status: fixed (Engine, camera-fix).** See QA-020 (the same occluder test). A thing whose
  foot is in the player's car's box (its width added across: the game's boards face the road)
  and whose height reaches the car's floor is not drawn; it fades over the last 0.5 m and three
  frames' travel before they touch (at 83 km/h the board went from clear of the car to inside
  it in 2 frames, so a fade that started at the touch left a ghost through the car for 4-5
  frames). The marker posts go through the same test (a post was drawn across the car's back
  at 9936-9942, between the camera and the car). Evidence (`$CAM2`, see QA-020): before
  `qa029_a.png` / `qa029_a_crop.png` (post across the car 9936-9944, translucent boards over
  the roof), after `final_qa029.png` / `final_qa029_zoom.png` (9920-9980 every 2: no post
  over the car; the board the car hits at 9946 is 68 % at 9944, 11 % at 9946, gone at 9948,
  never through the car); Loch Ness c30 4470-4530 `final_c30_4470.png` (hit at 4500-4502:
  one shot at ~40 %, then gone).

### QA-030 · S3 · Race scene build heavier than at a461f23 (headless)
- `--perf-log` at 3440x1440 with the player's art3d. Rest of race build median 4.3-4.4 ms
  (was 2.9-3.1), p99 14.6-16.4 ms (was 7.0-10.8). session-073935 has 157 pictures over
  16.7 ms, max 96.6 ms build at 7168. First 10 s of a race: 45-49.5k triangles median, 71.9k
  max. The harness `speed/` fails race_2p (p99 24.2 ms) and country_scandinavia (p99
  21.6 ms). The load was the same as last round (a 2-job harness alongside), so this is a
  lead: likely the per-prop fades, the occluder test and the landmark line-of-sight. Needs a
  solo run and the player's Shift+F2 log. Owner: **Engine**.
- **Engine (camera-fix): not a regression of the build; a small saving made.** Built
  a461f23, 9bb117b and this branch side by side and logged the same replays at 3440x1440
  (`tg_headless --perf-log`, `perf_report.py`; logs in `$CAM2`, see QA-020). Rest of race,
  build ms median / p95 / p99 / max, pictures over 16.7 ms:

  | Replay (how run) | a461f23 | 9bb117b | 12f8b00 (QA-020/029) | this commit |
  |---|---|---|---|---|
  | session-073935, alone | 1.98 / 3.33 / 4.03 / 9.0, 0 | 1.99 / 3.52 / 4.28 / 28.7, 3 | 2.00 / 3.42 / 4.74 / 8.0, 0 | 1.93 / 3.45 / 4.60 / 7.5, 0 |
  | session-173420, the two at once | 2.43 / 4.51 / 5.60 / 13.9, 0 | | | 2.40 / 4.31 / 5.47 / 11.2, 0 |
  | race_2p (2 views), the two at once | 4.40 / 8.36 / 10.81 / 30.1, 10 | | | 4.63 / 7.18 / 8.90 / 31.3, 5 |

  The re-sign-off's 4.3 ms median and 157 pictures over budget were the machine's load
  (three replays at once), not the build: alone it is ~2 ms as at a461f23. The hitches over
  16.7 ms land on other frames in every run (9bb117b's 28.7 ms at 4411-4412 is not in the
  other runs; two runs of the same build at once had their worst frames at 4051/4100 and
  6364/9507), so they are the OS, not the scene; one is the same in both builds: race_2p 4779
  (~20-24 ms: the cars, 7.3 + 6.6 ms for the two views, against ~3.5 steady). Where the build
  goes (section timers, session-073935 alone, ms a view): cars 0.91 (mesh_car/mesh_corners,
  ~45 % of the build in a gprof profile), road and roadside objects 0.66, sort and emit 0.32,
  sky and skyline 0.17, stage props 0.05 (their fades and the occluder test included), HUD
  0.03, camera 0.01. The landmark line-of-sight runs once when a course is placed, not per
  picture; the race's first picture is 3-6 ms. **Changed:** the scene's triangle and item
  buffers are kept from one picture to the next (they grew from nothing each picture by
  doubling: ~4 MB reallocated and copied, then freed): median 1.99 -> 1.88 ms in the timed
  run. Quick harness `speed/` (640x360): median 1.91 ms, p99 4.7, max 8.8, 0 over budget
  (9bb117b: 1.88 / 4.29 / 8.29). Left: the cars are the lever for more (Car rendering's
  mesh code); the player's Shift+F2 log is still needed; perf numbers taken beside other
  replays are not comparable with ones taken alone.

### QA-031 · S4 · A strip of lit-window dots along the top of the night sky
- Black Forest c14 3300 (`$CS3/sheet_c14.png`): a band of window dots across the top edge
  of the picture, above the ridge. Seen once on the 300-frame sheet; confirm with a dense
  render. Owner: **Art/Engine**.
- **Status: fixed (Engine, camera-fix).** Confirmed dense (`--set-course 1900:14`, 3260-3340
  every 4, `$CAM2/c14_3260.png`, zoom `c14_zoom.png`): the band is in every shot. Cause: the
  skyline's lit windows at night were drawn on every learned column taller than 4 units,
  whatever its shape (the day's window glints only on a column judged a building: a flat roof
  with a sheer step near by), so a ridge of firs, hills or a mountain got rows of lit windows,
  and against the night sky only the dots show. (On the sheets the learned skyline is the
  recording's Las Vegas, but a real Black Forest fir ridge was lit the same way.) **Fixed:**
  at night, windows on a big city's outline as before (`city_skyline`: Tokyo, New York,
  Frankfurt, LA, San Francisco, Yokohama, London, Paris), elsewhere only on columns judged
  buildings. After: `c14_3260_after.png` (no band; a few windows on building-like columns);
  Florence c27 2900-3500 `c27_after.png` (night: the town's towers and the houses keep their
  windows).

### QA-032 · S4 · country_uk: the grid and the gantry appear in one frame at the start
- `popping/country_uk` 6540 (18 skyline + 21 roadside blocks; `$QA3/sheets/country_uk_events_01.png`):
  the countdown shows "1" over an empty road, and the next shot shows the whole grid with
  the START gantry. Verify mode with `--native-from 6000`; check a native race start.
  Owner: **Engine**.
- **Status: fixed (Engine, camera-fix); the player's game was not affected.** Dense
  (`$CAM2/uk_start.png`, 6500-6580 every 2; `uk_6520_vs_orig.png`): through the countdown
  the modern view stood ~200 m behind the grid (no car, not even the player's), while the
  original shows the grid; at 6540 it cut to the grid. Cause: the view takes the game's
  camera (`$46`/`$59`, the start's fly-in) when the race-update count ticks or a car moves;
  the count ticks in the native port's RaceRenderViews only, and this replay entered its race
  in verify mode (the original's code), so with the cars still on the grid the view kept the
  fly-in's first place (`$46` was A294 at 6200, A560 = the car from 6300) until the first car
  moved. A native race start (session-20261102-173420 2284-2900, `native_start.png`) flies in
  and holds the grid as it should. **Fixed:** the game's camera words changing count as an
  update too. After: `uk_start_after.png` (6100-6580: the fly-in, the grid through 3-2-1, GO,
  no cut); harness `--only country_uk`: the 6540 event is gone (left: a 2-3 block change on a
  house by the gantry during the fly-in at 6272, QA-009 kind; 7120 the START gantry passing
  overhead, the known limit).

### QA-033 · S4 · Vector logos: stray edge lines, triangle seams
- Classic title at 1920x1080, frame 1200 at 2x (`$RS/title_logo2x.png`): two thin stray edge
  lines beside the stars. KEMCO splash at 3x (`$RS/kemco3x.png`): faint diagonal seams of the
  triangles in the face gradient of the K and the M. Otherwise clean. Owner: **Tech art**.

### QA-034 · S1 · 2-player race black on the player's PC
- Player (2026-11-03, beta 2): "I started the game, put two player mode (not on the launcher),
  just 2 player as usual, the game stays black and I cannot see anything." Recording
  `session-20261103-212009` (2 players chosen in the game's options, LOCATION / GET READY
  1230-1350, race from 1360, GO 1860, quit 2067). Owner: **Engine**.
- What is known. His game ran at full speed throughout (2067 frames in 35.2 s: the file's
  name and modification time), so it was drawing, not hung. The beta 2 `tg_headless` replays it
  to his recorded end hash (`65300ce3…`: the replay is his game exactly), and his session just
  before (`211730`, beta 2, 1 player, end hash matched) raced with no complaint. His recordings
  have two more 2-player races, and none before today (`$TP/scan_two_views.txt`, `$1F04` once a
  second over every recording): `session-20261103-192522` (beta 2: its end hash on beta 2's
  `tg_headless`) and `session-20261103-215220` (21:52, after the 21:27 install: its end hash on
  HEAD's). All three end alike, at full speed: a few seconds into the race the buttons are
  mashed (192522 3379-3546, 212009 1992-2008, 215220 2000-2060; in 215220 one is then held 15 s
  with 1 s of steering) and the game is closed. 215220 ran HEAD's material shader, so beta 2's
  basic shader is not the cause; what the three share is `present3d`'s 2-view picture. The
  attract demo's split was up once, in `session-20261103-170021` (no input; two views from
  ~2430), and he closed the game at 2502, 1.2 s later (`$TP/hl3_pairs.png`). His GPU: Intel
  Iris Xe (Alder Lake), Intel's OpenGL driver 31.0.101.4953 (the host's driver store).
- **Not reproduced here.** The game's own frontend (`src/main.c`, `src/gl3d.c`) built for Linux
  against SDL 3.2.24 (X11/Wayland off: SDL's offscreen video driver, an EGL pbuffer, no window;
  dummy audio) replays the session with new debug options (`--window WxH`, `--shots DIR`,
  `--frames N`, `--fake-hz N`: pictures paced as on an N Hz display): the 2-player race is
  drawn in every combination tried - beta 2 and HEAD, gcc and the Windows compiler (llvm-mingw
  clang -O3), Mesa D3D12 on the same Iris Xe and llvmpipe, 4x MSAA, 640x360 / 1366x768 /
  3440x1440, his own settings, ghosts and learned course files, 20 to 144 pictures a second
  (2.4 pictures a game frame), the SDL renderer fallback (`--no-gl`). No GL error (Mesa's
  `MESA_DEBUG`), no ASan / UBSan finding and no valgrind error in the 2-player race (headless,
  the same view code). The view's content is fixed by the game's state, which is his.
- **Cause (by elimination, not seen on his driver).** The only differences between a 2-view
  picture and the 1-view pictures his PC shows fine were in how the frontend drove OpenGL.
  `gl3d.c` draws with its own context into the window SDL's renderer draws into. A 1-player
  picture is: renderer clears, gl3d's context draws, renderer presents. A 2-player picture was
  (`present3d`, main.c): renderer clears; gl3d draws view 1 (depth cleared in a scissor); the
  renderer's context made current again (`SDL_FlushRenderer`); gl3d draws view 2; then the
  renderer's context drew the 4-pixel line between the views over gl3d's drawing before the
  present. OpenGL defines no order between two contexts' drawing into one window (its rules
  for one context seeing another's changes - a flush, then a sync or bind - are for shared
  objects, not the window); WGL only flushes the old context at `wglMakeCurrent`, the rest is
  the driver's. In a race the renderer's context draws after gl3d's in the same picture only in
  2-view pictures (and while a toast or the Tab menu is up).
- **Fix:** a picture of several views reaches the driver as a 1-view picture does: the
  renderer's clear, then all views drawn by gl3d's context in one go (`tg_gl3d_begin`: made
  current once, the whole window's depth cleared once; `tg_gl3d_draw` per view; `tg_gl3d_end`),
  then the present, nothing drawn by the renderer in between or over them; the line between
  the views is 2 pixels each view leaves to the clear's black (top/bottom and side by side).
  The 1-view picture's GL calls are the same as before but for one unscissored depth clear.
- **Evidence** (`$TP` = `$SCRATCH/twoplayer`):
  before (HEAD's frontend) `$TP/before_1366.png`, `$TP/before_3440.png` (menus, race intro,
  race; drawn here too); after `$TP/after_1366.png` (title, options with 1 / 2 players, next
  race, get ready, race 1500-2040), `$TP/after_3440.png`, side by side at 3440x1440
  `$TP/shots/after_sbs_3440/frame001680.png`, fallback `$TP/shots/after_nogl/frame001680.png`,
  attract demo `$TP/after_attract.png`, the 21:52 session's race `$TP/after_215220.png`. 1 player unchanged: `session-20261102-173420` race
  frames 2400-3600 every 150 at a virtual 60 Hz, before vs after: 8 of 9 shots identical, one
  differs in 1 pixel by 4/255 (`$TP/shots/p1_before`, `$TP/shots/p1_after`).
- **Not proven on his PC.** To make the next report decisive, the session's recording now notes
  (comment lines) what drew the game (`# renderer opengl, 3D races by OpenGL: <GL_RENDERER>,
  <GL_VERSION>, Nx MSAA`), each 3D race's views and window size, and the first OpenGL errors.
  If a 2-player race is still black, those lines and a Shift+F2 frame-time log say whether gl3d
  drew (gpu_ms, draws) and the next step is a single context for the whole picture.
- The black at 1360-1460 before the race shows: the original's own fade-in after loading (its
  picture is black there too; the modern view follows the game's brightness, `$52`), not this
  bug (`$TP/hl_pairs.png`). The 2-player menus (options, location, get ready) match the
  original's pages (`$TP/hl2_pairs.png`).

### QA-035 · S3 · SDL renderer fallback: no text, no menu pictures
- Found while checking QA-034 with `--no-gl` (the path a PC without OpenGL 3.3 takes): menus
  without their captions, the HUD without its numbers, no countdown (`$TP/nogl_opengl.png`).
  Cause: `draw3dList` (main.c) kept a texture for view3d's pictures 0-511 only and skipped the
  rest, but there are 517 (`tg_view3d_pic_count`): the HUD font (513), the menus' pictures
  (514-516) and the last car's livery (512). **Fixed:** as many slots as gl3d.c keeps (1024).
  After: `$TP/nogl_text.png` (options, get ready, the 2-player race with its HUD and the "3").
  Owner: **Engine**. Limit: the fallback still has no depth buffer (wheels can show through
  bodies, painter's order).

### QA-036 · S3 · A session that ends in a crash leaves an empty recording
- Three of the player's recordings are 0 bytes: `session-20261030-072155`,
  `session-20261102-161821`, `session-20261102-161855` (the last two 34 s apart). The game wrote
  the recording through stdio's buffer, which reached the disk only at `fclose` when the game
  closed; Windows has no line buffering (`setvbuf _IOLBF` is full buffering there), so a crash
  or a kill (Task Manager) lost the whole session. A 0-byte file means the game did not close
  normally (closing the window, Alt+F4 or the menu's Quit all write it); what was played
  then can't be known. The player's earlier "two player mode had crashed" is most likely
  QA-034 (a black race taken for a hang): his three 2-player races (2026-11-03 19:25, 21:20,
  21:52) all closed normally and none before them had 2 players; the 0-byte sessions (Oct 30,
  Nov 2 16:18 twice) can't be told apart: nothing of them reached the disk.
- **Fixed:** every line goes through `recordf` and the file is flushed at the end of each game
  frame that wrote one (before the frame runs), with the notes of the pictures before it; on
  Windows an unhandled exception adds `# crashed at frame N: exception 0x… at <module>+0x…`
  (the module holding the address: the game's .exe, or a driver DLL) and flushes, then lets Windows
  handle the crash as before (`EXCEPTION_CONTINUE_SEARCH`). Check: a live offscreen run killed
  with SIGKILL keeps its lines (HEAD's frontend: 0 bytes); the Windows handler is compiled,
  not run (the game is not started here). Owner: **Engine**.

### QA-037 · S3 · Rio: the Cristo Redentor looks crude (player report)
- (Filed as QA-034 on the landmarks branch, at the same time as the 2-player QA-034 above:
  renumbered at the merge; commit 01fa26a's message says QA-034.)
- Player: "on Rio de Janeiro stage the Cristo Redentor looks horrible". The landmark was four
  flat screen-space shapes in one colour (a trapezoid for Corcovado, a thin bar body, a bar
  for the arms, a half-dot head), no light, no detail (`landmark()`, LM_CHRIST). Owner: **Art/3D**.
- **Status: fixed (Art/3D).** Corcovado, the statue, its pedestal and terraces and Sugarloaf
  modelled in 3D (`tools/make_landmarks.py`), baked for the one side a far landmark is seen
  from (hidden triangles dropped, a painter's order from an id buffer) into
  `src/mod3d/landmark_meshes.h`, lit in the game with the scene's light (`lm_mesh`, `lm_light`),
  hazed, floodlit at night. Before/after renders c04 grid 2700 and start straight 3150 at
  1920x1080 and 3440x1440, dense 2560-3400 every 4 frames (no pop; it leaves the picture on
  the left at ~3330), night with `TG_LANDMARK=5` on New York (TRACKER, "Rio: Christ the
  Redeemer"). Limit: `--set-course` keeps the recording's learned outline (grey mesas) in front
  of Corcovado's foot; in a real Rio race Rio's own green peaks stand there.

### QA sign-off findings (2026-11-03): camera, ghosts, an occluding tree, a board's side

Reported by the QA sign-off in progress; fixed by Engine (part D). Renders `--set-course
1900:N` on session-20261030-081901, frame-60..frame+60 every 20 at 960x540: before
`$SCRATCH/engD/q3before/cNN_F/`, after `$SCRATCH/engD/q3after3/cNN_F/`, sheets (original /
before / after) `$SCRATCH/engD/q3_cNN.png`.

- **S2 · Off the road the camera pitched down; the car cut off at the bottom** (Nice 3300,
  Monaco 3300, Tokyo 3300, Yokohama 3300, Machu Picchu 3900, Black Forest 4500, Stockholm
  4500). Measured (camera log): the pitch sat on its -6.8 deg clamp following the road down,
  while the car's anchor was 21 deg under the view's axis (the half field of view is 17.7):
  the eye's height was the road's 11 m behind the car, not the car's; off the road on the
  inside of a bend the eye, placed on the track at the same offset, came within 4-5 m of the
  car (Tokyo); the camera kept looking along the road ahead while the car was 11 deg to the
  side (Black Forest); far out the camera lagged the car by 2.6 m. **Fixed:** the eye at the
  car's height + 2.9 m (never under the road behind), pitch within +-1.5 deg (a level
  horizon), the eye's horizontal distance from the car always the camera's, the view turned
  towards the car when it is further from the middle than the road's own scroll puts it,
  the car never lower in the picture than on a flat road (the eye comes down instead), the
  camera's sideways lag capped at its value at the road's edge. Nice, Monaco, Tokyo, Black
  Forest after: horizon level, car whole and near the middle, as the original.
- **S2 · Near-camera fades leave big translucent ghosts** (Stockholm 3900, Copenhagen 3300).
  Two causes: props fading over a long half-transparent ramp (3-9 m, or 30-60 % of the
  picture's width; houses never by width), and the see-through ground bands beside the road
  (48 m verges, the field bands) hanging in the air where the road climbs far ahead, stacked
  into pale swirls over the sky. **Fixed:** the near fade is 3.5-6.5 m or 30-42 % of the
  width, eased (smoothstep), for every stage prop; the see-through ground bands fade with
  their height above or below the car's ground (gone at 9 m).
- **S2 · Florence 4500: a dark-green slab hides the car.** The game's own roadside tree
  between the camera and the car (8 m from the camera, not near enough to fade). **Fixed:**
  whatever is nearer than the player's car and covers it on the screen fades by how much of
  the car it hides (gone at 40 %), as third-person games fade what occludes the player; the
  original draws its car over everything.
- **S2 · Rome 3900: the car drives through a chevron board the original shows to its
  right.** The roadside objects' distance from the road was a guess (1.25 + n / 96 half
  widths); the game's own (its collision test, `track_object` in `src/port/drive.c`) is the
  word's low byte x 128 in the car's side units: board `$11BC` stands at 2.35 half widths,
  not 3.21, and the car at its furthest out (-32767) is at 3.20, beyond it. Courses use
  offsets 92-255 (mostly 124 and 188); 255 x 128 is the car's limit. **Fixed** for every
  roadside object and the stage props' occupancy grid (`object_side`).

### QA-038 · S2 · Pit stop: the car on the grass, no pit lane, a black frame in, the view darkened out (owner)
- Owner: "game is almost perfect, but I NEED TO IMPROVE THE PITSTOP, BOTH THE MARKING ON THE SCREEN +
  THE ANIMATION". Before (8ff7a33, Rio `--set-course 1900:4`, session-081901): 4120 one black frame
  (the game's forced blank while it sets its pit scene up), 4160-4560 the car where the game holds
  it (here on the grass beside the road) slowing to 0 with nothing round it, a plain PIT STOP panel,
  ~4560 the whole view darkened (the original fading its pit scene out), the car back on the road at
  4600 (`$PIT/now_sheet.png`).
- **Gameplay presentation (2026-11-04, pit-stop): fixed.** A pit lane where the game has its pits
  (laid out from the pit sign and its divider boards), garages, the stop animated from the game's
  state (in, crew, jacks, wheel guns, fuel while the game's fuel rises, lollipop BRAKE/GO, out, eased
  back onto the game's place), a new pit panel in steps and a pit entry cue, no fades round the
  stop (QA-024). TRACKER: "Pit lane in the world...", "Pit garages...", "Pit stop animated...", "Pit
  stop HUD...". Evidence (`$PIT` = `$SCRATCH/pit`): `$PIT/dense/rio_final.png`, `london_final.png`, `chris03_final.png`,
  before at 8ff7a33 `rio_before.png`, `london_before.png`, pairs `$PIT/rio_before_after.png`,
  `$PIT/london_before_after.png`; passing `$PIT/passing/final_all.png`. Open: no 2-player stop and
  no computer rival's stop in any recording (drawn by the same code, not seen).

## Not tested / limits

- **The player's PC frame time** (GL, present, vsync): the game now logs it (Shift+F2,
  docs/qa/README.md "Frame-time log"); a log from the player's PC is still needed.
- **A 2-player pit stop, the computer rival's pit stop** (the pit lane and its panel in a split view):
  no recording has one (race_2p, race_2p_native and every player session scanned); drawn by the
  same code as player 1's stop.
- **Passwords screen as such, end of championship, game over, night courses
  other than what the recordings reach**: no recording reaches them, or not in a way
  these checks single out (Retro to add recordings; `--set-course` cannot show skylines).
- `chris_session_01` and `chris_session_03` are sampled every 30/60 frames: leaks and
  contact sheets only; no popping, car or flicker checks there.
- `chris_session_*` with `--native-from 2000` (as first planned) **desync** (03 sat in name
  entry for ~100 000 frames), so they run in verify mode.
- Popping / car checks are heuristics (see `docs/qa/README.md` for the limits); counts
  are leads, the before/after pairs are the evidence.
- During the quick run used for timing, a 4th `tg_headless` (a one-off probe) ran for about
  a minute; the speed numbers of that quick run are noisier than usual.
