"""Musical analysis + arrangement helpers shared by all styles.

* load(song)        -> meta, notes with corrected per-song roles
* chords(meta, ns)  -> chord per half bar (root pc, quality, bass pc)
* sections(...)     -> phrase starts (crash / fill points)
* helpers that turn the chord track into pads, power chords, timpani hits, etc.
The original lead and bass lines are never changed (note for note, same timing);
only the instruments and the added support parts differ between styles.
"""
import json
from collections import defaultdict

import numpy as np

from .parse_midi import events_path

# (voice, inst) -> role overrides; default role comes from parse_midi. Which SNES voice and
# instrument number plays which part in each song (no notes: the notes come from the ROM).
ROLE_MAP = {
    "01_title": {(0, "$01"): "pulse", (3, "$07"): "chord", (4, "$07"): "chord", (5, "$07"): "chord",
                 (3, "$05"): "fx", (4, "$05"): "fx", (5, "$05"): "fx",
                 (6, "$07"): "arp", (7, "$07"): "arp2", (2, "$08"): "lead", (7, "$08"): "lead"},
    "02_las_vegas": {(2, "$03"): "chord", (1, "$01"): "lead"},
    "03_hiroshima": {},
    "04_bordeaux": {(0, "$01"): "riff", (3, "$05"): "kick"},
    "05_frankfurt": {(1, "$0F"): "fx"},
    "06_qualified": {(v, "$03"): "chord" for v in (0, 1, 2, 4, 5, 6)} | {(3, "$02"): "lead"},
    "07_ending": {(5, "$07"): "chord", (6, "$07"): "chord", (7, "$07"): "chord",
                  (3, "$02"): "lead2", (1, "$03"): "lead"},
}
# hat sample variants: which hat instruments are "open"
OPEN_HAT = {("01_title", "$02")}

PC = "C C# D Eb E F F# G Ab A Bb B".split()


def load(song):
    d = json.loads(events_path(song).read_text())
    rm = ROLE_MAP.get(song, {})
    for n in d["notes"]:
        r = rm.get((n["voice"], n["inst"]))
        if r in ("kick", "snare", "hat"):
            n["role"], n["drum"] = "drum", r
        elif r:
            n["role"] = r
        if n["role"] == "drum" and n["drum"] == "hat" and (song, n["inst"]) in OPEN_HAT:
            n["drum"] = "ohat"
    return d["meta"], d["notes"]


def by_role(notes, *roles):
    return [n for n in notes if n["role"] in roles]


def drums(notes, kind):
    return [n for n in notes if n["role"] == "drum" and n["drum"] == kind]


# ------------------------------------------------------------------ harmony
TEMPLATES = {
    "": [0, 4, 7], "m": [0, 3, 7], "7": [0, 4, 7, 10], "m7": [0, 3, 7, 10],
    "maj7": [0, 4, 7, 11], "sus4": [0, 5, 7], "dim": [0, 3, 6], "5": [0, 7],
}


def chords(meta, notes, seg_beats=2.0):
    spb = meta["spb"]
    nseg = int(np.ceil(meta["pass_beats"] / seg_beats))
    hist = np.zeros((nseg, 12))
    bass = np.full(nseg, -1)
    bass_w = np.zeros((nseg, 12))
    for n in notes:
        if n["role"] in ("drum", "fx", "pulse"):
            continue
        w_role = {"bass": 1.5, "arp": 1.0, "arp2": 0.5, "chord": 1.6, "pad": 1.4, "riff": 1.2,
                  "lead": 0.6, "lead2": 0.6}.get(n["role"], 0.5)
        b0, b1 = n["beat"], n["beat"] + max(n["beats"], 0.1)
        for s in range(int(b0 // seg_beats), min(nseg, int(b1 // seg_beats) + 1)):
            ov = min(b1, (s + 1) * seg_beats) - max(b0, s * seg_beats)
            if ov > 0:
                hist[s, n["note"] % 12] += ov * w_role * (0.5 + n["vel"])
                if n["role"] in ("bass", "riff"):
                    bass_w[s, n["note"] % 12] += ov * (2.0 if abs(n["beat"] - s * seg_beats) < 0.3 else 1.0)
    out = []
    prev = None
    for s in range(nseg):
        h = hist[s]
        if h.sum() < 1e-6:
            out.append(prev or {"beat": s * seg_beats, "root": 0, "q": "m", "bass": 0, "pcs": [0, 3, 7]})
            continue
        bpc = int(bass_w[s].argmax()) if bass_w[s].sum() > 0 else -1
        best = None
        for root in range(12):
            for q, tpl in TEMPLATES.items():
                if q in ("7", "m7", "maj7", "dim", "5"):
                    pen = 0.15
                else:
                    pen = 0.0
                inn = sum(h[(root + i) % 12] for i in tpl)
                outn = h.sum() - inn
                sc = inn / len(tpl) ** 0.35 - 0.6 * outn - pen * h.sum()
                if root == bpc:
                    sc += 0.25 * h.sum()
                if prev and prev["root"] == root and prev["q"] == q:
                    sc += 0.05 * h.sum()
                if best is None or sc > best[0]:
                    best = (sc, root, q)
        _, root, q = best
        c = {"beat": s * seg_beats, "root": root, "q": q, "bass": bpc if bpc >= 0 else root,
             "pcs": [(root + i) % 12 for i in TEMPLATES[q]]}
        out.append(c)
        prev = c
    for c in out:
        c["t"] = c["beat"] * spb
        c["dur"] = seg_beats * spb
    return out


def chord_name(c):
    return PC[c["root"]] + c["q"]


def merge_chords(ch):
    """Join consecutive equal chords into longer events."""
    out = []
    for c in ch:
        if out and out[-1]["root"] == c["root"] and out[-1]["q"] == c["q"] and out[-1]["bass"] == c["bass"]:
            out[-1]["dur"] += c["dur"]
        else:
            out.append(dict(c))
    return out


def voicing(c, low=55, high=72, n=4):
    """Close voicing of chord pcs in [low, high], smooth-ish (lowest first)."""
    notes = []
    for p in range(low, high + 1):
        if p % 12 in c["pcs"]:
            notes.append(p)
    # keep at most n notes, prefer spread
    if len(notes) > n:
        idx = np.linspace(0, len(notes) - 1, n).round().astype(int)
        notes = [notes[i] for i in idx]
    return notes


def pad_notes(chords_, low=55, high=74, vel=0.6, n=4, gap=0.02):
    out = []
    for c in merge_chords(chords_):
        for p in voicing(c, low, high, n):
            out.append({"t": c["t"], "dur": c["dur"] - gap, "note": p, "vel": vel})
    return out


def power_chords(meta, notes, chords_, src_roles=("bass",), octave_shift=12, low=40, high=55):
    """Rock rhythm guitar: follow the bass rhythm, play root+5th+octave over the chord root
    (bass note moved into guitar range)."""
    out = []
    src = sorted(by_role(notes, *src_roles), key=lambda n: n["t"])
    for n in src:
        p = n["note"] + octave_shift
        while p < low:
            p += 12
        while p > high:
            p -= 12
        dur = max(n["dur"], 0.08)
        v = min(1.0, 0.65 + n["vel"] * 0.5)
        for iv in (0, 7, 12):
            out.append({"t": n["t"], "dur": dur, "note": p + iv, "vel": v})
    return out


def legato(ns, max_gap_beats=0.3, spb=0.4, cap=None):
    """Lengthen notes into the next one (for bowed / blown / sung lines)."""
    ns = sorted(ns, key=lambda n: n["t"])
    out = []
    for i, n in enumerate(ns):
        m = dict(n)
        if i + 1 < len(ns):
            gap = ns[i + 1]["t"] - (n["t"] + n["dur"])
            if 0 < gap < max_gap_beats * spb:
                m["dur"] = ns[i + 1]["t"] - n["t"] - 0.005
        if cap:
            m["dur"] = min(m["dur"], cap)
        out.append(m)
    return out


def mono(ns):
    """Keep a monophonic line (highest note on simultaneous onsets)."""
    by_t = defaultdict(list)
    for n in ns:
        by_t[round(n["t"], 3)].append(n)
    return [max(v, key=lambda n: n["note"]) for _, v in sorted(by_t.items())]


def phrase_starts(meta, notes, roles=("lead",), min_gap_beats=3.0):
    ls = sorted(by_role(notes, *roles), key=lambda n: n["beat"])
    starts = []
    last_end = -1e9
    for n in ls:
        if n["beat"] - last_end >= min_gap_beats:
            starts.append(n["beat"])
        last_end = max(last_end, n["beat"] + n["beats"])
    return starts


def section_beats(meta, notes, every=32):
    """Crash points: first downbeat, every `every` beats where drums play, lead phrase starts
    snapped to the bar."""
    kicks = [n["beat"] for n in drums(notes, "kick")]
    pts = set()
    if kicks:
        pts.add(round(min(kicks) / 4) * 4)
    for b in phrase_starts(meta, notes):
        pts.add(round(b / 4) * 4)
    b = 0
    while b < meta["pass_beats"]:
        if any(abs(k - b) < 0.3 for k in kicks):
            pts.add(b)
        b += every
    pts = sorted(p for p in pts if p < meta["pass_beats"] - 1)
    # thin: at least 8 beats apart
    out = []
    for p in pts:
        if not out or p - out[-1] >= 8:
            out.append(p)
    return out


def grid_notes(meta, start_beat, end_beat, step_beats, note, vel, dur_beats=None, swing=0.0,
               accent_every=None, accent=1.0):
    spb = meta["spb"]
    out = []
    b = start_beat
    i = 0
    while b < end_beat - 1e-6:
        v = vel * (accent if accent_every and i % accent_every == 0 else 1.0)
        off = swing * step_beats if i % 2 == 1 else 0.0
        out.append({"t": (b + off) * spb, "dur": (dur_beats or step_beats * 0.5) * spb, "note": note, "vel": v})
        b += step_beats
        i += 1
    return out


def active_ranges(meta, ns, gap_beats=2.0):
    """Beat ranges where a set of notes plays (merged)."""
    rs = []
    for n in sorted(ns, key=lambda n: n["beat"]):
        s, e = n["beat"], n["beat"] + max(n["beats"], 0.25)
        if rs and s - rs[-1][1] <= gap_beats:
            rs[-1][1] = max(rs[-1][1], e)
        else:
            rs.append([s, e])
    return rs


def to_notes(ns, transpose=0, vel_scale=1.0, vel_min=0.0, vel_curve=1.0, dur_min=0.0, keep_bends=True):
    """Normalise source notes to renderer notes (velocities normalised per role)."""
    if not ns:
        return []
    vmax = max(n["vel"] for n in ns) or 1.0
    out = []
    for n in ns:
        v = (n["vel"] / vmax) ** vel_curve
        v = vel_min + (1 - vel_min) * v
        out.append({"t": n["t"], "dur": max(n["dur"], dur_min), "note": n["note"] + transpose,
                    "vel": float(np.clip(v * vel_scale, 0.01, 1.0)), "pan": n.get("pan", 0.0),
                    "bends": n.get("bends", []) if keep_bends else []})
    return out
