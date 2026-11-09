"""Stage 'band' versions: each city's local band covers its race tune in a genre with personality.

Arranging rules:
  * energy arc with real drops (intro -> verses -> stripped break -> build -> peak -> falling outro)
  * roster enters/exits per section, the melody is handed between instruments (timbre rhythm)
  * genre groove: patterns, swing / 16th bounce, ghost notes, per-instrument push/pull, fills not
    always on the 4-bar grid, stop-bar + roll before the peak
  * the melody (the game's heart, read from the player's ROM) is kept note for note but
    *performed*: articulation, ornaments (grace, pipe cuts, mordents, trills, brass falls,
    tremolo), phrase dynamics, anticipations
  * call & response: a second player answers in the melody's gaps with genre licks
  * non-vocal hooks: a city motif, 3x, not evenly

Option C ("Band", race=False) -> <out>/stages-band/; option D ("Race", race=True): the same band
at racing speed with a racing drive under it -> <out>/stages-race/.
"""
from pathlib import Path

import numpy as np

from . import arrange as A
from . import realism as RL
from . import render as R
from .stages import M, tonic_of, slug, TUNE, KITS

# The defaults the owner's renders were made with: GM voices upgraded to real sampled
# instruments (realism.py); the game's accompaniment figure not reused as a hook (it made the
# renders read as 16-bit game music).
REAL = True
HOOKS = False
FOLDER = {"C": "stages-band", "D": "stages-race"}

# ------------------------------------------------------------------ instruments
def G(prog, bank=0):
    return {"type": "sf2", "program": prog, "bank": bank}


def S(patch):
    return {"type": "surge", "patch": patch}


DK = dict(k=36, rim=37, s=38, clap=39, t5=41, h=42, t4=43, p=44, t3=45, o=46, t2=48, c=49, t1=50, R=51,
          ch=52, bell=53, tamb=54, sp=55, cow=56, C=57, vib=58, bongo=60, lbongo=61, mconga=62, conga=63,
          lconga=64, timh=65, timl=66, agoh=67, agol=68, cab=69, mar=70, wh=71, whl=72, gui=73, clv=75,
          wbh=76, wbl=77, cui=78, cuo=79, tri=81, shk=82, jb=83, cas=85, surm=86, sur=87)
KIT_KEYS = {"k", "rim", "s", "clap", "t5", "h", "t4", "p", "t3", "o", "t2", "c", "t1", "R", "ch", "bell", "sp", "C"}
MEL_PERC = {"TK": (116, 45), "TKL": (116, 36)}  # melodic percussion (sf2 program, note)
VCH = {"X": 1.0, "x": .78, "o": .58, "g": .3}

SCALES = {"aeolian": [0, 2, 3, 5, 7, 8, 10], "harmonic": [0, 2, 3, 5, 7, 8, 11], "dorian": [0, 2, 3, 5, 7, 9, 10],
          "minpent": [0, 3, 5, 7, 10], "blues": [0, 3, 5, 6, 7, 10], "phrygdom": [0, 1, 4, 5, 7, 8, 10],
          "in": [0, 1, 5, 7, 8], "hira": [0, 2, 3, 7, 8], "chinese": [0, 3, 5, 7, 10]}


def pat(s):
    s = s.replace(" ", "")
    assert len(s) == 16, s
    return s


# ------------------------------------------------------------------ the band
class Band:
    def __init__(self, prof, race=False):
        self.prof = prof
        self.race = race
        song = TUNE[prof["tune"]]
        self.song = song
        self.meta, self.notes = A.load(song)
        self.PB = self.meta["pass_beats"]
        self.bpm = prof["bpm"]
        self.spb = 60.0 / self.bpm
        self.r8, self.r16 = prof.get("swing", .5), prof.get("swing16", .5)
        self.tonic = tonic_of(self.meta)
        self.key_pcs = [(self.tonic + i) % 12 for i in SCALES["aeolian"]]
        self.rng = np.random.default_rng(sum(map(ord, prof["city"])))
        self.ch = A.chords(self.meta, self.notes)
        lead = sorted(A.mono(A.by_role(self.notes, "lead")), key=lambda n: n["beat"])
        self.mel = [dict(b=n["beat"], d=n["beats"], note=n["note"], vel=n["vel"],
                          ioi=(lead[i + 1]["beat"] if i + 1 < len(lead) else self.PB) - n["beat"])
                     for i, n in enumerate(lead)]
        vmax = max(n["vel"] for n in self.mel)
        for n in self.mel:
            n["vel"] = 0.55 + 0.45 * n["vel"] / vmax
        self._sections()
        self.parts = []
        self.log = []

    # ---------------- form & energy
    def _sections(self):
        pts = [0] + [p for p in A.section_beats(self.meta, self.notes) if p > 0] + [self.PB]
        secs = []
        pts = [int(round(p / 4) * 4) for p in pts[:-1]] + [self.PB]
        for a, b in zip(pts, pts[1:]):
            if secs and b - a < 8:
                secs[-1][1] = b
            elif b > a:
                secs.append([a, b])
        if len(secs) > 1 and secs[-1][1] - secs[-1][0] < 8:
            secs[-2][1] = secs.pop()[1]
        # split very long sections (>48 beats) so the arc has room to move
        out = []
        for a, b in secs:
            while b - a > 48:
                out.append([a, a + 32])
                a += 32
            out.append([a, b])
        self.secs = out
        n = len(out)
        cov = [sum(min(x["ioi"], 4) for x in self.mel if a <= x["b"] < b) / (b - a) for a, b in out]
        lv = [2] * n
        lv[0] = 1
        cands = [i for i in range(2, n - 1) if cov[i] > 0.3] or list(range(1, n - 1)) or [n - 1]
        brk = min(cands, key=lambda i: abs(out[i][0] - 0.55 * self.PB))
        lv[brk] = 0
        sig = [tuple((round(x["b"] - a, 1), x["note"]) for x in self.mel if a <= x["b"] < b)[:12] for a, b in out]
        for i in range(1, brk):
            if sig[i] and any(sig[i] == sig[j] for j in range(i)):
                lv[i] = 3
        run = 0
        for i in range(brk + 1, n):
            run += 1
            lv[i] = 2 if run % 4 == 0 else 3
        if n - 1 > brk + 1:
            lv[-1] = 2
        for i in range(1, n):
            if i != brk and cov[i] < 0.05 and not any(a <= k["beat"] < b for k in A.drums(self.notes, "kick")
                                                       for a, b in [out[i]]):
                lv[i] = 1
        if self.race:
            lv = [2 if x == 1 else x for x in lv]   # no energy drop where the loop wraps
        self.lv, self.brk = lv, brk
        self.peak_start = out[brk + 1][0] if brk + 1 < n else self.PB
        self.lift_from = self.peak_start if self.prof.get("lift") else None
        self.stop_bars = {out[brk][1] - 4} if brk + 1 < n else set()

    def sec_of(self, b):
        for i, (a, e) in enumerate(self.secs):
            if a <= b < e:
                return i
        return len(self.secs) - 1

    def level(self, b):
        return self.lv[self.sec_of(b)]

    def chord(self, b):
        return self.ch[int(np.clip(b // 2, 0, len(self.ch) - 1))]

    def bars(self, levels, stop_ok=False):
        b = 0
        while b < self.PB:
            i = self.sec_of(b)
            if self.lv[i] in levels and (stop_ok or b not in self.stop_bars):
                yield b, i, self.lv[i]
            b += 4

    # ---------------- time
    def warp(self, b):
        k = np.floor(b)
        f = b - k
        h0 = np.floor(f / .5) * .5
        h = (f - h0) / .5
        h = h / .5 * self.r16 if h < .5 else self.r16 + (h - .5) / .5 * (1 - self.r16)
        f = h0 + h * .5
        f = f / .5 * self.r8 if f < .5 else self.r8 + (f - .5) / .5 * (1 - self.r8)
        return k + f

    def emit(self, name, spec, ns, gain=-8, pan=0.0, rev=0.15, push=0.0, jit=0.006, drums=False, delay=0.0,
             vel_lv=True, transpose_ok=True):
        """ns in beat domain: dict(b, d, note, vel). Converted to seconds with swing, push/pull, jitter."""
        out = []
        lvmul = {0: .74, 1: .82, 2: .92, 3: 1.0}
        for n in ns:
            if n["b"] < 0 or n["b"] >= self.PB:
                continue
            t0 = self.warp(n["b"]) * self.spb
            t1 = self.warp(n["b"] + max(n["d"], 0.02)) * self.spb
            t = max(0.0, t0 + push + self.rng.normal(0, jit))
            note = n["note"]
            if transpose_ok and not drums and self.lift_from is not None and n["b"] >= self.lift_from:
                note += self.prof["lift"]
            v = n["vel"] * (lvmul[self.level(n["b"])] if vel_lv and not drums else 1.0)
            v *= 1 + self.rng.normal(0, 0.04)
            out.append({"t": t, "dur": max(0.03, t1 - t0 - 0.004), "note": int(note), "vel": float(np.clip(v, .05, 1)),
                        "pan": 0.0, "bends": []})
        if out:
            mix = dict(gain=gain, pan=pan, rev=rev, delay=delay)
            if drums:
                mix["bus"] = "drums"
            if REAL:
                for nm, sp, ns2, mx in RL.upgrade(name, spec, out, mix):
                    self.parts.append(dict(name=nm, spec=sp, notes=ns2, **mx))
            else:
                self.parts.append(dict(name=name, spec=spec, notes=out, **mix))

    # ---------------- harmony helpers
    def near(self, pc, ref, lo=None, hi=None):
        c = [p for p in range(ref - 6, ref + 7) if p % 12 == pc % 12]
        p = min(c, key=lambda p: abs(p - ref))
        while lo is not None and p < lo:
            p += 12
        while hi is not None and p > hi:
            p -= 12
        return p

    def ext_pcs(self, c, ext):
        pcs = list(c["pcs"])
        if ext in ("jazz", "soul"):
            r = c["root"]
            minor = (r + 3) % 12 in pcs
            if (r - self.tonic) % 12 == 7 and not minor:
                pcs.append((r + 10) % 12)          # V7
            elif minor:
                pcs.append((r + 10) % 12)          # m7
            elif (r + 11) % 12 in self.key_pcs:
                pcs.append((r + 11) % 12)          # maj7
            else:
                pcs.append((r + 9) % 12)           # 6
            if ext == "soul" or minor:
                if (r + 2) % 12 in self.key_pcs or ext == "soul":
                    pcs.append((r + 2) % 12)       # 9
        return list(dict.fromkeys(pcs))

    def voicing(self, c, low, high, n, ext=None):
        return A.voicing({"pcs": self.ext_pcs(c, ext)}, low, high, n)

    def step(self, note, k, scale):
        pcs = sorted((self.tonic + i) % 12 for i in SCALES[scale])
        cand = [p for p in range(note - 24, note + 25) if p % 12 in pcs]
        base = min(cand, key=lambda p: abs(p - note))
        i = cand.index(base) + k
        return cand[int(np.clip(i, 0, len(cand) - 1))]

    # ================================================================ players
    def drums(self, groove, kit="standard", gain=-4, perc_gain=-10, fill="toms", roll="s", crash=True, rev=0.08):
        kitn, perc, mel = [], [], {k: [] for k in MEL_PERC}
        fill_bars = set()
        for i, (a, e) in enumerate(self.secs):
            last = e - 4
            if self.lv[i] >= 1 and last not in self.stop_bars:
                fill_bars.add(last)
            for b in range(int(a) + 12, int(e) - 4, 16):
                if self.rng.random() < 0.3:
                    fill_bars.add(b + 4 * int(self.rng.integers(0, 2)))   # off-grid extra fills
        for b, i, lv in self.bars({0, 1, 2, 3}, stop_ok=True):
            if b in self.stop_bars:
                # stop bar: roll crescendo into the peak
                for j in range(16 if lv == 0 else 8):
                    st = j * (0.25 if lv == 0 else 0.5)
                    v = 0.3 + 0.7 * j / 15
                    (mel[roll] if roll in MEL_PERC else kitn).append(dict(b=b + st, d=.2, note=DK.get(roll, 0), vel=v, key=roll))
                continue
            g = groove.get(lv)
            if not g:
                continue
            g = g[i % len(g)] if isinstance(g, list) else g
            fb = b in fill_bars and lv >= 1
            fill_from = 2 if (fb and self.lv[min(i + 1, len(self.lv) - 1)] == 3) else 3
            for key, s in g.items():
                s = pat(s)
                for j, ch in enumerate(s):
                    if ch == "." or (fb and j >= fill_from * 4 and key not in ("k", "p", "sur", "surm")):
                        continue
                    n = dict(b=b + j / 4, d=0.2, note=DK.get(key, 0), vel=VCH[ch], key=key)
                    (mel[key] if key in MEL_PERC else kitn if key in KIT_KEYS else perc).append(n)
            if fb and fill != "none":
                seq = {"toms": ["t1", "t1", "t2", "t2", "t3", "t3", "t4", "t5"], "snare": ["s"] * 8,
                       "latin": ["timh", "timh", "timl", "timh", "timl", "timl", "timh", "timl"],
                       "taiko": ["TK"] * 6 + ["TKL"] * 2}[fill]
                steps = (4 - fill_from) * 4
                for j in range(steps):
                    key = seq[int(j / steps * len(seq))]
                    n = dict(b=b + fill_from + j / 4, d=.2, note=DK.get(key, 0), vel=.5 + .45 * j / steps, key=key)
                    (mel[key] if key in MEL_PERC else perc if key not in KIT_KEYS else kitn).append(n)
            if crash and lv >= 2 and b == self.secs[i][0]:
                kitn.append(dict(b=b, d=1, note=DK["c"], vel=.9, key="c"))
                kitn.append(dict(b=b, d=.2, note=DK["k"], vel=1, key="k"))
        spec = G(KITS[kit], 128)
        self.kit = kit
        self.emit("drums", spec, kitn, gain=gain, rev=rev, drums=True, jit=0.003)
        self.emit("perc", spec, perc, gain=perc_gain, pan=0.25, rev=rev + .05, drums=True, jit=0.006)
        for k, (prog, note) in MEL_PERC.items():
            self.emit("taiko" + k.lower(), G(prog), [dict(n, note=note) for n in mel[k]], gain=gain - 1,
                      rev=rev + .1, drums=True, jit=0.004)

    def bass(self, spec, pats, low=31, gain=-4, name="bass", push=-0.006, rev=0.04, art=0.9):
        ns = []
        prev = low + 7
        for b, i, lv in self.bars(set(pats)):
            p = pats[lv]
            p = p[i % len(p)] if isinstance(p[0], list) else p
            if p == "walk":
                p = [(0, 1, "R", .9), (1, 1, "W", .7), (2, 1, "W", .8), (3, 1, "A", .7)]
            for off, dur, deg, v in p:
                c = self.chord(b + off)
                nxt = self.chord(b + 4)
                r = c["root"]
                if deg == "R":
                    pc = r
                elif deg == "5":
                    pc = r + 7
                elif deg == "3":
                    pc = c["pcs"][1]
                elif deg == "7":
                    pc = r + 10
                elif deg == "8":
                    pc = r
                elif deg == "A":
                    pc = nxt["root"] - 1 if self.rng.random() < .6 else nxt["root"] + 1
                else:  # W: walking chord/scale tone
                    pc = self.rng.choice(c["pcs"] + [r + 2 if (r + 2) % 12 in self.key_pcs else r + 7])
                note = self.near(pc, prev, low, low + 14)
                if deg == "8":
                    note = self.near(r, prev, low, low + 11) + 12
                ns.append(dict(b=b + off, d=dur * art, note=note, vel=v))
                if deg != "8":
                    prev = note
        self.emit(name, spec, ns, gain=gain, push=push, rev=rev)

    def comp(self, name, spec, pats, low=55, high=72, n=4, ext=None, gain=-9, pan=0.0, rev=0.15,
             push=0.008, ant=False, delay=0.0, oct=0):
        ns = []
        strum = 0
        for b, i, lv in self.bars(set(pats)):
            p = pats[lv]
            p = p[i % len(p)] if isinstance(p[0], list) else p
            for off, dur, v, kind in p:
                at = b + off
                bar_n = int(b // 4)
                if ant and off >= 3.5 and (ant != "even" or bar_n % 2 == 1):
                    c = self.chord(b + 4)
                else:
                    c = self.chord(at)
                vo = [x + oct for x in self.voicing(c, low, high, n, ext)]
                if kind == "L":
                    vo = [self.near(c["root"], low - 5, low - 12)]
                elif kind == "U":
                    vo = vo[-3:]
                if kind in ("C", "L", "U"):
                    for x in vo:
                        ns.append(dict(b=at, d=dur, note=x, vel=v))
                elif kind == "S":
                    strum += 1
                    order = vo if strum % 2 else vo[::-1]
                    for k, x in enumerate(order):
                        ns.append(dict(b=at + k * 0.014 / self.spb, d=dur, note=x, vel=v * (1 if k in (0, len(vo) - 1) else .85)))
                elif kind in ("A", "alb"):
                    seq = vo if kind == "A" else [vo[0], vo[-1], vo[len(vo) // 2], vo[-1]]
                    k, t = 0, 0.0
                    while t < dur - 1e-6:
                        ns.append(dict(b=at + t, d=0.24, note=seq[k % len(seq)], vel=v * (1 if k % 4 == 0 else .8)))
                        k += 1
                        t += 0.25
                elif kind == "T":
                    t = 0.0
                    while t < dur - 1e-6:
                        for x in vo:
                            ns.append(dict(b=at + t, d=0.12, note=x, vel=v * (.9 + .1 * (t % 1 == 0))))
                        t += 0.25
        self.emit(name, spec, ns, gain=gain, pan=pan, rev=rev, push=push, jit=0.008, delay=delay)

    def pad(self, name, spec, levels, low=55, high=74, n=4, ext=None, gain=-14, pan=0.0, rev=0.3, vel=.6):
        ns = []
        for c in A.merge_chords(self.ch):
            b0, b1 = c["beat"], c["beat"] + c["dur"] / self.meta["spb"]
            b = b0
            while b < b1:
                e = min(b1, self.secs[self.sec_of(b)][1])
                if e <= b:
                    e = b1
                if self.level(b) in levels and not any(s <= b < s + 4 for s in self.stop_bars):
                    for x in self.voicing(c, low, high, n, ext):
                        ns.append(dict(b=b - 0.05, d=e - b, note=x, vel=vel))
                b = e
        self.emit(name, spec, ns, gain=gain, pan=pan, rev=rev, jit=0.0)

    def drone(self, name, spec, levels, intervals=(0, 7), octave=3, gain=-15, rev=0.2):
        ns = []
        for b, i, lv in self.bars(levels, stop_ok=True):
            for iv in intervals:
                ns.append(dict(b=b, d=4.02, note=12 * (octave + 1) + self.tonic + iv, vel=.55))
        self.emit(name, spec, ns, gain=gain, rev=rev, jit=0.0, transpose_ok=True)

    # ---------------- the melody, performed
    def _perform(self, ns, art="norm", orn=(), oct=0, harm=None, scale="aeolian", lo=None, hi=None, push=False):
        g = 0.055 / self.spb                       # grace length in beats (~55 ms)
        out = []
        for i, n in enumerate(ns):
            b, ioi = n["b"], n["ioi"]
            if push and b % 4 == 0 and i > 0 and ns[i - 1]["b"] + ns[i - 1]["ioi"] <= b and ns[i - 1]["ioi"] >= 1:
                b, ioi = b - 0.5, ioi + 0.5                                  # anticipation
            d = {"legato": min(ioi - 0.04, 4), "norm": min(ioi * 0.8, 1.6), "stacc": min(ioi * 0.5, 0.35)}[art]
            d = max(d, 0.08)
            note = n["note"] + oct
            if harm:
                note = self.step(note, harm, scale)
            while lo is not None and note < lo:
                note += 12
            while hi is not None and note > hi:
                note -= 12
            v = n["vel"]
            r = self.rng.random()
            longn = ioi >= 0.75
            if "trem" in orn and longn:
                st = max(0.25, 0.08 / self.spb)
                t = 0.0
                while t < d - 1e-6:
                    out.append(dict(b=b + t, d=st * .9, note=note, vel=v * (1 if t == 0 else .8)))
                    t += st
                continue
            if "trill" in orn and ioi >= 1.5 and r < .45:
                up = self.step(note, 1, scale)
                st = max(0.125, 0.07 / self.spb)
                t, k = 0.0, 0
                while t < d * 0.8:
                    out.append(dict(b=b + t, d=st * .9, note=note if k % 2 == 0 else up, vel=v * .9))
                    t += st
                    k += 1
                out.append(dict(b=b + t, d=max(d - t, .05), note=note, vel=v))
                continue
            if "mordent" in orn and ioi >= 1 and r < .35:
                up = self.step(note, 1, scale)
                out += [dict(b=b, d=g, note=note, vel=v), dict(b=b + g, d=g, note=up, vel=v * .85)]
                out.append(dict(b=b + 2 * g, d=max(d - 2 * g, .05), note=note, vel=v))
                continue
            if "cut" in orn and ioi >= 0.5 and r < .55:
                out.append(dict(b=b - g, d=g * .8, note=self.step(note, 2 if r < .3 else 1, scale), vel=v * .7))
            elif "grace" in orn and longn and r < .35:
                out.append(dict(b=b - g, d=g * .9, note=note - 1 if (note - 1) % 12 not in self.key_pcs else note - 2, vel=v * .65))
            elif "slide" in orn and longn and r < .3:
                out.append(dict(b=b - 1.6 * g, d=g * .8, note=note - 2, vel=v * .55))
                out.append(dict(b=b - 0.8 * g, d=g * .8, note=note - 1, vel=v * .65))
            if "fall" in orn and ioi >= 2 and r > .55:
                out.append(dict(b=b, d=d * .6, note=note, vel=v))
                for k, dn in enumerate((1, 2, 4)):
                    out.append(dict(b=b + d * .6 + k * 0.1, d=0.09, note=note - dn, vel=v * (.6 - .12 * k)))
                continue
            out.append(dict(b=b, d=d, note=note, vel=v))
        # phrase dynamics: swell toward the phrase's highest note, relax after
        ph, cur = [], []
        for n in out:
            if cur and n["b"] - (cur[-1]["b"] + cur[-1]["d"]) > 1.0:
                ph.append(cur)
                cur = []
            cur.append(n)
        ph.append(cur)
        for p in ph:
            if len(p) < 3:
                continue
            top = max(range(len(p)), key=lambda k: p[k]["note"])
            for k, n in enumerate(p):
                f = k / max(top, 1) if k <= top else 1 - 0.35 * (k - top) / max(len(p) - top, 1)
                n["vel"] *= 0.82 + 0.22 * f
        return out

    def lead(self, voices, brk=None, peak=None, scale="aeolian", push=False):
        """voices: list of 'voice' (list of layers); rotated per section. A layer is a dict
        (spec, gain, oct, art, orn, harm, pan, rev, delay, name, lo, hi)."""
        by_layer = {}
        k = 0
        for i, (a, e) in enumerate(self.secs):
            lv = self.lv[i]
            if lv == 0 and brk:
                voice, tag = brk, "brk"
            elif lv == 3 and peak:
                voice, tag = peak, "peak"
            else:
                voice, tag = voices[k % len(voices)], f"v{k % len(voices)}"
                k += 1 if lv else 0
            ns = [n for n in self.mel if a <= n["b"] < e]
            for li, L in enumerate(voice):
                if L.get("min_lv", 0) > lv:
                    continue
                key = (tag, li)
                if key not in by_layer:
                    by_layer[key] = (L, [])
                by_layer[key][1].extend(self._perform(ns, L.get("art", "norm"), L.get("orn", ()), L.get("oct", 0),
                                                      L.get("harm"), scale, L.get("lo"), L.get("hi"),
                                                      push and L.get("push", True)))
        for (tag, li), (L, ns) in by_layer.items():
            nm = L.get("name", f"lead_{tag}_{li}")
            self.emit(nm, L["spec"], ns, gain=L.get("gain", -1), pan=L.get("pan", 0), rev=L.get("rev", .18),
                      delay=L.get("delay", 0), jit=0.007)
            self.log.append(f"{nm}:{tag}")

    def answer(self, name, spec, scale, cells=None, levels=(2, 3), gain=-9, pan=-0.3, lo=60, hi=86, prob=0.6,
               rev=0.2, delay=0.0, orn=()):
        """Call & response: licks in the melody's gaps, ending on a chord tone."""
        cells = cells or [[(0, .5), (.5, .5), (1, 1)], [(0, .25), (.25, .25), (.5, .5), (1, .5), (1.5, .5)],
                          [(0, .75), (.75, .25), (1, 1)], [(0, .33), (.33, .33), (.67, .33), (1, 1)]]
        ns = []
        for i, n in enumerate(self.mel):
            if n["ioi"] < 2.0 or self.level(n["b"]) not in levels or self.rng.random() > prob:
                continue
            start = n["b"] + max(0.5, np.ceil(min(n["d"], 1.0) * 2) / 2)
            end = n["b"] + n["ioi"] - 0.25
            if end - start < 1.0 or any(s <= start < s + 4 for s in self.stop_bars):
                continue
            cell = cells[int(self.rng.integers(len(cells)))]
            span = cell[-1][0] + cell[-1][1]
            t0 = max(start, end - span) if span < end - start else start
            p = int(np.clip(n["note"] + self.rng.choice([-5, -3, 3, 5]), lo + 5, hi - 5))
            dirn = self.rng.choice([-1, 1])
            for j, (o, d) in enumerate(cell):
                b = t0 + o
                if b + d > end + 0.25:
                    break
                if j == len(cell) - 1:
                    c = self.chord(b)
                    p = self.near(min(c["pcs"], key=lambda pc: abs(self.near(pc, p) - p)), p)
                else:
                    p = self.step(p, int(dirn * self.rng.choice([1, 1, 2])), scale)
                if p < lo or p > hi:
                    dirn = -dirn
                    p = self.step(p, 2 * dirn, scale)
                ns.append(dict(b=b, d=d * .9, note=int(p), vel=.65 + .25 * (j == 0) + .1 * self.rng.random(), ioi=d))
        if orn:
            ns = self._perform([dict(n, ioi=n["ioi"]) for n in ns], "norm", orn)
        self.emit(name, spec, ns, gain=gain, pan=pan, rev=rev, jit=0.01, delay=delay)

    def hook(self, name, spec, levels, roles=("arp", "riff", "arp2"), oct=0, gain=-9, pan=0.2, rev=0.15,
             art=None, delay=0.0, thin=False, vel_min=.5):
        """The original accompaniment riff (identity marker) on a signature instrument.
        Off by default: ablation showed this SNES arp figure is what makes renders read as '16-bit game music'."""
        if not HOOKS:
            return
        src = []
        for r in roles:
            src = A.by_role(self.notes, r)
            if src:
                break
        if not src:
            return
        vmax = max(n["vel"] for n in src)
        ns = []
        for n in src:
            if self.level(n["beat"]) not in levels or any(s <= n["beat"] < s + 4 for s in self.stop_bars):
                continue
            if thin and (n["beat"] * 2) % 1 > 1e-3:
                continue
            d = n["beats"] if not art else (min(n["beats"] * 2, 1.0) if art == "long" else min(n["beats"], .2))
            ns.append(dict(b=n["beat"], d=d, note=n["note"] + oct, vel=vel_min + (1 - vel_min) * n["vel"] / vmax))
        self.emit(name, spec, ns, gain=gain, pan=pan, rev=rev, delay=delay, jit=0.004)

    def motif(self, name, spec, motif, octave=5, gain=-8, pan=0.0, rev=0.35, perc=False, where=None):
        """City signature motif: intro, first peak, last section (3 appearances, uneven spacing)."""
        where = where or sorted({0, self.peak_start, self.secs[-1][0]})
        ns = []
        for w in where:
            for o, semi, d, v in M[motif]:
                note = semi if perc else 12 * (octave + 1) + self.tonic + semi
                ns.append(dict(b=w + o, d=d, note=note, vel=v))
        self.emit(name, spec, ns, gain=gain, pan=pan, rev=rev, drums=perc, jit=0.0, vel_lv=False)

    def fx(self, name, spec, note, levels=(1, 2, 3), every=8, gain=-16, pan=0.4, rev=0.4):
        ns = []
        for b, i, lv in self.bars(set(levels)):
            if (b // 4) % (every // 4 or 1) == 0 and self.rng.random() < .7:
                ns.append(dict(b=b + float(self.rng.choice([0.5, 1.5, 2.5])), d=1.5, note=note + int(self.rng.integers(-2, 3)), vel=.6))
        self.emit(name, spec, ns, gain=gain, pan=pan, rev=rev, jit=0.02)

    def riser(self, gain=-14):
        """Reverse cymbal into each peak entrance (every energy lift in race mode)."""
        ns = []
        for i in range(1, len(self.secs)):
            if (self.lv[i] == 3 and self.lv[i - 1] < 3) or (self.race and self.lv[i] > self.lv[i - 1]):
                s = self.secs[i][0]
                ns.append(dict(b=s - 1.7 / self.spb, d=1.7 / self.spb, note=60, vel=.8))
        self.emit("riser", G(119), ns, gain=gain, rev=0.3, jit=0.0, vel_lv=False)


    def race_drive(self):
        """Racing engine: 16th hats, motorik kick, pulsing octave bass and spiccato string gallop
        layered under the band so every city keeps its genre but always feels like it's at speed."""
        hats, kick, pulse, gal, gal_hi = [], [], [], [], []
        prev = 43
        for b, i, lv in self.bars({1, 2, 3}):
            for j in range(16):
                if lv == 1 and j % 2:
                    continue
                v = .75 if j % 4 == 2 else .5 if j % 2 == 0 else .3
                hats.append(dict(b=b + j / 4, d=.1, note=DK["o" if (lv == 3 and j % 4 == 2) else "h"], vel=v))
            if lv >= 2:
                for q in (range(4) if lv == 3 else (0, 2)):
                    kick.append(dict(b=b + q, d=.2, note=DK["k"], vel=.85))
            if lv < 2:
                continue
            for q in range(4):
                c = self.chord(b + q)
                r = self.near(c["root"], prev, 40, 52)
                prev = r
                steps = (0, .5) if lv == 2 else (0, .25, .5, .75)
                for k, st in enumerate(steps):
                    pulse.append(dict(b=b + q + st, d=.22 if lv == 3 else .4, note=r + (12 if k % 2 else 0) * (lv == 2),
                                      vel=.85 if k == 0 else .65))
                cel = self.near(c["root"], 50, 43, 55)
                for st, v in ((0, .9), (.5, .6), (.75, .7)):        # gallop: 8th + two 16ths
                    gal.append(dict(b=b + q + st, d=.2, note=cel, vel=v))
                if lv == 3:
                    hi = self.near(c["pcs"][2] if len(c["pcs"]) > 2 else c["root"] + 7, 74, 67, 81)
                    for st, v in ((0, .85), (.5, .55), (.75, .65)):
                        gal_hi.append(dict(b=b + q + st, d=.2, note=hi, vel=v))
        kit = G(KITS[getattr(self, "kit", "standard")], 128)
        self.emit("drive", kit, hats + kick, gain=-9, pan=0.0, rev=0.06, drums=True, jit=0.002)
        self.emit("pulse_bass", S("Basses/Rubber Bass.fxp"), pulse, gain=-12, push=-0.004, rev=0.02, jit=0.002)
        self.emit("gallop_lo", G(42), gal, gain=-12, pan=-0.4, rev=0.12, jit=0.004)
        self.emit("gallop_hi", G(48), gal_hi, gain=-14, pan=0.4, rev=0.15, jit=0.004)


def race_prof(prof):
    """Racing tempo: 140-176 BPM, swing mostly straightened."""
    p = dict(prof)
    bpm = prof["bpm"]
    p["bpm"] = int(round(min(176, max(140, bpm * (1.15 if bpm < 150 else 1.05)))))
    for k in ("swing", "swing16"):
        if k in p:
            p[k] = .5 + (p[k] - .5) * .3
    return p


def L(spec, gain=-1, **kw):
    """A melody layer."""
    return dict(spec=spec, gain=gain, **kw)


# ------------------------------------------------------------------ render
def band_name(prof, option):
    return f"{slug(prof['country'])}__{slug(prof['city'])}__{option}"


def render_band(prof, option, out, formats=("ogg",)):
    """option "C" (band) or "D" (race): <out>/stages-band|stages-race/<country>/<country>__<city>__<option>.ogg"""
    race = option == "D"
    if race:
        prof = race_prof(prof)
    song = TUNE[prof["tune"]]
    name = band_name(prof, option)
    dest = Path(out) / FOLDER[option] / slug(prof["country"])
    info = {"country": prof["country"], "city": prof["city"], "tune": song, "option": option, "bpm": prof["bpm"],
            "band": prof["band"], "genre": prof["genre"], "story": prof["story"], "base": "faithful melody"}

    def notes_fn(meta, notes):
        meta["spb"] = 60.0 / prof["bpm"]
        meta["bpm"] = prof["bpm"]
        meta["pass_s"] = meta["pass_beats"] * meta["spb"]
        return notes

    def parts_fn(meta, notes, ch, parts):
        B = Band(prof, race)
        prof["fn"](B)
        if race:
            B.race_drive()
        if prof.get("riser", True):
            B.riser()
        info["form"] = [f"{a}-{e}:{['break', 'light', 'main', 'peak'][l]}" for (a, e), l in zip(B.secs, B.lv)]
        info["melody_handoff"] = B.log
        return B.parts

    custom = {"name": name, "dest": dest, "parts_fn": parts_fn, "notes_fn": notes_fn,
              "reverb_style": prof.get("reverb", "rock"), "bus_style": "faithful", "info": info}
    return R.render_song(song, "faithful", None, custom=custom, formats=formats)
