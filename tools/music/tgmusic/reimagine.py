"""Music-theory rearrangements of the original note data (the music itself changes, not only the sound).

v1  Reharmonized  - first statement of every phrase untouched; repeats get chord substitutions
                    (pytheory diatonic subs / bVI maj7 / relative chords, filtered by melody fit),
                    applied secondary dominants (V7/x) into chord changes, bass+arps re-voiced to
                    the new chords, chromatic walking approaches, and a counter-melody in 3rds/6ths
                    (contrary motion, no parallel 5ths/8ves, moves when the melody holds).
v2  Re-grooved    - new rhythm section by section (backbeat / four-on-the-floor / half-time /
                    breakbeat) with 16th fills into every section, octave-pump bass, regenerated
                    16th arpeggios from the harmony, and call-and-response answer phrases.
v3  Modal lift    - A: original | B: parallel mode (minor<->major colour) | C: whole-step
                    "truck-driver" key change prepared by the new key's V, with a twin lead in
                    diatonic thirds. The loop drops back to the original key.

The lead melody's pitches and rhythm are kept in every version (v3 transposes/recolours it),
so the tune stays identifiable (OC ReMix "source must be identifiable and dominant").
Theory helpers: kennethreitz/pytheory (MIT).
"""
import copy
import math
from collections import defaultdict

import numpy as np
import pytheory as pt

from . import arrange as A

NAMES = "C Db D Eb E F Gb G Ab A Bb B".split()
NAME_PC = {n: i for i, n in enumerate(NAMES)} | {"C#": 1, "D#": 3, "F#": 6, "G#": 8, "A#": 10}
QSYM = {"": "", "m": "m", "7": "7", "m7": "m7", "maj7": "maj7", "sus4": "sus4", "dim": "dim", "5": ""}
BLOCK = 16  # beats (4 bars)
PITCHED_FIXED = ("lead", "lead2", "drum", "fx")

VERSIONS = {
    "v1": "Reharmonized: substitutions + secondary dominants on repeats, walking bass, counter-melody",
    "v2": "Re-grooved: new drum grooves per section + fills, octave-pump bass, new 16th arps, answer phrases",
    "v3": "Modal lift: middle section in the parallel mode, final section up a whole step, twin lead",
}


# ------------------------------------------------------------------ basics
def key_of(meta):
    root, mode = meta["key"].split()
    return NAME_PC[root], mode


def scale_pcs(tonic, mode):
    steps = {"major": [0, 2, 4, 5, 7, 9, 11], "minor": [0, 2, 3, 5, 7, 8, 10],
             "dorian": [0, 2, 3, 5, 7, 9, 10], "lydian": [0, 2, 4, 6, 7, 9, 11]}[mode]
    return [(tonic + s) % 12 for s in steps]


def mk(beat, beats, note, vel, role, spb, **kw):
    n = {"t": beat * spb, "dur": beats * spb, "beat": beat, "beats": beats, "pitch": float(note), "note": int(note),
         "vel": float(vel), "pan": 0.0, "echo": False, "voice": -1, "inst": "gen", "role": role, "drum": None,
         "drum_pitch": None, "bends": []}
    n.update(kw)
    return n


def set_note(n, note, spb=None):
    d = note - n["note"]
    n["note"] = int(note)
    n["pitch"] = n["pitch"] + d
    return n


def nearest_with_pc(ref, pcs, lo=0, hi=127):
    best = None
    for p in range(max(lo, ref - 12), min(hi, ref + 12) + 1):
        if p % 12 in pcs and (best is None or abs(p - ref) < abs(best - ref)):
            best = p
    return ref if best is None else best


def chord_from_pcs(pcs, root):
    s = {(p - root) % 12 for p in pcs}
    for q, tpl in A.TEMPLATES.items():
        if set(tpl) == s:
            return q
    return None


def pt_chord(c):
    return pt.Chord.from_symbol(NAMES[c["root"]] + QSYM[c["q"]])


def from_pt(ch):
    tones = ch.tones
    root = tones[0].midi % 12 if tones[0].midi is not None else NAME_PC.get(tones[0].name, 0)
    pcs = sorted({t.midi % 12 for t in tones})
    q = chord_from_pcs(pcs, root)
    return None if q is None else {"root": root, "q": q, "pcs": [(root + i) % 12 for i in A.TEMPLATES[q]]}


def felt_lengths(lead, meta):
    """SNES notes are staccato; the felt length is onset-to-next-onset."""
    out = []
    for i, n in enumerate(lead):
        nxt = lead[i + 1]["beat"] if i + 1 < len(lead) else meta["pass_beats"]
        out.append(dict(n, ioi=max(n["beats"], nxt - n["beat"])))
    return out


def blocks(meta):
    return int(math.ceil(meta["pass_beats"] / BLOCK))


def melody_weights(notes, nseg, seg=2.0):
    w = [defaultdict(float) for _ in range(nseg)]
    for n in A.by_role(notes, "lead", "lead2"):
        b0, b1 = n["beat"], n["beat"] + max(n["beats"], 0.1)
        for s in range(int(b0 // seg), min(nseg, int(b1 // seg) + 1)):
            ov = min(b1, (s + 1) * seg) - max(b0, s * seg)
            if ov > 0:
                strong = 1.6 if abs(n["beat"] - round(n["beat"])) < 0.05 and round(n["beat"]) % 2 == 0 else 1.0
                w[s][n["note"] % 12] += ov * strong
    return w


def fit(c, mw):
    """Melody/chord compatibility 0..1: chord tones full, available tensions half, avoid notes penalised."""
    tot = sum(mw.values())
    if tot <= 0:
        return 1.0
    pcs = set(c["pcs"])
    tens = {(c["root"] + 2) % 12}  # 9th is available on every chord type we use
    if c["q"] in ("m", "m7"):
        tens.add((c["root"] + 5) % 12)  # 11 on minor
    sc = 0.0
    for pc, w in mw.items():
        if pc in pcs:
            sc += w
        elif pc in tens:
            sc += 0.5 * w
        elif any((pc - p) % 12 == 1 for p in pcs):
            sc -= 0.6 * w  # b9 against a chord tone = avoid note
    return sc / tot


def repeat_blocks(meta, notes):
    """Blocks whose lead material already appeared earlier (these get the variation)."""
    nb = blocks(meta)
    sig = []
    for b in range(nb):
        s = {(round((n["beat"] - b * BLOCK) * 4) / 4, n["note"]) for n in A.by_role(notes, "lead")
             if b * BLOCK <= n["beat"] < (b + 1) * BLOCK}
        sig.append(s)
    rep = set()
    for b in range(1, nb):
        if not sig[b]:
            continue
        for a in range(b):
            if sig[a] and len(sig[a] & sig[b]) / len(sig[a] | sig[b]) >= 0.55:
                rep.add(b)
                break
    if nb <= 5:
        rep |= set(range(1, nb))
    elif len(rep) < max(1, nb // 4):
        rep |= {b for b in range(1, nb) if b % 2 == 1}
    return rep


# ------------------------------------------------------------------ v1 reharmonization
def reharmonize(meta, notes):
    tonic, mode = key_of(meta)
    spb = meta["spb"]
    ch = A.chords(meta, notes)
    nseg = len(ch)
    mw = melody_weights(notes, nseg)
    rep = repeat_blocks(meta, notes)
    seg_rep = [int(c["beat"] // BLOCK) in rep for c in ch]
    if blocks(meta) <= 2:  # tiny fanfare: keep the opening bar, vary the rest
        seg_rep = [c["beat"] >= 4 for c in ch]
    new = copy.deepcopy(ch)
    key_name = NAMES[tonic]
    log = defaultdict(int)
    # 1) substitutions per merged span
    spans = []
    for i, c in enumerate(ch):
        if spans and ch[spans[-1][0]]["root"] == c["root"] and ch[spans[-1][0]]["q"] == c["q"] and seg_rep[i] == seg_rep[spans[-1][0]]:
            spans[-1][1] = i + 1
        else:
            spans.append([i, i + 1])
    cache = {}
    for k, (i0, i1) in enumerate(spans):
        if not seg_rep[i0]:
            continue
        c = ch[i0]
        key = (c["root"], c["q"])
        if key not in cache:
            cands = []
            try:
                for s in pt.reharmonize(pt_chord(c), key_name, mode):
                    if s["technique"] in ("diatonic substitution",):
                        d = from_pt(s["chord"])
                        if d:
                            cands.append(("diatonic sub", d))
            except Exception:
                pass
            r = c["root"]
            if c["q"] in ("m", "m7"):
                cands += [("bVI maj7", {"root": (r + 8) % 12, "q": "maj7"}), ("i m7", {"root": r, "q": "m7"}),
                          ("III (relative)", {"root": (r + 3) % 12, "q": ""})]
            elif c["q"] in ("", "maj7", "7"):
                cands += [("vi (relative)", {"root": (r + 9) % 12, "q": "m7"}), ("I maj7", {"root": r, "q": "maj7"}),
                          ("sus4", {"root": r, "q": "sus4"})]
            for _, d in cands:
                d["pcs"] = [(d["root"] + i) % 12 for i in A.TEMPLATES[d["q"]]]
            cache[key] = cands
        span_w = defaultdict(float)
        for i in range(i0, i1):
            for pc, w in mw[i].items():
                span_w[pc] += w
        base = fit(c, span_w)
        scored = [(fit(d, span_w), name, d) for name, d in cache[key]]
        scored = [s for s in scored if s[0] >= max(0.55, base - 0.08)]
        if not scored:
            continue
        scored.sort(key=lambda s: -s[0])
        # rotate among the good options so the repeat does not always pick the same colour
        _, name, d = scored[k % min(2, len(scored))]
        for i in range(i0, i1):
            new[i].update(root=d["root"], q=d["q"], pcs=list(d["pcs"]), bass=d["root"])
        log[name] += 1
    # 2) applied dominants V7/x on the last half-bar before a change (span >= 4 beats)
    for k in range(len(spans) - 1):
        i0, i1 = spans[k]
        j = i1  # first seg of next span
        if not seg_rep[i1 - 1] or i1 - i0 < 2 or j >= nseg:
            continue
        tgt = new[j]
        if tgt["root"] == tonic and k % 2 == 0:
            continue
        dom = {"root": (tgt["root"] + 7) % 12, "q": "7"}
        dom["pcs"] = [(dom["root"] + i) % 12 for i in A.TEMPLATES["7"]]
        if fit(dom, mw[i1 - 1]) >= 0.5 and (dom["root"], "7") != (new[i1 - 1]["root"], new[i1 - 1]["q"]):
            new[i1 - 1].update(root=dom["root"], q="7", pcs=dom["pcs"], bass=dom["root"])
            log["V7/x approach"] += 1
    # 3) re-voice accompaniment to the new chords
    out = []
    for n in notes:
        m = dict(n)
        if n["role"] in PITCHED_FIXED or n["role"] == "pulse":
            out.append(m)
            continue
        s = min(nseg - 1, int(n["beat"] // 2))
        o, c = ch[s], new[s]
        if (o["root"], o["q"]) == (c["root"], c["q"]):
            out.append(m)
            continue
        pc = n["note"] % 12
        if n["role"] in ("bass", "riff"):
            if pc in (o["root"], o["bass"]):
                tgt = {c["root"]}
            elif pc in o["pcs"]:
                idx = o["pcs"].index(pc)
                tgt = {c["pcs"][min(idx, len(c["pcs"]) - 1)]}
            elif pc in scale_pcs(tonic, mode) and not any((pc - p) % 12 == 1 for p in c["pcs"]):
                tgt = {pc}
            else:
                tgt = set(c["pcs"])
        else:
            tgt = {pc} if pc in c["pcs"] else set(c["pcs"])
        set_note(m, nearest_with_pc(n["note"], tgt))
        out.append(m)
    # 4) walking approach into chord changes (bass)
    bass = sorted([n for n in out if n["role"] == "bass"], key=lambda n: n["beat"])
    adds = []
    for a, b in zip(bass, bass[1:]):
        s_b = min(nseg - 1, int(b["beat"] // 2))
        if not seg_rep[s_b] or a["beats"] < 1.4 or b["beat"] - (a["beat"] + a["beats"]) > 0.3:
            continue
        if (new[s_b]["root"], new[s_b]["q"]) == (new[min(nseg - 1, int(a["beat"] // 2))]["root"], new[min(nseg - 1, int(a["beat"] // 2))]["q"]):
            continue
        step = -1 if b["note"] > a["note"] else 1
        ap = b["note"] + step
        a["beats"] -= 0.5
        a["dur"] = a["beats"] * spb
        adds.append(mk(b["beat"] - 0.5, 0.45, ap, a["vel"] * 0.9, "bass", spb, pan=a["pan"]))
        log["walking approach"] += 1
    out += adds
    # 5) counter-melody (3rds/6ths below, contrary motion, no parallel perfects; moves when melody holds)
    lead = felt_lengths(sorted(A.by_role(notes, "lead"), key=lambda n: n["beat"]), meta)
    longish = [n for n in lead if n["ioi"] >= 0.9]
    if len(longish) < 0.15 * max(1, len(lead)):
        # busy running line: counter-melody becomes sustained notes on every half-bar (strong beats)
        src, seen = [], set()
        for n in lead:
            hb = int(n["beat"] // 2)
            if hb not in seen and abs(n["beat"] - hb * 2) < 0.3:
                seen.add(hb)
                src.append(dict(n, ioi=2.0))
    else:
        src = [n for n in lead if n["ioi"] >= 0.9 or (n["ioi"] >= 0.45 and abs(n["beat"] * 2 - round(n["beat"] * 2)) < 0.05)]
    cm, prev, last_lead = [], None, None
    for n in src:
        s = min(nseg - 1, int(n["beat"] // 2))
        if not seg_rep[s]:
            continue
        c = new[s]
        cands = [p for p in range(n["note"] - 10, n["note"] - 2) if p % 12 in c["pcs"]]
        if not cands:
            continue
        lead_dir = 0 if last_lead is None else np.sign(n["note"] - last_lead)
        last_lead = n["note"]

        def cost(p):
            iv = n["note"] - p
            k = 0.0 if iv in (3, 4, 8, 9) else 1.0
            if prev is not None:
                piv = prev[1] - prev[0]
                if (iv % 12) in (0, 7) and piv % 12 == iv % 12 and p != prev[0]:
                    k += 5  # parallel 5th/8ve
                mv = np.sign(p - prev[0])
                k += 0.4 if lead_dir != 0 and mv == lead_dir else 0.0  # prefer contrary/oblique
                k += 0.05 * abs(p - prev[0])
            return k
        p = min(cands, key=cost)
        L = min(n["ioi"], 4.0)
        if L >= 2.0 and n["ioi"] < 8:  # melody holds -> counter line moves (rhythmic complement)
            half = L / 2
            cm.append(mk(n["beat"], half - 0.05, p, 0.75, "counter", spb))
            p2 = nearest_with_pc(p + 2, c["pcs"], p - 5, n["note"] - 3)
            cm.append(mk(n["beat"] + half, half - 0.05, p2, 0.7, "counter", spb))
            prev = (p2, n["note"])
        else:
            cm.append(mk(n["beat"], L - 0.05, p, 0.75, "counter", spb))
            prev = (p, n["note"])
    out += cm
    log["counter-melody notes"] = len(cm)
    for i, c in enumerate(new):
        c["t"], c["dur"] = c["beat"] * spb, 2.0 * spb
    return out, new, dict(log)


# ------------------------------------------------------------------ v2 regroove
GROOVES = {  # per bar: (kind, beat offset, vel)
    "backbeat": [("kick", 0, 1), ("kick", 1.5, .8), ("kick", 2.5, .85), ("snare", 1, 1), ("snare", 3, 1)]
                + [("hat", b / 2, .7 if b % 2 == 0 else .5) for b in range(8)],
    "drive": [("kick", b, 1) for b in range(4)] + [("snare", 1, 1), ("snare", 3, 1)]
             + [("ohat", b + 0.5, .75) for b in range(4)] + [("hat", b, .45) for b in range(4)],
    "half": [("kick", 0, 1), ("kick", 1.75, .7), ("snare", 2, 1)] + [("hat", b / 2, .6) for b in range(8)],
    "break": [("kick", 0, 1), ("kick", 1.5, .8), ("kick", 2.75, .85), ("snare", 1, 1), ("snare", 3, 1),
              ("snare", 3.75, .35), ("snare", 2.25, .3)] + [("hat", b / 4, .65 if b % 2 == 0 else .4) for b in range(16)],
}
CYCLE = ["backbeat", "drive", "half", "break"]


def regroove(meta, notes):
    spb = meta["spb"]
    nb = blocks(meta)
    ch = A.chords(meta, notes)
    nseg = len(ch)
    ranges = A.active_ranges(meta, A.drums(notes, "kick") + A.drums(notes, "snare"), gap_beats=4)
    in_rng = lambda b: any(s - 0.01 <= b < e + 0.5 for s, e in ranges)
    dp = {k: (np.median([n["drum_pitch"] for n in A.drums(notes, k)]) if A.drums(notes, k) else 60)
          for k in ("kick", "snare", "hat", "tom")}
    feel = {b: CYCLE[b % 4] for b in range(nb)}
    log = defaultdict(int)
    out = [dict(n) for n in notes if not (n["role"] == "drum" and in_rng(n["beat"]))]
    # drums
    for b in range(nb):
        g = feel[b]
        for bar in range(4):
            b0 = b * BLOCK + bar * 4
            if b0 >= meta["pass_beats"] - 0.01 or not in_rng(b0 + 0.01):
                continue
            last = bar == 3
            for kind, off, v in GROOVES[g]:
                if last and off >= 2:
                    continue
                out.append(mk(b0 + off, 0.2, 60, v, "drum", spb, drum=kind, drum_pitch=dp.get(kind if kind != "ohat" else "hat", 60)))
            if last:  # 16th fill into the next section: snare, then toms descending, crescendo
                for i in range(8):
                    off = 2 + i * 0.25
                    kind = "snare" if i < 3 else "tom"
                    out.append(mk(b0 + off, 0.2, 60, 0.55 + 0.06 * i, "drum", spb, drum=kind,
                                  drum_pitch=int(70 - 2.2 * (i - 3)) if kind == "tom" else dp["snare"]))
                out.append(mk(b0 + 2, 0.2, 60, 0.9, "drum", spb, drum="kick", drum_pitch=dp["kick"]))
                log["fills"] += 1
        log[g] += 1
    # octave-pump bass in drive/break sections
    pumped = []
    for n in [n for n in out if n["role"] == "bass"]:
        g = feel[min(nb - 1, int(n["beat"] // BLOCK))]
        if g in ("drive", "break") and n["beats"] >= 0.9:
            out.remove(n)
            k, b = 0, n["beat"]
            while b < n["beat"] + n["beats"] - 0.2:
                pumped.append(mk(b, 0.42, n["note"] + (12 if k % 2 else 0), n["vel"] * (0.85 if k % 2 else 1), "bass", spb, pan=n["pan"]))
                b += 0.5
                k += 1
            log["pumped bass notes"] += 1
    out += pumped
    # regenerated arpeggios (16ths in drive/break, 8ths in half; backbeat keeps the original arp)
    arp_src = A.by_role(notes, "arp")
    centre = int(np.median([n["note"] for n in arp_src])) if arp_src else 67
    centre = int(np.clip(centre, 60, 76))
    new_arp = []
    for b in range(nb):
        g = feel[b]
        if g == "backbeat":
            continue
        step = 0.5 if g == "half" else 0.25
        out = [n for n in out if not (n["role"] == "arp" and b * BLOCK <= n["beat"] < (b + 1) * BLOCK)]
        beat = b * BLOCK
        i = 0
        pattern = [0, 1, 2, 3, 2, 1] if g == "drive" else [0, 2, 1, 3, 2, 1, 3, 2]
        while beat < min((b + 1) * BLOCK, meta["pass_beats"] - 0.01):
            c = ch[min(nseg - 1, int(beat // 2))]
            v = A.voicing(c, centre - 7, centre + 9, 4)
            if v:
                acc = 0.85 if abs(beat - round(beat)) < 1e-6 else 0.6
                new_arp.append(mk(beat, step * 0.8, v[pattern[i % len(pattern)] % len(v)], acc, "arp", spb))
            beat += step
            i += 1
        log["new arp sections"] += 1
    out += new_arp
    # call-and-response: answer every other lead phrase (last 3 notes, octave up, in the gap)
    lead = sorted(A.by_role(notes, "lead"), key=lambda n: n["beat"])
    phrase, k = [], 0
    for i, n in enumerate(lead):
        phrase.append(n)
        nxt = lead[i + 1]["beat"] if i + 1 < len(lead) else meta["pass_beats"]
        n = dict(n, beats=min(max(n["beats"], 0.5), 1.0))  # let the last melody note ring before answering
        gap = nxt - (n["beat"] + n["beats"])
        if gap >= 1.25:
            k += 1
            if k % 2 == 0 and len(phrase) >= 3:
                tail = phrase[-3:]
                span = tail[-1]["beat"] + tail[-1]["beats"] - tail[0]["beat"]
                start = n["beat"] + n["beats"] + 0.25
                sc = min(1.0, (gap - 0.5) / max(span, 0.1))
                for m in tail:
                    out.append(mk(start + (m["beat"] - tail[0]["beat"]) * sc, max(0.2, m["beats"] * sc - 0.03),
                                  m["note"] + 12, 0.7, "counter", spb))
                log["answer phrases"] += 1
            phrase = []
    return out, None, dict(log)


# ------------------------------------------------------------------ v3 modal lift
def modal_lift(meta, notes):
    tonic, mode = key_of(meta)
    spb = meta["spb"]
    nb = blocks(meta)
    pb = meta["pass_beats"]
    if nb >= 3:
        b1 = max(1, round(nb / 3)) * BLOCK
        b2 = max(b1 + BLOCK, round(2 * nb / 3) * BLOCK)
    else:
        b1, b2 = pb * 0.33, pb * 0.66
    b2 = min(b2, pb - 4)
    if mode == "minor":
        mmap = {3: 4, 8: 9, 10: 11}       # Aeolian -> parallel major (Picardy colour)
        bmode = "major"
    else:
        mmap = {4: 3, 9: 8, 11: 10}       # Ionian -> parallel minor (darker middle)
        bmode = "minor"
    lift = 2
    out = []
    log = {"A": f"beats 0-{b1:g} original", "B": f"beats {b1:g}-{b2:g} parallel {bmode}",
           "C": f"beats {b2:g}-{pb:g} up a whole step"}
    for n in notes:
        m = dict(n)
        if n["role"] == "drum":
            out.append(m)
            continue
        if b1 <= n["beat"] < b2:
            r = (n["note"] - tonic) % 12
            if r in mmap:
                set_note(m, n["note"] + (mmap[r] - r))
        elif n["beat"] >= b2:
            set_note(m, n["note"] + lift)
        out.append(m)
    # pivot: the bar before C is re-centred on the new key's dominant (bass to V of new key)
    vroot = (tonic + lift + 7) % 12
    for m in out:
        if m["role"] in ("bass", "riff") and b2 - 2 <= m["beat"] < b2:
            set_note(m, nearest_with_pc(m["note"], {vroot}))
    # twin lead in diatonic thirds below in section C
    sc = scale_pcs((tonic + lift) % 12, mode)
    twin = 0
    for n in [m for m in out if m["role"] == "lead" and m["beat"] >= b2]:
        pc = n["note"] % 12
        if pc in sc:
            i = sc.index(pc)
            tpc = sc[(i - 2) % 7]
            p = n["note"] - ((pc - tpc) % 12)
        else:
            p = n["note"] - 3
        out.append(mk(n["beat"], n["beats"], p, 0.7, "counter", spb, bends=n.get("bends", [])))
        twin += 1
    log["twin lead notes"] = twin
    return out, None, log


def transform(song, version, meta, notes):
    fn = {"v1": reharmonize, "v2": regroove, "v3": modal_lift}[version]
    out, ch, log = fn(meta, notes)
    out.sort(key=lambda n: n["t"])
    return out, ch, log
