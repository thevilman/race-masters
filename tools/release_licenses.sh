#!/bin/sh
# Copies the licences of everything built into the programs into a release's licenses folder:
#   sh tools/release_licenses.sh BUILD_DIR OUT_DIR
# LakeSnes (MIT; also in music-tools/tg_audio), SDL3 (zlib; fetched into BUILD_DIR by CMake),
# stb_vorbis (MIT or public domain), and the SIL Open Font License of the fonts drawn into the
# programs (Barlow Condensed in the game's HUD and menus, Oxanium and Barlow Condensed in the
# launcher's logo). music-tools/ is this project's own code (GPL-3.0-or-later); what it
# downloads on the player's computer is listed with its licences in
# music-tools/music/THIRD_PARTY.md.
set -e
build="$1"
out="$2"
root="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$out"
cp "$root/LICENSE" "$out/RaceMasters-GPL-3.0.txt"  # (the project's own: GPL-3.0-or-later)
cp "$root/third_party/lakesnes/LICENSE.txt" "$out/LakeSnes-LICENSE.txt"
cp "$root/third_party/stb_vorbis/LICENSE.txt" "$out/stb_vorbis-LICENSE.txt"
cp "$root/third_party/barlow/OFL.txt" "$out/BarlowCondensed-OFL.txt"
cp "$root/third_party/oxanium/OFL.txt" "$out/Oxanium-OFL.txt"
sdl=""
for f in "$build/_deps/sdl3-src/LICENSE.txt" "$build"/_deps/*sdl*-src/LICENSE.txt; do
  if [ -f "$f" ]; then sdl="$f"; break; fi
done
if [ -z "$sdl" ]; then
  echo "SDL3's LICENSE.txt not found under $build/_deps" >&2
  exit 1
fi
cp "$sdl" "$out/SDL3-LICENSE.txt"
