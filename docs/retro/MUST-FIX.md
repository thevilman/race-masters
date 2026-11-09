# Must-fix list: fidelity and readability (retro audit)

Retro gaming expert, 2026-11-02, branch `retro-audit` at `b1589b2`. Evidence and frames are
in `docs/retro/COVERAGE.md` and `docs/retro/CITIES.md`; QA ids refer to `docs/qa/BUGS.md`.
Owners are the roles of `docs/MODERN-TEAM-PLAN.md`. Ordering rule: **first what makes the
player misread the game's state or lose information he needs to play (positions, times,
points, qualification, fuel), then what shows the wrong place, then polish.**

Priority: **P1** the player is misinformed or cannot see something he needs; **P2** the
place or mode is visibly wrong against the original; **P3** fidelity polish.

| Rank | Id | P | What | Evidence | Owner (second) |
|---|---|---|---|---|---|
| 1 | R-01 | P1 | **Pit stop shows a junk menu over the whole screen** instead of the race/pit scene: no fuel gauge, position or race during the stop | C3 72610–72760 (`pit_c03/pair_072680.png`); COVERAGE row 23 | Engine (UI for a modern pit overlay) |
| 2 | R-02 | P1 | **WORLD RANKING never redrawn**: the old RESULTS table stays up; the championship order that decides qualification is never shown | S195 8760, PPU dump: BG1 vScroll 255, rows 38–57; COVERAGE results item 3 | Engine (capture BG1 scroll) + UI (ranking layout) |
| 3 | R-03 | P1 | **RESULTS: gaps printed as times under TIME** (the player's complaint) and the player's row not marked unless named PLAYER | S195 8400; `view3d.c:6048–6081`, `6058`; data `$1F12`/`$1F26` | UI |
| 4 | R-04 | P1 | **2-player race drawn as one view with sprite cars** when entered from the menus, and in the attract demo | QA-001 | Engine |
| 5 | R-08 | P1 | **Race-loading junk menu ("KLM/NO/H/J") and the OPTIONS→race photo leak** | S095 8178 (BG1 = race tiles, original black); CF 6056–6160 (BG1 empty); QA-003/010 | UI + Engine (`capture_menu` guard) |
| 6 | R-05 | P2 | **Every course's skyline missing, generic or late**: the original shows a recognisable horizon from the grid on all 32 courses (dunes, LA's hillside sign, Sugarloaf, Golden Gate, Bay Bridge, torii in the sea, Colosseum, Big Ben…); LA shows Las Vegas' water tower | CITIES.md (all 32), QA-006, QA-007 | Art + 3D (authored skyline per course) + Engine (load at race start, by `$1F06`) |
| 7 | R-06 | P2 | **Time of day ignores the game**: Black Forest and Florence start at night, Hiroshima at dawn, Cologne/Sicily fall to night — the modern look shows daylight (on invented white snow) | `--set-course` 9, 13, 14, 27; COVERAGE row 25 | Engine (follow the palette script) + Art (night/dawn looks) |
| 8 | R-07 | P2 | **Previous course's ground/sky at the race start** (desert in Rio, Munich, London, Tokyo, Stockholm grids; whole races in desert in verify runs) | QA-005; CITIES.md | Engine |
| 9 | R-09 | P2 | **Sky colour**: the original's skies are clear saturated cyan (royal blue, gradient on two courses); the modern skies are pale grey/lavender with big clouds everywhere — the biggest mood change | CITIES.md notes | Art |
| 10 | R-10 | P2 | **Roadside kits per country lost or swapped**: no palms in LA, round trees instead of spruces + no warning triangles in Stockholm, no vineyards/cypresses in Bordeaux, no forest walls in Kyoto/Rain Forest/Black Forest-night | CITIES.md | 3D + Art |
| 11 | R-11 | P2 | **START gantry** without the START banner, chequered flags and traffic-light pods | LV 3600, every course | 3D |
| 12 | R-12 | P3 | **Failed-to-qualify scene**: the original's driver + "PLAYER 1 nTH" label replaced by the car; the place is lost | C1 8250 | UI |
| 13 | R-13 | P3 | **Race clock +0.05 s** against the original at the same frame | LV 3600, LA 11380 | UI |
| 14 | R-14 | P3 | **NEXT RACE map panel empty for ~0.5 s**; **invented car names** (ROSSO…); 2P name entry does not say which player | S095 8100, CG 6000; `view3d.c:5127`; R2 2952 | UI |
| 15 | R-15 | P3 | **1-player split view of the original** (rival COMPUTER below) not kept: decide and document | CS 6000 (verify) | Art director (style guide) |
| 16 | R-16 | — | **Recordings for the unreached screens**: native pit stop, end of country + password, ending, pause, 2P results/ranking, full day/night races | COVERAGE "Recordings still needed" | QA (Retro to play them) |

## Status

- **R-03 fixed (UI)**, with R-02: results and ranking drawn from WRAM; see `docs/TRACKER.md`
  "Results and standings". The ranking carries no top-3 cut: the code only gates on a player
  finishing in the first five of each race (`MenuCheckQualified`, `$0F:871A`), so that is the
  line drawn (on RESULTS) and the outcome stated (on both pages).
- **R-02 fixed (UI)**: BG1's scroll is now read (`bg1_at`), the page is chosen by it.
- **R-08 fixed (UI)**: BG1 tile-base guard ($1000) and a GET READY loading card between a race-starting menu and the race (QA-003, QA-010).
- **R-12 fixed (UI)**: the failed-to-qualify (and other end) screens show the player's name and place again.
- **R-05 partly (Art/3D)**: every course has its landmark on the skyline now (32 of 32), and
  a 3D stage kit of its city; the far skyline is still learned from the screen, not authored
  per course (open).
- **R-09 fixed (Art)**: the sky keeps the game's saturated cyan to the horizon, fewer clouds
  (`docs/STYLE.md`).
- **R-10 fixed (3D)**: each country's roadside kit in 3D (palms in LA, spruces, warning
  triangles and red houses in Scandinavia, vineyards and cypresses in Bordeaux, forest walls in
  Kyoto, Rain Forest and Black Forest); see `docs/TRACKER.md` "Stages per country".
- **R-11 fixed (3D)**: the START gantry with banner, chequered ends and start lights.
- **R-15 decided (Art)**: one full view in 1-player races; the rival's progress is on the
  HUD map (`docs/STYLE.md`).
- **R-01 fixed (UI)**: a pit stop no longer drops out of the race (no junk menu); the race view continues with a modern pit panel and the fuel gauge. Checked in verify mode only (C3 72560–73200); a native pit stop with a real refuel is still to record.

## QA sign-off (2026-11-03, HEAD `a461f23`)

Re-checked on the current build; evidence paths as in `docs/qa/BUGS.md` "Sign-off" (`$SO`, `$QA2`).

| Id | QA status | Evidence |
|---|---|---|
| R-01 | **verified** (now also native) | native pit stop **with a refuel** found on Rio (`--set-course 1900:4` on session-081901): 4200-4800 every 30 frames, PIT LANE, the stop, gauge filling to 100 % FULL, GO GO GO, panel fades, race view throughout (`$SO/pit04.png`); verify: chris_session_03 42120, 82680, 85800 panel over the race. Minor: QA-024 (one black frame at a pit exit, verify) |
| R-02 | **verified** | WORLD RANKING pages: session-095942 7800-7920, session-195339 8700-8760, chris_session_03 29640, 57720, 174720, 187200, 205920, 251160 |
| R-03 | **verified** | `tools/qa/check_results.py`: session-195339 native 8400, session-095942 native 7400, chris_session_02 verify 8880: 30/30 rows equal to the original and WRAM, times non-decreasing |
| R-04 | **verified** | = QA-001 |
| R-05 | partly (unchanged) | every course has its landmark and kit; LA's hillside sign hidden by the stage's towers (QA-007 reopened) |
| R-06 | **verified** | course sheets c09 Hiroshima dusk/night, c14 Black Forest night then day, c27 Florence night with lamps, c13 Cologne day (`$SO/night.png`); but see QA-026 (a day race darkening for ~1 s) |
| R-07 | **verified** | = QA-005 |
| R-08 | **verified** | `leaks/` 0 over 94 658 shots; GET READY card before every race on every sheet |
| R-09 | **verified** | cyan skies on all 32 course sheets and every session |
| R-10 | **verified** | per-country kits on the 32 sheets (palms LA, spruce + triangles Scandinavia, vineyards Bordeaux, forest walls Black Forest) |
| R-11 | **verified** | START gantry with lights and chequer: session-173420 2878, country_france 7440, all grids |
| R-12 | **verified** | chris_session_02 9120: RACE OVER, the player's name and place, the failed message, against the original's name-and-place line (`$SO/r12.png`) |
| R-13, R-14 | not re-checked | still open (P3) |
| R-15 | decided | one full view (STYLE.md) |
| R-16 | partly | native refuel now seen via `--set-course`; still unrecorded: ending, pause, 2P results/ranking, end of country + password in native |

## Notes per item

- **R-01 Pit stop.** `tg_view3d_capture` treats a pit stop as "not in a race"
  (`view3d.c:1740`), and the menu capture then runs every frame (`view3d.c:1708`) on the
  HUD's tiles. Minimal fix: no menu capture while `$1E86` (or `$1E88` in 2P) is negative,
  and keep drawing the race view; the modern pit scene (car in the lane, crew, the fuel
  gauge filling) is the UI/3D follow-up. Check it in **native** mode with a new recording
  (R-16) — the only pit seen is in verify mode.
- **R-02 / R-03 Results and ranking.** Read the tables from WRAM, not by OCR: order
  `$1F12`, absolute times `$1F26 + 2·driver` (frames; strictly increasing after
  `MenuDrawRaceResults`), race points `$0E:8024 + place`, championship points `$1F4E +
  driver`, ranking `$1F62`, names `$1F86`/`$1FA3` (players) and `$0D:C47B` (drivers).
  Use BG1 (with its scroll) only to know which page is up. RESULTS: POS · DRIVER · TIME
  (absolute) · GAP (`+6.43`, muted) · PTS, the player's row(s) highlighted by driver id.
  WORLD RANKING: 20 rows, POS · DRIVER · PTS, players highlighted, after race 3–4 the top-3
  cut marked ("qualifies for the next country").
- **R-05 Skylines.** Ship one far skyline per course (32), authored from the original's
  strip (the strip pictures are exported to the scratch folder, never into git) and the
  real city (CITIES.md column 6). Load it by course number at race load, so it is there on
  the grid; keep it a far, flat layer like the original.
- **R-06 Time of day.** The day/night switch is in the game's palette script at fixed
  times (RE `docs/03`); take the time of day from the course number and the palette, not
  from "how dark the screen looks", and drop the white snow ground outside Scandinavia.
- **R-08 Menu guard.** Accept a BG1 as a menu only when its tile base is the menu font's
  (`$1000` in every menu dump seen; `$0000` while the race loads) and the screen is lit;
  when the capture fails but a modern menu was up a moment ago, fade it out rather than
  showing the original.
- **R-09/R-10/R-11** belong in the art director's style guide: "clear saturated sky,
  recognisable flat horizon, the country's roadside kit, the START gantry" are the four
  things that make a Top Gear course read as Top Gear.

## Not checked (no recording reaches it)

End of country (qualified / failed + password), the ending and credits, pause, a pit stop
in native mode, 2-player RESULTS/RANKING with both names, the second half of the day/night
courses. See COVERAGE.md "Recordings still needed".
