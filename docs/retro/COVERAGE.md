# Coverage: every screen and state of the original against the modern look

Retro gaming expert, audit of 2026-11-02, branch `retro-audit` at `b1589b2` (QA harness on
top of `ba9c35c`). Build: `cmake -S . -B build-retro -DCMAKE_BUILD_TYPE=Release
-DTG_BUILD_FRONTEND=OFF && cmake --build build-retro --target tg_headless`. No game or
renderer code was changed.

`$SCRATCH` below is
`$SCRATCH`
(renders, PPU/WRAM dumps and exported pictures; nothing from the ROM is in git).

**How each row was checked.** `tg_headless … --shots3d 960x540 --art3d <art>` (640x360 for
the long runs): a shot with `_sel.ppm` is a redrawn menu, `_3dx.ppm` the modern race,
neither = the original picture is what the player sees. Recordings: the player's
`recordings/session-*.txt` (`--port native`), `tests/inputs/*` (`--port verify`, the
`country_*` with `--native-from 6000`). Where a frame number is given it is a game frame of
that recording. The QA contact sheets of the full run
(`$SCRATCH/qa/runs/full-20261102-185250/sheets/`) were used to find the screens; every
finding below that is not in `docs/qa/BUGS.md` was confirmed with my own render or PPU/WRAM
dump (scratch: `$SCRATCH/retro/`). Screen order and facts about the original come from the
reverse-engineering notes of the USA ROM
([top-gear-snes-re](https://github.com/pedro-walter/top-gear-snes-re), `docs/03`, `docs/10`,
`docs/13`), [Wikipedia](https://en.wikipedia.org/wiki/Top_Gear_(video_game)),
[Playing With Superpower](https://playingwithsuperpower.com/top-gear-review/) and the C port's
own routines (`src/port/menu_*.c`).

Status: **redrawn** = modern screen, faithful; **redrawn, wrong** = modern screen with a
correctness problem; **original** = original picture shows; **not redrawn** = a modern
screen shows but not this one's content; **not reached** = no recording gets there.

## Counts

33 screens/states of the original (rows of the matrix below):

| Status | Count | Rows |
|---|---|---|
| redrawn, faithful | 20 | KEMCO, title, options, skill, country, password, 2P setup, name entry, controls, gears, car select, NEXT RACE, 1P race, HUD, race messages, speech balloons, 2P split (race_2p), night (New York), failed to qualify, game over |
| redrawn, wrong | 5 | attract demo (one view), race-loading junk menu, 2P races from the menus (one view), day/night changes, RESULTS (gaps as times, player not marked) |
| not redrawn (something else shows) | 2 | **pit stop** (junk menu over the whole screen), **WORLD RANKING** (old RESULTS stays up) |
| original shows (leak) | 1 | OPTIONS → race photo with the cursor pill (QA-003) |
| original black, correct | 1 | power-on |
| not reached by any recording | 4 | end of country (qualified/failed + password), ending, pause; (a separate "qualified for next race" screen does not exist) |

Courses: **32 of 32** looked at in the original and the modern look (9 from recordings, 23
with `--set-course 1900:N`); see `CITIES.md`. The modern look has the course's recognisable
skyline in **none** of them at the moment it matters (from the grid): missing, generic or
late in all 32.

## The matrix

Recordings: **A** `tests/inputs/attract.txt` (verify, no input) · **C1/C2/C3**
`tests/inputs/chris_session_01/02/03.txt` (verify) · **R2** `race_2p.txt` (verify) ·
**CF/CJ/…** `country_france/japan/…` (verify `--native-from 6000`) · **S195** player
`session-20261102-195339` · **S095** `session-20261030-095942` · **S081**
`session-20261030-081901` (all player sessions `--port native`) · **CF/CI/CSc/CS/CG/CJ/CU**
`country_france/italy/scandinavia/s_america/germany/japan/uk` · **A** also covers the
title idle.

| # | Screen / state (original) | Reached by | Modern look | Status | Notes (lost information, correctness) |
|---|---|---|---|---|---|
| 1 | Power-on (black) | A 0–180 | original black | ok | nothing to draw |
| 2 | KEMCO "presents" splash | A 204–780; after every game over (C1 8490) | `_sel` KEMCO card | **redrawn** | — |
| 3 | Title: Mode 7 logo, licence/Kemco/Gremlin lines, PRESS START | A 816–2340 | `_sel` logo + red car | **redrawn** | copyright/licence lines reduced to a small footer; fine |
| 4 | Attract demo race (split screen, "DEMO") | A 2404–7200; idle title in C3 | `_3dx` one full view | **redrawn, wrong** | QA-001 (one view instead of two), QA-017 ("DE MO") |
| 5 | Options: 1 PLAYER/2 PLAYERS, SKILL LEVEL, COUNTRY, SPEED, PLAY GAME | S195 500–920, C1 1320 | `_sel` OPTIONS cards | **redrawn** | — |
| 6 | Skill level (Amateur / Professional / Championship) | S195, `skill_*` inputs | inside the OPTIONS card | **redrawn** | — |
| 7 | Country select (flags) | CF 4140 | `_sel` COUNTRY + flags | **redrawn** | — |
| 8 | Password entry (8 letters + keyboard) | CF 3540–4080, C3 32760, 65520 | `_sel` PASSWORD keyboard | **redrawn** | — |
| 9 | 2-player setup (PLAYER 1 / PLAYER 2 / EXIT) | R2 1872, 2736 | `_sel` PLAYER SETUP | **redrawn** | — |
| 10 | Name entry | S195, R2 1944–2232 (P1), 2952–3168 (P2) | `_sel` ENTER NAME keyboard | **redrawn** | header does not say which player is typing in 2P (original's label row is the player's name/number) — check with UI |
| 11 | Control method (4 layouts) | R2 2484, 3456 | `_sel` CONTROL METHOD pad | **redrawn** | — |
| 12 | Gear select (AUTO / MANUAL) | R2 2248–2448 | `_sel` GEAR SELECT | **redrawn** | — |
| 13 | Car select (4 cars, MAX SPEED / 0-60 / GRIP / FUEL) | R2 2592, CF 4992 | `_sel` CHOOSE YOUR MACHINE + 3D car | **redrawn** | names ROSSO/GLACIER/VIOLET/AZURE are invented (`view3d.c:5127`); the original shows no names (fans call them Cannibal/Sidewinder/Razor/Weasel) |
| 14 | NEXT RACE / LOCATION: city, country, track map, distance, lap record, laps | S195 8880, S095 7970–8100, C1 1890 | `_sel` NEXT RACE | **redrawn** | the TRACK panel is empty for the first ~0.5 s (S095 8100, CG 6000) while the original's map is already there |
| 15 | OPTIONS → race: photo with the PLAY GAME cursor pill, text cleared | CF 6056–6160, CI 6020–6116, CSc 6096–6196 | none | **original (leak)** | QA-003. BG1 is empty (`ppu` at CF 6120), `capture_menu` finds < 6 letters and returns false, the original shows ~1.7 s. In these runs NEXT RACE is skipped (country chosen by password; may be a `--native-from` effect — the player's own sessions always show NEXT RACE) |
| 16 | Race loading (black, BG1 holds race tiles) | S095 8178, CF 6220 | `_sel` junk menu "KLM / NO / H / J" | **redrawn, wrong** | QA-010 identified: the original is **black**; BG1 map `$0C00`, tile base `$0000` = race graphics whose tile numbers 1–36 OCR as letters. `capture_menu` should require the menu's BG1 tile base (`$1000`) / a lit screen |
| 17 | Race, 1 player | every race | `_3dx` | **redrawn** | in verify mode the original 1P race is a **split screen** (top: player, bottom: the COMPUTER rival with its own HUD; CS 6000); the C port's native mode and the modern look show one full view: the rival's view is gone (a design decision; say so in the style guide) |
| 18 | Race HUD: speed, time, nitro count, gear, position (blinking), map, fuel gauge | every race | modern HUD panels | **redrawn** | all items present (position and lap become permanent — good). Race time differs by ~0.05 s from the original's at the same frame (LV 3600: 00'02"05 vs 00:02.10; LA 11380: 00'30"55 vs 00:30.60) — check the conversion (the game shows frames×100/60 truncated) |
| 19 | Race messages: 3-2-1-GO, positions, FINAL LAP, n LAPS LEFT, FUEL LOW, OUT OF FUEL, RACE OVER, pit arrows | every race | modern text (`message_text`) | **redrawn** | blink kept (QA-018) |
| 20 | Speech balloons (the rivals' taunts on overtakes and bumps) | LA 9620, LV 7200 | the game's picture over the 3D | **redrawn (picture)** | kept as pixel art; fine |
| 21 | 2-player split race | R2 | `_3dx` two views | **redrawn** | no skyline in either view (QA-006) |
| 22 | 2-player race entered from the menus of a player session | C2 3060–8712, S095 8182–11160 | `_3dx` one view + sprite cars | **redrawn, wrong** | QA-001 |
| 23 | Pit stop (pit scene, refuelling) | C3 ~72660, 81900, 85620 (Tokyo, 7 laps) | `_sel` junk menu "JKL / D / P Q R S T U V" | **not redrawn (junk)** | Confirmed in a render of C3 72500–72760 (`$SCRATCH/retro/export/pit_c03/shots/pair_072680.png`): the original shows player 1's **pit-lane scene** (overhead lane, tyres, the HUD with fuel gauge) in the top half and the race (COMPUTER) below; the modern look replaces the **whole screen** with a menu built from the HUD's tiles. Cause: `tg_view3d_capture` sets `in_race` false while `$1E86` is negative (`view3d.c:1740`), and then `capture_menu` runs on BG1 (`view3d.c:1708`) — the comment at 1659 ("the original's pit scene is shown as it is") is no longer true. The player loses the refuelling (fuel gauge), his position and the race for the whole stop (a full fill is ~340 frames, 5.7 s). Only seen in verify mode (no native recording pits) |
| 24 | Night race | New York (course 2): no recording; `--set-course 1900:2` | `_3dx` | **redrawn** | night sky, lamps, headlights drawn; the original's **lit-window skyline** is missing (none by 4200). Courses that *start* at night (Hiroshima dawn, Black Forest, Florence) are drawn in daylight on white snow — see row 25 and CITIES.md |
| 25 | Day→night / night→day (courses 9, 13, 14, 26, 27) | `--set-course 1900:N` on S081 (13; 9, 14, 27 at 4200) | `_3dx` stays day | **redrawn, wrong** | the original turns night at ~47 s (S081+13 at 6000: dark blue sky, dark ground); the modern look stays in daylight, with a white glare ground (`$SCRATCH/retro/cologne/pair_006000.png`). Hiroshima (red dawn), Black Forest and Florence (night starts) are drawn in daylight on white snow at 22 s. The time of day must follow the game's palette script, not a guess |
| 26 | RESULTS (10 rows: name, time / +gap, race points) | S195 8120–8700, S095 7166–7920, C1 8010 | `_sel` RESULTS table | **redrawn, wrong** | see "The results table bug": gaps read as times; player not highlighted unless named PLAYER |
| 27 | WORLD RANKING (20 drivers, championship points; BG1 scrolled) | S195 ~8700–8860, after every RESULTS | `_sel` still the RESULTS table | **not redrawn** | see "The results table bug" item 3 |
| 28 | Failed to qualify for the next race ("SORRY…", driver leaning on the rail) | C1 8250, 47700; C2 9044; C3 20940 | `_sel` RACE OVER card, red car | **redrawn** | the original's message scene (the driver, PLAYER 1 + place label) is replaced by the car; the place label is lost |
| 29 | Qualified for the next race (between races 1–3) | — (the recordings that finish top 5 go straight to NEXT RACE; no separate screen seen) | — | not reached | per the RE notes the "congratulations" scene comes only at the end of a country |
| 30 | End of country: qualified + PASSWORD / failed + password | no recording | — | **not reached** | the password shown there is information the player must write down: the redraw must show it large |
| 31 | Ending (the championship's congratulations), credits | no recording; reached by `--set-course 8400:31` on session-20261102-195339 cut after 8800 (the race's results with the course on the UK's last: MenuAdvanceCourse wraps to 0 and the game shows its ending) | 8808+ | **redrawn** (draw_champion) | |
| 32 | Game over (= back to KEMCO and title, points reset) | C1 8370–8640, C2 9272+ | KEMCO, title | **redrawn** | no separate game-over screen exists in the original |
| 33 | Pause | no recording | — | **not reached** | `RacePauseCheck`; check that the modern look shows a paused state |


## The results table bug (the player won; the table "showed the others with better times")

Recording `recordings/session-20261102-195339.txt` (`--port native`, 11380 frames, ends on
its recorded wram hash `157e6005…`). It has **one** finished race: Las Vegas, which the player
wins (race over ~7764). The second race, Los Angeles, is cut off in lap 1 at 11380, so the
table in question is the Las Vegas one: RESULTS 8120–~8700, then WORLD RANKING ~8700–8860,
then NEXT RACE Los Angeles. Shots: `$SCRATCH/retro/res195339/pair_008400.png` (results)
and `pair_008760.png` (ranking), original on the left, modern on the right.

**What the original draws** (`MenuDrawRaceResults`, `src/port/menu_results.c`, the
original `$0F:882D`): ten lines on BG1, two tile rows each (map rows 6, 8 … 24), names at
column 2, times at column 13, points at column 23:

- the winner's row: the name, dotted leaders, the race time, the points;
- every other row: the name, dotted leaders, the gap to the winner (with `+`), the points.

The data behind it, read from WRAM at frame 8761 (`$SCRATCH/retro/ppu195339/wram8761.bin`):

| Where | What |
|---|---|
| `$1F12`–`$1F1B` | finishing order, driver ids (0 = player 1, 1 = player 2 / "COMPUTER" in 1P, 2–19 the named drivers): `0 1 3 19 12 5 10 13 2 4` |
| `$1F26 + 2·driver` (16 bit, frames at 60/s) | each driver's **absolute** race time, made strictly increasing by `MenuDrawRaceResults`: the player 4248 (1'10"80), the computer 4251 (1'10"85), the third 4634 (1'17"23) … the tenth 5194 (1'26"57) |
| `$0E:8024 + place` (ROM) | race points by place: 20 15 12 10 8 6 4 3 2 1 |
| `$1F4E + driver` (8 bit) | **championship** points (after this race) |
| `$1F62`–`$1F75` | championship order (20 drivers), sorted by `MenuDrawStandings` |
| `$1F86`, `$1FA3` (8 chars) | the players' names (8 characters, space-padded); `$1F04` = 1 in a 2-player game |

**What the modern table does** (`draw_results`, `src/mod3d/view3d.c:6027`, fed by
`capture_menu`, `view3d.c:1379`): it OCRs BG1 rows 3–27 into cells (`'`, `"`, `+` and the
dots are not letter tiles, so `+0'00"05` becomes the cell `0 00 05`), and prints row *r*'s
cell 1 under the header **TIME** as `1:10.80` for row 0 and `+0:00.05`, `+0:06.43`, … for
the others. The numbers are copied correctly — the data is not wrong — but:

1. **Gaps are shown as times.** In a column titled TIME, `1:10.80` sits above `0:00.05`,
   `0:06.43`, `0:09.23` … in the same size and colour, with only a thin `+` in front. Read
   as times, every other car looks **faster** than the winner — exactly what the player
   reported. The original gets away with it because its `+0'00"05` follows a dotted
   leader and the gap is the only number; in a modern table with a TIME header it reads as
   a lap/race time. The right data is in WRAM: absolute time = `$1F26 + 2·$1F12[place]`
   (or winner time + gap). Fix (UI): a **TIME** column with the absolute time for every
   row (`1:10.80`, `1:10.85`, `1:17.23` …) and a separate, smaller, muted **GAP** column
   (`+0.05`, `+6.43` …, or `—` for the winner); read them from `$1F26`/`$1F12` rather than
   OCR.
2. **The player's row is not highlighted** unless the player kept the default name:
   `me = strstr(name, "PLAYER") != NULL` (`view3d.c:6058`). The player's winning row is drawn as
   an ordinary row (only the generic gold "1"). Fix: compare the row's driver id
   (`$1F12[place]` = 0, or 1 when `$1F04` = 1) or the names at `$1F86`/`$1FA3`
   (`g_player_name[]` already holds them).
3. **The WORLD RANKING page is never redrawn — the old RESULTS stays up.** After RESULTS
   the original scrolls BG1 down to the standings: BG1 is a 32×64 map (`tilemapHigher` = 1),
   `MenuDrawStandings` writes the 20 lines at map rows 38–57 (cell `$2606`, header at rows
   34–35), and the PPU dump at 8760 shows **BG1 vScroll = 255** (`$SCRATCH/retro/ppu195339/frame008760.ppu`).
   `capture_menu` reads `bg1_map + ty*32 + tx` for `ty` 0–27 and ignores the scroll
   (`TgSpriteState` has no BG1 scroll at all: `game.c:141` copies only `tilemapAdr` /
   `tileAdr`), so it keeps reading the results rows and the modern look shows the RESULTS
   table, with its TIME column, under the title **RESULTS** while the original shows
   **WORLD RANKING** — 20 drivers and their championship points. On race 1 the numbers
   happen to coincide; from race 2 on the player never sees the championship order, which
   decides whether he qualifies for the next country (top 3 after race 4). Fix (Engine +
   UI): pass BG1 hScroll/vScroll/size into `TgSpriteState` and read the 28 visible rows at
   `(vs >> 3)` with wrap at 64; better, draw both pages from WRAM (`$1F12`, `$1F26`,
   `$1F4E`, `$1F62`) and only use BG1 to know which page is up. Give the ranking its own
   layout (20 rows, POS / DRIVER / PTS, player highlighted, the top-3 qualification line
   marked after race 3–4).
4. Minor: the POS column is the row index, which is right only because the game lists in
   place order; the POINTS bars scale to the race's maximum (20), fine for RESULTS but the
   ranking needs championship points.

## Information the player needs that the modern look loses

Ordered by how much it costs the player:

1. **The championship standings** (WORLD RANKING) — never shown; the player cannot see
   whether he is top 3 for the next country. (Results item 3.)
2. **Who finished where and how far behind** — the gap column reads as times; his own row
   is not marked. (Results items 1–2.)
3. **The whole race during a pit stop** — fuel gauge, position, the race behind: a junk
   menu instead. (Row 23.)
4. **The 2-player split** when the 2P race is entered from the menus / in the demo: player
   2 (or 1) loses his view. (QA-001, row 22.)
5. **The time of day** on 5 courses and the night lighting that tells the player the
   course has changed mood. (Row 25.)
6. **Where am I**: the course's landmark skyline, present in the original from the grid on
   every course, is missing or late in the modern look; Los Angeles even shows another
   city's water tower (QA-006/007, CITIES.md).
7. **The rival's view in 1-player** (the original's bottom half: the COMPUTER car and its
   HUD) — gone with the full view. A deliberate choice, but say it in the style guide.
8. **The place label** on the failed-to-qualify scene ("PLAYER 1 nTH"). (Row 28.)
9. **The password at the end of a country** — not reached by any recording, so not
   checked; it must be shown large and stay readable (row 30).

## Correctness problems found (beyond `docs/qa/BUGS.md`)

Each confirmed with my own renders/dumps (paths in the rows above):

- **P-1 Pit stop drawn as a junk menu** (row 23) — `in_race` false during a pit stop lets
  `capture_menu` OCR the race HUD. Engine: during `$1E86`/`$1E88` < 0 never capture menus;
  draw the race (or a modern pit overlay with the fuel filling).
- **P-2 WORLD RANKING never captured** (results item 3) — BG1 vertical scroll ignored.
- **P-3 Results: gaps under a TIME header, player row only marked when named PLAYER**
  (results items 1–2).
- **P-4 Race-loading junk menu** (row 16, = QA-010) — `capture_menu` accepts BG1 when it
  holds race tiles; require the menu tile base / a lit screen, else keep the last frame
  or black.
- **P-5 OPTIONS → race leak** (row 15, = QA-003) — explained: BG1 is cleared, only the
  photo and the cursor pill remain; keep the last modern menu (fading out) instead of
  falling back to the original.
- **P-6 Time of day ignores the game's script** (row 25) — night starts (Black Forest,
  Florence), dawn (Hiroshima) and night falls (Cologne, Sicily) not followed; white
  "snow" ground invented on those courses.
- **P-7 Original sky colour lost** — every original course sky is a clear saturated cyan
  (royal blue in Chichen Itza, gradient in Sicily); the modern sky is pale grey/lavender
  with large clouds on every course (CITIES.md).
- **P-8 START gantry** — the original's banner reads START, has chequered flags and
  traffic-light pods (lights count down); the modern gantry is a blank white/red bar.
- **P-9 Race clock 0.05 s ahead** of the original's at the same frame (row 18).
- **P-10 NEXT RACE map panel empty for the first ~0.5 s** (row 14).
- **P-11 Invented car names** (row 13).
- **P-12 `--set-course` note in the team plan is wrong for the original**: set before the
  race load (frame 1900 of S081) the original *does* load the course's skyline, roadside
  pictures and day/night script — every course can be checked without a recording.

## Recordings still needed

For the QA role (and the player): recordings that reach what no input reaches today.

1. A **native** race with a **pit stop** (any 6–8 lap course: San Francisco, Rio, Tokyo,
   Munich, London, Frankfurt) — to check P-1 in the mode the player plays.
2. **Four races of one country finished top 3** → the qualified screen with the
   **password** (and one finished 4th–5th overall → failed-for-next-country + password).
3. **The ending** after Stonehenge (a save-state or a long recording; the UK password (Championship level, per
   [almarsguides](https://almarsguides.com/retro/walkthroughs/snes/games/topgear/)) gets there in 4 races).
4. **Pause** during a race (START) in 1P and 2P.
5. A **2-player** session that goes through RESULTS and WORLD RANKING (both names on the
   table).
6. A full race on each **day/night** course (Hiroshima 43 s, Cologne 47 s, Black Forest
   21 s, Florence 85 s, Sicily 102 s) — `--set-course 1900:N` on S081 shows them for up to
   ~68 s (the recording ends at 6349); a recording to the finish would also cover their RESULTS.
