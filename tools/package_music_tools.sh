#!/bin/sh
# Puts the modern-music generator into a release's music-tools folder, for the launcher's
# Build music (docs/AUDIO.md):
#   sh tools/package_music_tools.sh BUILD_DIR OUT_DIR
# OUT_DIR/music   tools/music (generate.py, setup_tools.py, tgmusic/, the pinned requirements),
#                 without its tests
# OUT_DIR/audio   tools/audio (what reads the notes from the player's ROM)
# OUT_DIR/tg_audio[.exe]  tools/audio's tg_audio, built by the game's CMake build (target tg_audio)
# Code only: no music, no samples, no instrument library, nothing of the game. The players make
# the music on their own computers from their own ROMs.
set -e
build="$1"
out="$2"
root="$(cd "$(dirname "$0")/.." && pwd)"
rm -rf "$out"
mkdir -p "$out/music" "$out/audio"
(cd "$root/tools/music" && find . -type f ! -path './tests/*' ! -path '*/__pycache__/*' ! -name '*.pyc') |
  while read -r f; do
    mkdir -p "$out/music/$(dirname "$f")"
    cp "$root/tools/music/$f" "$out/music/$f"
  done
for f in "$root"/tools/audio/*; do
  [ -f "$f" ] && cp "$f" "$out/audio/"
done
if [ -f "$build/tg_audio.exe" ]; then
  cp "$build/tg_audio.exe" "$out/"
elif [ -f "$build/tg_audio" ]; then
  cp "$build/tg_audio" "$out/"
else
  echo "tg_audio is not built in $build (cmake --build $build --target tg_audio)" >&2
  exit 1
fi
# Nothing that isn't code, text or the program.
bad=$(find "$out" -type f \( -name '*.ogg' -o -name '*.wav' -o -name '*.mp3' -o -name '*.flac' -o -name '*.mid' \
  -o -name '*.sfz' -o -name '*.sf2' -o -name '*.spc' -o -name '*.sfc' -o -name '*.smc' -o -name '*.zip' \))
if [ -n "$bad" ]; then
  echo "music-tools would carry data files:" >&2
  echo "$bad" >&2
  exit 1
fi
