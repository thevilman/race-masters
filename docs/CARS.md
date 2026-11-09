# The cars' models and textures (tgcar version 3)

The five cars of the modern look - ROSSO, GLACIER, VIOLET, AZURE (the select screen's) and AMBER
(the rivals' fifth, VIOLET's body) - are files in the player's art folder,
`art3d/cars/car-<name>.tgcar`, read by `src/mod3d/view3d.c` (`car_mesh`). They are not from the
ROM and are not in the repository (like the rest of `art3d/`); the scripts that make them are.

## Making them

```
python3 tools/cars/make_cars.py --blender BLENDER --vehicles VEHICLES_PY --out ART/cars \
    [--work DIR] [--cars rosso,glacier,violet,azure,amber] [--preview DIR]
```

- `BLENDER`: Blender 4.2 LTS for Linux (the official tarball from download.blender.org, e.g.
  `blender-4.2.23-linux-x64.tar.xz`, sha256 checked), run headless (`-b`); the bakes run on
  the CPU (Cycles), no GPU needed. ~20 s a car.
- `VEHICLES_PY`: the specialist remake's `vehicles.py` (the player's concept folder, not in
  the repository): the cars' design - each body's station drawing, cabin
  profile, axles, its lamps, intakes, vents, wing - built exactly as it builds them, so the cars
  stay the ones approved (and, through them, the original game's four).
- Two stages, both committed:
  1. `tools/cars/build_cars.py` (inside Blender): geometry, UVs, bakes, levels of detail.
  2. `tools/cars/paint_cars.py` (Python 3, numpy, scipy, Pillow): the texture and the file.
- `--preview DIR` writes each atlas as PNG (`_rgb`: what the texture holds, `_tint`: the paint
  mask, `_painted`: with the car's own paint).

### The geometry (build_cars.py)

On top of vehicles.py's body:

- **The greenhouse with real openings.** vehicles.py lays its glass over a closed painted
  cabin; here the same roof, screens and side windows are panes set 7 mm into openings, in a
  black frame (a ring of quads round each pane, the pillars and rails out to the cabin's
  outline), lined inside (a dark shell 12 mm in, so the far pillars are there when seen
  through the glass).
- **An interior** to be seen through the glass: floor, dashboard with its binnacle, two bucket
  seats (cushion, back, side wings, headrest), a steering wheel (rim and three spokes, placed
  where the screen leaves it room), a roll bar following the cabin's sides, a low bulkhead.
- **One wheel per car, lean and whole**: a tyre with bead, sidewall bulge, shoulder and tread
  (lettering and grooves are texture), the rim's lip, barrel and back, vehicles.py's spoke
  design (count, sweep, width per car), hub and centre cap, a brake disc with its hat, and the
  caliper apart (it steers with the wheel, it doesn't spin). Every face of a solid of
  revolution is turned away from its core circle (vehicles.py's were inside out in places:
  the game culls by the normals, so a sidewall facing in is not drawn).
- **A rear diffuser** with four strakes on the cars without one; exhaust tips in chrome.
- **The game mesh (level 0)** is that model without its fine detail - shut lines, seams,
  gaskets, louvres, slats, grille bars, bolts, plate markings, drilled holes, wipers - which is
  baked into the texture from the full model. Panels keep two bevel segments, small parts one;
  flat patches lose their grid (limited dissolve, 1.2 deg); small round parts (48-sided lamp
  and exhaust cylinders) are decimated; the arches' rolled lip is kept as a four-sided tube.
- **Levels 1 and 2** by edge collapse (Blender's Decimate keeps the UV seams), at 50 % and 22 %
  of level 0; level 2 without the interior (its glass is drawn opaque). The game makes its two
  lightest levels at load by vertex clustering of level 2, as before.

| triangles | level 0 | 1 | 2 | far (made at load) | wheel |
|---|---|---|---|---|---|
| ROSSO before (version 2) | 10 439 | 5 835 | 3 951 | 712 / 488 | 1 263 |
| ROSSO now | 7 334 | 3 666 | 1 612 | 1 102 / 820 | 1 190 + caliper 44 |
| GLACIER before | 8 685 | 4 838 | 3 265 | 757 / 547 | 1 331 |
| GLACIER now | 6 960 | 3 479 | 1 530 | 1 034 / 753 | 1 226 + 44 |
| VIOLET before | 14 937 | 10 187 | 8 242 | 833 / 511 | 1 263 |
| VIOLET now | 8 649 | 4 323 | 1 902 | 1 290 / 828 | 1 190 + 44 |
| AZURE before | 8 215 | 4 297 | 2 694 | 719 / 561 | 1 286 |
| AZURE now | 8 351 | 4 175 | 1 837 | 1 129 / 840 | 1 202 + 44 |
| AMBER (VIOLET's body) | as VIOLET | | | | |

Level 0 now includes the interior (~1 200 triangles) and the open greenhouse; the old level 0
was a 20 % collapse of the full model, much of it spent on its tubes (seams, gaskets, tread
lines) that are texture now.

### UVs and bakes (build_cars.py)

One 1024 x 1024 atlas per car. Smart UV projection (48 deg), islands made equally dense, then
scaled by what they are worth (wheel 1.7, interior 0.42, underside 0.3, the number plate 3,
lamps 1.6), packed with a 0.7 % margin. Bakes into it (Cycles, CPU):

- position and normal of the game mesh (emission), for everything painted by place;
- albedo from the full model (diffuse colour, selected to active, cage 12 mm; metallic set to 0
  first, since Cycles' diffuse colour is base x (1 - metallic)): the shut lines, seams, louvres,
  slats, bolts land in the texture;
- the paint mask (emission: paint white) and material ids (emission: one value per material)
  from the full model;
- ambient occlusion on the game mesh: the body with all four wheels, then the wheel alone (it
  turns: nothing of the body on it).

### The texture (paint_cars.py)

Every texel knows where it is on the car, so the materials and the livery are painted by place:

- **Tyres**: tread grooves and sipes; sidewall lettering round the rim (RACE MASTERS PERFORMANCE
  RADIAL 225/45 ZR16), a fine ring. The right wheels are their own mesh whose outer sidewall takes the
  texture of the point opposite front to back - a right wheel is a left one turned round, so
  its lettering reads the right way (a mirrored wheel reads it backwards).
- **Rims**: a machined face, a polished lip, darker spoke sides, five lug nuts, a chevron badge
  on the cap. **Brakes**: a dark disc with its pad track, concentric marks, two rings of
  cross-drilled holes (vehicles.py's 18 and 15), a vented edge, a dark hat; a label on the
  caliper.
- **Lamps**: each lamp unit found as a cluster (texels joined in 3D); headlamps as rows of
  reflector bowls with a projector, a dark ring and a chrome ring, a fluted lens; tail lamps as
  a honeycomb with a dark border and a clear reverse light at the inner end. Lamps are tinted
  by the vertex like the paint, so the brake lights brighten as before.
- **Intakes**: a fine diamond grille mesh (below the beltline: not the window frames).
  **Carbon**: a 2 x 2 twill on the made carbon parts and the satin trim low down (splitters,
  valances, diffusers). **Glass**: a dark ceramic band thinning out from each pane's edge, a
  shade band at the top of the screen. **Interior**: carpet and plastics; seats with a pleated
  insert and stitching in each car's accent colour. **Exhaust tips** polished; an invented
  number plate (RMS 17, 28, 39, 50, 44).
- **Liveries**, projected on the body: ROSSO twin white stripes nose to tail; GLACIER a black
  sash down the doors with a red pinstripe and a black bonnet panel; VIOLET gold pinstripes
  along the shoulder and over the bonnet; AZURE a white line along its two-tone edge and an
  endurance number panel; AMBER a wide black stripe with a chequered band. Invented sponsors
  only (ZEPHYRA, KORVAX OIL, HALCYON, STRATO, MERIDIA, NIMBUS, and the project's own RACE MASTERS), in
  the repository's fonts (Barlow Condensed, Oxanium: SIL OFL). The race number's roundel (or
  panel) is painted; its digits are drawn by the game, every car its own.
- **Ink**: the decals that must stand out from whatever paint the car wears (stripes, the sash,
  sponsors, the white line, AMBER's stripe and chequer) are an ink mask, not colours: the game
  makes a white-ink and a black-ink texture from it and picks by the paint's lightness (white
  on dark paint, black on light: a rival's vivid paint is the game's choice). Fixed-colour
  decals (VIOLET's gold, GLACIER's red pinstripe, the roundel) stay as painted.
- **The paint stays the game's**: where the body is paint the texel holds only its shading
  (rgb ~ 1, lines, a light occlusion) and its alpha says "paint here"; the renderer multiplies
  in the vertex's colour - the model's own for the players, its vivid paint (below) for a
  rival (the colour lock). Open panels are kept at 1 (paint brightness measured against version
  2 in the same views: within 1-3 %).
- Every texel outside the islands is filled from its nearest island (edge padding: mipmaps
  don't bleed), and a 32 x 32 white block takes the corners drawn in plain colour (the far
  plain wheel).

## The file (version 3)

Little-endian. `"TGCR"`, u32 version 3, u32 levels (3). Per level: u32 flags (bit 0: it has an
interior - its glass is see-through), u32 triangles, then per corner 32 bytes: 3 f32 position
(x right, y up, z forward, metres, the footprint's middle at 0), 3 f32 normal, u8 class, u8 r g b,
u16 u v. The glass comes last in each level (drawn after what it lets through). Then the wheel
(u32 triangles + corners: the left front, around its hub, outside at -x), its fixed part (the
caliper), the right wheel (outside at +x); 4 hubs (3 f32: front left, front right, rear left,
rear right); the race number (f32 along, up, out, radius; u32 shape 0 roundel / 1 panel; u32 the
digits' ARGB); the white block (2 f32 u v); the atlas (u32 w h, then w x h ARGB8888, its first
row at v = 0); the ink mask (u32 1, then w x h u8; optional). Version 1 and 2 files are still
read (no texture: drawn in their colours).

Classes: 0 trim (chrome or satin by colour), 1 paint, 2 glass, 3 tail lamp, 4 head lamp,
5 rubber, 6 stripe (paint), 7 interior (satin), 8 chrome.

## Drawing them

- The atlas is two of the view's pictures (`CAR_TEX`: white ink, `CAR_TEX + 5`: black ink),
  sent ahead on the loading card, with their mip levels (made at load, a 2 x 2 box); each car
  drawn with the one its paint needs (Rec. 709 lightness over 0.58: black ink). `TgV3Pic.mask`: its alpha is the tint mask, the
  surface's colour is rgb x mix(1, vertex colour, alpha); `TgV3Pic.mips`: drawn trilinear.
- GL (`src/gl3d.c`): every mip level uploaded, LINEAR_MIPMAP_LINEAR, up to 8x anisotropic
  where the driver has it; the material shader takes the texture's colour before the light
  (the clear coat, glass, chrome, rubber are e015572's). See-through glass grows opaque as it
  turns away (Fresnel). The basic fallback lights the paint at the corners and draws the rest
  of the texture unlit; the SDL fallback draws the corners' colours.
- Software (`tg_v3_raster`, headless): the same, the mip level from the uv's screen
  derivatives per pixel, bilinear in two levels (as GL's LINEAR_MIPMAP_LINEAR).
- **Metallic flake**: one flake to a texel, one in twenty catching the light (an integer hash of
  the texel, the same in GLSL and C), sparkling in the sun's lobe, faded out once a texel is
  smaller than about two thirds of a pixel (else it would twinkle as noise at a distance).
  Paint at night keeps a little light of its own (the street lamps, the field's lights).
- `tools/qa/gl_check.py` runs the game's GLSL on Mesa against the software picture on 25
  showcase pictures of the five cars, textured.

## The rivals' paint: vivid (owner: "you could add more vivid colors to the computer cars")

The game gives a race's rivals a few dull colours, many cars sharing one (Las Vegas: 18 rivals
in a slate `#525273` and an orange `#CD6228`). The colour lock stays (each rival's colour is
taken from its palette once a race and held), but each game colour now stands for a family of
a curated palette of twelve modern racing paints (`vivid_paint` in view3d.c):

| paint | sRGB | | paint | sRGB |
|---|---|---|---|---|
| racing red | `#D0121E` | | British racing green | `#0D5A33` |
| sunset orange | `#F5620F` | | teal | `#00A3AE` |
| signal yellow | `#F5C30C` | | electric blue | `#1752E6` |
| gold | `#C9971F` | | royal purple | `#5E22B0` |
| lime green | `#7DC81A` | | hot magenta | `#DC1A8A` |
| pearl white | `#EDEDE6` | | gloss black | `#141418` |

Families, the game colour's own first: warm (reds to yellows) orange, red, yellow, magenta,
gold; greens lime, British green, yellow, teal; blues and cyans electric blue, teal, purple,
British green, black; purples and magentas purple, magenta, blue, red; light greys and silver
pearl white, gold, teal; dark greys and slate black, blue, British green, purple (HSL saturation
under 0.18 is a grey). Which member a rival wears: its place on the grid among the cars of its
palette, front first, taken at the race's first frame (`vivid_rank`), the family in turn. The
game's grid alternates its colours, so cars side by side or one behind the other never share a
paint and a paint comes back only every four or five cars of a family: Las Vegas's grid wears
nine paints, each twice. Deterministic (the same frame of the same replay), constant for the
race; the neutral silver before the palettes hold colours is unchanged (the trace check knows
it). The players' own cars (and 1-player's COMPUTER car) keep their model's paint.

## Research (2026-11-03) and what was taken

- **LODs for racing cars**: Microsoft / Simplygon, "How to Create AAA Quality Vehicle LODs for
  Racing Games" (2025): automatic reduction for the levels, keep the silhouette, shading
  importance for glossy paint (we weld normals by crease angle at every level), distant cars as
  simple proxies with a baked material (our clustered far levels share the atlas). Unity racing
  car assets as a scale: ~9.4k / 5.9k / 2.2k / 0.3k triangles over four levels, body / windows /
  rims split, a texture atlas.
  ([Microsoft Game Dev](https://developer.microsoft.com/en-us/games/articles/2025/12/level-of-detail-vehicle-models-simplygon/),
  [Unity Asset Store](https://assetstore.unity.com/packages/slug/24864))
- **Texture budgets**: 1024-2048 for a main racing car's atlas (Polycount discussions); here
  1024 per car with ~200 texels a metre on the body, the wheel denser.
  ([Polycount](https://polycount.com/discussion/comment/1468930))
- **High to low baking**: Cycles' "selected to active" with a cage extrusion: the full model's
  detail into the game mesh's texture.
  ([Blender manual 2.6, bake](https://wiki.blender.jp/Doc:2.6/Manual/Render/Cycles/Bake))
- **Recolourable paint**: a mask channel decides where the paint colour goes, the rest of the
  texture fixed (BeamNG vehicle material layers - paint unmasked, fixed details masked; SCS
  paint job masks; Outerra's recolour channel).
  ([BeamNG](https://documentation.beamng.com/modding/materials/vehicle/typicalmaterials/),
  [SCS](https://modding.scssoft.com/wiki/Documentation/Engine/Units/accessory_paint_job_data),
  [Outerra](https://github-wiki-see.page/m/Outerra/anteworld/wiki/CDR-Shader-%E2%80%93-Re%E2%80%90Coloring,-Dirt,-Rust))
- **Glass and interiors**: opaque first, translucent after; third-person cars with a partial
  interior (Forza draws only what the camera can see of it).
  ([Khronos forum](https://community.khronos.org/t/drawing-a-mixture-of-opaque-translucent-panels/36482),
  [Forza forums](https://forums.forza.net/t/realistic-mirrors-interior-view/620452))

## Limits, open

- The game's GL window on the player's PC (MSAA, anisotropic filtering, the driver) is not seen
  here; gl_check proves the shader on Mesa (D3D12 and llvmpipe).
- The race number's digits are flat polygons a centimetre off the curved door (as before).
- No normal map: panel detail is in the albedo and occlusion only (the 32-byte vertex has no
  tangent; a derivative-based frame would be the next step).
