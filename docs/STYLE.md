# Style guide: the modern look of the stages

Art director, 2026-11-03. For everyone who adds or changes what the race view draws
(`src/mod3d/view3d.c`, `src/mod3d/view3d_stages.inc`). Cars, HUD and menus have their own
owners and are not covered here.

## The look in one line

A **modern arcade racer in clean low-poly**: crisp silhouettes, flat-shaded faces lit by one
sun, a **clear saturated sky** like the original's cyan, the **country's own light and
colours**, and every stage carrying **its city's signs** — readable at 200 km/h.

## Rules

1. **Silhouette first.** A thing must be recognisable as a dark shape against the sky at
   100 m: palms are a thin trunk and a star of fronds, a spruce a dark cone, a torii two posts
   and a curved lintel, a phone box a red slab with a lit top. No detail that only reads at
   5 m (it costs triangles and flickers at speed).
2. **Flat colour, one light.** Plain per-face colours lit by the fixed sun (`kLight`) and
   hazed by distance (`fogged`). No textures for props (every textured picture between plain
   faces breaks a batch); the only made textures are in the atlas (glow, warning board,
   chevrons). Text in the world only on the START banner, the pit sign and the pit lane (its
   paint, boards and garages, near the camera only).
3. **The sky is the original's.** Clear saturated cyan (~`#00B8F8`) towards the horizon,
   deeper blue overhead, small clouds, never grey or lavender in daytime (MUST-FIX R-09).
   The country's light tints the *horizon glow and the haze*, not the sky's hue.
4. **A place in three depths.** Near the road: street furniture and the country's
   vegetation (the original's roadside kit, R-10). Middle: houses, trees, parked vehicles in
   the city's palette. Far: the city's blocks and towers, forest walls, and the landmark on
   the skyline from the grid (R-05).
5. **Density tells town from country.** Each course is split into stretches of town and
   open country (deterministic from the track, the same every lap); towns dense and tall,
   country sparse and low; the start/finish is always a "stadium" with grandstands, pits,
   flags and the START gantry (R-11).
6. **Never on the road, never popping.** Placement is computed once per course from a hash
   of the segment, checked against a grid of where the road runs (also where the loop passes
   near itself). Everything fades in over the last 60 m of its draw distance (alpha), and
   is the same every lap.
7. **Night is lit.** On night stretches (the game's own day/night script) windows glow warm,
   lamps carry a glow, neon signs light up. Day: windows are dark glass reflecting the sky.

## Palette and light per country

| Country | Light | Ground / vegetation | Built colours | Accents |
|---|---|---|---|---|
| USA | hard white sun, warm haze | sand, sage, olive scrub; palms (LA), cacti (LV) | white/gold casinos, blue glass, brick walk-ups (NY), pastel Victorians (SF) | neon pink/cyan, cab yellow, International Orange |
| South America | bright tropical, humid | deep saturated greens, red earth | ochre/pink/blue favela boxes, grey dry stone (Peru), pale limestone (Mexico) | sea blue, parrot red |
| Japan | clear; dawn red in Hiroshima | dark woods, cherry pink, maple red | grey concrete, dark timber, white plaster | vermilion torii, neon, nobori green/red |
| Germany | clean pale light | dark spruce, chestnut | white render with dark timber, red roofs, grey stone | autobahn blue, Bavarian blue/white |
| Scandinavia | low sun, cold blue | icy ground, spruce, birch | falu red houses with white trim, ochre | yellow warning triangles, copper green |
| France | warm afternoon | vineyards, poplars, plane trees | cream stone, grey zinc mansards, terracotta (south) | Riviera blue, lavender |
| Italy | golden | cypress, umbrella pine, olive | ochre/terracotta, travertine, white marble | terracotta roofs |
| UK | overcast, flat | hedgerows, oaks, heather | red brick, grey stone, Portland white | pillar-box red, racing green |

## References (researched 2026-11-03) and what was taken

- **Horizon Chase Turbo** (Aquiris): low-poly near scenery against detailed far
  backdrops, a bright saturated palette, and *a landmark you recognise at every city*
  (its Porto Alegre stages are built from real local landmarks). Taken: the landmark on the
  skyline from the grid, bright sky, simple props in the city's colours.
  ([press kit](https://press.aquiris.com.br/games/horizon-chase-turbo),
  [Epic store](https://store.epicgames.com/p/horizon-chase-turbo),
  [Pocket Gamer](https://www.pocketgamer.co.uk/horizon-chase-world-tour/horizon-chase-turbo-and-world-tour-receive-celebratory-updates-featuring-new-tra/))
- **art of rally** (Funselektor): diorama-like places, painterly flat colour, cars and
  scenery in the same low-poly vocabulary; each region (Finland, Sardinia, Norway, Japan,
  Germany) told by its vegetation, houses and ground colour more than by monuments. Taken:
  per-country vegetation and house kits, the ground colour carrying the region.
  ([Byteside review](https://byteside.com/2020/09/art-of-rally-game-review-funselektor),
  [Checkpoint](https://checkpointgaming.net/reviews/2020/10/art-of-rally-review-style-substance-and-passion/))
- **OutRun 2** (Sega AM2): famous European-style locales readable at speed: big shapes,
  strong sky, roadside rows (palms, poplars) that give a rhythm. Taken: rows of the country's
  tree along the road, big simple buildings.
  ([Sega Retro](https://segaretro.org/OutRun_2), [GameSpot](https://www.gamespot.com/reviews/outrun2-review/1900-6111602/))
- **Inertial Drift**: neon-lit night stages, cel-flat colours. Taken: night as a mood of
  its own (lit windows, glowing signs), not a dimmed day.
  ([MobyGames](https://www.mobygames.com/game/150400/inertial-drift/),
  [racinggames.gg](https://racinggames.gg/article/inertial-drift-twilight-rivals-edition-review-the-most-unique-arcade-racer-in-ages))

## Budgets

The QA speed check (`python3 tools/qa/run_qa.py --quick`, `speed/`) measures the scene build
(`tg_view3d_draw_view`, CPU) in headless: steady **p99 under 16.7 ms**, no stall over 100 ms.
Before this work: median 6.2 ms, p99 12.5 ms, ~89 000 triangles, 26 batches median.

- **Stage props in view: at most ~8 000 triangles** (a tree 12–40, a house 14–30, a tower
  20–50, a lamp 10). Props off the screen sides are culled before they are built.
- **Batches: no new pictures in the world** except the START banner's text: plain colours
  and the atlas only, so the props add no batch (QA-008 saw up to 7351 batches; keep the
  median under 40).
- **Draw distance**: street furniture 140 m, trees and houses 260 m, big buildings 420 m,
  each fading out over its last 60 m.
- **Placement** is computed once per course (a few ms at the race load), not per frame.
- **Skyline landmarks modelled in 3D** (`tools/make_landmarks.py` -> `src/mod3d/landmark_meshes.h`,
  drawn by `lm_mesh`): a far landmark is only seen from one side (it has a fixed direction, like
  the sun), so it is modelled as a solid and baked for that side (hidden triangles dropped, a
  painter's order), then lit in the game with `kLight` and hazed like the far land; no textures,
  no new batch; thin members (a lattice's chords and braces, cables) as ribbons facing the
  viewer, so the sky shows through. Rio's Corcovado with the statue: ~4.1k triangles,
  Sugarloaf ~0.75k; the Eiffel Tower ~1.7k, Tokyo Tower ~1.6k, the Leaning Tower ~1.9k. The
  landmark's proportions true within itself, exaggerated against its mountain (the statue ~7x,
  as on a postcard), its height on the screen H as before (LM_HANG).

## The pit lane (`src/mod3d/view3d_pits.inc`)

Where the game has its pits (13 of 32 courses: the kind-$3A pit sign in the course's object table)
there is a real pit lane, laid out once per course from the sign, the same every lap:

- **The game's rule painted:** a solid white line on the road (or the verge) at the sign's distance
  for 60 m before it - past it at the sign, the game takes the car in. The lane splits off after
  the sign behind a hatched gore (yellow), PIT IN painted large.
- **Track width kept:** the wall (concrete, red/white coping, team sponsors on its track face, a
  see-through catch fence on it) stands at the road's edge or the game's own divider if further
  out; the game's rivals drive the whole road there.
- **Lane:** fast lane and working lane split by a dashed line; the speed-limit lines across it and a
  60 roundel on the entry gantry (PIT LANE), the end-of-limit roundel; boxes outlined white, the
  players' in yellow with their colour and number; the exit gore and PIT OUT; a green exit light.
- **Garages:** eight, 10 m each, white with the team colour on the fascia and its name, roll-up
  doors (up, half, shut), a lit interior with benches and tyre sets, a canopy over the working
  lane, a dark glass upper floor. The players' garages carry their car's colour and their name;
  the others are invented teams (KESTREL, HALCYON, ORBITAL...), no real brands.
- **Crew:** low-poly figures (boxes between joints; two-bone legs and arms placed from the feet's and
  hands' targets), team-colour tops, dark trousers, helmets in the trim colour; a sine walk by the
  distance walked (no sliding feet), kneeling wheel gunners, jacks with handles, tyres in hand, a
  fuel rig with a sagging hose, a lollipop (BRAKE red, GO green). A crew member between the chase
  camera and the car is seen through by screen door, as a rival is.
- **Budget:** the complex is culled by segment (430 m) and its details by distance (text 120-170 m,
  crews 170 m, full figures 55 m); text only at close range (one batch a run of words).

## Out of scope / decided

- R-15 (the original's 1-player split view with the computer rival below): the modern
  look keeps one full view; the rival's progress is on the HUD map. Decided.
- No ROM art in props: every model is built in code from boxes, prisms and frustums (the
  modelled landmarks by a script from lofts, ellipsoids, boxes and height fields: original
  work, no third-party model).
