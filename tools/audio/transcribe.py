#!/usr/bin/env python3
"""Turns tg_audio's note logs (song_U_notes.txt: every DSP key-on / key-off while a song
plays) into Standard MIDI Files and per-song facts (tempo, key, instruments).

Used by build_audio.py; can also run alone:

    python3 tools/audio/transcribe.py WORK_DIR/songs/song_4_notes.txt WORK_DIR/songs/song_4.spc out.mid

How notes are named: the driver plays sample S at DSP pitch P (P = $1000 plays it at
32 kHz). The sample's own fundamental f0 (Hz at 32 kHz) is measured from its looped part
(harmonic product spectrum), so a note sounds at f0 * P / $1000 Hz -> nearest MIDI note.
Samples that do not loop (drums, hits) go to MIDI channel 10 with one drum note each.
"""
import math
import struct
import sys

import numpy as np

RATE = 32000
PPQ = 480
GM_DRUMS = [36, 38, 42, 46, 49, 45, 50, 39, 51, 37, 56, 54]
KEYS = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]
# Krumhansl-Kessler key profiles
MAJOR = [6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88]
MINOR = [6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17]


def read_notes(path):
    """-> list of (sample, voice, srcn, pitch, vol) for key-ons, with durations."""
    ons, events = {}, []
    for line in open(path):
        p = line.split()
        if p[0] == "on":
            t, v, srcn, pitch, vl, vr = map(int, p[1:])
            if v in ons:
                events.append(ons[v] + (t - ons[v][0],))
            ons[v] = (t, v, srcn, pitch, max(abs(vl), abs(vr)))
        elif p[0] == "off":
            t, v = int(p[1]), int(p[2])
            if v in ons:
                events.append(ons[v] + (t - ons[v][0],))
                del ons[v]
    for v, o in ons.items():
        events.append(o + (RATE // 2,))
    events.sort()
    return events  # (t, voice, srcn, pitch, vol, dur)


def sample_f0(pcm, loop_start):
    """Fundamental of a looped sample at 32 kHz playback (harmonic product spectrum)."""
    if loop_start is None or len(pcm) - loop_start < 16:
        return None
    loop = pcm[loop_start:].astype(np.float64)
    x = np.tile(loop, int(math.ceil(RATE / len(loop))))[:RATE]
    x = x - x.mean()
    if not x.any():
        return None
    spec = np.abs(np.fft.rfft(x * np.hanning(len(x))))
    hps = spec.copy()
    for h in (2, 3, 4):
        d = spec[::h]
        hps[: len(d)] *= d
    lo, hi = 30, 4000  # 1 Hz bins
    k = lo + int(np.argmax(hps[lo:hi]))
    # prefer a lower octave if it is nearly as strong (HPS octave errors)
    while k // 2 >= lo and hps[k // 2] > 0.3 * hps[k]:
        k //= 2
    return float(k)


def midi_note(f):
    return 69 + 12 * math.log2(f / 440.0)


def vlq(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.append((n & 0x7F) | 0x80)
        n >>= 7
    return bytes(reversed(out))


def write_midi(path, events, f0, drums, bpm, title):
    """One track per DSP voice; melodic samples on their own channel, drums on channel 10."""
    sec_per_tick = 60.0 / bpm / PPQ
    tracks = []
    meta = b"\x00\xff\x03" + vlq(len(title)) + title.encode("ascii", "replace")
    tempo = int(60e6 / bpm)
    meta += b"\x00\xff\x51\x03" + struct.pack(">I", tempo)[1:]
    tracks.append(meta + b"\x00\xff\x2f\x00")
    for voice in range(8):
        evs = [e for e in events if e[1] == voice]
        if not evs:
            continue
        msgs = []
        for t, v, srcn, pitch, vol, dur in evs:
            if srcn in drums:
                ch, note = 9, drums[srcn]
            elif f0.get(srcn):
                ch, note = voice if voice < 9 else voice + 1, int(round(midi_note(f0[srcn] * pitch / 4096.0)))
            else:
                continue
            if not 0 <= note <= 127 or pitch == 0:
                continue
            vel = max(1, min(127, vol))
            on = int(round(t / RATE / sec_per_tick))
            off = int(round((t + max(dur, 1)) / RATE / sec_per_tick))
            msgs.append((on, 1, bytes([0x90 | ch, note, vel])))
            msgs.append((max(off, on + 1), 0, bytes([0x80 | ch, note, 0])))
        msgs.sort(key=lambda m: (m[0], m[1]))
        data = b"\x00\xff\x03" + vlq(len("voice %d" % voice)) + ("voice %d" % voice).encode()
        last = 0
        for tick, _, m in msgs:
            data += vlq(tick - last) + m
            last = tick
        tracks.append(data + b"\x00\xff\x2f\x00")
    with open(path, "wb") as f:
        f.write(b"MThd" + struct.pack(">IHHH", 6, 1, len(tracks), PPQ))
        for tr in tracks:
            f.write(b"MTrk" + struct.pack(">I", len(tr)) + tr)


def estimate_bpm(events):
    """Tempo from the key-on times: the beat that best explains them (60-200 BPM), using
    the fact that the driver's notes fall on a fixed tick grid."""
    onsets = np.array(sorted(set(e[0] for e in events)), dtype=np.float64) / RATE
    if len(onsets) < 8:
        return None, None
    res = 0.005
    n = int(onsets[-1] / res) + 1
    train = np.zeros(n)
    train[(onsets / res).astype(int)] = 1
    ac = np.fft.irfft(np.abs(np.fft.rfft(train, 2 * n)) ** 2)[:n]
    best, best_lag = 0, None
    for bpm10 in range(600, 2001):
        lag = 60.0 / (bpm10 / 10.0) / res
        i = int(round(lag))
        if i >= n:
            continue
        score = ac[i] + 0.5 * ac[min(n - 1, int(round(lag * 2)))] + 0.5 * ac[min(n - 1, int(round(lag / 2)))]
        if score > best:
            best, best_lag = score, bpm10 / 10.0
    # step: the most common gap between onsets (the shortest note value used)
    gaps = np.diff(onsets)
    gaps = gaps[gaps > 0.02]
    step = float(np.median(gaps)) if len(gaps) else None
    return best_lag, step


def estimate_key(audio):
    """Key from the audio's chroma (Krumhansl-Kessler); returns (name, correlation)."""
    x = audio.astype(np.float64).mean(axis=1)[: RATE * 120]
    n = 8192
    chroma = np.zeros(12)
    freqs = np.fft.rfftfreq(n, 1 / RATE)
    valid = (freqs > 55) & (freqs < 2000)
    pcs = np.round(12 * np.log2(freqs[valid] / 440.0) + 69).astype(int) % 12
    win = np.hanning(n)
    for i in range(0, len(x) - n, n):
        mag = np.abs(np.fft.rfft(x[i:i + n] * win))[valid]
        np.add.at(chroma, pcs, mag)
    best = (-2, None)
    for k in range(12):
        for prof, mode in ((MAJOR, "major"), (MINOR, "minor")):
            c = np.corrcoef(chroma, np.roll(prof, k))[0, 1]
            if c > best[0]:
                best = (c, "%s %s" % (KEYS[k], mode))
    return best[1], round(float(best[0]), 2)


def main():
    sys.path.insert(0, __file__.rsplit("/", 1)[0])
    from build_audio import decode_brr
    notes, spc, out = sys.argv[1:4]
    data = open(spc, "rb").read()
    ram = data[0x100:0x10100]
    dirp = data[0x10100 + 0x5D] << 8
    events = read_notes(notes)
    f0, drums = {}, {}
    for srcn in sorted(set(e[2] for e in events)):
        s = ram[dirp + srcn * 4] | ram[dirp + srcn * 4 + 1] << 8
        lp = ram[dirp + srcn * 4 + 2] | ram[dirp + srcn * 4 + 3] << 8
        pcm, looping, blocks = decode_brr(ram, s)
        ls = (lp - s) // 9 * 16 if looping and s <= lp < s + blocks * 9 else None
        f0[srcn] = sample_f0(pcm, ls)
        if f0[srcn] is None:
            drums[srcn] = GM_DRUMS[len(drums) % len(GM_DRUMS)]
    bpm, step = estimate_bpm(events)
    write_midi(out, events, f0, drums, bpm or 120, "song")
    print("bpm", bpm, "step", step, "f0", f0, "drums", drums)


if __name__ == "__main__":
    main()
