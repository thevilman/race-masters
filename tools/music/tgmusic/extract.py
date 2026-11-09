"""Step 1: the notes, from the player's own ROM.

Runs tools/audio/build_audio.py with tools/audio's tg_audio, which plays every song with the
game's own sound driver on the emulated sound chip and logs it as MIDI + a sheet per song
(rebuild/<song>/). Then parse_midi.py turns those into the note lists the arrangements read.
Everything lands in the work folder, outside the repository.

tg_audio: the release downloads carry it ready-made (music-tools/tg_audio[.exe], built by the
game's CMake build from the same source), so players need no compiler. In a source checkout
without one it is built here with CMake (out of tree, in the work folder).
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

from . import config

# The seven songs (parse_midi.SONGS; parse_midi itself needs mido, loaded only to parse).
SONGS = ["01_title", "02_las_vegas", "03_hiroshima", "04_bordeaux", "05_frankfurt", "06_qualified", "07_ending"]

# tools/music/tgmusic/extract.py -> tools/audio (in a release: music-tools/music/... ->
# music-tools/audio).
AUDIO_TOOLS = Path(__file__).resolve().parents[2] / "audio"
EXE = "tg_audio.exe" if os.name == "nt" else "tg_audio"
ROM_SHA256 = "ca9889f17f184b3d99a2eaaa82af73e366f03ed00313fdd369e5e023b208e788"  # Top Gear (USA), README.md
STAMP = "extract.json"


def rom_sha256(path):
    data = Path(path).read_bytes()
    if len(data) % 1024 == 512:  # a copier header, as src/rom.c removes it
        data = data[512:]
    return hashlib.sha256(data).hexdigest()


def tools_hash():
    """The extraction's own source: a new version of it extracts again."""
    h = hashlib.sha256()
    for name in ("tg_audio.c", "build_audio.py", "rebuild.py", "transcribe.py", "CMakeLists.txt"):
        h.update((AUDIO_TOOLS / name).read_bytes())
    return h.hexdigest()


def find_tg_audio(given=None):
    """A ready-made tg_audio: --tg-audio, $TG_AUDIO, beside the music tools (a release's
    music-tools/tg_audio[.exe]), or in a build*/ folder of a source checkout. None: build it."""
    if given:
        if not Path(given).is_file():
            raise SystemExit(f"--tg-audio: {given} not found")
        return Path(given).resolve()
    cands = [os.environ.get("TG_AUDIO"), AUDIO_TOOLS.parent / EXE]
    repo = AUDIO_TOOLS.parents[1]
    if (repo / "CMakeLists.txt").exists():
        cands += sorted(str(p) for p in repo.glob(f"build*/{EXE}"))
    for c in cands:
        if c and Path(c).is_file():
            return Path(c).resolve()
    return None


def _run(cmd, log, env=None, progress=None, heartbeat=None):
    print("    $ " + " ".join(f'"{c}"' if " " in str(c) else str(c) for c in cmd), flush=True)
    if progress is not None:
        from .progress import run_cancellable
        code = run_cancellable(cmd, progress, log=log, env=env, heartbeat=heartbeat)
    else:
        with open(log, "a", encoding="utf-8") as f:
            code = subprocess.run([str(c) for c in cmd], stdout=f, stderr=subprocess.STDOUT, env=env).returncode
    if code:
        raise SystemExit(f"failed ({code}): {Path(str(cmd[0])).name} - see {log}")


def done(work, rom_hash):
    try:
        st = json.loads((Path(work) / STAMP).read_text())
    except (OSError, ValueError):
        return False
    rb = config.audio_dir() / "rebuild"
    return (st.get("rom_sha256") == rom_hash and st.get("tools") == tools_hash()
            and all((rb / s / f"{s}.mid").exists() and (rb / s / "sheet.json").exists() for s in SONGS))


def extract(rom, keep_all=False, jobs=4, tg_audio=None, progress=None, heartbeat=None):
    """ROM -> <work>/audio/rebuild/<song>/{<song>.mid, sheet.json} -> <work>/events/<song>.json.

    tg_audio: a ready-made tg_audio (find_tg_audio). progress: cancel between and during the
    steps (tgmusic.progress); heartbeat(elapsed seconds) while the songs play."""
    work = config.work_dir()
    work.mkdir(parents=True, exist_ok=True)
    h = rom_sha256(rom)
    if h != ROM_SHA256:
        print(f"  warning: this ROM's SHA-256 is {h}, not Top Gear (USA)'s {ROM_SHA256}; carrying on", flush=True)
    if done(work, h):
        print("  already extracted from this ROM (delete the work folder to do it again)", flush=True)
    else:
        log = work / "extract.log"
        log.write_text("")
        tool = find_tg_audio(tg_audio)
        if tool is None:
            if shutil.which("cmake") is None:
                raise SystemExit("tools/audio's tg_audio is needed: the release downloads have it in music-tools/; "
                                 "in a source checkout build it (cmake --build build --target tg_audio) or install "
                                 "CMake and a C compiler")
            build = work / "build-tg-audio"
            _run(["cmake", "-S", AUDIO_TOOLS, "-B", build, "-DCMAKE_BUILD_TYPE=Release"], log, progress=progress)
            _run(["cmake", "--build", build, "-j", str(max(1, jobs))], log, progress=progress)
            tool = build / EXE
        print(f"  tg_audio: {tool}", flush=True)
        out = config.audio_dir()
        shutil.rmtree(out, ignore_errors=True)
        env = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")  # nothing written next to tools/audio
        from .progress import PYFLAGS
        print("  playing every song on the emulated sound chip (about 10 minutes)...", flush=True)
        _run([sys.executable, *PYFLAGS, AUDIO_TOOLS / "build_audio.py", "--rom", rom, "--tool", tool, "--out", out,
              "--work", work / "audio-work", "--reference", ""], log, env, progress=progress, heartbeat=heartbeat)
        if not keep_all:
            prune(work)
        (work / STAMP).write_text(json.dumps({"rom_sha256": h, "tools": tools_hash()}, indent=1))
    from . import parse_midi
    parse_midi.main(parse_midi.SONGS)


def prune(work):
    """Keep only what the generator reads (the MIDI and sheet per song); the rest of tools/audio's
    output (raw renders, stems, samples, .spc: ~1 GB) is not needed again."""
    shutil.rmtree(Path(work) / "audio-work", ignore_errors=True)
    out = config.audio_dir()
    for p in list(out.iterdir()):
        if p.name != "rebuild":
            shutil.rmtree(p) if p.is_dir() else p.unlink()
    for song_dir in (out / "rebuild").iterdir():
        if not song_dir.is_dir() or song_dir.name not in SONGS:
            shutil.rmtree(song_dir) if song_dir.is_dir() else song_dir.unlink()
            continue
        for p in list(song_dir.iterdir()):
            if p.name not in (f"{song_dir.name}.mid", "sheet.json"):
                shutil.rmtree(p) if p.is_dir() else p.unlink()
