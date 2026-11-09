# Modern look: QA sign-off

## Re-sign-off, 2026-11-03 evening, HEAD `f77ed95`

Test engineer, focused re-sign-off of the fixes claimed since `a461f23`. No game or renderer
code was changed in this round. Evidence folders (scratch, never in git):

- `$QA3` = `$SCRATCH/qa/runs/full-20261103-170156`
  (full harness: `report.md`, `summary.json`, `sheets/`, `logs/`)
- `$CS3` = `.../scratchpad/qa/courses/20261103-170233` (32 course sheets, 2700-4800 every 300,
  original inset)
- `$RS` = `.../scratchpad/rs` (targeted renders: `c29d/`, `c10d/`, `c17d/`, `c01d/` are
  dense renders every 2-4 frames; `title_classic/`, `kemco3x.png`, `title_logo2x.png`,
  `lod_sheet.png`, `selsheet2.png`, `perf/`, `la095942/`, `lv_sky.png`, `la_9000.png`,
  `la_sign.png`, `s195339sky/`, `head073935/`)

**Images are not comparable with the earlier rounds.** The art agent changed `tg_headless`
to draw the modern look first. Before that change, the look's own fades (the landmark, the
learned skyline columns, the near and occluder fades) never advanced in a headless shot.
Popping and car counts, and anything that depends on a fade, now measure something
different, so they are not a trend.

**Shot spacing matters.** The modern look's eased state (fades, camera ease) advances only
on the frames the headless tool draws. The course sheets take a shot every 300 frames, so
any finding on them that depends on a fade or on the camera was confirmed with a dense
render (every 2-4 frames). For example, Monaco c23 3600 has no car on the 300-frame sheet
but shows the car when shot every 20 frames (`$RS/c23car/`).

### Recommendation

**NO-GO for the beta.** The game logic is still untouched: 2/2 regressions pass, 42/42
replay hashes match with the modern look on, there are 0 leaks in 114 157 shots, and
check_results is 30/30. Six of the claimed fixes hold: the sky, the menu freeze,
LA's sign, the Rome chevron, car headings and the car LOD levels.

The camera off the road is still broken, now in a new way. On hilly stages the camera, and
the car with it, goes **inside the drawn terrain**. The whole picture turns flat green and
the player's car is gone for 1-1.5 s: Yokohama c10 3316-3400+, Sheffield c29 ~3200-3340 and
4760-4806. On a crest the car can also leave the picture or be cut in half by the bottom
edge: Sheffield 4476-4512, Copenhagen c17 4500-4580, Yokohama 3200-3312. A near tree still
fills the whole view for ~0.25 s and hides the car (Los Angeles c01 3604-3620). Any player
who leaves the road on these stages will see this. Fix QA-022 and QA-020 (below). Then rerun
the dense renders cited here, the quick harness and the 32-course sheets. If those are
clean, it is a go.

### What was run

| Run | Result |
|---|---|
| Full harness `run_qa.py --skip chris_session_03 --jobs 2` (`$QA3`) | 5025 s; 42 recordings + 2 regressions; **178 checks pass, 0 fatal, 66 image/speed fail** (34 popping, 30 cars, 2 speed, 0 blank) |
| Regressions `race_1p`, `race_2p` (verify, pinned wram + frame) | PASS (112.6 s, 134.4 s) |
| Replay hashes with the modern look on: **30 player sessions** (`--port native`, every non-empty `recordings/session-*.txt`, 9 more than last round), race_1p/2p, attract, chris_session_01/02, 7 `country_*` | **42/42 PASS** |
| `leaks/`, `blank/` | 0 leaks in 114 157 shots; blank 42/42 PASS |
| `speed/` | 35 PASS; FAIL `race_2p` (p99 24.2 ms, 62 frames over 16.7 ms) and `country_scandinavia` (p99 21.6 ms), both on a machine running 3 tg_headless; no stall over 100 ms anywhere |
| `tools/qa/check_results.py` | session-195339 native 8400, session-095942 native 7400, chris_session_02 verify 8880: 3/3 PASS (30/30 rows) |
| `perf_report.py` on `tg_headless --perf-log` at 3440x1440 with the player's art3d: 3 race starts (session-20261102-173420: 1, session-20261103-073935: 2) | see "Frame time" |
| `course_sheets.py --jobs 1 --from 2700 --to 4800 --every 300`, all 32 | 902 s; all looked at |
| Targeted renders | listed per item below |

#### Frame time (headless CPU, `--perf-log`, 3440x1440)

| Recording | First 10 s of each race: build median / p99 / max | Rest of race: median / p99 / max | Triangles first 10 s med / max | Over 16.7 ms (all) |
|---|---|---|---|---|
| session-20261102-173420 | 3.5 / 8.7 / 11.3 ms | 4.3 / 14.6 / 45.6 ms | 45.4k / 49.7k | 40 |
| session-20261103-073935 (2 races) | 5.0 / 17.8 / 41.0 ms | 4.4 / 16.4 / 96.6 ms | 49.5k / 71.9k | 157 |

There is no texture upload in a race after its first frame (2.7 MB then). The menus' worst
picture is 27-29 ms (capture), with no 0.5 s freeze (QA-025 verified). The race build is
heavier than at `a461f23`: the rest-of-race median went from 2.9-3.1 to 4.3-4.4 ms, and p99
from 7-10.8 to 14.6-16.4 ms. The machine was loaded the same way as last round (a 2-job
harness beside it), so this is a lead for Engine (QA-030), not a verdict. It still needs the
player's Shift+F2 log.

### Previously open items, re-checked

| Id | Verdict | Evidence |
|---|---|---|
| QA-026 sky darkens / night in a day race | **verified** | session-095942 every 20 frames (the spacing that showed night before): Las Vegas 3760-4000 stays day; the sky follows the original's own deep blue at 3780-3840 and LA 9060-9140 (original inset the same blue); no lit windows, no night (`$RS/lv_sky.png`, `la_9000.png`). session-195339 9820-9980: the same, the original's blue then cyan (`$RS/s195339sky/`). Remaining, S4: at LV 3900 the ground near the horizon is near black for one shot while the original's ground turns green |
| QA-025 title -> menu freeze | **verified (headless)** | perf logs: no frame over 30 ms outside races (worst 27.2 ms at 1692 and 29.0 ms at 2686, capture); was 461-725 ms at 841-1021 |
| QA-007 LA hillside sign | **verified** | session-095942: the sign readable on the grid and the start straight 8960-9140 and 10200-10240 (`$RS/la_9000.png`, `la_sign.png`); at 10280-10300 the road has turned away from the sign's fixed direction. Course sheets: c01 2700/3000/4800 show it; the landmark is in view on every course's grid (Tokyo Tower, torii, Golden Gate, Ferris wheel...) |
| QA-023 Rome chevron | **verified** | c25 3900: the board is to the right of the car on the verge, as in the original. New lead QA-029: a board is drawn through the car when the car actually hits it |
| QA-022 camera off the road | **reopened (S2)** | Verified at the cited frames: Machu Picchu c05 3900, Black Forest c14 4500, Tokyo c08 3300, Nice c21 3300, Monaco c23 3300, Stockholm c16 4500 have a level horizon and the whole car. **New failures, confirmed dense:** camera inside the terrain (flat green picture, no car) at Yokohama c10 3316-3400+ (`$RS/c10d/`) and Sheffield c29 ~3200-3340 and 4760-4806 (`$RS/c29d/`, `s29a.png`, `s29c.png`); car out of the picture at Sheffield 4476-4512 (`s29b.png`); car cut in half by the bottom edge over a crest at Copenhagen c17 4500-4580 (`$RS/c17d/`, `s17z.png`) and Yokohama 3200-3312. Also a green terrain wedge across the road at country_uk 9704 (`$QA3/sheets/country_uk_events_01.png`). The original keeps the car on the grass, level, in every one of these |
| QA-020 near props, ghosts, Florence car | **reopened (S2)** | Verified at the cited frames: Florence c27 4500 car visible; Stockholm c16 3900 and Copenhagen c17 3300 have no big ghosts. **Still seen, dense (every 4 frames):** Los Angeles c01 3596-3620, a roadside tree passes between the camera and the car and fills the whole view at 3608-3620, the car hidden (`$RS/c01d/`, `s01a.png`); c01 3896-3904, a building fades through translucent over the left half (`s01b.png`); on the sparse sheets, translucent houses at San Francisco c03 3900/4200 and trees across the car at Nice c21 3600 and Monaco c23 3620 |
| QA-009 popping | **still open (S3), not comparable** | 1251 skyline pops (1173 of 1-2 blocks), 249 roadside pops (83 of 3+), 42 roadside and 5 skyline flickers; 70 events explained by the original; 34 of 42 recordings fail `popping/`. The big events looked at: the START gantry leaving the top (race_1p 4280-4304: known limit), the grid appearing at once at the country_uk race start (6540, QA-032), the posts. Most of the car events are cars entering from below or neon (QA-013/027) |
| QA-024 black frame at a pit exit | not re-run (chris_session_03 skipped) | Engine: the original's own fade of the player's half; QA to judge `blank/` against the player's half in a split picture |

### The team's other claims

| Claim | Verdict | Evidence |
|---|---|---|
| Car headings along the road (was 12 deg off) | **verified by eye** | session-073935 3300-4500 every 100 at 960x540: player and rivals point along the road in the bends at 3700, 3800, 4400 and 4500 (`$RS/head073935/`); LA 9100-9140 the same |
| Car light-LOD continuity | **verified** | showcase of the 4 cars x 2 sides at `TG_CAR_LOD` 0-4 (`$RS/lod_sheet.png`): every level whole, no torn windows or holes. S4: at level 4 a thin dark sliver on the roof (the window edge) on the red and teal cars |
| Classic 3D title (car, gradient, grid, vector logo) | **verified** | no `--input`, `--title-style classic`, 0-1800 every 30 at 1920x1080 (`$RS/title_classic/`): the logo flies in, the car drives in and turns, all fan lines are there, no original frame leaks (690 is the black original between the splash and the title). Logo at 2x (`title_logo2x.png`): clean edges and stems; S4: two thin stray edge lines beside the stars (QA-033) |
| Vector KEMCO splash, PRESENTS from Oxanium | **verified** | `kemco3x.png` (3x): clean outline, bevel and extrusion, PRESENTS crisp; S4: faint triangle seams in the face gradient of K and M (QA-033) |
| Music options and logging | **verified (headless)** | session-20261103-152152, `--music modern --music-switch 5000:synthwave --music-log -`: title faithful (no city version) at 60, stop at 2886, Las Vegas D at 2929, switch to synthwave at 5000, Hiroshima-tune synthwave for LA at 8268; ends on the recorded wram `538407ef...` |
| Car select Left / Right linear | **verified, with a new bug** | `--select-linear` on race_2p_native with scripted presses (`$RS/sel.txt`, `selsheet2.png`): the line ROSSO-GLACIER-VIOLET-AZURE, wraps both ways, UP/DOWN ignored, a fast double RIGHT, RIGHT+START moves then takes the car. QA-028: the first RIGHT (2580, 18 frames after the screen came up) is swallowed by the game but counted, so the next press jumps ROSSO -> VIOLET; a two-step grid move shows the in-between card for 3-5 frames |
| Launcher writes the game's setting names | **code-reviewed, not run** | `full_screen` / `wide_screen` written; old `fullscreen` / `wide` read and win once; the game's save rewrites only its own keys, so the old lines go. The Windows build was not started (rule) |
| Landmarks in a fixed place per city | **verified** | landmark on the grid of all 32 course sheets; LA's sign as above |

### New this round

| Id | Sev | What | Where | Owner |
|---|---|---|---|---|
| QA-028 | S3 | Car select linear: a Left/Right given while the screen has just come up is ignored by the game but counted by the translator, so the next press skips a car; a two-step move shows the in-between card for 3-5 frames | race_2p_native scripted, 2580/2600, 2660-2667 | Engine |
| QA-029 | S3 | A roadside board is drawn through the player's car when the car hits it | session-195339 9940-9960 (LA), Loch Ness c30 4500 | Engine/3D |
| QA-030 | S3 | Race scene build heavier than at `a461f23` (median +40 %, p99 ~+50 % headless); `speed/` fails race_2p and country_scandinavia | perf logs above | Engine |
| QA-031 | S4 | A strip of lit-window dots along the top edge of the night sky | Black Forest c14 3300 (sparse sheet, to confirm dense) | Art/Engine |
| QA-032 | S4 | country_uk race start: the grid and the gantry appear in one frame at countdown "1" | country_uk 6540 (verify, `--native-from 6000`) | Engine |
| QA-033 | S4 | Vector logos: thin stray edge lines beside the title's stars; faint triangle seams in the KEMCO face gradient | `$RS/title_logo2x.png`, `kemco3x.png` | Tech art |

**Count:** 7 previously open items re-checked: **4 verified** (QA-007, 023, 025, 026),
**2 reopened** (QA-022, QA-020, both S2), **1 still open** (QA-009, S3). Of the 8 other
claims, 7 are verified (one of them with new bug QA-028) and the launcher change is
code-reviewed only. **6 new bugs** (QA-028 to QA-033: 3 S3, 3 S4).

### Limits

- Everything is headless CPU. The player's Shift+F2 frame-time log is still missing.
- Course sheets are 300 frames apart, so fades and the camera ease differ from what the game
  draws. The S2 findings above were confirmed every 2-4 frames.
- `--set-course` keeps the recording's learned skyline and pictures.
- chris_session_03 was skipped (QA-024 not re-run).
- The Windows game and launcher were not started.

---

# Earlier round

Test engineer, final round, 2026-11-03. Build: `mod/3d-renderer` HEAD `a461f23` (tg_headless
built from it; no game or renderer code changed in this round). Evidence folders (scratch,
never in git):

- `$QA2` = `$SCRATCH/qa/runs/full-20261103-102059`
  (full harness: `report.md`, `summary.json`, `sheets/`, `logs/`)
- `$SO` = `$SCRATCH/signoff`
  (perf logs, targeted renders); 32 course sheets in
  `$SCRATCH/qa/courses/20261103-103258`

## Recommendation

**NO-GO for "finished, ready for the player"; GO for one more play-test build.**

The game is untouched and nothing of the original shows through: every hash matches, no leak
in 94 658 shots, every menu and end screen is redrawn, and the results figures are right.
Menus, results, ranking, loading cards, pit stop, name entry, the attract demo and the 32
stages all look finished. Speed holds the 16.7 ms budget in every native replay, also at the
player's 3440x1440.

Four things a player meets in an ordinary race still look broken (S2, below). Each one is
local and should not take long to fix. After those four are fixed, rerun the quick harness and
the 32-course sheets, then get one Shift+F2 frame-time log from the player's PC. If those
are clean, it is a go.

## What was run

| Run | Result |
|---|---|
| Full harness `python3 tools/qa/run_qa.py --jobs 2` **including `chris_session_03`** | 6583 s; 34 recordings + 2 regressions; **138 checks pass, 0 fatal, 61 image/speed checks fail** (29 popping, 28 cars, 3 speed, 1 blank) |
| Regressions `race_1p`, `race_2p` (verify, pinned wram + frame) | PASS |
| Replay hashes with the modern look on: 21 player sessions (`--port native`, every `recordings/session-*.txt`), race_1p/2p, attract, chris_session_01/02/03, 7 `country_*` | **34/34 PASS** |
| `leaks/` (original graphics shown) | **0** in 94 658 shots |
| `blank/` | 1 fail: `chris_session_03` 41640, 72900 (verify only; QA-024) |
| `speed/` | PASS on all 21 player sessions and 7 countries (14 sessions newly passing). Fails only in the verify-mode chris sessions. A solo rerun (`full-20261103-121122`) clears chris_session_02 and leaves one stall in chris_session_01, 377 ms at its first race (2100). The other spikes (up to 2.4 s in chris_session_03) came from a machine loaded with up to 4 tg_headless (see Limits) |
| `tools/qa/check_results.py` | session-195339 native 8400, session-095942 native 7400, chris_session_02 verify 8880: **30/30 rows** = original = WRAM; PASS ×3 each |
| `tools/qa/perf_report.py` on `tg_headless --perf-log`, 3440x1440, player's art3d, 5 race starts in 3 recordings | see "Frame time" |
| 32 courses, `tools/qa/course_sheets.py` (`--set-course 1900:N` on session-20261030-081901, 2700-4800 every 300, original inset) | looked at all 32; new bugs QA-022/023, QA-020 reopened |
| Targeted renders | native pit stop with refuel (Rio), Frankfurt bubble/near fade, name banner, R-12 end screen, Las Vegas/LA skies, LA's sign, blank frame, menu-hitch frames |

### Frame time (headless CPU, `--perf-log` at 3440x1440)

| Recording | Races | First 10 s of each race: build median / p99 / max | Rest of race: median / p99 / max | Triangles first 10 s med / max |
|---|---|---|---|---|
| session-20261103-073935 (warm) | 2 | 3.9 / 12.6 / 22.9 ms | 3.1 / 10.8 / 29.0 ms | 46k / 72.8k |
| session-20261102-173420 | 1 | 3.2 / 7.2 / 14.2 ms | 2.9 / 7.0 / 15.4 ms | 44k / 51k |
| session-20261102-195339 | 2 | 3.2 / 8.1 / 13.5 ms | 3.2 / 9.0 / 17.3 ms | 47k / 67k |

No texture upload in a race after its first frame (2.7 MB at the race's first frame). The
start-of-race slowdown is gone on the CPU side, but there are two notes. The first-10-s triangle
count (46k median, 73k max) is higher than the 38k / 53k in the tracker. The only stall is the
menu hitch QA-025 (0.5-0.7 s, 6.1 s on a cold disk). `perf_report.py` could not read headless
logs (`KeyError: 'width'`); fixed in this round.

## Verified (fixed items re-checked on this build)

**BUGS.md: 11 verified, 3 reopened.** Verified: QA-001, 002 (native), 003, 004, 005, 006, 008
(headless), 010, 012, 019 (HUD), 021. Reopened: QA-007 (LA sign hidden), QA-009 (popping),
QA-020 (near props). **MUST-FIX: 11 verified** (R-01 now also native with a refuel, R-02,
R-03, R-04, R-06, R-07, R-08, R-09, R-10, R-11, R-12), none reopened; R-05 partly, R-15
decided. Evidence per item in `docs/qa/BUGS.md` ("QA sign-off" lines) and
`docs/retro/MUST-FIX.md` ("QA sign-off" table).

Also seen working on the sheets: KEMCO/title/menus/password/name entry/gear/control/car
select all modern; the results and ranking slide; the GET READY card; the countdown, GO, lap
and position messages, taunt bubbles and name banners in the modern HUD; the START gantry,
grandstands and flags; night courses lit; the player's car in its chosen colour (white
Glacier); the 2-player split and the two-view attract demo.

## Open, prioritised

### S2: fix before "ready for the player"

| Id | What | Where | Owner |
|---|---|---|---|
| QA-022 | Camera tilts into the ground and drops when the car is off the road or over a crest; the player's car is cut off by the bottom edge or leaves the picture (the original stays level) | c05 3900, c14 4500, c08/c10/c21/c23 3300, c16 4500 | Engine |
| QA-020 (reopened) | A near prop fills half the view and hides the car; big translucent ghosts of fading props over the road | Florence c27 4500; Stockholm c16 3900; Copenhagen c17 3300 | Engine |
| QA-026 | In a day race the sky darkens to deep blue, or to night with lit windows, for about 1 s. What appears depends on which frames the look learned from, so the player's view needs confirming | session-095942 3840-3900 (Las Vegas), 9120 (LA); session-195339 9900 | Engine |
| QA-025 | 0.5-0.7 s freeze (6 s on a cold disk) when the title is left for the menus, every session | perf logs frame 841/844/1021 | Engine (UI) |

### S3: should fix, not blocking

- QA-009 (reopened): popping is still flagged after motion compensation. 1084 skyline pops (mostly
  1-2 blocks) and 228 roadside pops. The ones you notice: LA skyline towers appearing at the
  start (session-095942 8532, 8920), a ground patch (10090).
- QA-007 (reopened): the LA hillside sign is hidden behind the stage's towers (Art/3D).
- QA-023: the car drives through a chevron board (Rome c25 3900) (Engine/3D).
- QA-024: one black modern frame at a pit-lane exit (chris_session_03 72900, verify only) (Engine).
- QA-011: cars passing through each other in a pack (open since the first round).
- QA-019 remainder: the KEMCO trackside banners (Hiroshima/Tokyo) are still pixel pictures (Art).
- Verify-mode first race of chris_session_01: 377 ms stall at 2100 (the player's native game
  does not have it).

### S4 / nice-to-have

QA-013 and QA-027: the car-colour check counts billboards and neon (300 events, the samples
looked at are all false positives), so it needs a mask (QA). Also: QA-014 (posts in one shot),
QA-016 (cloud steps), QA-017 (DEMO label), QA-018 (blinking messages, UI decision), R-13
(clock +0.05 s), R-14 (NEXT RACE map empty for 0.5 s, invented car names), warm/orange
skies, authored skylines per course (R-05).

## Known limitations

- **No frame-time log from the player's PC yet.** All timings are headless CPU (scene build +
  capture), with no GL submit, present, vsync or GPU time. The game's Shift+F2 log
  (`docs/qa/README.md`) is the missing evidence for QA-008 and the start-of-race slowdown.
- **Untested states:** 2-player RESULTS/RANKING with both names, 2-player menus as a
  whole, the ending and credits, game over, pause, end of country with the password in
  native mode, the passwords screen against the original. The native pit stop with a refuel
  has been seen, but only through `--set-course` (the player did not record one). `--set-course`
  keeps the recording's skyline graphics, so the learned skylines of the 32 course sheets
  are not each course's own.
- **Sampling:** chris_session_01/03 are shot every 30/60 frames (no popping/car checks there);
  QA-026 shows that what the modern look learns depends on which frames are shot, so headless
  renders are not exactly what the game draws from every frame.
- **Load during this run:** the course sweep, perf logs and targeted renders ran beside the
  2-job harness. For a few minutes there were 4 tg_headless at once: two perf logs started in
  parallel by mistake, one over the 3-process limit. That explains the verify-mode speed spikes,
  and the solo rerun confirms it for chris_session_01/02. Image and hash checks are not
  affected (deterministic).
- Popping/car checks are heuristics: the counts are leads, and the before/after pairs are the
  evidence.

## Tools changed in this round (QA only)

- `tools/qa/perf_report.py` reads `tg_headless --perf-log` files too (CPU columns; the rest
  shown as not measured).
- `tools/qa/course_sheets.py` (new): contact sheets of all 32 courses with the original as an
  inset, at most 3 tg_headless, PPMs deleted after the sheet.
