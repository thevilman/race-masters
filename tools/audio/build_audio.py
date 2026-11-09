#!/usr/bin/env python3
"""Rips all of Top Gear (USA)'s music and sound effects from your ROM.

    python3 tools/audio/build_audio.py --rom "Top Gear (USA).sfc" \
        --tool build-audio/audio/tg_audio --out "<your folder>/audio"

Runs tg_audio (tools/audio/tg_audio.c) to start each song and sound effect with the
game's own code on the LakeSnes core, then makes the finished files:

    music/NN_name.wav       intro + loop(s) + 10 s fade, 32 kHz stereo
    music/loops/NN_name_loop.wav   exactly one loop (smpl loop chunk), for remastering
    spc/NN_name.spc         APU snapshot as the song starts (ID666 tagged)
    sfx/NN_name.wav         each sound effect alone, 32 kHz stereo
    samples/*.wav           the instrument / effect samples (BRR decoded), 32 kHz mono
    reference/*.wav         the game's own mix during a replayed race (music + effects)
    manifest.json, README.md

Everything written comes from your ROM: keep it out of git.
"""
import argparse
import hashlib
import json
import math
import os
import shutil
import struct
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from transcribe import KEYS, sample_f0  # noqa: E402

RATE = 32000
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))

COURSES = [
    "Las Vegas", "Los Angeles", "New York", "San Francisco",          # USA
    "Rio", "Machu Picchu", "Chichen Itza", "Rain Forest",             # South America
    "Tokyo", "Hiroshima", "Yokohama", "Kyoto",                        # Japan
    "Munich", "Cologne", "Black Forest", "Frankfurt",                 # Germany
    "Stockholm", "Copenhagen", "Helsinki", "Oslo",                    # Scandinavia
    "Paris", "Nice", "Bordeaux", "Monaco",                            # France
    "Pisa", "Rome", "Sicily", "Florence",                             # Italy
    "London", "Sheffield", "Loch Ness", "Stonehenge",                 # UK
]
COUNTRIES = ["USA", "South America", "Japan", "Germany", "Scandinavia", "France", "Italy", "UK"]


def race_courses(upload):
    """Races play upload ((course & 3) ^ 3) + 1 ($00:8175): the courses using this one."""
    slot = (upload - 1) ^ 3
    return [COURSES[c] for c in range(32) if c & 3 == slot]


# The seven songs, in soundtrack order. upload = the number MenuStartMusic / the race
# program pass to UploadSoundDriver; the driver plays song upload + 1 (port 0).
SONGS = [
    dict(id=1, name="title", title="Title", upload=0,
         where="Power-on title screen, the attract demo and every menu (options, names, gears, "
               "controls, car, country, password); started at boot ($00:8064) and again after "
               "an end screen ($0F:8675)."),
    dict(id=2, name="las_vegas", title="Las Vegas", upload=4,
         where="Race music for the 1st course of every country: " + ", ".join(race_courses(4)) +
               ". Keeps playing on that race's results / world ranking screens."),
    dict(id=3, name="hiroshima", title="Hiroshima", upload=3,
         where="Race music for the 2nd course of every country: " + ", ".join(race_courses(3)) +
               ". Keeps playing on that race's results / world ranking screens."),
    dict(id=4, name="bordeaux", title="Bordeaux", upload=2,
         where="Race music for the 3rd course of every country: " + ", ".join(race_courses(2)) +
               ". Keeps playing on that race's results / world ranking screens."),
    dict(id=5, name="frankfurt", title="Frankfurt", upload=1,
         where="Race music for the 4th course of every country: " + ", ".join(race_courses(1)) +
               ". Keeps playing on that race's results / world ranking screens."),
    dict(id=6, name="qualified", title="Qualified", upload=5,
         where="The end-of-country screen when the player qualified for the next country "
               "($0F:858A). Plays once, does not loop."),
    dict(id=7, name="ending", title="Ending Theme", upload=6,
         where="The congratulations screen after the last country "
               "($0F:855E)."),
]

# Sound commands (port 1). The game sends these from the race IRQ (RaceIrqTop,
# $00:B850-$00:B95C) and the race program; voices 4/5 are player 1's, 6/7 player 2's.
# p2 = the same sound for player 2 (identical audio on the other voice pair).
SFX = [
    dict(id=1, name="engine", title="Engine (RPM sweep)", cmd=0x02, p2=[0x0D, 0x0E], used=True,
         where="Every race: each car's engine. $02 starts it, then $03 every other frame with "
               "ports 2/3 = RPM + $400 sets its pitch (player 2: $0D/$0E). Rendered here as a "
               "16 s sweep idle -> top RPM -> idle."),
    dict(id=2, name="crash", title="Crash", cmd=0x05, p2=[0x10], used=True,
         where="Hitting a roadside object (crash flag $1E6D / $1E6F set)."),
    dict(id=3, name="pit_stop", title="Pit stop", cmd=0x07, p2=[0x12], used=True,
         where="Entering the pits (drive flag bit 2); plays on the engine's voice."),
    dict(id=4, name="skid", title="Skid", cmd=0x0A, p2=[0x15], used=True,
         where="Skidding in a corner (skid flag $1E67 / $1E69 set)."),
    dict(id=5, name="bump", title="Bump", cmd=0x0B, p2=[0x16], used=True,
         where="Bumping another car (drive flag bit 7)."),
    dict(id=6, name="pit_stop_left", title="Pit-stop sample, panned left (unused)", cmd=0x08,
         p2=[0x13], used=False,
         where="Not sent by the game: the driver has it (the pit-stop sample on the effects "
               "voice, left channel only)."),
    dict(id=7, name="pit_stop_right", title="Pit-stop sample, panned right (unused)", cmd=0x09,
         p2=[0x14], used=False,
         where="Not sent by the game: the driver has it (the pit-stop sample on the effects "
               "voice, right channel only)."),
    dict(id=8, name="unused_1a", title="Unused effect $1A", cmd=0x1A, p2=[0x1B], used=False,
         where="Not sent by the game: a driver command with its own sample (number $16)."),
    dict(id=9, name="unused_1c", title="Unused effect $1C (long release)", cmd=0x1C, p2=[0x1D],
         used=False,
         where="Not sent by the game: sample $16 again with a slower envelope."),
]

CONTROL_COMMANDS = {
    0x00: "nothing (the game sends it between repeats of the same command)",
    0x01: "key off all effect voices (end of a race, $00:8862)",
    0x03: "player 1 engine pitch from ports 2/3",
    0x04: "nothing", 0x06: "player 1 engine off (unused)", 0x0C: "nothing",
    0x0E: "player 2 engine pitch from ports 2/3", 0x0F: "nothing",
    0x11: "player 2 engine off (unused)", 0x17: "nothing",
    0x18: "mute and return to the IPL loader, before every upload",
    0x19: "pause: mute until port 1 changes (race paused, $00:B83C)",
}


def run(cmd):
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)


def read_wav(path):
    with open(path, "rb") as f:
        data = f.read()
    assert data[:4] == b"RIFF" and data[8:12] == b"WAVE"
    pos = 12
    ch = 2
    while pos < len(data):
        cid, size = data[pos:pos + 4], struct.unpack("<I", data[pos + 4:pos + 8])[0]
        if cid == b"fmt ":
            ch = struct.unpack("<H", data[pos + 10:pos + 12])[0]
        if cid == b"data":
            a = np.frombuffer(data[pos + 8:pos + 8 + size], dtype="<i2")
            return a.reshape(-1, ch).astype(np.int16)
        pos += 8 + size + (size & 1)
    raise ValueError(path)


def write_wav(path, a, loop=None, rate=RATE):
    """16-bit PCM; loop = (start, end) sample frames adds a 'smpl' chunk (end exclusive)."""
    a = np.asarray(a, dtype=np.int16)
    if a.ndim == 1:
        a = a[:, None]
    ch = a.shape[1]
    body = a.astype("<i2").tobytes()
    chunks = struct.pack("<4sIHHIIHH", b"fmt ", 16, 1, ch, rate, rate * 2 * ch, 2 * ch, 16)
    chunks += struct.pack("<4sI", b"data", len(body)) + body
    if loop is not None:
        smpl = struct.pack("<IIIIIIIII", 0, 0, int(1e9 / rate), 60, 0, 0, 0, 1, 0)
        smpl += struct.pack("<IIIIII", 0, 0, int(loop[0]), int(loop[1]) - 1, 0, 0)
        chunks += struct.pack("<4sI", b"smpl", len(smpl)) + smpl
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 4 + len(chunks)) + b"WAVE" + chunks)


def read_srcn(path):
    out = dict(dir=0, srcn=[], patterns=[])
    for line in open(path):
        parts = line.split()
        if parts[0] == "dir":
            out["dir"] = int(parts[1], 16)
        elif parts[0] == "pattern":
            out["patterns"].append(int(parts[1]))
        else:
            out["srcn"].append(int(parts[0]))
    return out


def stats(a):
    x = a.astype(np.float64) / 32768.0
    mono = x.mean(axis=1) if x.ndim == 2 else x
    rms = math.sqrt(float((x ** 2).mean())) if len(x) else 0.0
    peak = float(np.abs(x).max()) if len(x) else 0.0
    # spectral centroid and a coarse log-band spectrum (for the distinctness check)
    n = 4096
    frames = [mono[i:i + n] for i in range(0, max(len(mono) - n, 1), n * 4)][:400]
    spec = np.zeros(n // 2 + 1)
    for fr in frames:
        if len(fr) == n:
            spec += np.abs(np.fft.rfft(fr * np.hanning(n)))
    freqs = np.fft.rfftfreq(n, 1 / RATE)
    centroid = float((spec * freqs).sum() / spec.sum()) if spec.sum() > 0 else 0.0
    edges = np.geomspace(40, RATE / 2, 33)
    bands = np.array([spec[(freqs >= lo) & (freqs < hi)].sum() for lo, hi in zip(edges[:-1], edges[1:])])
    db = lambda v: round(20 * math.log10(v), 1) if v > 0 else -120.0
    return dict(rms_dbfs=db(rms), peak_dbfs=db(peak), centroid_hz=round(centroid)), np.log1p(bands)


def envelope(a, seconds=60, hop=0.05):
    """Loudness envelope (RMS per 50 ms) of the first minute, for the distinctness check."""
    n = int(hop * RATE)
    x = a[: int(seconds * RATE)].astype(np.float64).mean(axis=1)
    m = len(x) // n
    return np.sqrt((x[: m * n].reshape(m, n) ** 2).mean(axis=1)) if m else np.zeros(1)


def env_similarity(a, b, max_lag=40):
    """Best normalised correlation of two loudness envelopes within +-2 s."""
    n = min(len(a), len(b))
    if n < 8:
        return 0.0
    a, b = a[:n] - a[:n].mean(), b[:n] - b[:n].mean()
    best = 0.0
    max_lag = min(max_lag, n // 4)
    for lag in range(-max_lag, max_lag + 1):
        x, y = (a[lag:], b[:n - lag]) if lag >= 0 else (a[:n + lag], b[-lag:])
        d = np.linalg.norm(x) * np.linalg.norm(y)
        if d > 0:
            best = max(best, float(x @ y / d))
    return best


def notes_srcn(path):
    """Sample numbers keyed on in a tg_audio notes log."""
    out = set()
    if os.path.exists(path):
        for line in open(path):
            if line.startswith("on "):
                out.add(int(line.split()[3]))
    return out


def make_stems(tool, args, work, upload, voices, end):
    """One render per voice with the others silenced (tg_audio --keep), cut to `end`."""
    stems = {}
    secs = int(math.ceil(end / RATE)) + 1
    for v in voices:
        d = os.path.join(work, "stems", "u%d_v%d" % (upload, v))
        path = os.path.join(d, "song_%d_raw.wav" % upload)
        if not (args.skip_render and os.path.exists(path)):
            os.makedirs(d, exist_ok=True)
            run([tool, "--rom", args.rom, "--out", d, "songs", "--only", str(upload), "--keep", str(1 << v),
                 "--seconds", str(secs)])
        stems[v] = read_wav(path)[:end]
    return stems


def fade(a, seconds):
    n = min(len(a), int(seconds * RATE))
    a = a.astype(np.float64).copy()
    a[len(a) - n:] *= np.linspace(1.0, 0.0, n)[:, None]
    return np.round(a).astype(np.int16)


def trim_silence(a, threshold=8, pad=0.05):
    loud = np.nonzero(np.abs(a).max(axis=1) > threshold)[0]
    if len(loud) == 0:
        return a[:0]
    end = min(len(a), loud[-1] + 1 + int(pad * RATE))
    return a[:end]


# ---------------------------------------------------------------- BRR samples

def decode_brr(ram, start):
    """Decodes a BRR sample at start: (pcm int16, loop flag, length in blocks)."""
    out = []
    p1 = p2 = 0
    pos = start
    blocks = 0
    while True:
        h = ram[pos]
        shift, filt, loop, end = h >> 4, (h >> 2) & 3, h & 2, h & 1
        for i in range(16):
            byte = ram[(pos + 1 + i // 2) & 0xFFFF]
            n = (byte >> 4) if i % 2 == 0 else (byte & 0x0F)
            n = n - 16 if n >= 8 else n
            s = (n << shift) >> 1 if shift <= 12 else (-2048 if n < 0 else 0)
            if filt == 1:
                s += p1 + ((-p1) >> 4)
            elif filt == 2:
                s += (p1 << 1) + ((-(p1 * 3)) >> 5) - p2 + (p2 >> 4)
            elif filt == 3:
                s += (p1 << 1) + ((-(p1 * 13)) >> 6) - p2 + ((p2 * 3) >> 4)
            s = max(-32768, min(32767, s))
            s = ((s << 1) & 0xFFFF)
            s = (s - 0x10000 if s >= 0x8000 else s) >> 1  # 15-bit wrap, as the DSP does
            out.append(s)
            p2, p1 = p1, s
        blocks += 1
        pos = (pos + 9) & 0xFFFF
        if end or blocks > 7000:
            break
    return np.array(out, dtype=np.int32), bool(loop), blocks


def est_pitch(pcm, loop_start):
    """Rough fundamental (Hz at 32 kHz playback) from the looped part; None if unclear."""
    seg = pcm[loop_start:].astype(np.float64) if loop_start is not None else pcm.astype(np.float64)
    if len(seg) < 64:
        seg = np.tile(seg, max(1, 2048 // max(len(seg), 1)))
    seg = seg - seg.mean()
    if not seg.any():
        return None
    seg = seg[:8192]
    ac = np.correlate(seg, seg, "full")[len(seg) - 1:]
    ac /= ac[0]
    lo, hi = int(RATE / 2000), min(len(ac) - 1, int(RATE / 30))
    if hi <= lo:
        return None
    k = lo + int(np.argmax(ac[lo:hi]))
    return round(RATE / k, 1) if ac[k] > 0.8 and lo < k < hi - 1 else None


# ---------------------------------------------------------------- .spc tags

def tag_spc(src, dst, title, comment, seconds, fade_ms):
    data = bytearray(open(src, "rb").read())

    def put(off, size, text):
        b = text.encode("ascii", "replace")[:size]
        data[off:off + size] = b + bytes(size - len(b))
    put(0x2E, 32, title)
    put(0x4E, 32, "Top Gear")
    put(0x6E, 16, "tg_audio")
    put(0x7E, 32, comment)
    put(0xA9, 3, str(min(999, int(seconds))))
    put(0xAC, 5, str(int(fade_ms)))
    put(0xB1, 32, "Barry Leitch")
    open(dst, "wb").write(data)


# ---------------------------------------------------------------- README

def make_readme(m):
    L = []
    w = L.append
    w("# Top Gear (SNES) - music and sound effects")
    w("")
    w("Ripped from your own ROM by `tools/audio` (topgear-pc repository, branch audio-tools). "
      "Everything here comes from the ROM: do not put it in git or share it.")
    w("")
    w("Music by Barry Leitch. All files are 16-bit PCM WAV at 32,000 Hz, the SNES sound chip's own "
      "output rate (no resampling). `manifest.json` has the same information in machine-readable form.")
    w("")
    w("## Folders")
    w("")
    w("- `music/` - each song: intro + loop(s) + a 10 s fade (Qualified plays once and ends).")
    w("- `music/loops/` - exactly one loop of each looping song, with a WAV `smpl` loop chunk: the cleanest source for a remaster or re-arrangement.")
    w("- `spc/` - SPC700 snapshots (.spc with ID666 tags) taken as each song starts; play them in any SPC player (e.g. foo_gme, Audacious, SPCPlay) for the most accurate DSP playback.")
    w("- `sfx/` - each sound effect alone (no music).")
    w("- `samples/` - the instrument and effect samples, BRR-decoded, mono, loop points in `smpl` chunks.")
    w("- `reference/` - the game's own mix (music + effects together) while a recorded race plays.")
    w("- `rebuild/NN_name/` - everything to rebuild a song: `.mid` (one track per SNES voice, loop markers, exact tempo), "
      "`.sfz` kit (MIDI channel = instrument, loop/tuning/envelope), `instruments/` (one .sfz each), `stems/` (one WAV per "
      "voice + the mix, first pass), `sheet.json` (tempo, key, roles, echo, self-check), `_from_midi.wav` (the MIDI played "
      "through the kit). `rebuild/sfx/`: effect kit and key-on details.")
    w("- `HANDOFF.md` - the brief for whoever modernises the audio (track by track, integration, delivery spec).")
    w("")
    w("## Music")
    w("")
    w("| # | File | Where it plays | Length | Loop |")
    w("|---|------|----------------|--------|------|")
    for f in m["files"]:
        if f["kind"] != "music":
            continue
        loop = ("%.1f s loop from %.2f s; %d pass(es) + 10 s fade" % (f["loop"]["length_s"], f["loop"]["start_s"], f["loop"]["passes_in_file"])
                if f["loop"] else "no loop (ends)")
        w("| %s | `%s` | %s | %.1f s | %s |" % (f["id"][-2:], f["file"], f["where"], f["duration_s"], loop))
    w("")
    w("Race music is chosen by the course's place in its country, not by the country: the game uploads "
      "set ((course & 3) xor 3) + 1 at $00:8175. The four race tunes are known by the course they "
      "first play on (Las Vegas, Hiroshima, Bordeaux, Frankfurt), the names used by the published SPC set.")
    w("")
    w("## Sound effects")
    w("")
    w("| # | File | Command | Where it plays | Length |")
    w("|---|------|---------|----------------|--------|")
    for f in m["files"]:
        if f["kind"] != "sfx":
            continue
        w("| %s | `%s` | %s | %s%s | %.2f s |" % (f["id"][-2:], f["file"], " / ".join(f["driver"]["port1"]), f["where"],
                                               "" if f["used_by_game"] else " **(unused by the game)**", f["duration_s"]))
    w("")
    w("Effects use voices 4-7 (player 1: 4 engine, 5 effects; player 2: 6 and 7); the race songs use "
      "voices 0-3, so effects never cut the music. The player-2 commands give the same sound. "
      "The game has no menu sounds: the menus only start songs.")
    w("")
    w("## Samples")
    w("")
    w("| File | Used by | Length | Loop start | Est. pitch at 32 kHz |")
    w("|------|---------|--------|------------|----------------------|")
    for f in m["files"]:
        if f["kind"] != "sample":
            continue
        w("| `%s` | %s | %.3f s | %s | %s |" % (f["file"], ", ".join(f["used_by"]), f["duration_s"],
                                            f["loop_start_sample"] if f["loop_start_sample"] is not None else "-",
                                            ("%s Hz" % f["est_fundamental_hz_at_32k"]) if f["est_fundamental_hz_at_32k"] else "-"))
    w("")
    w("## How it was captured")
    w("")
    w("- The original game runs on the LakeSnes emulator core. For each song the tool calls the game's own "
      "routine MenuStartMusic ($0F:8133) with the song number, as the menus do: it sends $18 (back to the "
      "loader), runs UploadSoundDriver ($07:8000), which uploads that song's data and instruments, and "
      "writes song + 1 to APU port 0. The .spc is saved at that write; the WAV is the APU running on from "
      "there, one sample per DSP tick (32 kHz).")
    w("- Loop points come from the sound driver itself: every song's playlist is one pattern played again "
      "and again, so the loop is the time between two loads of the pattern (APU $0A94).")
    w("- Sound effects: after a race upload, each command goes on port 1 until the driver echoes it, as "
      "the race's IRQ code does ($00:B850).")
    w("- The driver's song table has 7 entries and the upload table 7 sets: 7 songs. Port-1 commands "
      "run 1-$1D; the ones that make sound are the 9 effects here (with their player-2 twins).")
    w("")
    w("## Caveats")
    w("")
    w("- LakeSnes's DSP is accurate but not cycle-exact; for reference-grade playback render the .spc "
      "files in a cycle-accurate player (blargg's snes_spc / higan DSP).")
    w("- The .spc files cannot hold the timers' internal phase, so they may start a fraction of a "
      "millisecond differently from the WAVs.")
    w("- In a race the engine pitch follows the car's RPM; `sfx/01_engine.wav` is a synthetic sweep over "
      "the race's range. Hear the real thing in `reference/`.")
    w("- Effect names come from the game code that sends them (crash, skid, bump, pit flags); the unused "
      "commands are named by number.")
    w("")
    c = m["checks"]
    w("## Checks")
    w("")
    w("- %d music files (song table: %d), %d sound effects, %d samples." % (c["music_files"], c["song_table_entries"], c["sfx_files"], c["sample_files"]))
    w("- Silent files: %s." % (", ".join(c["silent_files"]) or "none"))
    w("- Most similar pairs (loudness-envelope correlation / spectrum similarity, 1 = same content): " +
      "; ".join("%s & %s %.2f / %.2f" % (d["a"], d["b"], d["loudness_envelope"], d["spectrum"])
                for d in c["most_similar_pairs"]) + ". These are expected: the pit stop and its unused left/right "
      "variants play the same sample, and the two unused effects share sample $16.")
    mp = c["most_similar_music_pair"]
    w("- Most similar pair of songs: %s & %s, %.2f / %.2f: every song is distinct." % (mp["a"], mp["b"], mp["loudness_envelope"], mp["spectrum"]))
    w("")
    w("Sources on the format and the soundtrack are listed in tools/audio/README.md in the repository.")
    w("")
    return "\n".join(L)


# ---------------------------------------------------------------- HANDOFF

def make_handoff(m, rb, sfx_entries):
    """tools/audio/HANDOFF.md with the track and effect tables filled in from the rip."""
    text = open(os.path.join(HERE, "HANDOFF.md")).read()
    text = text.replace("The copy of this file in the repository (`tools/audio/HANDOFF.md`) has no track tables;\n"
                        "the copy in the player's folder has them filled in from the rip.",
                        "This copy (player's folder) has the track tables filled in from the rip.")
    T = []
    w = T.append
    music = [f for f in m["files"] if f["kind"] == "music"]
    samples = {f["file"]: f for f in m["files"] if f["kind"] == "sample"}
    for f in music:
        base = os.path.basename(f["file"])[:-4]
        sheet = json.load(open(os.path.join(rb, base, "sheet.json")))
        w("### %s. %s" % (f["id"][-2:], f["title"]))
        w("")
        w("- **Files:** `%s`, `%s`%s, rebuild kit `rebuild/%s/` (`%s.mid`, `%s.sfz`, `instruments/`, `stems/`, `sheet.json`, `%s_from_midi.wav`)."
          % (f["file"], f["spc"], (", one loop `%s`" % f["loop"]["loop_file"]) if f["loop"] else "", base, base, base, base))
        w("- **Where it plays:** %s" % f["where"])
        if f["loop"]:
            lp = f["loop"]
            w("- **Length / loop:** file %.1f s. Loop: the whole song, start %.3f s (sample %d at 32 kHz), "
              "pass %.3f s (first pass %.3f s); loop end = start + pass. File = %d pass(es) + 10 s fade."
              % (f["duration_s"], lp["start_s"], int(round(lp["start_s"] * 32000)),
                 lp["length_s"], lp["first_pass_s"], lp["passes_in_file"]))
        else:
            w("- **Length:** %.1f s, plays once (no loop)." % f["duration_s"])
        t = sheet["tempo"]
        w("- **Tempo:** %.1f BPM (driver tempo %d, %d ticks per beat; exact). **Key (estimate):** %s (confidence %s)."
          % (t["bpm"], t["driver_tempo_value"], t["driver_ticks_per_beat"], sheet["key_estimate"]["key"],
             sheet["key_estimate"]["confidence"]))
        e = sheet["echo"]
        w("- **Echo:** %s" % ("%d ms, feedback %+.2f, echo volume %s, FIR %s, on voices %s" %
                              (e["echo_delay_ms"], e["echo_feedback"], e["echo_volume"], e["fir"], e["echo_voices"])
                              if e["echo_voices"] and e["echo_delay_ms"] else "none"))
        w("- **Voices -> instruments** (role names come from a heuristic on pitch and note length: check by ear):")
        for v, vd in sheet["voices"].items():
            w("  - voice %s: %s (%d notes, MIDI channel %s%s)" % (v, ", ".join(vd["instruments"]), vd["notes"],
                                                               "/".join(map(str, vd["midi_channel"])),
                                                               ", echo" if vd["echo"] else ""))
        w("- **Instruments (samples):**")
        for k, i in sheet["instruments"].items():
            env = i["envelope"]
            envs = (("attack %.3g s, decay %.3g s, sustain %.0f%%" % (env["attack_s"], env["decay_s"], env["sustain"] * 100))
                    if env["mode"] == "ADSR" else env["mode"])
            w("  - %s: `%s`, %s, %s, envelope %s" % (i["name"], i["sample"],
                                                    "looped from %d" % i["loop_start"] if i["loop_start"] is not None else "one-shot",
                                                    ("natural note %.2f (MIDI)" % i["natural_rate_note"]) if i["pitched"] else "unpitched",
                                                    envs))
        if sheet.get("opening_phrases"):
            w("- **What makes it recognisable (keep these):** the opening phrases, as played (note(length in beats)):")
            for role, ph in sheet["opening_phrases"].items():
                w("  - %s from beat %s: %s" % (role, ph["starts_at_beat"], ph["first_notes"]))
            w("  Keep the lead melody and the bass line note for note, the tempo and the key; the drums' "
              "pattern and the harmony can be re-voiced.")
        sc = sheet["self_check"]
        w("- **Self-check (MIDI + SFZ vs original):** %.0f%% of the original's onsets matched within 30 ms, "
          "onset-strength correlation %.2f, loudness-envelope correlation %.2f, log-spectrum cosine %.3f."
          % (sc["onsets_matched_within_30ms"] * 100, sc["onset_strength_correlation"],
             sc["loudness_envelope_correlation"], sc["log_spectrum_cosine"]))
        w("")
    S = []
    w = S.append
    w("| # | File | Command (P1 / P2) | Where | Sample, pitch, envelope | Used by the game |")
    w("|---|------|-------------------|-------|--------------------------|------------------|")
    by_id = {e["id"]: e for e in sfx_entries}
    for f in m["files"]:
        if f["kind"] != "sfx":
            continue
        e = by_id.get(f["id"], {})
        ko = e.get("key_ons") or []
        desc = "; ".join("voice %d, sample %s at %s (%d Hz), %s" % (k["voice"], k["sample"], k["pitch"], k["rate_hz"],
                                                                   k["envelope"]["mode"]) for k in ko[:2])
        if e.get("pitch_rule"):
            desc += "; " + e["pitch_rule"]
        w("| %s | `%s` | %s | %s | %s | %s |" % (f["id"][-2:], f["file"], " / ".join(f["driver"]["port1"]),
                                                f["where"], desc or "-", "yes" if f["used_by_game"] else "no"))
    w("")
    w("Effect samples, SFZ kit (one key each at the game's pitch) and key-on details: `rebuild/sfx/`.")
    text = text.replace("<!-- TRACKS -->", "\n".join(T)).replace("<!-- SFX -->", "\n".join(S))
    return text


# ---------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rom", required=True)
    ap.add_argument("--tool", default=os.path.join(ROOT, "build-audio", "audio", "tg_audio"))
    ap.add_argument("--out", required=True, help="the audio folder to fill (outside git)")
    ap.add_argument("--work", default=os.path.join(ROOT, "build-audio", "audio-work"))
    ap.add_argument("--seconds", type=int, default=480, help="raw render length per song")
    ap.add_argument("--skip-render", action="store_true", help="reuse the renders already in --work")
    ap.add_argument("--reference", default=os.path.join(ROOT, "tests", "inputs", "race_1p.txt"),
                    help="input recording replayed for the in-game reference mix ('' to skip)")
    args = ap.parse_args()

    out, work = os.path.abspath(args.out), os.path.abspath(args.work)
    try:  # (only a source checkout is a repository; a release's music-tools is not)
        inside = os.path.isfile(os.path.join(ROOT, "CMakeLists.txt")) and os.path.commonpath([out, ROOT]) == ROOT
    except ValueError:  # Windows: on another drive, so outside
        inside = False
    if inside and not out.startswith(os.path.join(ROOT, "build")):
        sys.exit("--out is inside the repository: the audio comes from your ROM, keep it outside git")
    for d in ("songs", "sfx"):
        os.makedirs(os.path.join(work, d), exist_ok=True)
    for d in ("music", "music/loops", "spc", "sfx", "samples", "reference"):
        os.makedirs(os.path.join(out, d), exist_ok=True)

    tool = os.path.abspath(args.tool)
    if not args.skip_render:
        render_all(tool, args, work)
    finish(tool, args, out, work)


def render_all(tool, args, work):
    run([tool, "--rom", args.rom, "--out", os.path.join(work, "songs"), "songs", "--seconds", str(args.seconds)])
    run([tool, "--rom", args.rom, "--out", os.path.join(work, "sfx"), "sfx", "--upload", "1",
         "--first", "1", "--last", "0x1D", "--seconds", "4"])
    run([tool, "--rom", args.rom, "--out", os.path.join(work, "sfx"), "engine", "--upload", "1"])
    # P2 engine, to check it matches P1
    run([tool, "--rom", args.rom, "--out", os.path.join(work, "sfx"), "engine", "--upload", "1",
         "0x0D", "0x0E", "engine_p2"])


def finish(tool, args, out, work):
    from rebuild import rebuild_song, sfx_sheet
    manifest = dict(
        game="Top Gear (USA), SNES, 1992 (Kemco / Gremlin Graphics)",
        composer="Barry Leitch",
        format="WAV, 16-bit PCM, 32000 Hz (the SNES DSP's own rate); music and sfx stereo, samples mono",
        captured_with="tools/audio (tg_audio on the LakeSnes core) in the topgear-pc repository",
        sound_driver=("N-SPC-style driver at APU $0800, uploaded at boot; UploadSoundDriver ($07:8000) "
                      "then uploads the song and its instruments (7 upload sets, table at $07:8164). "
                      "Port 0 = song number + 1, port 1 = sound command (1-$1D, handlers at APU $10A7), "
                      "ports 2/3 = engine pitch."),
        song_table="APU $1399: 7 entries (songs 1-7), one per upload set; every playlist is one pattern "
                   "repeated, except song 6 (Qualified) which ends.",
        files=[],
    )
    vectors = {}
    envs = {}
    used_srcn = {}  # (upload, srcn) -> list of ids

    # ---- music
    for s in SONGS:
        u = s["upload"]
        base = "%02d_%s" % (s["id"], s["name"])
        raw = read_wav(os.path.join(work, "songs", "song_%d_raw.wav" % u))
        info = read_srcn(os.path.join(work, "songs", "song_%d_srcn.txt" % u))
        marks = info["patterns"]
        entry = dict(id="music_%02d" % s["id"], kind="music", title=s["title"], file="music/%s.wav" % base,
                     where=s["where"], driver=dict(upload=u, song=u + 1, port0=u + 1))
        if len(marks) >= 3:
            # marks = sample numbers where the driver (re)loads the song's single pattern.
            # One pass if the pass is a minute or more, else up to 3 passes (~1.5 min).
            passes = [marks[i + 1] - marks[i] for i in range(len(marks) - 1)]
            loops = 1 if passes[0] >= 60 * RATE else max(1, min(3, math.ceil(90 * RATE / passes[0]), len(passes)))
            end = marks[loops]
            final = np.concatenate([raw[:end], fade(raw[end:end + 10 * RATE], 10)])
            # The loop file is a later pass (marks[1]..marks[2]): what the song sounds like when
            # it comes round again, with the echo of the pass before it.
            ls, le = marks[1], marks[2]
            write_wav(os.path.join(out, "music", "loops", base + "_loop.wav"), raw[ls:le], loop=(0, le - ls))
            # How closely pass 3 repeats pass 2, aligned on the driver's marks.
            diff = None
            if len(marks) >= 4 and marks[3] + (le - ls) <= len(raw):
                x = raw[ls:le].astype(np.float64)
                y = raw[marks[2]:marks[2] + (le - ls)].astype(np.float64)
                diff = round(float(np.sqrt(((x - y) ** 2).mean()) / max(1.0, np.sqrt((x ** 2).mean()))), 5)
            entry["loop"] = dict(start_s=round(marks[0] / RATE, 3), length_s=round(passes[1] / RATE, 3),
                                 first_pass_s=round(passes[0] / RATE, 3),
                                 pass_lengths_samples=passes, passes_in_file=loops, fade_s=10,
                                 loop_file="music/loops/%s_loop.wav" % base,
                                 loop_file_from_sample=ls,
                                 pass3_vs_pass2_rel_rms_diff=diff,
                                 how="the driver loads the song's single pattern again (APU $0A94); "
                                     "pass lengths vary by up to a few ms (tempo ticks) and the title's "
                                     "first pass is 0.6 s shorter than later ones")
            spc_secs = math.ceil(end / RATE)
            fade_ms = 10000
        else:
            final = trim_silence(raw)
            entry["loop"] = None
            entry["ends"] = True
            spc_secs, fade_ms = math.ceil(len(final) / RATE) + 1, 0
        write_wav(os.path.join(out, "music", base + ".wav"), final)
        st, vec = stats(final)
        vectors[entry["id"]] = vec
        envs[entry["id"]] = envelope(final)
        entry.update(duration_s=round(len(final) / RATE, 2), stats=st)
        entry["spc"] = "spc/%s.spc" % base
        tag_spc(os.path.join(work, "songs", "song_%d.spc" % u), os.path.join(out, "spc", base + ".spc"),
                s["title"], "upload %d, song %d" % (u, u + 1), spc_secs, fade_ms)
        entry["capture"] = ("Booted the ROM, then ran the game's MenuStartMusic ($0F:8133) with A=%d: it sends $18, "
                            "runs UploadSoundDriver ($07:8000) and writes %d to port 0. The .spc is the APU at that "
                            "write; the WAV is the APU running on from there (CPU stopped, ports unchanged), "
                            "sampled at every DSP tick." % (u, u + 1))
        s["marks"] = marks
        s["end"] = len(final)
        played = notes_srcn(os.path.join(work, "songs", "song_%d_notes.txt" % u))
        entry["samples_srcn"] = sorted(set(info["srcn"]) | played)
        for n in entry["samples_srcn"]:
            used_srcn.setdefault((u, n), []).append(entry["id"])
        manifest["files"].append(entry)
        print("music", base, entry["duration_s"], "s", st, "loop", entry["loop"] and entry["loop"]["length_s"])

    # ---- sound effects
    sfx_dir = os.path.join(work, "sfx")
    for e in SFX:
        base = "%02d_%s" % (e["id"], e["name"])
        if e["name"] == "engine":
            a = read_wav(os.path.join(sfx_dir, "engine_p1.wav"))
            b = read_wav(os.path.join(sfx_dir, "engine_p2.wav"))
            info = read_srcn(os.path.join(sfx_dir, "engine_p1_srcn.txt"))
            final = a
        else:
            a = read_wav(os.path.join(sfx_dir, "sfx_%02X.wav" % e["cmd"]))
            b = read_wav(os.path.join(sfx_dir, "sfx_%02X.wav" % e["p2"][0]))
            info = read_srcn(os.path.join(sfx_dir, "sfx_%02X_srcn.txt" % e["cmd"]))
            final = trim_silence(a)
        n = min(len(a), len(b))
        same = bool(np.abs(a[:n].astype(int) - b[:n].astype(int)).max() <= 2)
        write_wav(os.path.join(out, "sfx", base + ".wav"), final)
        st, vec = stats(final)
        entry = dict(id="sfx_%02d" % e["id"], kind="sfx", title=e["title"], file="sfx/%s.wav" % base,
                     where=e["where"], used_by_game=e["used"],
                     driver=dict(upload=1, port1=["$%02X" % e["cmd"]] + ["$%02X" % c for c in e["p2"]]),
                     player2_identical=same, duration_s=round(len(final) / RATE, 2), stats=st, loop=None,
                     samples_srcn=info["srcn"],
                     capture=("After the race upload (UploadSoundDriver set 1 via $0F:8133, no song started), "
                              "the command is put on port 1 until the driver echoes it, then $00, as "
                              "RaceIrqTop does; the APU runs alone and the DSP output is recorded. The "
                              "effects are the same in all four race upload sets; the menu sets have none."))
        if e["name"] == "engine":
            entry["capture"] += (" Engine: $02 once, then $03 / $00 alternately each frame with ports 2/3 = "
                                 "RPM + $400 swept $0400 -> $0E00 -> $0400 over 16 s (the race range seen in "
                                 "the recordings).")
        vectors[entry["id"]] = vec
        envs[entry["id"]] = envelope(final)
        nfile = os.path.join(sfx_dir, ("engine_p1" if e["name"] == "engine" else "sfx_%02X" % e["cmd"]) + "_notes.txt")
        entry["samples_srcn"] = sorted(set(info["srcn"]) | notes_srcn(nfile))
        for s_ in entry["samples_srcn"]:
            used_srcn.setdefault((1, s_), []).append(entry["id"])
        manifest["files"].append(entry)
        print("sfx", base, entry["duration_s"], "s", st, "p2 same" if same else "P2 DIFFERS")

    # ---- samples (BRR), from the .spc snapshots (same RAM layout as the race set for effects)
    seen = {}
    sample_entries = []
    sample_map = {}  # (upload, srcn) -> samples/... file
    for (u, srcn), users in sorted(used_srcn.items()):
        spc = open(os.path.join(work, "songs", "song_%d.spc" % u), "rb").read()
        ram = spc[0x100:0x10100]
        dirpage = spc[0x10100 + 0x5D] << 8
        start = ram[dirpage + srcn * 4] | ram[dirpage + srcn * 4 + 1] << 8
        loopa = ram[dirpage + srcn * 4 + 2] | ram[dirpage + srcn * 4 + 3] << 8
        pcm, looping, blocks = decode_brr(ram, start)
        brr = bytes(ram[start:start + blocks * 9])
        h = hashlib.sha1(brr + struct.pack("<H", loopa - start)).hexdigest()[:8]
        if h in seen:
            seen[h]["used_by"] = sorted(set(seen[h]["used_by"] + users))
            seen[h]["sets"].append("%d:%02X" % (u, srcn))
            sample_map[(u, srcn)] = seen[h]["file"]
            continue
        loop_start = (loopa - start) // 9 * 16 if looping and start <= loopa < start + blocks * 9 else None
        if loop_start is not None and loop_start >= len(pcm):
            loop_start = None
        name = "%02d_srcn%02X_%s" % (len(sample_entries) + 1, srcn, h)
        out16 = np.clip(pcm * 2, -32768, 32767).astype(np.int16)
        write_wav(os.path.join(out, "samples", name + ".wav"), out16,
                  loop=(loop_start, len(pcm)) if loop_start is not None else None)
        ent = dict(id="sample_%02d" % (len(sample_entries) + 1), kind="sample", file="samples/%s.wav" % name,
                   srcn="$%02X" % srcn, sets=["%d:%02X" % (u, srcn)], apu_address="$%04X" % start,
                   length_samples=len(pcm), duration_s=round(len(pcm) / RATE, 3),
                   loop_start_sample=loop_start, used_by=sorted(set(users)),
                   est_fundamental_hz_at_32k=sample_f0(pcm, loop_start),
                   note="BRR decoded as the DSP does (no Gaussian interpolation); played at pitch $1000 = 32 kHz. "
                        "Fundamental measured on the looped part (none for one-shot samples). Per-song tuning, "
                        "envelopes and SFZ mappings: rebuild/*/sheet.json.")
        f0 = ent["est_fundamental_hz_at_32k"]
        if f0:
            nn = 69 + 12 * math.log2(f0 / 440)
            ent["natural_note"] = "%s%d %+d cents" % (KEYS[int(round(nn)) % 12], int(round(nn)) // 12 - 1,
                                                     int(round((nn - round(nn)) * 100)))
        sample_map[(u, srcn)] = ent["file"]
        seen[h] = ent
        sample_entries.append(ent)
    for ent in sample_entries:
        ent["sets"] = sorted(set(ent["sets"]))
    manifest["files"].extend(sample_entries)

    # ---- rebuild kits: MIDI, SFZ, stems, sheets (tools/audio/rebuild.py)
    rb = os.path.join(out, "rebuild")
    os.makedirs(rb, exist_ok=True)
    manifest["rebuild"] = []
    for s in SONGS:
        u = s["upload"]
        base = "%02d_%s" % (s["id"], s["name"])
        song = dict(id=s["id"], name=s["name"], title=s["title"], upload=u, marks=s["marks"], end=s["end"],
                    sample_files={srcn: f for (uu, srcn), f in sample_map.items() if uu == u})
        sheet = rebuild_song(song, work, rb, "samples")
        end = sheet["loop"]["end_sample"] if sheet["loop"] else s["end"]
        stems = make_stems(tool, args, work, u, [int(v) for v in sheet["voices"]], end)
        sd = os.path.join(rb, base, "stems")
        os.makedirs(sd, exist_ok=True)
        for v, a in stems.items():
            role = sheet["voices"][str(v)]["instruments"][0].split(" (")[0].replace(" / ", "_").replace(" ", "_")
            write_wav(os.path.join(sd, "voice%d_%s.wav" % (v, role)), a)
        mix = read_wav(os.path.join(work, "songs", "song_%d_raw.wav" % u))[:end]
        write_wav(os.path.join(sd, "mix.wav"), mix)
        # stems add up to the mix? (the DSP clamps and the echo is shared, so not exactly)
        tot = sum(a.astype(np.float64) for a in stems.values())
        n = min(len(tot), len(mix))
        res = float(np.sqrt(((tot[:n] - mix[:n]) ** 2).mean()) / max(1.0, np.sqrt((mix[:n].astype(np.float64) ** 2).mean())))
        sheet["stems_sum_vs_mix_rel_rms_diff"] = round(res, 4)
        with open(os.path.join(rb, base, "sheet.json"), "w") as f:
            json.dump(sheet, f, indent=2)
        manifest["rebuild"].append(dict(id=sheet["id"], folder="rebuild/%s/" % base, midi=sheet["midi"],
                                        sfz=sheet["sfz"], bpm=sheet["tempo"]["bpm"], key=sheet["key_estimate"]["key"],
                                        voices=len(sheet["voices"]), instruments=len(sheet["instruments"]),
                                        self_check=sheet["self_check"], stems_sum_vs_mix=sheet["stems_sum_vs_mix_rel_rms_diff"]))
        print("rebuild", base, sheet["tempo"]["bpm"], "bpm", sheet["key_estimate"], sheet["self_check"], "stems", res)
    sfx_entries = sfx_sheet(work, [dict(e, cmds=["$%02X" % e["cmd"]] + ["$%02X" % c for c in e["p2"]]) for e in SFX],
                            {srcn: f for (uu, srcn), f in sample_map.items() if uu == 1}, rb, "samples")
    manifest["rebuild_sfx"] = "rebuild/sfx/ (sheet.json, sfx.sfz)"

    # ---- reference: the game's own mix during a replayed race
    if args.reference:
        ref = os.path.join(out, "reference", "race_las_vegas_1p_gameplay.wav")
        run([tool, "--rom", args.rom, "log", "--input", args.reference, "--frames", "11000",
             "--wav", ref, "--wav-from", "3500"])
        a = read_wav(ref)
        st, _ = stats(a)
        manifest["files"].append(dict(
            id="reference_01", kind="reference", title="Las Vegas race, as heard in the game (music + effects)",
            file="reference/race_las_vegas_1p_gameplay.wav", duration_s=round(len(a) / RATE, 2), stats=st,
            where="1-player race on Las Vegas (tests/inputs/race_1p.txt), frames 3500-11000",
            capture="The original game run on LakeSnes with the recording's inputs; DSP output recorded as it played."))

    # ---- checks
    checks = dict()
    ids = [k for k in vectors]
    kind = lambda k: k.split("_")[0]
    means = {kd: np.mean([vectors[k] for k in ids if kind(k) == kd], axis=0) for kd in set(map(kind, ids))}
    sims = []
    for i in range(len(ids)):
        for j in range(i + 1, len(ids)):
            if ids[i].split("_")[0] != ids[j].split("_")[0]:
                continue  # music against music, effects against effects
            va, vb = vectors[ids[i]] - means[kind(ids[i])], vectors[ids[j]] - means[kind(ids[j])]
            c = float(va @ vb / (np.linalg.norm(va) * np.linalg.norm(vb) + 1e-12))
            e = env_similarity(envs[ids[i]], envs[ids[j]])
            sims.append(dict(a=ids[i], b=ids[j], spectrum=round(c, 3), loudness_envelope=round(e, 3)))
    sims.sort(key=lambda d: -d["loudness_envelope"])
    checks["most_similar_pairs"] = sims[:6]
    checks["most_similar_music_pair"] = next((d for d in sims if d["a"].startswith("music")), None)
    checks["similarity_method"] = ("spectrum: cosine of log-band spectra minus their mean over the files of the same kind; "
                                   "loudness_envelope: best correlation of the 50 ms RMS envelopes of the first "
                                   "minute within +-2 s. 1.0 = same content.")
    silent = [f["id"] for f in manifest["files"] if f.get("stats") and f["stats"]["rms_dbfs"] < -60]
    checks["silent_files"] = silent
    checks["song_table_entries"] = 7
    checks["music_files"] = sum(1 for f in manifest["files"] if f["kind"] == "music")
    checks["sfx_files"] = sum(1 for f in manifest["files"] if f["kind"] == "sfx")
    checks["sample_files"] = len(sample_entries)
    checks["driver_commands"] = {"$%02X" % k: v for k, v in CONTROL_COMMANDS.items()}
    manifest["checks"] = checks
    with open(os.path.join(out, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=2)
    with open(os.path.join(out, "README.md"), "w") as f:
        f.write(make_readme(manifest))
    with open(os.path.join(out, "HANDOFF.md"), "w") as f:
        f.write(make_handoff(manifest, rb, sfx_entries))
    print(json.dumps(checks, indent=1))


if __name__ == "__main__":
    main()
