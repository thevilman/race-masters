#!/usr/bin/env python3
"""Everything needed to REBUILD a Top Gear song, not just remaster a recording.

From tg_audio's DSP log of a song (song_U_notes.txt: key-ons/offs, pitch and volume
changes, envelope/echo/noise registers, the driver's tempo) and its .spc snapshot this
writes, per song:

    NN_name.mid      Standard MIDI File: one track per SNES voice, notes with pitch bend
                     (bend range 12 semitones) for detune / slides, velocity = voice
                     volume, CC10 = pan, CC91 = echo on/off, program = instrument,
                     loopStart / loopEnd markers, the driver's exact tempo.
    NN_name.sfz      sampler kit: one region per instrument (MIDI channel = instrument),
                     sample loop, tuning (pitch_keycenter + tune) and ADSR envelope.
    instruments/*.sfz  the same instruments one per file, for one DAW track each.
    sheet.json       voices, instruments (roles, tuning, envelopes), tempo, key, loop
                     points, echo settings, and the self-check against the original.
    NN_name_from_midi.wav  the MIDI rendered through the kit (no echo) by this script's
                     own small SFZ player, the basis of the self-check.

Method: the notes come from logging the DSP while the game's driver plays the song (not
from decoding the driver's sequence bytes), so they are exactly what the chip played.
"""
import json
import math
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from build_audio import decode_brr, read_wav, write_wav  # noqa: E402
from transcribe import KEYS, MAJOR, MINOR, sample_f0  # noqa: E402

RATE = 32000
PPQ = 960
BEND_RANGE = 12  # semitones, set with RPN 0 on every channel

# SNES envelope times (fullsnes / the SNES dev manual), seconds
ATTACK = [4.1, 2.6, 1.5, 1.0, 0.64, 0.38, 0.26, 0.16, 0.096, 0.064, 0.04, 0.024, 0.016, 0.01, 0.006, 0.0]
DECAY = [1.2, 0.74, 0.44, 0.29, 0.18, 0.11, 0.074, 0.037]
SUSTAIN_RATE = [None, 38, 28, 24, 19, 14, 12, 9.4, 7.1, 5.9, 4.7, 3.5, 2.9, 2.4, 1.8, 1.5, 1.2, 0.88, 0.74,
                0.59, 0.44, 0.37, 0.29, 0.22, 0.18, 0.15, 0.11, 0.092, 0.074, 0.055, 0.037, 0.018]
RELEASE = 256 / RATE  # key-off: linear, 8/2048 of full scale per sample


# ---------------------------------------------------------------- the log

def parse_log(path, end):
    """Notes (dicts) with their pitch/volume changes, the register history and tempo."""
    regs = {}
    reg_events = []
    tempo = []
    notes = []
    cur = {}

    def close(v, t):
        n = cur.pop(v, None)
        if n is not None:
            n["off"] = min(t, end)
            notes.append(n)

    for line in open(path):
        p = line.split()
        k, t = p[0], int(p[1])
        if t >= end:
            break
        if k == "reg":
            a, val = int(p[2], 16), int(p[3], 16)
            regs[a] = val
            reg_events.append((t, a, val))
        elif k == "tempo":
            tempo.append((t, int(p[2])))
        elif k == "on":
            v, srcn, pitch, vl, vr = map(int, p[2:])
            close(v, t)
            b = v << 4
            cur[v] = dict(on=t, voice=v, srcn=srcn, pitch=pitch, vl=vl, vr=vr,
                          adsr1=regs.get(b + 5, 0), adsr2=regs.get(b + 6, 0), gain=regs.get(b + 7, 0),
                          echo=bool(regs.get(0x4D, 0) >> v & 1), noise=bool(regs.get(0x3D, 0) >> v & 1),
                          pmod=bool(regs.get(0x2D, 0) >> v & 1), bends=[], vols=[])
        elif k == "pitch":
            v, pitch = int(p[2]), int(p[3])
            if v in cur:
                cur[v]["bends"].append((t, pitch))
        elif k == "vol":
            v, vl, vr = map(int, p[2:])
            if v in cur:
                cur[v]["vols"].append((t, vl, vr))
        elif k == "off":
            close(int(p[2]), t)
    for v in list(cur):
        close(v, end)
    notes.sort(key=lambda n: (n["on"], n["voice"]))
    return notes, reg_events, tempo


def echo_settings(reg_events, at):
    """The echo/main-volume registers in force at sample `at` (the song's steady state)."""
    regs = {}
    for t, a, v in reg_events:
        if t <= at:
            regs[a] = v
    s8 = lambda v: v - 256 if v >= 128 else v
    return dict(
        main_volume=[s8(regs.get(0x0C, 0)), s8(regs.get(0x1C, 0))],
        echo_volume=[s8(regs.get(0x2C, 0)), s8(regs.get(0x3C, 0))],
        echo_delay_ms=(regs.get(0x7D, 0) & 15) * 16,
        echo_feedback=round(s8(regs.get(0x0D, 0)) / 128, 3),
        echo_feedback_raw=s8(regs.get(0x0D, 0)),
        fir=[s8(regs.get((i << 4) | 0x0F, 0)) for i in range(8)],
        echo_voices=[v for v in range(8) if regs.get(0x4D, 0) >> v & 1],
        echo_writes_enabled=not bool(regs.get(0x6C, 0) & 0x20),
        noise_voices=[v for v in range(8) if regs.get(0x3D, 0) >> v & 1],
        noise_clock=regs.get(0x6C, 0) & 0x1F,
        pitch_mod_voices=[v for v in range(1, 8) if regs.get(0x2D, 0) >> v & 1],
    )


# ---------------------------------------------------------------- instruments

def envelope(adsr1, adsr2, gain):
    """SNES ADSR / GAIN -> SFZ ampeg_* (approximate: the SNES curves are linear attack,
    exponential decay; SFZ players differ). Also returns the raw description."""
    if adsr1 & 0x80:
        ar, dr = adsr1 & 15, (adsr1 >> 4) & 7
        sl, sr = adsr2 >> 5, adsr2 & 31
        sustain = (sl + 1) / 8
        d = dict(mode="ADSR", attack_rate=ar, decay_rate=dr, sustain_level=sl, sustain_rate=sr,
                 attack_s=ATTACK[ar], decay_s=DECAY[dr], sustain=round(sustain, 3),
                 sustain_decay_s=SUSTAIN_RATE[sr])
        sfz = dict(ampeg_attack=ATTACK[ar], ampeg_decay=DECAY[dr] if sl < 7 else 0,
                   ampeg_sustain=round(sustain * 100, 1), ampeg_release=round(RELEASE, 4))
        return d, sfz
    if not gain & 0x80:
        level = (gain & 0x7F) / 127
        return (dict(mode="GAIN direct", level=round(level, 3)),
                dict(ampeg_attack=0, ampeg_sustain=round(level * 100, 1), ampeg_release=round(RELEASE, 4)))
    mode, rate = (gain >> 5) & 3, gain & 31
    t = SUSTAIN_RATE[rate] or 0
    names = ["linear decrease", "exponential decrease", "linear increase", "bent increase"]
    d = dict(mode="GAIN " + names[mode], rate=rate, time_s=t)
    if mode >= 2:
        sfz = dict(ampeg_attack=round(t / 8 if mode == 2 else t / 6, 4), ampeg_sustain=100, ampeg_release=round(RELEASE, 4))
    else:
        sfz = dict(ampeg_attack=0, ampeg_decay=t, ampeg_sustain=0, ampeg_release=round(RELEASE, 4))
    return d, sfz


def load_sample(ram, dirp, srcn):
    s = ram[dirp + srcn * 4] | ram[dirp + srcn * 4 + 1] << 8
    lp = ram[dirp + srcn * 4 + 2] | ram[dirp + srcn * 4 + 3] << 8
    pcm, looping, blocks = decode_brr(ram, s)
    ls = (lp - s) // 9 * 16 if looping and s <= lp < s + blocks * 9 else None
    if ls is not None and ls >= len(pcm):
        ls = None
    return pcm, ls


def midi_of(freq):
    return 69 + 12 * math.log2(freq / 440.0)


# ---------------------------------------------------------------- MIDI

def vlq(n):
    assert n >= 0, n
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.append((n & 0x7F) | 0x80)
        n >>= 7
    return bytes(reversed(out))


def meta_text(kind, text):
    b = text.encode("ascii", "replace")
    return bytes([0xFF, kind]) + vlq(len(b)) + b


def write_midi(path, title, notes, insts, tempo_events, spt_beat, loop, end):
    """Absolute time: samples -> MIDI ticks with PPQ ticks per beat of spt_beat(T) samples."""
    # tempo map: (sample time, samples per beat)
    tmap = [(0, spt_beat(tempo_events[0][1]) if tempo_events else RATE / 2)]
    for t, T in tempo_events:
        if T > 0 and spt_beat(T) != tmap[-1][1]:
            tmap.append((t, spt_beat(T)))

    def tick(t):
        ticks = 0.0
        for i, (t0, spb) in enumerate(tmap):
            t1 = tmap[i + 1][0] if i + 1 < len(tmap) else None
            if t1 is not None and t > t1:
                ticks += (t1 - t0) / spb * PPQ
            else:
                ticks += (t - t0) / spb * PPQ
                break
        return int(round(ticks))

    conductor = []
    conductor.append((0, meta_text(0x03, title)))
    conductor.append((0, b"\xff\x58\x04\x04\x02\x18\x08"))
    for t0, spb in tmap:
        us = int(round(spb / RATE * 1e6))
        conductor.append((tick(t0), b"\xff\x51\x03" + struct.pack(">I", us)[1:]))
    if loop:
        conductor.append((tick(loop[0]), meta_text(0x06, "loopStart")))
        conductor.append((tick(loop[1]), meta_text(0x06, "loopEnd")))
    conductor.append((tick(end), meta_text(0x06, "end")))
    conductor.sort(key=lambda e: e[0])
    tracks = [conductor]
    for voice in range(8):
        vn = [n for n in notes if n["voice"] == voice]
        if not vn:
            continue
        ev = [(0, meta_text(0x03, "voice %d" % voice))]
        chans = sorted(set(insts[n["srcn"]]["channel"] for n in vn))
        for ch in chans:
            inst = next(i for i in insts.values() if i["channel"] == ch)
            ev.append((0, bytes([0xC0 | ch, inst["program"]])))
            # RPN 0: pitch-bend range
            ev += [(0, bytes([0xB0 | ch, 101, 0])), (0, bytes([0xB0 | ch, 100, 0])),
                   (0, bytes([0xB0 | ch, 6, BEND_RANGE])), (0, bytes([0xB0 | ch, 38, 0]))]
        for n in vn:
            inst = insts[n["srcn"]]
            ch = inst["channel"]
            exact = inst["n0"] + 12 * math.log2(max(n["pitch"], 1) / 4096.0)
            note = int(round(exact))
            if not 0 <= note <= 127:
                continue
            n["midi_note"] = note
            vel = max(1, min(127, max(abs(n["vl"]), abs(n["vr"]))))
            pan = pan_of(n["vl"], n["vr"])
            t_on, t_off = tick(n["on"]), max(tick(n["off"]), tick(n["on"]) + 1)

            def bend(semis):
                v = int(round(8192 + semis / BEND_RANGE * 8192))
                v = max(0, min(16383, v))
                return bytes([0xE0 | ch, v & 0x7F, v >> 7])
            ev.append((t_on, bytes([0xB0 | ch, 10, int(round((pan + 100) / 200 * 127))])))
            ev.append((t_on, bytes([0xB0 | ch, 91, 127 if n["echo"] else 0])))
            ev.append((t_on, bend(exact - note)))
            if n["noise"]:
                ev.append((t_on, meta_text(0x01, "noise")))
            ev.append((t_on, bytes([0x90 | ch, note, vel])))
            for t, p in n["bends"]:
                e = inst["n0"] + 12 * math.log2(max(p, 1) / 4096.0)
                ev.append((tick(t), bend(e - note)))
            ev.append((t_off, bytes([0x80 | ch, note, 0])))
        ev.sort(key=lambda e: e[0])
        tracks.append(ev)
    with open(path, "wb") as f:
        f.write(b"MThd" + struct.pack(">IHHH", 6, 1, len(tracks), PPQ))
        for ev in tracks:
            data, last = b"", 0
            for t, m in ev:
                data += vlq(t - last) + m
                last = t
            data += b"\x00\xff\x2f\x00"
            f.write(b"MTrk" + struct.pack(">I", len(data)) + data)


def pan_of(vl, vr):
    l, r = abs(vl), abs(vr)
    m = max(l, r, 1)
    return (r - l) / m * 100


def read_midi(path):
    """Minimal SMF reader -> notes (on_s, off_s, ch, note, vel, pan, bends[(s, semis)])."""
    data = open(path, "rb").read()
    ntr, ppq = struct.unpack(">HH", data[10:14])
    pos = 14
    raw = []
    for _ in range(ntr):
        size = struct.unpack(">I", data[pos + 4:pos + 8])[0]
        tr = data[pos + 8:pos + 8 + size]
        pos += 8 + size
        i = t = 0
        status = 0
        while i < len(tr):
            d = 0
            while True:
                b = tr[i]
                i += 1
                d = (d << 7) | (b & 0x7F)
                if b < 0x80:
                    break
            t += d
            b = tr[i]
            if b == 0xFF:
                kind, i = tr[i + 1], i + 2
                ln = 0
                while True:
                    c = tr[i]
                    i += 1
                    ln = (ln << 7) | (c & 0x7F)
                    if c < 0x80:
                        break
                raw.append((t, "meta", kind, tr[i:i + ln]))
                i += ln
                continue
            if b & 0x80:
                status, i = b, i + 1
            n = 1 if status & 0xF0 in (0xC0, 0xD0) else 2
            raw.append((t, "ev", status, tr[i:i + n]))
            i += n
    tempos = sorted((t, int.from_bytes(m, "big")) for t, k, kind, m in raw if k == "meta" and kind == 0x51)

    def secs(tk):
        s, last_t, us = 0.0, 0, 500000
        for t0, u in tempos:
            if t0 > tk:
                break
            s += (t0 - last_t) / ppq * us / 1e6
            last_t, us = t0, u
        return s + (tk - last_t) / ppq * us / 1e6

    evs = sorted([(t, st, d) for t, k, st, d in raw if k == "ev"], key=lambda e: e[0])
    state = {}
    active = {}
    notes = []
    for t, st, d in evs:
        ch, kind = st & 15, st & 0xF0
        s = state.setdefault(ch, dict(pan=64, bend=0.0, rev=0))
        if kind == 0xB0 and d[0] == 10:
            s["pan"] = d[1]
        elif kind == 0xB0 and d[0] == 91:
            s["rev"] = d[1]
        elif kind == 0xE0:
            s["bend"] = ((d[1] << 7 | d[0]) - 8192) / 8192 * BEND_RANGE
            if ch in active:
                active[ch]["bends"].append((secs(t), s["bend"]))
        elif kind == 0x90 and d[1] > 0:
            active[ch] = dict(on=secs(t), ch=ch, note=d[0], vel=d[1], pan=s["pan"], rev=s["rev"],
                              bends=[(secs(t), s["bend"])])
        elif kind == 0x80 or (kind == 0x90 and d[1] == 0):
            n = active.pop(ch, None)
            if n is not None:
                n["off"] = secs(t)
                notes.append(n)
    return notes


# ---------------------------------------------------------------- SFZ

def write_sfz(path, insts, sample_dir_rel, only=None):
    L = ["// Top Gear (SNES) instrument kit made by tools/audio/rebuild.py from your ROM.",
         "// One region per instrument; MIDI channel N plays instrument N (see sheet.json).",
         "// Pitch: pitch_keycenter + tune put each note at the pitch the SNES played.",
         "// Envelope: the SNES ADSR/GAIN converted to ampeg_* (approximate).",
         "// Velocity = SNES voice volume (linear amplitude); CC10 = pan.", "",
         "<control>", "default_path=%s" % sample_dir_rel, ""]
    for srcn, i in sorted(insts.items(), key=lambda kv: kv[1]["channel"]):
        if only is not None and srcn != only:
            continue
        L.append("// %s: sample $%02X (%s), %s" % (i["name"], srcn, i["sample_file"], i["envelope"]["mode"]))
        if i["envelope_variants"] > 1:
            L.append("// the driver used %d envelope settings with this sample; the most common is here" % i["envelope_variants"])
        r = ["<region>", "sample=%s" % os.path.basename(i["sample_file"])]
        if only is None:
            r += ["lochan=%d" % (i["channel"] + 1), "hichan=%d" % (i["channel"] + 1)]
        r += ["lokey=0", "hikey=127", "pitch_keycenter=%d" % i["keycenter"], "tune=%d" % i["tune_cents"],
              "amp_veltrack=100", "amp_velcurve_0=0", "amp_velcurve_127=1", "pan=-100", "pan_oncc10=200"]
        if i["loop_start"] is not None:
            r += ["loop_mode=loop_continuous", "loop_start=%d" % i["loop_start"], "loop_end=%d" % (i["length"] - 1)]
        else:
            r += ["loop_mode=no_loop"]
        r += ["%s=%s" % kv for kv in i["sfz_env"].items()]
        L.append(" ".join(r))
        L.append("")
    open(path, "w").write("\n".join(L))


def read_sfz(path):
    regions = []
    base = os.path.dirname(path)
    dp = ""
    for line in open(path):
        line = line.split("//")[0].strip()
        if not line:
            continue
        if line.startswith("default_path="):
            dp = line.split("=", 1)[1]
            continue
        if line.startswith("<region>"):
            kv = dict(x.split("=", 1) for x in line.split()[1:])
            kv["path"] = os.path.normpath(os.path.join(base, dp, kv["sample"]))
            regions.append(kv)
    return regions


# ---------------------------------------------------------------- a small SFZ player

def render(notes, regions, length_s, echo=None):
    """Plays read_midi notes through read_sfz regions (the subset write_sfz uses). With
    echo (sheet.json's echo settings), notes with CC91 > 0 also feed an SNES-style echo:
    delay line with feedback through the 8-tap FIR, mixed at the echo volume."""
    out = np.zeros((int(length_s * RATE) + RATE, 2))
    send = np.zeros_like(out)
    cache = {}
    bych = {int(r["lochan"]) - 1: r for r in regions}
    for n in notes:
        r = bych.get(n["ch"])
        if r is None:
            continue
        if r["path"] not in cache:
            a = read_wav(r["path"])[:, 0].astype(np.float64) / 32768
            cache[r["path"]] = a
        smp = cache[r["path"]]
        kc, tune = int(r["pitch_keycenter"]), float(r.get("tune", 0))
        ls = int(r["loop_start"]) if r.get("loop_mode") == "loop_continuous" else None
        a0 = int(n["on"] * RATE)
        dur = max(1, int((n["off"] - n["on"]) * RATE))
        rel = int(float(r.get("ampeg_release", 0.008)) * RATE)
        total = dur + rel
        if a0 >= len(out):
            continue
        total = min(total, len(out) - a0)
        # pitch (semitones from the key centre) at each output sample, with bends
        tt = a0 / RATE + np.arange(total) / RATE
        bt = [b[0] for b in n["bends"]]
        bv = [b[1] for b in n["bends"]]
        idx = np.searchsorted(bt, tt, side="right") - 1
        semis = n["note"] - kc + tune / 100 + np.array(bv)[np.clip(idx, 0, len(bv) - 1)]
        pos = np.concatenate([[0.0], np.cumsum(2 ** (semis / 12))[:-1]])
        if ls is not None and len(smp) > ls:
            ll = len(smp) - ls
            pos = np.where(pos < ls, pos, ls + np.mod(pos - ls, ll))
        else:
            pos = pos[pos < len(smp) - 1]
        if len(pos) == 0:
            continue
        i0 = pos.astype(int)
        fr = pos - i0
        i1 = np.minimum(i0 + 1, len(smp) - 1)
        x = smp[i0] * (1 - fr) + smp[i1] * fr
        # envelope
        m = len(x)
        t = np.arange(m) / RATE
        att = float(r.get("ampeg_attack", 0))
        dec = float(r.get("ampeg_decay", 0))
        sus = float(r.get("ampeg_sustain", 100)) / 100
        env = np.ones(m)
        if att > 0:
            env = np.minimum(1, t / att)
        if dec > 0:
            env = np.where(t > att, sus + (1 - sus) * np.exp(-(t - att) / (dec / 3)), env)
        else:
            env = np.where(t > att, sus, env)
        if m > dur:
            env[dur:] *= np.maximum(0, 1 - np.arange(m - dur) / max(rel, 1))
        x = x * env * (n["vel"] / 127)
        p = (n["pan"] / 127 * 200 - 100)
        gl, gr = 1 - max(0, p) / 100, 1 + min(0, p) / 100
        out[a0:a0 + m, 0] += x * gl
        out[a0:a0 + m, 1] += x * gr
        if n.get("rev", 0) > 0:
            send[a0:a0 + m, 0] += x * gl
            send[a0:a0 + m, 1] += x * gr
    if echo:
        out = out * (echo["main_volume"][0] / 128.0 if echo["main_volume"][0] else 1.0)
        if echo["echo_delay_ms"] > 0 and echo["echo_voices"] and echo["echo_writes_enabled"]:
            out += apply_echo(send, echo)
    return out[: int(length_s * RATE)]


def apply_echo(send, e):
    """SNES echo, block-wise: y[n] = FIR(w[n - D]) / 128, w = send + feedback * y."""
    D = int(e["echo_delay_ms"] * RATE / 1000)
    fir = np.array(e["fir"][::-1], dtype=np.float64) / 128.0
    fb = e["echo_feedback_raw"] / 128.0
    n = len(send)
    w = np.zeros((n + D, 2))
    y = np.zeros((n, 2))
    for b in range(0, n, D):
        k = min(D, n - b)
        # the delayed input for this block is w[b - D .. b - D + k) (w is offset by D)
        for c in range(2):
            src = np.concatenate([w[max(0, b - 7):b, c] if b >= 7 else np.zeros(7), w[b:b + k, c]])[-(k + 7):]
            y[b:b + k, c] = np.convolve(src, fir, mode="valid")[:k]
        w[b + D: b + D + k] = send[b:b + k] + fb * y[b:b + k]
    evol = np.array(e["echo_volume"], dtype=np.float64) / 128.0
    return y * evol


def onsets(x, hop=160):
    """Spectral-flux onset times (s) of a stereo signal."""
    mono = (x.mean(axis=1) if x.ndim == 2 else x).astype(np.float32)
    n = 1024
    win = np.hanning(n).astype(np.float32)
    view = np.lib.stride_tricks.sliding_window_view(mono, n)[::hop]
    flux = []
    prev = None
    for i in range(0, len(view), 2000):  # in chunks, to keep memory small
        mag = np.log1p(np.abs(np.fft.rfft(view[i:i + 2000] * win, axis=1))).astype(np.float32)
        if prev is not None:
            mag = np.vstack([prev, mag])
        flux.append(np.maximum(0, np.diff(mag, axis=0)).sum(axis=1))
        prev = mag[-1:]
    flux = np.concatenate(flux) if flux else np.zeros(1)
    thr = np.convolve(flux, np.ones(16) / 16, mode="same") * 1.5 + flux.mean() * 0.1
    peaks = np.nonzero((flux > thr) & (flux >= np.roll(flux, 1)) & (flux >= np.roll(flux, -1)))[0]
    return (peaks + 1) * hop / RATE, flux


def compare(orig, synth):
    """How close the MIDI+kit render is to the original (both 32 kHz stereo)."""
    n = min(len(orig), len(synth))
    o, s = orig[:n].astype(np.float64), synth[:n]
    so, fo = onsets(o)
    ss, fs = onsets(s)
    hit = [np.min(np.abs(ss - t)) <= 0.03 if len(ss) else False for t in so]
    m = min(len(fo), len(fs))
    flux_corr = float(np.corrcoef(fo[:m], fs[:m])[0, 1]) if m > 2 else 0.0

    def bands(x):
        mono = x.mean(axis=1)
        k = 4096
        spec = np.zeros(k // 2 + 1)
        for i in range(0, len(mono) - k, k * 4):
            spec += np.abs(np.fft.rfft(mono[i:i + k] * np.hanning(k)))
        f = np.fft.rfftfreq(k, 1 / RATE)
        edges = np.geomspace(40, RATE / 2, 33)
        return np.log1p(np.array([spec[(f >= a) & (f < b)].sum() for a, b in zip(edges[:-1], edges[1:])]))
    bo, bs = bands(o), bands(s)
    spec_cos = float(bo @ bs / (np.linalg.norm(bo) * np.linalg.norm(bs)))
    # loudness envelopes, 50 ms
    h = 1600
    eo = np.sqrt((o[: n // h * h].mean(axis=1).reshape(-1, h) ** 2).mean(axis=1))
    es = np.sqrt((s[: n // h * h].mean(axis=1).reshape(-1, h) ** 2).mean(axis=1))
    env_corr = float(np.corrcoef(eo, es)[0, 1]) if len(eo) > 2 else 0.0
    return dict(onsets_original=len(so), onsets_matched_within_30ms=round(float(np.mean(hit)) if hit else 0.0, 3),
                onset_strength_correlation=round(flux_corr, 3), log_spectrum_cosine=round(spec_cos, 4),
                loudness_envelope_correlation=round(env_corr, 3))


# ---------------------------------------------------------------- per song

def estimate_key_from_notes(notes):
    chroma = np.zeros(12)
    for n in notes:
        if n.get("tonal") and "midi_note" in n:
            chroma[n["midi_note"] % 12] += (n["off"] - n["on"]) / RATE
    if not chroma.any():
        return None, None
    best = (-2, None)
    for k in range(12):
        for prof, mode in ((MAJOR, "major"), (MINOR, "minor")):
            c = np.corrcoef(chroma, np.roll(prof, k))[0, 1]
            if c > best[0]:
                best = (c, "%s %s" % (KEYS[k], mode))
    return best[1], round(float(best[0]), 2)


def ticks_per_beat(notes, samples_per_tick):
    """The beat, in driver ticks, that best fits the onsets (ties go to 24 / 48 / 96)."""
    on = np.array(sorted(set(n["on"] for n in notes)), dtype=np.float64)
    best, best_tpb = -1, 24
    for tpb in (12, 16, 18, 24, 32, 36, 48, 64, 72, 96):
        beat = tpb * samples_per_tick
        bpm = 60 * RATE / beat
        if not 70 <= bpm <= 200:
            continue
        ph = np.mod(on, beat) / beat
        # how strongly the onsets cluster on the beat and its halves/quarters
        score = sum(abs(np.exp(2j * np.pi * k * ph).mean()) for k in (1, 2, 4))
        if score > best + 1e-9:
            best, best_tpb = score, tpb
    return best_tpb


def rebuild_song(song, work, out_root, samples_rel_root):
    """song: dict(id, name, title, upload, marks, sample_files{srcn: path rel to audio/})."""
    u = song["upload"]
    base = "%02d_%s" % (song["id"], song["name"])
    d = os.path.join(out_root, base)
    os.makedirs(os.path.join(d, "instruments"), exist_ok=True)
    marks = song["marks"]
    loop = (marks[0], marks[1]) if len(marks) >= 2 else None
    end = marks[1] if loop else song["end"]
    notes, reg_events, tempo = parse_log(os.path.join(work, "songs", "song_%d_notes.txt" % u), end)
    spc = open(os.path.join(work, "songs", "song_%d.spc" % u), "rb").read()
    ram = spc[0x100:0x10100]
    dirp = spc[0x10100 + 0x5D] << 8
    tempo_steady = [T for t, T in tempo if T > 0]
    T = tempo_steady[-1] if tempo_steady else 32
    samples_per_tick = 16384 / T  # timer 0 at 8 kHz / 16, scaled by tempo/256 per tick
    tpb = ticks_per_beat(notes, samples_per_tick)
    spt_beat = lambda TT: tpb * 16384 / TT
    bpm = round(60 * RATE / spt_beat(T), 3)

    # instruments: one per sample number
    insts = {}
    for n in notes:
        i = insts.setdefault(n["srcn"], dict(envs={}, voices=set(), notes=0, pitches=[]))
        key = (n["adsr1"], n["adsr2"], n["gain"])
        i["envs"][key] = i["envs"].get(key, 0) + 1
        i["voices"].add(n["voice"])
        i["notes"] += 1
        i["pitches"].append(n["pitch"])
    for ch, srcn in enumerate(sorted(insts)):
        i = insts[srcn]
        pcm, ls = load_sample(ram, dirp, srcn)
        f0 = sample_f0(pcm, ls)
        n0 = midi_of(f0) if f0 else 60.0  # natural-rate note; unpitched: pitch $1000 = middle C
        env_key = max(i["envs"], key=i["envs"].get)
        envd, sfz_env = envelope(*env_key)
        i.update(channel=ch if ch < 9 else ch + 1, program=srcn & 0x7F, n0=n0, f0_hz=f0,
                 keycenter=int(round(n0)), tune_cents=int(round((round(n0) - n0) * 100)),
                 loop_start=ls, length=len(pcm), sample_file=song["sample_files"][srcn],
                 envelope=envd, sfz_env=sfz_env, envelope_variants=len(i["envs"]),
                 adsr_raw=["$%02X $%02X $%02X" % k for k in i["envs"]], tonal=f0 is not None)
    for n in notes:
        n["tonal"] = insts[n["srcn"]]["tonal"]
    # roles
    for srcn, i in insts.items():
        notes_i = [insts[srcn]["n0"] + 12 * math.log2(max(p, 1) / 4096) for p in i["pitches"]]
        i["median_note"] = float(np.median(notes_i))
    tonal = [s for s in insts if insts[s]["tonal"]]
    for s, i in insts.items():
        if not i["tonal"]:
            i["name"] = "drums / percussion %d" % (sorted(x for x in insts if not insts[x]["tonal"]).index(s) + 1)
    if tonal:
        # Heuristic roles: the lowest pitched instrument is the bass; of the others (median
        # above middle C) the one with the longest notes carries the melody (lead); short
        # fast notes are arpeggios; the rest chords / pads or counter-melodies.
        beat = tpb * samples_per_tick
        for s_ in tonal:
            ds = [(n["off"] - n["on"]) / beat for n in notes if n["srcn"] == s_]
            insts[s_]["avg_beats"] = float(np.mean(ds)) if ds else 0.0
        bass = min(tonal, key=lambda s_: insts[s_]["median_note"])
        upper = [s_ for s_ in tonal if s_ != bass and insts[s_]["median_note"] >= 60]
        lead = max(upper, key=lambda s_: insts[s_]["avg_beats"] * math.sqrt(insts[s_]["notes"]), default=None)
        for s_ in tonal:
            i = insts[s_]
            i["name"] = ("bass" if s_ == bass else "lead" if s_ == lead else
                         "arpeggio" if i["avg_beats"] < 0.3 else
                         "chords / pad" if i["median_note"] < 72 else "counter-melody")
    for s, i in insts.items():
        i["name"] = "%s (driver sample $%02X)" % (i["name"], s)

    # files
    mid = os.path.join(d, base + ".mid")
    write_midi(mid, song["title"], notes, insts, tempo, spt_beat, loop, end)
    rel = os.path.relpath(os.path.join(out_root, "..", samples_rel_root), d)
    write_sfz(os.path.join(d, base + ".sfz"), insts, rel)
    for srcn, i in insts.items():
        nm = "ch%02d_%s.sfz" % (i["channel"] + 1, i["name"].split(" (")[0].replace(" / ", "_").replace(" ", "_"))
        write_sfz(os.path.join(d, "instruments", nm), insts, os.path.relpath(os.path.join(out_root, "..", samples_rel_root),
                                                                           os.path.join(d, "instruments")), only=srcn)
        i["instrument_sfz"] = "instruments/" + nm

    # self-check: render the MIDI through the kit, compare with the original's same span
    mnotes = read_midi(mid)
    regions = read_sfz(os.path.join(d, base + ".sfz"))
    echo = echo_settings(reg_events, end - 1)
    synth = render(mnotes, regions, end / RATE, echo)
    peak = np.abs(synth).max() or 1
    orig = read_wav(os.path.join(work, "songs", "song_%d_raw.wav" % u))[:end]
    gain = np.sqrt((orig.astype(np.float64) ** 2).mean()) / max(1e-9, np.sqrt((synth ** 2).mean()))
    synth_i16 = np.clip(synth * gain, -32768, 32767).astype(np.int16)
    write_wav(os.path.join(d, base + "_from_midi.wav"), synth_i16)
    check = compare(orig, synth * gain)
    check["note"] = ("MIDI rendered through the .sfz by rebuild.py's own small SFZ player plus an SNES-style echo from "
                     "the settings below, level matched; "
                     "compared with the original over the first pass")

    key, kconf = estimate_key_from_notes(notes)

    def phrase(role, count=16):
        """The first notes an instrument plays: name, length in beats (16ths rounded)."""
        srcs = [x for x, i in insts.items() if i["name"].startswith(role)]
        if not srcs:
            return None
        beat = spt_beat(T)
        out_ = []
        for n in notes:
            if n["srcn"] in srcs and "midi_note" in n:
                nn = n["midi_note"]
                out_.append("%s%d(%s)" % (KEYS[nn % 12], nn // 12 - 1,
                                          ("%g" % (round((n["off"] - n["on"]) / beat * 4) / 4)) or "0"))
                if len(out_) >= count:
                    break
        return dict(instrument=[insts[x]["name"] for x in srcs], first_notes=" ".join(out_),
                    starts_at_beat=round(min(n["on"] for n in notes if n["srcn"] in srcs) / beat, 2))
    hooks = {r: phrase(r) for r in ("lead", "bass", "counter-melody", "arpeggio", "chords")}
    hooks = {k: v for k, v in hooks.items() if v}
    voices = {}
    for v in range(8):
        vn = [n for n in notes if n["voice"] == v]
        if not vn:
            continue
        used = sorted(set(n["srcn"] for n in vn))
        voices[str(v)] = dict(notes=len(vn), instruments=[insts[s]["name"] for s in used],
                              midi_channel=[insts[s]["channel"] + 1 for s in used],
                              echo=any(n["echo"] for n in vn), noise=any(n["noise"] for n in vn),
                              pitch_changes_during_notes=sum(len(n["bends"]) for n in vn),
                              range=[min(n.get("midi_note", 0) for n in vn), max(n.get("midi_note", 0) for n in vn)])
    sheet = dict(
        id="music_%02d" % song["id"], title=song["title"], method=(
            "DSP log: every key-on/key-off, pitch and volume change and envelope/echo register the game's "
            "driver wrote while the song played on LakeSnes (tg_audio). Not decoded from the sequence bytes, "
            "so it is what the chip played; note names depend on the measured sample tuning."),
        midi=base + ".mid", sfz=base + ".sfz", stems="stems/", rendered_from_midi=base + "_from_midi.wav",
        tempo=dict(bpm=bpm, driver_tempo_value=T, driver_ticks_per_beat=tpb,
                   seconds_per_driver_tick=round(samples_per_tick / RATE, 5),
                   changes=[dict(at_s=round(t / RATE, 3), value=v) for t, v in tempo if v > 0]),
        key_estimate=dict(key=key, confidence=kconf, how="Krumhansl-Kessler profile on the note durations of the pitched instruments"),
        loop=dict(start_s=round(loop[0] / RATE, 4), end_s=round(loop[1] / RATE, 4),
                  start_sample=loop[0], end_sample=loop[1], midi_markers=["loopStart", "loopEnd"]) if loop else None,
        length_s=round(end / RATE, 3),
        echo=echo,
        voices=voices,
        instruments={"$%02X" % s: dict(
            name=i["name"], midi_channel=i["channel"] + 1, midi_program=i["program"], sample=i["sample_file"],
            instrument_sfz=i["instrument_sfz"], loop_start=i["loop_start"], length=i["length"],
            natural_rate_note=round(i["n0"], 2), f0_hz_at_32k=i["f0_hz"], pitch_keycenter=i["keycenter"],
            tune_cents=i["tune_cents"], pitched=i["tonal"], envelope=i["envelope"], sfz_envelope=i["sfz_env"],
            adsr1_adsr2_gain_used=i["adsr_raw"], notes=i["notes"], voices=sorted(i["voices"]))
            for s, i in sorted(insts.items())},
        opening_phrases=hooks,
        self_check=check,
        midi_conventions=dict(velocity="SNES voice volume max(|L|,|R|), linear amplitude", cc10="pan from L/R",
                              cc91="127 when the voice goes to the echo", pitch_bend="range %d semitones (RPN 0); "
                              "fine tuning and the driver's slides/vibrato" % BEND_RANGE,
                              channel="one per instrument (= sample)", track="one per SNES voice",
                              surround="negative SNES volumes (phase-inverted) are written as positive"),
    )
    with open(os.path.join(d, "sheet.json"), "w") as f:
        json.dump(sheet, f, indent=2)
    return sheet


def sfx_sheet(work, sfx_list, sample_files, out_root, samples_rel_root):
    """The effects: which sample at which pitch and envelope, plus a kit (one key each)."""
    d = os.path.join(out_root, "sfx")
    os.makedirs(d, exist_ok=True)
    spc = open(os.path.join(work, "songs", "song_1.spc"), "rb").read()
    ram = spc[0x100:0x10100]
    dirp = spc[0x10100 + 0x5D] << 8
    entries = []
    kit = ["// Top Gear (SNES) sound-effect samples, one key each at the pitch the game plays them.",
           "<control>", "default_path=%s" % os.path.relpath(os.path.join(out_root, "..", samples_rel_root), d), ""]
    key = 48
    for e in sfx_list:
        name = "engine_p1" if e["name"] == "engine" else "sfx_%02X" % e["cmd"]
        notes, regs, _ = parse_log(os.path.join(work, "sfx", name + "_notes.txt"), 10 ** 9) \
            if os.path.exists(os.path.join(work, "sfx", name + "_notes.txt")) else ([], [], [])
        kon = [dict(at_s=round(n["on"] / RATE, 4), voice=n["voice"], sample="$%02X" % n["srcn"], pitch="$%04X" % n["pitch"],
                    rate_hz=round(32000 * n["pitch"] / 4096),
                    later_pitches=sorted(set("$%04X" % b for _, b in n["bends"]))[:6], volume=[n["vl"], n["vr"]],
                    envelope=envelope(n["adsr1"], n["adsr2"], n["gain"])[0],
                    pitch_changes=len(n["bends"])) for n in notes]
        ent = dict(id="sfx_%02d" % e["id"], title=e["title"], commands=e["cmds"], key_ons=kon)
        if e["name"] == "engine":
            ent["pitch_rule"] = ("pitch register = RPM + $400 (ports 2/3), i.e. the engine sample "
                                 "plays at 32000 * (RPM + $400) / $1000 Hz; races use $0400-$0E00")
        if notes:
            n = notes[0]
            pcm, ls = load_sample(ram, dirp, n["srcn"])
            envd, sfz_env = envelope(n["adsr1"], n["adsr2"], n["gain"])
            # key centre such that `key` plays at the game's pitch
            # the engine keys on at pitch 0 and gets its pitch from the RPM a moment later: map
            # the key to the first real pitch (idle)
            p0 = n["pitch"] or next((b for _, b in n["bends"] if b), 0x0400)
            semis = 12 * math.log2(p0 / 4096)
            kc = key - semis
            r = ["<region>", "sample=%s" % os.path.basename(sample_files[n["srcn"]]), "key=%d" % key,
                 "pitch_keycenter=%d" % int(round(kc)), "tune=%d" % int(round((round(kc) - kc) * 100))]
            r += (["loop_mode=loop_continuous", "loop_start=%d" % ls, "loop_end=%d" % (len(pcm) - 1)]
                  if ls is not None else ["loop_mode=one_shot" if e["name"] != "engine" else "loop_mode=no_loop"])
            r += ["%s=%s" % kv for kv in sfz_env.items()]
            kit.append("// key %d: %s" % (key, e["title"]))
            kit.append(" ".join(r))
            ent["sfz_key"] = key
            key += 1
        entries.append(ent)
    open(os.path.join(d, "sfx.sfz"), "w").write("\n".join(kit) + "\n")
    with open(os.path.join(d, "sheet.json"), "w") as f:
        json.dump(entries, f, indent=2)
    return entries
