# Legal notes

## What this is

**Race Masters** is an unofficial, non-commercial fan project. It is a fork of the
[Top Gear SNES Port](https://github.com/thebradshaw/topgear-pc) by thebradshaw, which provides
the C port of the game, the launcher and the tools; the 3D mod (the 3D renderer, its art
pipeline, the 2D/3D transition, the pit stop, the music tools and the rest) is built on top
of it. Thanks to thebradshaw for that work.

## Not affiliated

Race Masters is not affiliated with, endorsed by or sponsored by Nintendo, Kemco, Gremlin
Graphics or their successors, or by any other rights holder. "Top Gear", "Nintendo",
"Super Nintendo", "SNES", "Kemco" and the other names mentioned are trademarks of their
respective owners; they are used only to say which game and which ROM this project works
with. Race Masters is not a "Top Gear" product.

## No game data

- Neither this repository nor its downloads contain the game: no ROM, and no graphics,
  palettes, music, sound samples, text or code bytes taken from it.
- The program needs a ROM of **Top Gear (USA)** for the Super Nintendo (SHA-256
  `ca9889f17f184b3d99a2eaaa82af73e366f03ed00313fdd369e5e023b208e788`). Use only a ROM you
  are legally entitled to use, such as one you dumped from a cartridge you own; the rules
  differ between countries and you are responsible for following yours.
- Everything of the game that appears on screen (its title logo and licence lines, menus,
  courses, rivals' taunts, pictures and sound) is read from your ROM while the program runs
  and is not stored in this project. What the 3D view learns from it is kept only in your own
  settings folder.
- The art in the repository is original and is drawn by committed scripts: the logo, icon
  and Start word (`tools/gen_launcher_art.py`), the HUD font atlas (`tools/make_font_atlas.py`),
  the landmark models (`tools/make_landmarks.py`). The cars' model files are made by
  `tools/cars/` and are not in the repository.
- Remade or re-arranged music is never part of this project or its downloads, and is not
  hosted anywhere; the program only plays files in its `audio-modern/` folder (or the per-user
  one), which each player makes on their own computer from their own ROM (the launcher's
  Build music). The downloads carry only the code that makes them (`music-tools/`); the
  instrument libraries and tools it uses are fetched from their official sources on the
  player's computer, after the player agrees (`tools/music/THIRD_PARTY.md`).
- Real places and landmarks appear as simple models made from scratch by our scripts; their
  owners have not endorsed this project.

## Components and licences

| Component | Where | Licence |
|---|---|---|
| LakeSnes (SNES emulator core, with local changes) | `third_party/lakesnes/` | MIT (`LICENSE.txt`; `ppu_full.c` is not part of LakeSnes, see `SOURCE.txt`) |
| SDL 3 | downloaded at build time, linked statically | zlib |
| stb_vorbis | `third_party/stb_vorbis/` | MIT or public domain (`LICENSE.txt`) |
| Barlow Condensed (font) | `third_party/barlow/` | SIL Open Font License 1.1 (`OFL.txt`) |
| Oxanium (font) | `third_party/oxanium/` | SIL Open Font License 1.1 (`OFL.txt`) |

The fonts are drawn into the programs (the HUD's font atlas, the launcher's logo). Release
downloads carry these licences in their `licenses/` folder (`tools/release_licenses.sh`).
The Python tools use Pillow, NumPy and fontTools, which are not distributed with the
project.

## This project's licence

Race Masters is free software under the **GNU General Public License, version 3 or (at your
option) any later version** ([LICENSE](LICENSE)), the licence its music generator needs
(`tools/music` uses GPL-3.0 libraries and plug-ins, see `tools/music/THIRD_PARTY.md`). The
third-party components above keep their own licences.
