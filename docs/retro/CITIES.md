# The 32 courses: what the original shows, what the 3D stage should add

Retro gaming expert, audit of 2026-11-02 (branch `retro-audit`, base `b1589b2`). For the
art director and the 3D modeller: one row of facts per course, then a concrete list of what
the 3D stage needs so it reads as *that* city and not as "a road in the desert".

## How this was made, and its limits

- **Course list, laps, day/night**: the course-text and lap tables in the ROM as documented
  by the reverse-engineering project [pedro-walter/top-gear-snes-re](https://github.com/pedro-walter/top-gear-snes-re)
  (`docs/03`, `docs/06`, `docs/13`), cross-checked with
  [Wikipedia: Top Gear (video game)](https://en.wikipedia.org/wiki/Top_Gear_(video_game)),
  [Playing With Superpower review](https://playingwithsuperpower.com/top-gear-review/) and
  [almarsguides](https://almarsguides.com/retro/walkthroughs/snes/games/topgear/). Course
  numbers are the game's own (`$1F06` bits 0-4); `tg_view3d` reads the same number as
  `v->course`.
- **Original skyline / roadside**: seen in `tg_headless` renders of the original picture and
  in the pictures `--export-art` writes (skyline strip `sky-<key>.bmp`, roadside objects
  `art-<key>-N.bmp`), into the scratch folder only
  (`$SCRATCH/retro/export/<country>/art/` and `$SCRATCH/retro/courses/c<N>/art/`, never into git). 9 courses come from
  recordings (Las Vegas, Los Angeles and the first course of every other country, the
  `country_*` inputs). The other 23 were seen with `--set-course 1900:N` on
  `session-20261030-081901` (native): **set at frame 1900, before the race loads, the
  original loads that course's own skyline, roadside pictures, palette and day/night script**
  (the team plan says the skyline does not follow `--set-course`; at frame 1900 of this
  recording it does — presumably the plan's check set the course later, after the race
  load; not verified). Shots at 2600/3200 (4400 for the night starts). The
  sky strips are only exported for the part the camera saw, so a few strips are partial.
- **Real-world look**: Wikipedia's article for each city/landmark (linked per row) and
  general knowledge. Proportions are rough, for blocking out, not survey data.

## Rules for every stage (from the original's grammar)

The original builds every course from the same kit: a **horizon strip** (one picture per
course that scrolls sideways only: city or landscape silhouette, sometimes one landmark), a
**ground colour pair** (the alternating stripes), **road kerbs** (red/white), and a small set
of **roadside sprites**: trees or cacti or rocks for the country, chevron boards `>>`/`<<`,
sponsor boards (KEMCO, GREMLIN and fictional brands), barrels and hazard barriers, flag
poles, the start/finish gantry and the pit entrance markers. So the 3D stage should:

1. Keep the **skyline as a far layer** (it never gets nearer in the original): a 3D far
   silhouette with the city's landmark placed so it appears **from the grid** (QA-006: today
   it fades in 10-35 s late).
2. Keep the **ground colour** of the course from the first frame (QA-005), not the previous
   course's.
3. Use the country's **vegetation** for the near roadside, never another country's (desert
   cacti in Europe, Las Vegas water tower in LA — QA-007).
4. Keep the original's **readability props** where the original has them: chevrons at the
   same corners, the pit arrow, the gantry. Landmarks are *in addition*, never replacing a
   chevron.
5. Night courses: lit windows, street lamps, headlights; the **day/night switch happens at
   a fixed time** (below), with a ~3.7 s fade — the stage must follow the game's palette
   change, not a guess.

## The table

Day/night: **D** day, **N** night, **D→N 47 s** turns to night 47 s after the start (fade
~3.7 s), and so on (RE `docs/03`).

### USA (courses 0-3)

| # | City | Laps | Light | Original (seen in renders / export) | What the 3D stage should add |
|---|---|---|---|---|---|
| 0 | **Las Vegas** | 3 | D | Sky **clear saturated cyan**; horizon = low sand-dune silhouette only (no city, no landmark); ground pale sand stripes with **green grass stretches** (4800, 6000); roadside: saguaro cacti (2 sizes), round green bushes, red/white chevron boards; start gantry with **START** banner, chequered flags and **traffic lights**. Modern: grey-lavender overcast sky, grey mountain haze with a ghostly tower, sand everywhere (no grass), plain white gantry without START/lights. (`session-20261102-195339` 3000-7200) | Desert of the Mojave: red-brown mesas on the horizon (Spring Mountains, low, 1:15 height:width), sand ground, Joshua trees, barrel cacti, sage; at the city side of the loop a **Strip silhouette**: a black glass pyramid (Luxor, 4:5 height:base), a thin needle tower with a pod at the top (Stratosphere, ~10:1), casino slabs in white/gold; the diamond **"Welcome to Fabulous Las Vegas"** sign (white diamond, red letters, gold star) at the start straight; billboards with bulb borders. Hard white sun, short shadows, warm haze. [Las Vegas Strip](https://en.wikipedia.org/wiki/Las_Vegas_Strip) |
| 1 | **Los Angeles** | 3 | D | Sky clear cyan; horizon = **olive-green hills with a hillside sign in white letters**; ground bright green stripes; roadside: **curved double palms**, round broadleaf trees, grey rocks, chevrons. Modern: teal-grey sky with clouds, green hills, a **water tower** (wrong, QA-007), no palms at all; first 20 s desert + night (QA-005). (`session-20261030-095942` 8100-10760) | Brown-green chaparral hills behind; the **hillside sign** (9 white block letters on a hillside, letters ~14 m tall over ~110 m) — the original shows it (QA-007): it must be this, not a water tower; downtown towers (a dozen slabs, one with a crown, in blue glass); **tall thin Washingtonia palms** (25 m, tiny head) lining the road, freeway overpass concrete, sodium lights; golden smoggy haze, low warm sun. |
| 2 | **New York** | 3 | N | Night: **navy sky**, dense skyline of lit windows (yellow) with red/green neon dots and a few spires (no single landmark), dark green ground, **tall street lamps** as the roadside, a green headlight cone ahead of the car. Modern (`--set-course 2`): purple night sky, lamps, headlights, but **no skyline at all** by 4200. (S081 `--set-course 1900:2`) | Night all race. Dense skyline with lit windows (warm yellow, 30 % lit), **Empire State** (stepped art-deco tower + mast, 8:1) and **Chrysler** (arched crown in steel), twin-tower-era block silhouettes are fine for 1992; **Brooklyn Bridge** stone towers with Gothic arches and cables on the water side; Statue of Liberty (green copper, torch) far on the water; yellow cabs, steam from manholes, fire escapes on brick walk-ups, street lamps every ~30 m, headlights on. [Manhattan skyline](https://en.wikipedia.org/wiki/Skyline_of_New_York_City) |
| 3 | **San Francisco** | 6 | D | Sky cyan; horizon = a **red Golden Gate tower and span** on the left over a **blue bay band**, then a **white downtown skyline** (Transamerica-like spikes) on the right; ground green; roadside: street lamps, chevrons. Modern: no skyline (3000). (`--set-course 1900:3`) | **Golden Gate Bridge** (International Orange, two art-deco towers 227 m, suspension cables) across the water; **Transamerica Pyramid** (white needle-pyramid, 6:1); steep hills with rows of Victorian "Painted Ladies" (pastel, bay windows); cable-car rails and a red cable car; cypress and eucalyptus; cool blue light with **fog banks** low on the bay. [Golden Gate Bridge](https://en.wikipedia.org/wiki/Golden_Gate_Bridge) |

### South America (courses 4-7)

| # | City | Laps | Light | Original | What the 3D stage should add |
|---|---|---|---|---|---|
| 4 | **Rio** (Brazil) | 6 | D | Sky clear cyan; horizon = **pointed green peaks** (Sugarloaf-like) with a **blue sea strip and a small white town** at their foot; ground green stripes; roadside almost only chevrons (export: 5 pictures, all chevrons); START gantry. In verify mode the 1P race is the **split screen** (top player, bottom COMPUTER). Modern: no skyline for the whole run seen (6300-8100), pale grey-blue sky, desert on the grid (6000). (`country_s_america`) | Steep green granite peaks: **Sugarloaf** (rounded dome, 396 m, with a cable-car line) and **Corcovado** with **Christ the Redeemer** (white statue, arms out, on the highest peak); the game has a **tunnel near the start** (keep it); Copacabana black-and-white wave pavement on the sea side, palms, favela boxes in ochre/pink/blue on hillsides; bright tropical sun, sea-blue. [Christ the Redeemer](https://en.wikipedia.org/wiki/Christ_the_Redeemer_(statue)) |
| 5 | **Machu Picchu** (Peru) | 4 | D | Sky cyan; horizon = **purple-brown jagged Andes peaks with snow tips** over a teal forest band; ground bright green; roadside: chevrons, tall broadleaf trees. No ruins visible in the seen part of the strip. Modern: no peaks, generic green. (`--set-course 1900:5`) | Andes: very steep green peaks (**Huayna Picchu**, sugar-loaf shape) with mist bands; **grey dry-stone terraces** stepping down the slopes, roofless stone houses, thatched gables; llamas; cloud-forest vegetation (bromeliads, tall grass); cool diffuse light. [Machu Picchu](https://en.wikipedia.org/wiki/Machu_Picchu) |
| 6 | **Chichen Itza** (Mexico) | 6 | D | Sky **deep royal blue**; horizon (seen part) = a **dense jungle canopy line over brown earth with rocks** — no pyramid in the part of the strip seen; ground green; roadside: small bushes, chevrons. Modern: lavender sky, open fields. (`--set-course 1900:6`) | Flat scrub-jungle (low, dense, olive green); **El Castillo**: a 9-terrace stepped pyramid in pale grey limestone (24 m, base 55 m, temple on top, stairs each side); Temple of the Warriors' column rows; cenote (round water hole); hot white light, pale sky. [El Castillo](https://en.wikipedia.org/wiki/El_Castillo,_Chichen_Itza) |
| 7 | **Rain Forest** (Brazil) | 3 | D | Sky cyan; horizon = **soft teal-green forested hills**; ground green; roadside: small palm-like trees; START gantry. Modern: open fields, no forest wall. (`--set-course 1900:7`) | Not a city: a wall of rainforest — kapok giants (60 m, buttress roots), palms, lianas, giant ferns; a brown river; red earth verges; humid haze, light shafts, saturated deep greens; parrots as a moving accent. [Amazon rainforest](https://en.wikipedia.org/wiki/Amazon_rainforest) |

### Japan (courses 8-11)

| # | City | Laps | Light | Original | What the 3D stage should add |
|---|---|---|---|---|---|
| 8 | **Tokyo** | 7 | D | Sky clear cyan; horizon = **dense city wall**: lilac/grey/white office blocks, some with **neon/billboard panels** (a red "24"-like sign), no single landmark; ground green stripes; roadside: tall **nobori banners** (vertical green/red flags with kanji and a chequered border) and chevrons. Modern: purple-blue sky, **no skyline at all** in 6000-8100, green fields; banners appear (8100). (`country_japan`) | **Tokyo Tower** (red/white lattice, Eiffel-like, 333 m); Shinjuku glass towers; Mount Fuji far (snow cap, 1:3 slope) on a clear day; elevated expressway concrete, dense signs with vertical lettering, vending machines, overhead cables on concrete poles, cherry trees. [Tokyo Tower](https://en.wikipedia.org/wiki/Tokyo_Tower) |
| 9 | **Hiroshima** | 5 | N→D 43 s | At 4200 (22 s): a **red-orange dawn sky** over **dark red hills**, a **torii gate standing in the sea with the sun above it** (Itsukushima), black ground; roadside: **blue-white KEMCO boards** in rows, lamps, chevrons. Modern: pink-white cloudy sky and a **white glare ground** — the dawn and the night ground are lost; KEMCO boards present. (`--set-course 1900:9`) | Starts at dusk/night, day after 43 s: dawn colours. **Atomic Bomb Dome** (ruined brick shell with a bare steel dome frame) by a river; **Itsukushima torii** (vermilion gate standing in the sea, 16 m) and its shrine on stilts; black-walled **Hiroshima Castle**; streetcars, river bridges, pines. [Genbaku Dome](https://en.wikipedia.org/wiki/Hiroshima_Peace_Memorial) |
| 10 | **Yokohama** | 3 | D | Sky cyan; horizon = a **white cable-stayed Bay Bridge** over a blue harbour band, low city; START gantry with traffic-light pods. Modern: no skyline (and nothing exported for it by 3200). (`--set-course 1900:10`) | Waterfront: **Landmark Tower** (296 m, tapered slab), **Cosmo Clock 21** Ferris wheel (giant, with a clock face), **Bay Bridge** (white cable-stayed H towers), red-brick warehouses, harbour cranes, a Chinatown gate (red/gold paifang). [Minato Mirai 21](https://en.wikipedia.org/wiki/Minato_Mirai_21) |
| 11 | **Kyoto** | 6 | D | Sky cyan; horizon = a **dense dark-green wooded hillside** with **stone lanterns** and a small **temple roof**; ground green; START gantry. Modern: open fields, no forest wall. (`--set-course 1900:11`) | Wooded hills on three sides; **To-ji five-storey pagoda** (dark wood, 55 m, stacked roofs); **Kinkaku-ji** (gold-leaf pavilion on a pond); tunnels of **orange torii** (Fushimi Inari); wooden machiya houses, stone lanterns, maples (autumn red) and cherry; soft light. [Kinkaku-ji](https://en.wikipedia.org/wiki/Kinkaku-ji) |

### Germany (courses 12-15)

| # | City | Laps | Light | Original | What the 3D stage should add |
|---|---|---|---|---|---|
| 12 | **Munich** | 7 | D | Sky clear cyan; horizon = a **low row of cream/white palace-like buildings with red roofs** behind a dark treeline and a **blue water band** (Nymphenburg-like), very flat; ground green; roadside: chevrons only (export: 2 pictures). Modern: pale sky, a faint grey ghost skyline from ~7800, desert on the grid (6300). (`country_germany`) | **Alps** as a far white-capped wall; **Frauenkirche** twin towers (red brick, green onion domes, 99 m); **Neues Rathaus** (neo-Gothic, grey stone, clock tower); Olympic stadium tent roof (acrylic net); beer-garden chestnut trees, blue-white Bavarian lozenge flags and a **maypole** (blue/white spiral); clean pale light. (Tracker: "skyline windows on jagged roofs (Munich)" — the original's horizon here is dithered hills + roofs.) [Frauenkirche](https://en.wikipedia.org/wiki/Frauenkirche,_Munich) |
| 13 | **Cologne** | 3 | D→N 47 s | Day: cyan sky; horizon = a **riverside town** across a **blue river band**: brown/cream gabled houses with **domed towers and Gothic spires**; ground green; roadside: **yellow warning triangles**, a yellow/black hazard barrier, chevrons. **Night at ~47 s** (6000: navy sky, black ground, the town in dark tones, headlight glow). Modern: no town until late, generic ghost towers; **stays in daylight** after 47 s with a white glare ground. (`--set-course 1900:13`, 2400-6000) | **Cologne Cathedral** (two black Gothic spires, 157 m, dominate everything) and the **Hohenzollern Bridge** (three steel arches over the Rhine); the Rhine with barges; at night the cathedral floodlit, the bridge lamps; grey sky by day. [Cologne Cathedral](https://en.wikipedia.org/wiki/Cologne_Cathedral) |
| 14 | **Black Forest** | 6 | N→D 21 s | At 4200 (22 s) still **night**: navy sky, black fir silhouettes, dark ground; roadside: **firs** (2 sizes), chevrons. Modern: **broad daylight on white snow** with firs — the night start is lost and the snow is invented. (`--set-course 1900:14`) | Dense, very dark **fir/spruce forest** on rolling hills, morning mist in valleys; **half-timbered farmhouses** with huge hipped roofs, a sawmill, a wooden church; stacked logs on the verge; dawn after 21 s (cold blue to warm). [Black Forest](https://en.wikipedia.org/wiki/Black_Forest) |
| 15 | **Frankfurt** | 8 | D | Sky cyan; horizon = a **river band with a town of cream/grey blocks and a few towers** (low, no glass cluster in 1992 art); roadside: street lamps, chevrons. Modern: no skyline. (`--set-course 1900:15`) | **"Mainhattan"**: a cluster of glass towers (in 1992: Messeturm, the twin Deutsche Bank towers — mirror glass, 155 m — Commerzbank's predecessor) over the river Main; the **Römer** (three stepped gables, half-timbered) in the old town; river bridges, trams; neutral daylight. [Frankfurt skyline](https://en.wikipedia.org/wiki/List_of_tallest_buildings_in_Frankfurt) |

### Scandinavia (courses 16-19)

| # | City | Laps | Light | Original | What the 3D stage should add |
|---|---|---|---|---|---|
| 16 | **Stockholm** | 3 | D | Sky clear cyan; horizon = **harbour front of yellow/ochre/red houses with a white ferry**, over a **blue water band**; ground **icy pale blue-white** stripes; roadside: **tall curved street lamps**, **yellow warning-triangle signs**, dark **spruces**; at 7800 a stretch with a deep-blue sky and only lamps (open water). Modern: white-grey ground and lamps (good), but round deciduous trees, no warning signs, a ghost cluster of generic towers, desert on the grid (6600). (`country_scandinavia`) | Water everywhere (islands); **City Hall** (red brick, square tower with three gold crowns on top, 106 m); **Gamla Stan** ochre/orange/red narrow houses with steep roofs; copper-green church spires; birch and pine, granite outcrops; low sun, long shadows. [Stockholm City Hall](https://en.wikipedia.org/wiki/Stockholm_City_Hall) |
| 17 | **Copenhagen** | 3 | D | Sky cyan; horizon = **red-brick and white palace buildings** with a **white ferry** on a blue harbour band; **icy pale ground** (as Stockholm); START gantry. Modern: pale ground (good), no skyline; desert in `chris_session_03`. (`--set-course 1900:17`) | **Nyhavn** canal: gabled houses in saturated yellow/red/blue with wooden sailing ships; green copper spires (the spiral of Vor Frelsers Kirke); the **Little Mermaid** on a rock; bicycles everywhere, flat land, overcast soft light. (QA: Copenhagen in `chris_session_03` was drawn with Las Vegas desert ground.) [Nyhavn](https://en.wikipedia.org/wiki/Nyhavn) |
| 18 | **Helsinki** | 3 | D | Sky cyan with a **yellow sun low on the left**; horizon = **low rounded green hills**; ground **icy pale blue-white stripes**; START gantry with traffic lights. Modern: grey-white ground (good), no hills/sun. (`--set-course 1900:18`) | **Helsinki Cathedral** (white, central green dome + four small domes, on a stair); **Uspenski Cathedral** (red brick, gold onion domes); granite shore, pine and birch; snow possible; pale long twilight. [Helsinki Cathedral](https://en.wikipedia.org/wiki/Helsinki_Cathedral) |
| 19 | **Oslo** | 3 | D | Sky cyan; horizon = **grey snow-capped mountains with green forest at their feet**; icy pale ground; START gantry. Modern: pale ground, no mountains. (`--set-course 1900:19`) | Fjord with steep wooded slopes; **Holmenkollen ski jump** on the hill (white ramp silhouette); **Akershus Fortress** (stone walls, copper roofs) on the shore; City Hall (twin brick towers); pines, snow on the hills; cold clear light. (The Opera House dates from 2008: avoid for a 1992 setting.) [Holmenkollbakken](https://en.wikipedia.org/wiki/Holmenkollbakken) |

### France (courses 20-23)

| # | City | Laps | Light | Original | What the 3D stage should add |
|---|---|---|---|---|---|
| 20 | **Paris** | 3 | D | Sky clear cyan; horizon = flat dark-green treeline with tiny buildings and **a small dark Eiffel Tower**, visible **from the grid** (6600) and wandering with the road; ground green; roadside: round bushes, chevrons. Modern: pale sky; a pale ghost Eiffel lattice only from ~8400 (QA-006). (`country_france`) | **Eiffel Tower** (brown-bronze iron lattice, 300 m, 1:2.5 base:height curve; QA saw it fade in late at 8340); Arc de Triomphe (square arch); **Sacré-Cœur** (white domes on a hill); Haussmann blocks (cream stone, 6 floors, grey zinc mansard roofs, black iron balconies), plane trees, Morris advertising columns (green, onion top), Wallace fountains; pale grey-white sky. [Eiffel Tower](https://en.wikipedia.org/wiki/Eiffel_Tower) |
| 21 | **Nice** | 7 | D | Sky cyan; horizon = **green hills dotted with white villas** over a **blue sea strip**; ground green; START gantry. Modern: lavender sky with clouds, no hills or sea. (`--set-course 1900:21`) | **Promenade des Anglais**: the sea in azure, pebble beach, the famous **blue chairs**, palm rows; Belle Époque hotels (white/pastel, the pink dome of the Negresco); Castle Hill (green); bright Riviera sun, deep blue sky. [Promenade des Anglais](https://en.wikipedia.org/wiki/Promenade_des_Anglais) |
| 22 | **Bordeaux** | 6 | D | Horizon (seen part) = **rows of vineyards** (green rows with posts) on rolling ground; roadside: **cypresses**, bushes, chevrons; the 3000 shot is on a crest (all green). Modern: open fields, no vines. (`--set-course 1900:22`) | Pale limestone 18th-century facades (Place de la Bourse, the water mirror), **Pont de pierre** (17 brick-and-stone arches) over the brown Garonne; outside town **rows of vineyards** and a château with turrets; warm afternoon light. [Place de la Bourse](https://en.wikipedia.org/wiki/Place_de_la_Bourse) |
| 23 | **Monaco** | 3 | D | At 3000 the sky is clear cyan (the "grey sky" of the RE notes not seen here); horizon = a **green hill (the Rock) covered in white buildings** over a **blue sea strip**; START gantry. Modern: green hills, no town, no sea. (`--set-course 1900:23`) | Keep the original's clear sky (the RE notes mention a grey sky; not seen at 3000). Cliffs dropping to the sea, **harbour full of white yachts**, tall apartment blocks stacked on the rock, the **Casino** (Beaux-Arts, two towers, green roofs) and the Prince's Palace; the F1 street circuit's **tunnel** and Armco barriers with sponsor boards; palms and pines. [Monte Carlo Casino](https://en.wikipedia.org/wiki/Monte_Carlo_Casino) |

### Italy (courses 24-27)

| # | City | Laps | Light | Original | What the 3D stage should add |
|---|---|---|---|---|---|
| 24 | **Pisa** | 3 | D | Sky clear cyan; horizon = **cream Duomo + white Leaning Tower**, white **red-roofed Tuscan houses**, cypresses, **blue hills**; ground green; roadside: bushes, chevrons. Modern: grey ghost hills, a faint leaning tower only at ~10200. (`country_italy`) | **Leaning Tower** (white marble cylinder, 8 storeys of arcades, 56 m, leaning ~5°) with the **Duomo** (striped marble, dome) and round **Baptistery** on the green lawn of the Piazza dei Miracoli; flat Tuscan plain, umbrella pines, cypresses; warm light. [Leaning Tower of Pisa](https://en.wikipedia.org/wiki/Leaning_Tower_of_Pisa) |
| 25 | **Rome** | 6 | D | Sky cyan; horizon = the **Colosseum (ochre arches)** among cream/brown **city blocks** and green bushes; START gantry. Modern: no skyline. (`--set-course 1900:25`) | **Colosseum** (travertine ellipse, 4 tiers of arches, broken on one side, 48 m); **St Peter's dome** on the horizon; Forum columns; umbrella pines (flat crowns on tall bare trunks), ochre/terracotta buildings, cobbles; golden light. [Colosseum](https://en.wikipedia.org/wiki/Colosseum) |
| 26 | **Sicily** | 3 | D→N 102 s | Sky **deep-to-light blue gradient**; horizon = **yellow-green rounded hills with white villages** (no Etna in the seen part); ground green; START gantry. Night falls at ~102 s (not reached by the 3200 frames rendered). Modern: lavender sky, plain hills. (`--set-course 1900:26`) | **Mount Etna** (wide smoking cone, snow on top, glowing at night); Greek **Doric temple** (Valley of the Temples, honey stone) on a ridge; lemon and olive groves, prickly pear, dry stone walls; dry coast; night falls at 102 s: a red glow at Etna's top sells it. [Mount Etna](https://en.wikipedia.org/wiki/Mount_Etna) |
| 27 | **Florence** | 3 | N→D 85 s | At 4200 (22 s) **night**: navy sky, a **townscape of terracotta roofs with the red Duomo dome, the campanile and Palazzo Vecchio's tower**, blue hills behind; roadside: street lamps, rocks, round trees, chevrons. Modern: **daylight on white snow** with low-poly trees — night start lost, snow invented, no townscape. (`--set-course 1900:27`) | **Duomo** (red-brick octagonal dome, 114 m, white/green marble walls) and Giotto's campanile; **Palazzo Vecchio** (crenellated tower); **Ponte Vecchio** (shops on a bridge) over the Arno; terracotta roofs, cypresses on the hills; starts at night (lamps, floodlit dome), day at 85 s. [Florence Cathedral](https://en.wikipedia.org/wiki/Florence_Cathedral) |

### UK (courses 28-31)

| # | City | Laps | Light | Original | What the 3D stage should add |
|---|---|---|---|---|---|
| 28 | **London** | 8 | D | Sky clear cyan; horizon = **Parliament with Big Ben** in the centre, a dome (St Paul's-like), grey city blocks, a **white grandstand** on the right; ground green; roadside: bushes, chevrons. Modern: a ghost cluster of generic glass towers, no Big Ben, desert on the grid (6000). (`country_uk`) | **Big Ben / Elizabeth Tower** (gold-grey Gothic clock tower, 96 m) and Parliament on the river; **Tower Bridge** (two Gothic towers, blue walkways, bascules); **St Paul's** dome; **red double-deck buses**, black cabs, red phone boxes and pillar boxes, London plane trees; overcast flat light. [Tower Bridge](https://en.wikipedia.org/wiki/Tower_Bridge) |
| 29 | **Sheffield** | 3 | D (RE: orange-brown sky) | Horizon not seen (the 3000 shot is on a crest, the strip export is empty); roadside: grey rocks, chevrons. Modern: green fields. (`--set-course 1900:29`) | Steel city: works with **chimneys and cooling towers**, red-brick terraces on hills, the Park Hill flats slab; moorland of the Peak District behind (brown-purple heather); an orange-brown sky (per the RE notes; not seen here) = industrial haze. (Gremlin Graphics was a Sheffield studio — a home-town nod is fair: a GREMLIN board.) [Sheffield](https://en.wikipedia.org/wiki/Sheffield) |
| 30 | **Loch Ness** (Scotland) | 3 | D | Sky cyan; horizon = **green rolling hills with purple mountains behind** and a small dark structure on a hill (castle?); ground green; START gantry. Modern: green fields, no hills. (`--set-course 1900:30`) | Long dark loch between steep hills; **Urquhart Castle** (ruined square tower on a headland); heather (purple), Scots pine, bracken; low cloud and mist; a Nessie silhouette in the water is the obvious joke and fits the game's humour (speech balloons). [Urquhart Castle](https://en.wikipedia.org/wiki/Urquhart_Castle) |
| 31 | **Stonehenge** | 4 | D | Sky cyan; horizon = a **very low flat plain** with a thin line of **lilac-grey stones** (the circle, tiny); START gantry. Modern: green fields, nothing on the horizon. (`--set-course 1900:31`) | Open chalk grassland of Salisbury Plain, huge sky; **Stonehenge**: a ring of grey sarsens (4 m uprights with lintels, ~30 m ring) and the inner trilithons; round barrows (grass mounds); hedgerows; low sun (sunrise or sunset). The last course of the championship: worth a special sky. [Stonehenge](https://en.wikipedia.org/wiki/Stonehenge) |

## What the original actually shows (the seen courses)

Seen in all 32 courses (9 from recordings, 23 with `--set-course` before the race load):

- **The sky is always a clear, saturated cyan** (SNES colour ~`#00B8F8`, flat, no clouds),
  Las Vegas included (the RE notes' "orange-brown sky" for Las Vegas and Sheffield was not
  what these renders show; Sheffield was not seen). The modern skies are pale, greyish or
  lavender with big clouds: the single biggest change of mood against the original
  (MUST-FIX R-09).
- **Every horizon is a recognisable, flat silhouette, present from the grid**: dunes (Las
  Vegas), the hillside sign (LA), Sugarloaf peaks and sea (Rio), city wall with neon (Tokyo),
  palace row and lake (Munich), harbour houses and ferry (Stockholm), Eiffel Tower (Paris),
  Duomo and Leaning Tower (Pisa), Parliament and Big Ben (London). The modern skyline is
  missing, generic (the same grey "ghost towers" in Munich, Stockholm, Pisa, London) or late.
  This is the first thing to fix for "soul": **ship one authored far skyline per course**
  instead of learning it from the screen (MUST-FIX R-05).
- **Roadside kits are small and country-specific**: cacti (USA desert), palms (LA), nobori
  banners (Tokyo), lamps + warning triangles + spruces (Stockholm), bushes (Paris, Pisa,
  London), always the red/white chevrons. The modern roadside keeps the chevrons and some
  trees but swaps species (round trees in Stockholm, no palms in LA) and drops the signs.
- **Ground**: green stripes almost everywhere; sand (Las Vegas, with green stretches),
  icy blue-white (Stockholm). The modern ground starts as the previous course's (desert) on
  every first race (QA-005) — in `chris_session_03` (verify mode) Copenhagen and Paris (and
  the other courses of that run) stay desert for the whole race.
- **Night and dawn are real moods in the original**: New York (navy + lit windows),
  Hiroshima (red dawn over the sea torii), Black Forest and Florence (navy night starts),
  Cologne (night falls at 47 s). The modern look draws Hiroshima, Black Forest and Florence
  in **daylight on white snow** and keeps Cologne in daylight after 47 s (MUST-FIX R-06).
- **Snow/ice ground is Scandinavian only** (Stockholm, Copenhagen, Helsinki, Oslo: icy
  pale stripes). The modern white "snow" ground in Black Forest, Florence, Hiroshima is
  invented (it is the night/dawn ground misread).
- **The START gantry** is a banner with "START", chequered flags and traffic-light pods on
  lattice legs; the modern gantry is a plain white/red bar on two posts.


## Sources

- Reverse-engineering notes on the USA ROM: <https://github.com/pedro-walter/top-gear-snes-re> (`docs/03`, `docs/06`, `docs/10`, `docs/13`)
- <https://en.wikipedia.org/wiki/Top_Gear_(video_game)>
- <https://playingwithsuperpower.com/top-gear-review/> (horizon strips, headlights at night)
- <https://almarsguides.com/retro/walkthroughs/snes/games/topgear/> (course order, passwords)
- Per-city Wikipedia articles linked in the table rows.
