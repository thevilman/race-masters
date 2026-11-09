# 3D mod: plan

Branch `mod/3d-renderer`. A real 3D renderer for the races, on top of the C port, with the
game itself unchanged.

## Decisions (2026-10-29)

| Topic | Decision |
|---|---|
| Engine | Real 3D: meshes, depth buffer, perspective camera. Built on SDL3's GPU API (already linked; Direct3D 12 / Vulkan / Metal). |
| Looks | Two 3D looks, switched with a key at any time: **SNES look** (the original sprites and road colours, taken from your ROM at runtime, on billboards and road geometry) and **new look** (new models and textures). The original 2D picture stays available as it is today (`--original`, renderer off). |
| Gameplay | Unchanged: physics, AI, lap times, collisions and controls stay the game's own and keep passing the recording tests. The 3D renderer only reads the game state. |
| Allowed changes | Extra camera views (higher chase, bumper, free/replay). Nothing else. |
| Track shape | A real 3D circuit with a world position for every point (changed 2026-10-29, was camera-relative). Built once per course from the curve and slope tables: one lap turns one full circle, the small gap where the loop meets its start is spread over the lap, extreme gradients (drops the original never climbs back) are capped and the leftover rise or fall is spread over the lap. Turns, hills and their order are the original's. |
| Cars | Every car in the race, players' and computer's, is in the scene with its own track position, side position, lap and world position and orientation, every race update. |
| Art | I extract reference prints (sprites, backdrops, road) from the game; you return upscaled images; they are used both as textures and as references for low-poly models I build. |
| Repository | Private for now. ROM-derived art (extracted prints, upscales) stays outside git, in a local assets folder; only code and original assets are committed. |

## How the game gives us a 3D scene

Read-only, from work RAM during a race (see `docs/notes/engine.md`):

- Track, per segment of 32 track units: `$4C00` curve, `$4000` slope, `$5800` stripe phase,
  `$6400` roadside object (side, distance from the road, art).
- Cars: 18-byte records at `$0200` (track position, side position, kind).
- Player car, speed, gear, nitro, fuel, lap, time: as read by the HUD.
- Art: car and object sprites from OAM/VRAM and the ROM, backdrop layers BG1/BG2, palettes.
- The game updates 20 times a second; the existing high-frame-rate interpolation gives
  smooth positions between updates.

## Milestones

1. **Scene snapshot.** One C function turns RAM into a plain `TgScene` struct (the 3D
   circuit, every car, HUD values), unit-tested. Everything else reads only this.
2. **Art extraction.** Decode the race sprites, backdrop layers and road colours from your ROM
   into RGBA textures at startup; `tg_headless --dump-art DIR` writes them as PNGs (your
   first batch of prints).
3. **3D road, headless.** Build road geometry around the camera from the snapshot and draw
   it with SDL GPU offscreen; screenshots side by side with the original for course 1.
4. **SNES look in game.** Billboarded original sprites for cars and roadside objects, backdrop
   as a cylinder, original HUD drawn on top. Toggle key between 2D, SNES-look 3D and
   new-look 3D.
5. **Cameras.** Original-matching chase camera (default), higher chase, bumper, free.
6. **New look, iteratively.** Per batch of your upscaled images: textures first, then
   low-poly models (cars, trees, signs, buildings), lighting, fog, sky per country.
7. **All 32 courses** checked with the recording suite for crashes and visual glitches.

## Status

- 2026-10-29, milestone 1: `src/mod3d/track3d.c` lays the course out as a closed 3D
  circuit (Las Vegas: 704 segments, about 1.7 km a lap, matching the shape of the game's
  minimap); `src/mod3d/scene.c` reads all 20 cars with world positions. `tg_headless
  --track3d-obj FILE` writes the circuit as an OBJ, `--scene-log FILE` every car's place
  per race update. Reading leaves the game untouched (same work RAM hash). Unit test in
  `tests/test_core.c`.
- 2026-10-29, milestone 2 (first part): `tg_headless --ppu-shots --interp-steps 2` saves
  the PPU state with each shot, and `tools/extract_sprites.py` cuts every car and roadside
  object out of it, one PNG per distinct picture, using the game's own object tags. Run it
  with full view on (`0 fullview on` in the recording) so the top view's sprites are the
  ones in the dump.
- 2026-10-29, milestones 3-5 (first playable version): F7 in the game draws races in 3D
  (`src/mod3d/view3d.c`). Where it hooks into the game:
  - `src/port/program.c`: the race program counts its picture updates
    (`tg_port_race_updates`, after each `RaceRenderViews`), so the 3D view steps with the
    game's own updates and moves smoothly between them.
  - The camera is the game's: the player's track position the original renders from
    (`$46`), its eye $90 track units behind, as the original's car list and depths have it.
  - The pictures are the game's: each frame the sprites `RaceBuildSprites` chose (steering
    frames, brake lights, size steps) are put together by the object tags it leaves on OAM
    entries and stand in the 3D world as billboards; their world size comes from the
    original's projection (picture size x depth), calibrated on the cars.
  - The skyline, sky, ground and road colours are taken from the original picture.
  - 3D mode turns on the full-screen 1-player view and the object tags (high frame rate's)
    while it is on; the game's logic doesn't change (same work RAM with it on or off).
  - Drawn through SDL_Renderer (SDL_RenderGeometry) from a triangle list: the 3D maths is
    ours, the GPU rasterises. Moving to SDL's GPU API needs shader tooling first.
  - Ctrl+F7: simple box models made from the pictures' colours; Shift+F7: cameras.
- 2026-10-29, polish: the skyline is the game's BG2 panorama, built up from the original
  picture at the game's own scroll ($B0), never from under a sprite and only on flat road,
  and drawn round the camera at that scroll; the sky is a clean gradient behind it.
  Every picture's size comes from the game's projection (pixels x depth), calibrated on
  the player's car straight ahead. Cars in the models look are bodies built round their
  own rear sprite; roadside objects there are their pictures on crossed boards. The HUD
  is the game's own (BG1). Hills are balanced so flat road stays flat. Player 2's car
  (the 1-player rival) has its own sprite tag, TG_P2_CAR (visible.c), as both players'
  cars are number 0. Pictures are taken once the race's main loop runs ($1E58 = 0).
- 2026-10-30, overnight polish (found by replaying the player's own session from
  recordings/, and by racing the first course of every country):
  - The camera is the game's own ($46), including its fly-in at the start, placed as the
    original's picture: level, eye 2.9 m up, the car low on the screen.
  - Sizes follow the game's projection: picture size x depth is constant for every object
    at every distance; roadside objects are sized from close sightings (they were 3.5x too
    big), cars calibrated on the player's car straight ahead.
  - No flicker: objects standing on the road always draw after the road under them;
    3D races present every display refresh with vsync and move smoothly between frames.
  - The horizon is the game's own ($AE, where the sky stage hands over to the road band),
    mapped into the full-screen view by its own line mapping (TgSpriteState.line_at). The
    skyline panorama is built from the original picture at the game's scroll ($B0) only
    while racing and moving, never under a sprite, only when a pixel is seen twice, and not
    near the start gantry (whose banner the game draws in the background).
  - The HUD is the game's BG1 in zones (corners, messages centred) with the map's dots
    (OAM entries 0-2). Pit stops and 2-player races show the original picture.
  - Player 2's car (the 1-player rival) has its own sprite tag, TG_P2_CAR.
  - Courses up to 2048 segments (Japan's is 1248; the limit was 1024).
  - What the 3D view learns is kept between sessions in the settings folder
    (course3d-<key>.bin, cars3d.bin).
- 2-player races in 3D: a view per player (TgView3D.two; tg_view3d_draw_view).
- The art step: any picture can be replaced by a BMP in art3d/ next to the game
  (art-<course>-<n>, sky-<course>, car-<id>); it keeps the original's place and size, only
  its detail changes. tg_headless --export-art writes them (the prints to upscale).
- Model cars are built from slices of their sprite from behind; the start gantry is built
  in 3D; slopes are shaded; the ground bands fade into the ground.
- 2026-10-30, the modern look (TG_V3_LOOK_MODERN, now the default; Ctrl+F7 cycles modern,
  the game's pictures, models). Everything in it is made here, placed and sized where the
  game puts its pictures:
  - Cars: low-poly sports cars (wedge body, glass cabin, roof, spoiler, light bar, wheels,
    soft shadow) in the paint of the car's own picture (silver cars stay silver).
  - Roadside objects: each picture is classified by its colours and shape (cactus, broadleaf
    tree, pine, cypress, bush, chevron board, warning sign, lamp) and drawn as a model of
    that kind; chevrons point the way the road turns. Unknown pictures stay billboards.
  - Road: textured asphalt, red and white kerbs, edge and lane lines; grain-textured
    shoulders fading into the open ground.
  - Sky: a deep gradient with a sun and glow, the course's skyline in the haze; distance haze
    on everything, the open ground hazed row by row.
  - Solids are drawn with back faces culled and each face lit by its own direction;
    see-through colours (shadows, glow, fading bands) are blended in the game and in
    tg_headless (which writes the modern look as frame*_3dx.ppm).
- 2026-10-30, modern look, later the same night: shaped car bodies (cross-sections),
  rims, mirrors, brake lights (a car slowing in a game update); clouds and a sun fixed in
  the world; rocks, grass tufts and snow lumps by the road; lamps with arms and chevrons
  pointing as their pictures do; Japan's banner groups drawn as pictures where the game
  puts them (only the start group is a gantry). The skyline sits on the highest ground
  ahead when a hill rises over the horizon (as the original draws it on top of the road
  band), is drawn Scale2x-smoothed, and is learned on gantry-lined courses too. The made
  textures share one filtered atlas: the modern look reaches the GPU in ~4 batches a
  frame. Every roadside picture of the 13 courses learned so far maps to a model kind.
- Open: roadside objects
  marked special (bit 15: groups, the start banner); the HUD is text, not the game's.
- Open: road width per segment (the stripe word picks one of the limits at `$017B`; the
  ROM table that maps them isn't decoded yet, so the usual width is used everywhere);
  whether the bottom view's car is a human or the computer in a given race.

## Constraints

- The 3D renderer never writes game state. With it off, every existing test must pass.
- Nothing extracted from or derived from the ROM is committed.
