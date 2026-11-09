"""Per-stage (city) remixes of the 4 race tunes, built on the FAITHFUL versions.

In the original game the race music depends only on the course's position in a country, so
8 tracks share each tune. Here every one of the 32 stages gets its own variant, coloured by
its city: signature instruments, local rhythm, a recognisable local motif on section starts,
scale colour, and (option B) a re-arrangement from reimagine.py. The melody is always kept.

Option A = "Postcard": faithful arrangement + city instrumentation & percussion.
Option B = "Local band": a rearranged faithful version (v1/v2/v3) where the city instruments
           take over parts and the groove/kit changes.

The local motifs (M) are generic figures (bell chimes, a cuckoo call, a fanfare, ...), and
the percussion patterns (P) are genre patterns; neither comes from the game.

render_stage(stage, "A"|"B", out) writes <out>/stages/<country>/<country>__<city>__<A|B>.ogg.
"""
from pathlib import Path

import numpy as np

from . import arrange as A
from . import render as R

TUNE = {1: "02_las_vegas", 2: "03_hiroshima", 3: "04_bordeaux", 4: "05_frankfurt"}
COUNTRIES = ["USA", "South America", "Japan", "Germany", "Scandinavia", "France", "Italy", "UK"]

# GM ranges for octave folding (program -> lo, hi)
RANGE = {21: (53, 89), 22: (60, 96), 24: (40, 84), 25: (40, 84), 27: (40, 86), 28: (40, 76), 32: (28, 55), 33: (28, 55),
         36: (28, 55), 37: (28, 55), 40: (55, 100), 41: (48, 88), 42: (36, 76), 45: (36, 96), 46: (24, 103),
         56: (55, 84), 57: (40, 72), 58: (28, 55), 59: (55, 84), 60: (41, 77), 61: (41, 84), 62: (36, 96),
         64: (56, 87), 65: (49, 81), 66: (44, 76), 68: (58, 91), 71: (50, 91), 72: (74, 108), 73: (60, 96),
         74: (60, 96), 75: (60, 96), 77: (55, 86), 78: (60, 96), 79: (60, 89), 104: (48, 84), 105: (48, 84),
         106: (50, 84), 107: (45, 91), 108: (52, 88), 109: (55, 77), 110: (55, 100), 114: (52, 88), 116: (36, 52)}
GM_PERC = 128
KITS = {"standard": 0, "room": 8, "power": 16, "electronic": 24, "808": 25, "jazz": 32, "brush": 40, "orchestra": 48}

# ------------------------------------------------------------------ percussion patterns (beat offset, GM note, vel)
P = {
    "samba": [(1, 41, .9), (3, 41, 1.0), (3.5, 43, .5)]
             + [(o / 4, 60, .55 if i in (0, 3, 6, 9, 10, 13) else 0) for i, o in enumerate(range(16))]
             + [(0, 67, .7), (0.5, 68, .6), (1.5, 67, .7), (2, 68, .6), (2.5, 67, .6), (3, 68, .7)]
             + [(o / 4, 70, .65 if o % 4 in (1, 3) else .4) for o in range(16)],
    "samba_full": [(0, 36, .9), (1, 41, 1.0), (2, 36, .8), (3, 41, 1.0)]
                  + [(o / 4, 60, .6) for o in (0, 2, 3, 5, 7, 8, 10, 11, 13, 15)]
                  + [(0, 67, .75), (0.75, 68, .6), (1.5, 67, .7), (2, 68, .6), (2.75, 67, .6), (3.5, 68, .7)]
                  + [(o / 4, 70, .7 if o % 4 in (1, 3) else .45) for o in range(16)] + [(2.5, 79, .5), (3.75, 78, .45)],
    "taiko": [(0, 116, 1.0), (1.5, 116, .7), (2, 116, .9), (3, 116, .6), (3.5, 116, .8)],
    "taiko_big": [(0, 116, 1.0), (0.5, 116, .5), (1, 116, .8), (1.75, 116, .6), (2, 116, 1.0), (2.5, 116, .6),
                  (3, 116, .9), (3.25, 116, .5), (3.5, 116, .8), (3.75, 116, .6)],
    "matsuri": [(0, 116, 1.0), (1.5, 116, .7), (2, 116, .9), (3.5, 116, .7)] + [(o / 2, 80, .5) for o in range(8)]
               + [(0, 56, .55), (1, 56, .4), (2, 56, .55), (3, 56, .4)],
    "kotsuzumi": [(0, 61, .8), (1.5, 60, .7), (2.5, 61, .6), (3, 60, .8)],
    "congas": [(0, 64, .8), (0.5, 62, .5), (1, 63, .7), (1.5, 62, .5), (2, 64, .8), (2.75, 63, .6), (3, 62, .6), (3.5, 63, .7)],
    "rainforest": [(o / 4, 69, .55 if o % 2 else .35) for o in range(16)] + [(0, 64, .7), (1.5, 63, .6), (2.5, 64, .6), (3, 62, .5)],
    "bombo": [(0, 41, 1.0), (1.5, 41, .6), (2, 41, .9), (2.5, 45, .5), (3, 41, .7)] + [(o / 2, 69, .45) for o in range(8)],
    "ritual": [(0, 41, 1.0), (1, 43, .7), (2, 41, .9), (2.5, 45, .6), (3, 43, .8), (3.5, 45, .6)] + [(o / 2, 77, .5) for o in range(8)],
    "woodblock": [(o / 2, 76 if o % 2 == 0 else 77, .6) for o in range(8)],
    "tambourine8": [(o / 2, 54, .6 if o % 2 else .45) for o in range(8)],
    "tambourine16": [(o / 4, 54, .65 if o % 4 == 2 else .38) for o in range(16)],
    "tarantella": [(o / 4, 54, .7 if o % 4 in (0,) else .4) for o in range(16)] + [(0, 41, .7), (2, 41, .7)],
    "clap24": [(1, 39, .8), (3, 39, .8)],
    "shaker16": [(o / 4, 70, .55 if o % 2 else .35) for o in range(16)],
    "ride": [(o, 51, .55) for o in range(4)] + [(o + 0.5, 42, .3) for o in range(4)],
    "swingride": [(0, 51, .6), (1, 51, .6), (1.67, 51, .45), (2, 51, .6), (3, 51, .6), (3.67, 51, .45), (1, 44, .5), (3, 44, .5)],
    "four_floor": [(o, 36, 1.0) for o in range(4)] + [(o + 0.5, 46, .55) for o in range(4)] + [(1, 39, .7), (3, 39, .7)],
    "disco": [(o, 36, .9) for o in range(4)] + [(o + 0.5, 46, .6) for o in range(4)] + [(o / 4, 42, .3) for o in range(0, 16, 2)],
    "pipe_snare": [(o / 4, 38, .5 if o % 4 == 0 else .25) for o in range(16)],
    "bodhran": [(0, 41, .9), (0.5, 45, .4), (0.75, 45, .4), (1, 41, .7), (1.5, 45, .5), (2, 41, .9), (2.5, 45, .4), (3, 41, .7), (3.5, 45, .5), (3.75, 45, .4)],
    "clock": [(o, 76 if o % 2 == 0 else 77, .55) for o in range(4)],
    "gong": [(0, 52, .9)],
    "oompah_kit": [(0, 36, .9), (2, 36, .9), (1, 38, .6), (3, 38, .6)],
    "linn": [(1, 39, .8), (3, 39, .8), (0, 36, .8), (2.5, 36, .6)] + [(o / 4, 42, .35) for o in range(0, 16, 2)],
}

# motifs at section starts: (beat offset, semitones from tonic, beats, vel)
M = {
    "westminster": [(0, 3, 1, .9), (1, 2, 1, .85), (2, 0, 1, .85), (3, -5, 2, .95)],
    "cuckoo": [(0, 7, .5, .9), (0.75, 3, .75, .85), (2, 7, .5, .9), (2.75, 3, .75, .85)],
    "lur": [(0, 0, 2, .8), (2, 7, 1.5, .9), (3.5, 0, 2.5, .8)],
    "temple": [(0, 0, 6, 1.0)],
    "vegas_bell": [(0, 12, .25, .9), (0.25, 16, .25, .9), (0.5, 19, .25, .9), (0.75, 24, .75, 1.0)],
    "cable_car": [(0, 12, .4, 1.0), (0.5, 12, .4, 1.0), (1.5, 12, .4, .9)],
    "whistle": [(0, 71, .5, 1.0), (0.75, 71, .25, .9), (1, 72, .75, 1.0)],
    "conch": [(0, -12, 3, .9), (3, -10, 1, .7)],
    "fanfare": [(0, 0, .5, .9), (0.5, 7, .5, .9), (1, 12, 1.5, 1.0)],
    "gong_perc": [(0, 52, 4, 1.0), (0, 49, 4, .7)],
}


# ------------------------------------------------------------------ helpers
def fold(ns, prog):
    lo, hi = RANGE.get(prog, (36, 96))
    out = []
    for n in ns:
        k = n["note"]
        while k > hi:
            k -= 12
        while k < lo:
            k += 12
        out.append(dict(n, note=k))
    return out


def sf2(prog, bank=0):
    return {"type": "sf2", "program": prog, "bank": bank}


def tonic_of(meta):
    root, _ = meta["key"].split()
    return {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11, "Eb": 3, "Bb": 10, "Ab": 8}[root]


SCALES = {"minpent": [0, 3, 5, 7, 10], "in": [0, 2, 3, 7, 8], "dorian": [0, 2, 3, 5, 7, 9, 10]}


def snap(ns, tonic, scale):
    pcs = [(tonic + s) % 12 for s in SCALES[scale]]
    out = []
    for n in ns:
        k = n["note"]
        if k % 12 not in pcs:
            k = min((k + d for d in (-1, 1, -2, 2)), key=lambda x: (x % 12 not in pcs, abs(x - n["note"])))
        out.append(dict(n, note=k))
    return out


class Stage:
    def __init__(self, meta, notes, ch, parts):
        self.meta, self.notes, self.ch, self.parts = meta, notes, ch, parts
        self.spb = meta["spb"]
        self.tonic = tonic_of(meta)
        self.drum_rng = A.active_ranges(meta, A.drums(notes, "kick") + A.drums(notes, "snare"), gap_beats=4)
        self.lead_rng = A.active_ranges(meta, A.by_role(notes, "lead"), gap_beats=8)
        self.sections = A.section_beats(meta, notes)

    # sources
    def src(self, roles, vel_min=0.5):
        for r in (roles if isinstance(roles, tuple) else (roles,)):
            ns = A.to_notes(A.by_role(self.notes, r), vel_min=vel_min, keep_bends=False)
            if ns:
                return ns
        return []

    def part(self, name):
        return next((p for p in self.parts if p["name"] == name), None)

    def add(self, name, spec, ns, **mix):
        if ns:
            self.parts.append(dict(name=name, spec=spec, notes=ns, **mix))

    # ops
    def dbl(self, name, roles, prog, transpose=0, gain=-8, pan=0.0, legato=False, vel=1.0, rev=0.15, **kw):
        ns = self.src(roles)
        if legato:
            ns = A.legato(ns, 0.6, self.spb)
        ns = [dict(n, note=n["note"] + transpose, vel=n["vel"] * vel) for n in ns]
        if kw.pop("scale", None):
            ns = snap(ns, self.tonic, kw.get("scale_name", "minpent"))
        kw.pop("scale_name", None)
        self.add(name, sf2(prog), fold(ns, prog), gain=gain, pan=pan, rev=rev, **kw)

    def swap(self, part, prog, gain=None, legato=False, transpose=0, keep_under=None, scale=None):
        """City instrument takes over a faithful part; optionally keep the faithful sound underneath."""
        p = self.part(part)
        if not p:
            return
        ns = [dict(n, note=n["note"] + transpose) for n in p["notes"]]
        if legato:
            ns = A.legato(ns, 0.6, self.spb)
        if scale:
            ns = snap(ns, self.tonic, scale)
        if keep_under is not None:
            self.parts.append(dict(p, name=p["name"] + "_orig", gain=p.get("gain", 0) + keep_under))
        p.update(spec=sf2(prog), notes=fold(ns, prog))
        if gain is not None:
            p["gain"] = gain

    def quiet(self, part, db):
        p = self.part(part)
        if p:
            p["gain"] = p.get("gain", 0) + db

    def drop(self, *names):
        self.parts[:] = [p for p in self.parts if p["name"] not in names]

    def kit(self, kit, gain=None):
        p = self.part("drums")
        if p:
            p["spec"] = sf2(KITS[kit], GM_PERC)
            if gain is not None:
                p["gain"] = gain

    def in_drums(self, b):
        return any(s - 0.01 <= b < e + 0.5 for s, e in self.drum_rng)

    def perc(self, name, pattern, gain=-12, pan=0.0, kit="standard", bar=4, rev=0.1, everywhere=False, seed=7):
        rng = np.random.default_rng(seed)
        ns = []
        b = 0.0
        while b < self.meta["pass_beats"] - 0.01:
            if everywhere or self.in_drums(b + 0.01):
                for off, note, v in pattern:
                    if v <= 0:
                        continue
                    t = (b + off) * self.spb + rng.normal(0, 0.004)
                    ns.append({"t": max(0.0, t), "dur": 0.25, "note": note, "vel": float(np.clip(v * rng.uniform(.9, 1.05), .05, 1))})
            b += bar
        taiko = [dict(n, note=40 if n["vel"] > .85 else 45) for n in ns if n["note"] == 116]  # melodic GM taiko
        rest = [n for n in ns if n["note"] != 116]
        self.add(name + ("_taiko" if rest else ""), sf2(116), taiko, gain=gain, pan=pan, bus="drums", rev=rev)
        self.add(name, sf2(KITS[kit], GM_PERC), rest, gain=gain - (3 if taiko else 0), pan=pan, bus="drums", rev=rev)

    def motif(self, name, motif, prog, octave=5, gain=-10, pan=0.0, rev=0.3, every=1, perc=False):
        ns = []
        base = 12 * (octave + 1) + self.tonic
        for i, sb in enumerate(self.sections):
            if i % every:
                continue
            for off, semi, beats, v in M[motif]:
                note = semi if perc else base + semi
                ns.append({"t": (sb + off) * self.spb, "dur": beats * self.spb, "note": note, "vel": v})
        spec = sf2(KITS["standard"], GM_PERC) if perc else sf2(prog)
        extra = {"bus": "drums"} if perc else {}
        self.add(name, spec, ns if perc else fold(ns, prog) if prog in RANGE else ns, gain=gain, pan=pan, rev=rev, **extra)

    def comp(self, name, prog, offsets, low=55, high=72, n=4, dur=0.4, vel=0.7, gain=-10, pan=0.0, rev=0.15, drums_only=True, **kw):
        out = []
        nseg = len(self.ch)
        b = 0.0
        while b < self.meta["pass_beats"] - 0.01:
            if not drums_only or self.in_drums(b + 0.01):
                for off in offsets:
                    c = self.ch[min(nseg - 1, int((b + off) // 2))]
                    for k in A.voicing(c, low, high, n):
                        out.append({"t": (b + off) * self.spb, "dur": dur * self.spb, "note": k, "vel": vel})
            b += 4
        self.add(name, sf2(prog), out, gain=gain, pan=pan, rev=rev, **kw)

    def pad(self, name, prog, low=55, high=74, n=4, vel=0.55, gain=-14, pan=0.0, rev=0.3, lead_only=False, **kw):
        ns = A.pad_notes(self.ch, low, high, vel=vel, n=n)
        if lead_only:
            ns = [x for x in ns if any(s <= x["t"] / self.spb < e for s, e in self.lead_rng)]
        self.add(name, sf2(prog), ns, gain=gain, pan=pan, rev=rev, **kw)

    def oompah(self, tuba=58, brass=61, gain_t=-6, gain_b=-11):
        bass, chords = [], []
        nseg = len(self.ch)
        b = 0.0
        while b < self.meta["pass_beats"] - 0.01:
            if self.in_drums(b + 0.01):
                for off in (0, 2):
                    c = self.ch[min(nseg - 1, int((b + off) // 2))]
                    r = c["bass"] if off == 0 else (c["bass"] + 7) % 12
                    bass.append({"t": (b + off) * self.spb, "dur": 0.8 * self.spb, "note": 36 + (r - 36) % 12, "vel": .85})
                for off in (1, 3):
                    c = self.ch[min(nseg - 1, int((b + off) // 2))]
                    for k in A.voicing(c, 58, 72, 3):
                        chords.append({"t": (b + off) * self.spb, "dur": 0.35 * self.spb, "note": k, "vel": .7})
            b += 4
        self.add("tuba", sf2(tuba), fold(bass, tuba), gain=gain_t, pan=-0.1, rev=0.12)
        self.add("oompah_brass", sf2(brass), chords, gain=gain_b, pan=0.2, rev=0.15)

    def drone(self, prog, gain=-14, intervals=(0, 7), octave=3):
        ns = []
        for s, e in self.lead_rng:
            b = s
            while b < e:
                L = min(16, e - b)
                for iv in intervals:
                    ns.append({"t": b * self.spb, "dur": L * self.spb - 0.02, "note": 12 * (octave + 1) + self.tonic + iv, "vel": .7})
                b += 16
        self.add("drone", sf2(prog), ns, gain=gain, pan=0.0, rev=0.2)

    def tremolo(self, name, roles, prog, step=0.25, transpose=0, gain=-10, pan=0.0, min_beats=0.5, rev=0.15):
        out = []
        for n in A.legato(self.src(roles), 0.6, self.spb):
            beats = n["dur"] / self.spb
            if beats < min_beats:
                out.append(dict(n, note=n["note"] + transpose))
                continue
            b = 0.0
            while b < beats - 0.05:
                out.append(dict(n, t=n["t"] + b * self.spb, dur=step * 0.9 * self.spb, note=n["note"] + transpose,
                                vel=n["vel"] * (0.85 if int(b / step) % 2 else 1)))
                b += step
        self.add(name, sf2(prog), fold(out, prog), gain=gain, pan=pan, rev=rev)

    def arpgen(self, name, prog, step=0.25, pattern=(0, 1, 2, 3, 2, 1), low=60, high=76, vel=.6, gain=-12, pan=0.0, scale=None, rev=0.2):
        out = []
        nseg = len(self.ch)
        b, i = 0.0, 0
        while b < self.meta["pass_beats"] - 0.01:
            if self.in_drums(b + 0.01):
                v = A.voicing(self.ch[min(nseg - 1, int(b // 2))], low, high, 4)
                if v:
                    acc = 1.0 if abs(b - round(b)) < 1e-6 else 0.8
                    out.append({"t": b * self.spb, "dur": step * 0.9 * self.spb, "note": v[pattern[i % len(pattern)] % len(v)], "vel": vel * acc})
            b += step
            i += 1
        if scale:
            out = snap(out, self.tonic, scale)
        self.add(name, sf2(prog), fold(out, prog), gain=gain, pan=pan, rev=rev)

    def fx(self, name, prog, note, every=8, gain=-16, pan=0.4, seed=3):
        rng = np.random.default_rng(seed)
        ns = []
        b = 2.0
        while b < self.meta["pass_beats"] - 2:
            ns.append({"t": (b + rng.uniform(0, 2)) * self.spb, "dur": rng.uniform(0.6, 1.5), "note": note + int(rng.integers(-3, 4)), "vel": rng.uniform(.5, .9)})
            b += every * rng.uniform(.7, 1.3)
        self.add(name, sf2(prog), ns, gain=gain, pan=float(rng.uniform(-pan, pan)), rev=0.35)


# ------------------------------------------------------------------ the 32 stages
# (country, city, tune, story, A-recipe, B-version, B-recipe, reverb_style)
def r(f):
    return f


STAGES = []


def stage(country, city, tune, story, b_version, reverb="faithful"):
    def deco(fn):
        STAGES.append(dict(country=country, city=city, tune=tune, story=story, b_version=b_version, fn=fn, reverb=reverb))
        return fn
    return deco


# ---- USA
@stage("USA", "Las Vegas", 1, "Neon strip showband: brass hits, alto sax, vibes, jackpot-bell flourish", "v1", "synthwave")
def las_vegas(S, B):
    S.motif("jackpot", "vegas_bell", 112, octave=5, gain=-12, pan=0.3)
    S.comp("brass_hits", 61, (0, 2.5), 55, 74, dur=0.35, vel=.8, gain=-11, pan=-0.2)
    S.dbl("vibes", ("arp", "riff"), 11, gain=-15, pan=0.35)
    if B:
        S.swap("lead", 65, gain=-4, legato=True, keep_under=-9)
        S.dbl("tenor_counter", "counter", 66, gain=-9, pan=-0.3, legato=True)
        S.comp("piano", 0, (1, 3), 55, 72, dur=0.3, vel=.6, gain=-13, pan=0.25)
        S.perc("ride", P["ride"], gain=-15, pan=0.3)
    else:
        S.dbl("alto_sax", "lead", 65, gain=-9, legato=True, pan=-0.1)


@stage("USA", "Los Angeles", 2, "West-coast G-funk: slap bass, whistle synth, claps", "v2", "synthwave")
def los_angeles(S, B):
    S.dbl("slap", "bass", 36, gain=-7, pan=0.0)
    S.perc("claps", P["clap24"], gain=-12, pan=0.0)
    if B:
        S.swap("lead", 81, gain=-4, keep_under=-9)
        S.comp("funk_gtr", 28, (0.5, 1.25, 2.5, 3.25), 55, 67, n=3, dur=0.15, vel=.75, gain=-12, pan=0.4)
        S.perc("tamb", P["tambourine8"], gain=-15, pan=-0.3)
        S.quiet("bass", -4)
    else:
        S.dbl("gfunk_whistle", "lead", 80, transpose=12, gain=-14, legato=True, pan=0.2)


@stage("USA", "New York", 3, "Manhattan jazz club: tenor sax, piano comping, ride cymbal; B = muted trumpet, walking upright, brushes", "v1")
def new_york(S, B):
    S.comp("piano", 0, (0, 1.5), 52, 70, dur=0.4, vel=.65, gain=-12, pan=0.25)
    if B:
        S.swap("lead", 59, gain=-4, legato=True, keep_under=-10)
        S.swap("bass", 32, gain=-3)
        S.dbl("sax_counter", "counter", 66, gain=-9, legato=True, pan=-0.3)
        S.kit("brush", gain=-3)
        S.drop("drums_snap")
        S.perc("swing_ride", P["swingride"], gain=-13, pan=0.3)
    else:
        S.dbl("tenor_sax", "lead", 66, gain=-8, legato=True, pan=-0.15)
        S.perc("ride", P["ride"], gain=-15, pan=0.3)


@stage("USA", "San Francisco", 4, "Cable-car bells & Summer of Love: Hammond organ, 12-string jangle; B = major lift + sitar", "v3")
def san_francisco(S, B):
    S.motif("cable_bell", "cable_car", 113 if B else 14, octave=5, gain=-11, pan=0.3)
    S.pad("hammond", 16, 52, 72, gain=-14, pan=-0.2)
    S.dbl("jangle12", ("arp", "riff"), 25, gain=-11, pan=0.35)
    if B:
        S.dbl("sitar", "lead", 104, gain=-9, pan=-0.3)
        S.dbl("organ_lead", "counter", 17, gain=-11, pan=0.2)
        S.perc("tamb", P["tambourine8"], gain=-14, pan=-0.35)


# ---- South America
@stage("South America", "Rio", 1, "Carnival samba: surdo, tamborim, agogô, shakers, whistle, nylon guitar; B = full batucada + trumpets", "v2", "synthwave")
def rio(S, B):
    S.motif("apito", "whistle", 0, perc=True, gain=-12, every=2)
    S.dbl("nylon", ("arp", "riff"), 24, gain=-11, pan=-0.3)
    if B:
        S.perc("batucada", P["samba_full"], gain=-6, pan=0.0)
        S.dbl("trumpets", "lead", 56, gain=-8, legato=True, pan=0.15)
        S.comp("brass_stabs", 61, (0.5, 2.75), 55, 74, dur=0.3, vel=.85, gain=-12, pan=-0.2)
        S.quiet("drums", -4)
    else:
        S.perc("samba", P["samba"], gain=-9, pan=0.1)


@stage("South America", "Machu Picchu", 2, "Andes: pan flute (quena), charango tremolo, bombo; B = pentatonic charango + second flute", "v1", "orchestral")
def machu_picchu(S, B):
    S.perc("bombo", P["bombo"], gain=-9)
    if B:
        S.swap("lead", 75, gain=-3, legato=True, keep_under=-10)
        S.swap("arp", 105, gain=-8, scale="minpent")
        S.dbl("quena2", "counter", 73, gain=-9, legato=True, pan=-0.3)
    else:
        S.dbl("panflute", "lead", 75, gain=-6, legato=True, pan=0.15)
        S.tremolo("charango", ("arp", "riff"), 105, step=0.25, gain=-14, pan=-0.3)


@stage("South America", "Chichen Itza", 3, "Maya temple: marimba, ocarina, slit-drum woodblocks; B = conch call, ritual toms, marimba lead", "v3")
def chichen_itza(S, B):
    S.dbl("marimba", ("riff", "arp", "bass"), 12, transpose=12, gain=-10, pan=-0.3)
    if B:
        S.motif("conch", "conch", 60, octave=4, gain=-10, rev=0.45)
        S.perc("ritual", P["ritual"], gain=-8)
        S.dbl("marimba_lead", "lead", 12, gain=-6, pan=0.2)
        S.dbl("ocarina2", "counter", 79, gain=-10, legato=True, pan=-0.2)
    else:
        S.dbl("ocarina", "lead", 79, gain=-8, legato=True, pan=0.15)
        S.perc("slit", P["woodblock"], gain=-14, pan=0.3)


@stage("South America", "Rain Forest", 4, "Amazon: kalimba, rain-stick & congas, birds; B = steel-drum lead and a jungle groove", "v2")
def rain_forest(S, B):
    S.dbl("kalimba", ("arp", "riff"), 108, gain=-10, pan=-0.3)
    S.perc("rain", P["rainforest"], gain=-11, pan=0.2)
    S.fx("birds", 123, 84, every=6, gain=-16)
    if B:
        S.dbl("steel_drums", "lead", 114, gain=-6, pan=0.15)
        S.perc("congas", P["congas"], gain=-9, pan=-0.2)
        S.pad("rain_pad", 75, 62, 79, n=3, gain=-17)


# ---- Japan
@stage("Japan", "Tokyo", 1, "Neon city-pop: DX electric piano, slap bass, synth brass; B = maj7 reharm with disco strings", "v1", "synthwave")
def tokyo(S, B):
    S.comp("dx_ep", 5, (0, 1.5, 3), 55, 74, dur=0.5, vel=.65, gain=-11, pan=0.25)
    S.dbl("slap", "bass", 37, gain=-8)
    if B:
        S.comp("synth_brass", 62, (0, 2.5), 55, 74, dur=0.4, vel=.8, gain=-12, pan=-0.2)
        S.dbl("strings_counter", "counter", 48, transpose=12, gain=-11, legato=True, pan=-0.3)
        S.perc("shaker", P["shaker16"], gain=-16, pan=0.3)
    else:
        S.motif("brass_fan", "fanfare", 62, octave=4, gain=-12)


@stage("Japan", "Hiroshima", 2, "Koto arpeggios, shakuhachi melody, taiko; B = pentatonic koto, shakuhachi lead, taiko ensemble, temple bell", "original", "orchestral")
def hiroshima(S, B):
    if B:
        S.swap("arp", 107, gain=-6, scale="minpent")
        S.swap("lead", 77, gain=-3, legato=True, keep_under=-10)
        S.perc("taiko", P["taiko_big"], gain=-6)
        S.motif("temple_bell", "temple", 14, octave=3, gain=-11, every=2, rev=0.5)
        S.quiet("drums", -3)
    else:
        S.dbl("koto", "arp", 107, gain=-9, pan=-0.3)
        S.dbl("shakuhachi", "lead", 77, gain=-8, legato=True, pan=0.15)
        S.perc("taiko", P["taiko"], gain=-10)


@stage("Japan", "Yokohama", 3, "Harbour & Chinatown: yangqin, erhu, gongs; B = matsuri festival drums and shamisen", "v2")
def yokohama(S, B):
    S.motif("gong", "gong_perc", 0, perc=True, gain=-12, every=2)
    if B:
        S.dbl("shamisen", ("riff", "bass"), 106, transpose=12, gain=-8, pan=-0.3)
        S.perc("matsuri", P["matsuri"], gain=-8)
        S.dbl("shinobue", "lead", 72, gain=-11, pan=0.2)
    else:
        S.dbl("yangqin", ("riff", "arp"), 15, transpose=12, gain=-10, pan=-0.3)
        S.dbl("erhu", "lead", 110, gain=-8, legato=True, pan=0.15)


@stage("Japan", "Kyoto", 4, "Ancient temples: koto melody, shakuhachi pad, temple bell; B = major lift, kotsuzumi, sho organ", "v3", "orchestral")
def kyoto(S, B):
    S.motif("temple_bell", "temple", 14, octave=3, gain=-10, rev=0.55)
    if B:
        S.swap("lead", 107, gain=-3, keep_under=-9)
        S.perc("kotsuzumi", P["kotsuzumi"], gain=-10)
        S.pad("sho", 20, 67, 84, n=4, gain=-17)
        S.dbl("shakuhachi2", "counter", 77, gain=-9, legato=True, pan=-0.3)
    else:
        S.dbl("koto", "lead", 107, gain=-8, pan=0.2)
        S.pad("shakuhachi_pad", 77, 60, 74, n=2, gain=-17)


# ---- Germany
@stage("Germany", "Munich", 1, "Oktoberfest: oom-pah tuba & brass, glockenspiel; B = major lift with accordion and clarinet", "v3")
def munich(S, B):
    S.oompah()
    S.dbl("glock", ("arp", "riff"), 9, gain=-16, pan=0.35)
    S.quiet("bass", -5)
    if B:
        S.dbl("accordion", "lead", 21, gain=-6, legato=True, pan=-0.15)
        S.dbl("clarinet", "counter", 71, gain=-9, legato=True, pan=0.3)


@stage("Germany", "Cologne", 2, "Cathedral organ + WDR electronic studio; B = Kraftwerk: electronic kit, vocoder lead, sequencer", "v2", "orchestral")
def cologne(S, B):
    S.pad("cathedral_organ", 19, 48, 72, n=4, gain=-14)
    if B:
        S.kit("electronic", gain=-1)
        S.dbl("vocoder", "lead", 54, gain=-8, legato=True)
        S.arpgen("sequencer", 80, step=0.25, pattern=(0, 2, 1, 3), low=60, high=76, gain=-13, pan=-0.3)
    else:
        S.dbl("seq_pluck", "arp", 80, gain=-14, pan=0.3)


@stage("Germany", "Black Forest", 3, "Cuckoo clocks & hunting horns, zither; B = horn quartet counter-melody, clock ticks, music box", "v1")
def black_forest(S, B):
    S.motif("cuckoo", "cuckoo", 79, octave=5, gain=-10, pan=0.35)
    S.dbl("horns", "lead", 60, transpose=-12, gain=-9, legato=True, pan=-0.2)
    if B:
        S.dbl("horn_counter", "counter", 60, gain=-8, legato=True, pan=0.25)
        S.perc("clock", P["clock"], gain=-15, pan=0.4)
        S.dbl("musicbox", ("riff", "lead"), 10, transpose=12, gain=-17, pan=-0.4)
    else:
        S.dbl("zither", ("riff", "bass"), 15, transpose=12, gain=-11, pan=0.3)


@stage("Germany", "Frankfurt", 4, "Techno capital: four-on-the-floor, supersaw stabs; B = trance/acid with electronic kit and sequencer", "v2", "synthwave")
def frankfurt(S, B):
    S.perc("four_floor", P["four_floor"], gain=-8, kit="808")
    S.comp("supersaw", 81, (0.5, 1.5, 2.5, 3.5), 60, 76, dur=0.25, vel=.75, gain=-13, pan=0.2, duck=0.5)
    if B:
        S.kit("electronic", gain=-3)
        S.arpgen("acid_seq", 87, step=0.25, pattern=(0, 0, 2, 1, 0, 3, 1, 2), low=48, high=64, gain=-12, pan=-0.2)
        S.pad("trance_pad", 90, 60, 79, gain=-15, duck=0.6)


# ---- Scandinavia
@stage("Scandinavia", "Stockholm", 1, "Swedish disco-pop: string section, offbeat piano, disco hats; B = full disco groove + octave bass", "v2", "synthwave")
def stockholm(S, B):
    S.comp("piano", 1, (0.5, 1.5, 2.5, 3.5), 55, 72, dur=0.25, vel=.65, gain=-12, pan=0.25)
    S.pad("strings", 48, 60, 79, gain=-14)
    if B:
        S.perc("disco", P["disco"], gain=-9)
        S.dbl("string_lead", "lead", 48, transpose=12, gain=-10, legato=True)
    else:
        S.perc("disco_hats", [(o + 0.5, 46, .55) for o in range(4)], gain=-15, pan=0.3)


@stage("Scandinavia", "Copenhagen", 2, "Andersen fairy tale & Tivoli: celesta, music box, pizzicato; B = major lift with calliope lead", "v3")
def copenhagen(S, B):
    S.dbl("celesta", "arp", 8, transpose=12, gain=-12, pan=-0.3)
    S.dbl("pizz", "bass", 45, transpose=12, gain=-12, pan=0.3)
    if B:
        S.dbl("calliope", "lead", 82, gain=-8, pan=0.1)
        S.dbl("glock_counter", "counter", 9, gain=-13, pan=-0.2)
    else:
        S.dbl("musicbox", "lead", 10, transpose=12, gain=-14, pan=0.2)


@stage("Scandinavia", "Helsinki", 3, "Kantele & Sibelius winter: cold strings, sleigh tambourine; B = horn counter, timpani, choir", "v1", "orchestral")
def helsinki(S, B):
    S.dbl("kantele", ("riff", "bass"), 46, transpose=12, gain=-10, pan=-0.3)
    S.pad("cold_strings", 49, 55, 79, gain=-14)
    if B:
        S.dbl("sibelius_horns", "counter", 60, gain=-8, legato=True, pan=0.25)
        S.pad("choir", 52, 60, 76, n=3, gain=-16, lead_only=True)
        S.motif("timpani", "lur", 47, octave=2, gain=-10)
    else:
        S.perc("sleigh", P["tambourine16"], gain=-17, pan=0.35)


@stage("Scandinavia", "Oslo", 4, "Hardanger fiddle, lur horn, aurora pad; B = major lift with Grieg pizzicato", "v3", "orchestral")
def oslo(S, B):
    S.motif("lur", "lur", 60, octave=3, gain=-10, rev=0.45, every=2)
    S.dbl("hardanger", "lead", 110, gain=-7, legato=True, pan=0.2)
    S.pad("aurora", 88, 60, 79, gain=-16)
    if B:
        S.dbl("grieg_pizz", ("arp", "bass"), 45, gain=-11, pan=-0.3)
        S.dbl("fiddle2", "counter", 40, gain=-10, legato=True, pan=-0.2)


# ---- France
@stage("France", "Paris", 1, "Musette accordion & jazz manouche 'la pompe'; B = reharm, accordion lead, upright bass, brushes, violin", "v1")
def paris(S, B):
    S.comp("manouche", 24, (1, 3), 52, 67, n=4, dur=0.2, vel=.8, gain=-11, pan=-0.3)
    if B:
        S.swap("lead", 21, gain=-3, legato=True, keep_under=-10)
        S.swap("bass", 32, gain=-3)
        S.kit("brush", gain=-3)
        S.drop("drums_snap")
        S.dbl("grappelli", "counter", 40, gain=-9, legato=True, pan=0.3)
    else:
        S.dbl("accordion", "lead", 21, gain=-7, legato=True, pan=0.15)


@stage("France", "Nice", 2, "Riviera: harp, vibraphone, flute; B = sunny major lift, bossa guitar and soprano sax", "v3", "synthwave")
def nice(S, B):
    S.dbl("harp", "arp", 46, gain=-10, pan=-0.3)
    S.comp("vibes", 11, (0.5, 2.5), 60, 76, dur=0.6, vel=.6, gain=-14, pan=0.3)
    if B:
        S.swap("lead", 64, gain=-3, legato=True, keep_under=-10)
        S.comp("bossa", 24, (0, 0.75, 1.5, 2.5, 3.25), 52, 67, dur=0.25, vel=.7, gain=-12, pan=-0.2)
        S.perc("shaker", P["shaker16"], gain=-16, pan=0.3)
    else:
        S.dbl("flute", "lead", 73, gain=-9, legato=True, pan=0.15)


@stage("France", "Bordeaux", 3, "Chateaux & baroque: harpsichord continuo, strings; B = baroque counterpoint, oboe", "v1", "orchestral")
def bordeaux(S, B):
    S.dbl("harpsichord", ("riff", "bass"), 6, transpose=12, gain=-10, pan=-0.3)
    S.pad("strings", 48, 55, 76, gain=-14)
    if B:
        S.dbl("oboe", "lead", 68, gain=-8, legato=True, pan=0.15)
        S.dbl("violin_counter", "counter", 40, gain=-9, legato=True, pan=0.3)


@stage("France", "Monaco", 4, "Grand-Prix glamour / French house: filtered strings, funk guitar; B = four-on-the-floor house + brass", "v2", "synthwave")
def monaco(S, B):
    S.comp("disco_strings", 48, (0.5, 1.5, 2.5, 3.5), 60, 79, dur=0.3, vel=.7, gain=-13, pan=-0.2, duck=0.5)
    S.comp("funk_gtr", 28, (0.25, 0.75, 1.75, 2.25, 2.75, 3.75), 55, 67, n=3, dur=0.12, vel=.7, gain=-14, pan=0.35)
    if B:
        S.perc("house", P["four_floor"], gain=-8, kit="808")
        S.comp("brass", 61, (0, 2.5), 55, 74, dur=0.3, vel=.8, gain=-12, pan=0.2)
    else:
        S.perc("tamb", P["tambourine8"], gain=-15, pan=-0.3)


# ---- Italy
@stage("Italy", "Pisa", 1, "Tuscan folk: mandolin tremolo, accordion; B = major lift, tarantella tambourine, clarinet", "v3")
def pisa(S, B):
    S.tremolo("mandolin", "lead", 25, step=0.125, transpose=12, gain=-12, pan=-0.25)
    S.pad("accordion_pad", 21, 55, 72, n=3, gain=-16)
    if B:
        S.perc("tarantella", P["tarantella"], gain=-10)
        S.dbl("clarinet", "counter", 71, gain=-9, legato=True, pan=0.3)


@stage("Italy", "Rome", 2, "Morricone spaghetti-western: whistle, twang guitar, choir; B = trumpet counter and timpani", "v1", "orchestral")
def rome(S, B):
    S.dbl("whistle", "lead", 78, gain=-7, legato=True, pan=0.15)
    S.dbl("twang", "arp", 27, gain=-11, pan=-0.3)
    S.pad("choir", 52, 55, 74, n=3, gain=-16, lead_only=True)
    if B:
        S.dbl("trumpet", "counter", 56, gain=-8, legato=True, pan=0.3)
        S.motif("timpani", "fanfare", 47, octave=2, gain=-10)


@stage("Italy", "Sicily", 3, "Tarantella: tambourine, accordion stabs, mandolin; B = re-groove with clarinet and jaw-harp twang", "v2")
def sicily(S, B):
    S.perc("tarantella", P["tarantella"], gain=-10)
    S.comp("accordion", 21, (0.5, 1.5, 2.5, 3.5), 55, 72, n=3, dur=0.3, vel=.7, gain=-13, pan=0.3)
    if B:
        S.dbl("clarinet", "lead", 71, gain=-7, legato=True, pan=-0.1)
        S.arpgen("marranzano", 105, step=0.5, pattern=(0, 0, 1, 0), low=48, high=60, gain=-15, pan=-0.3)
    else:
        S.tremolo("mandolin", "lead", 25, step=0.125, transpose=12, gain=-13, pan=-0.25)


@stage("Italy", "Florence", 4, "Renaissance: lute, recorder, string consort; B = harpsichord, viola consort, tabor drum", "v1", "orchestral")
def florence(S, B):
    S.dbl("lute", "arp", 24, gain=-10, pan=-0.3)
    S.dbl("recorder", "lead", 74, gain=-8, legato=True, pan=0.15)
    if B:
        S.dbl("harpsichord", "bass", 6, transpose=12, gain=-12, pan=0.3)
        S.dbl("viola", "counter", 41, gain=-9, legato=True, pan=-0.2)
        S.perc("tabor", P["bodhran"], gain=-12)
    else:
        S.pad("strings", 48, 55, 74, gain=-15)


# ---- UK
@stage("UK", "London", 1, "Big Ben Westminster chimes, Britpop jangle, Mellotron flute; B = re-groove with brass & tambourine", "v2")
def london(S, B):
    S.motif("big_ben", "westminster", 14, octave=4, gain=-9, rev=0.45, every=1)
    S.dbl("jangle", ("arp", "riff"), 25, gain=-11, pan=-0.35)
    S.pad("mellotron", 73, 60, 76, n=3, gain=-16)
    if B:
        S.comp("britpop_brass", 61, (0, 2.5), 55, 74, dur=0.4, vel=.8, gain=-12, pan=0.25)
        S.perc("tamb", P["tambourine8"], gain=-14, pan=0.35)


@stage("UK", "Sheffield", 2, "Steel City synth-pop (Human League era): synth brass, LinnDrum claps; B = saw lead, string machine", "v1", "synthwave")
def sheffield(S, B):
    S.comp("synth_brass", 62, (0, 1.5, 3), 55, 74, dur=0.35, vel=.75, gain=-12, pan=0.2)
    S.perc("linn", P["linn"], gain=-12, kit="electronic")
    if B:
        S.swap("lead", 81, gain=-4, keep_under=-9)
        S.pad("string_machine", 50, 60, 79, gain=-14)
        S.dbl("counter_synth", "counter", 80, gain=-11, pan=-0.3)


@stage("UK", "Loch Ness", 3, "Highland bagpipes with drone, pipe-band snare; B = major lift, fiddle, bodhran", "v3", "orchestral")
def loch_ness(S, B):
    S.drone(109, gain=-15, octave=3)
    if B:
        S.swap("lead", 109, gain=-3, keep_under=-11)
        S.dbl("fiddle", "counter", 110, gain=-8, legato=True, pan=0.3)
        S.perc("bodhran", P["bodhran"], gain=-10)
    else:
        S.dbl("bagpipe", "lead", 109, gain=-7, pan=0.1)
        S.perc("pipe_snare", P["pipe_snare"], gain=-15, pan=-0.2)


@stage("UK", "Stonehenge", 4, "Druid mystery: choir, celtic harp, tin whistle; B = major lift, harp lead, bodhran, deep drone", "v3", "orchestral")
def stonehenge(S, B):
    S.pad("choir", 52, 55, 76, n=3, gain=-14)
    S.dbl("harp", ("arp", "riff"), 46, gain=-10, pan=-0.3)
    if B:
        S.dbl("harp_lead", "lead", 46, gain=-6, pan=0.2)
        S.perc("bodhran", P["bodhran"], gain=-10)
        S.drone(89, gain=-17, octave=2)
        S.dbl("whistle2", "counter", 72, gain=-11, legato=True, pan=-0.2)
    else:
        S.dbl("tin_whistle", "lead", 72, gain=-9, legato=True, pan=0.15)


# ------------------------------------------------------------------ render
def slug(s):
    return s.lower().replace(" ", "_")


def stage_name(st, option):
    return f"{slug(st['country'])}__{slug(st['city'])}__{option}"


def render_stage(st, option, out, formats=("ogg",)):
    song = TUNE[st["tune"]]
    bold = option == "B"
    version = st["b_version"] if bold and st["b_version"] != "original" else None
    name = stage_name(st, option)
    dest = Path(out) / "stages" / slug(st["country"])

    def parts_fn(meta, notes, ch, parts):
        S = Stage(meta, notes, ch, parts)
        st["fn"](S, bold)
        return S.parts

    custom = {"name": name, "dest": dest, "parts_fn": parts_fn,
              "reverb_style": st["reverb"] if bold else "faithful", "bus_style": "faithful",
              "info": {"country": st["country"], "city": st["city"], "tune": song, "option": option,
                       "story": st["story"], "base": f"faithful{'' if not version else ' ' + version}"}}
    return R.render_song(song, "faithful", version, custom=custom, formats=formats)
