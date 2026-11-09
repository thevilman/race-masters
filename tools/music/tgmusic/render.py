"""Render one song in one style: arrangement -> stems -> mix -> master -> loop-ready files.

render_song(song, style, dest=...) writes <dest>/<song>__<style>.ogg (LOOPSTART / LOOPLENGTH
in samples, as src/audio/music.c reads them) and a .json with what was rendered.
"""
import hashlib
import json
import math
import os
import subprocess
import time
from pathlib import Path

import numpy as np
import pedalboard as pb
import pyloudnorm
import soundfile as sf

from . import arrange as A
from . import config
from . import instruments as I
from . import reimagine as RI
from . import synth
from .pitch import yin_f0

SR = config.SR
DRAGON = config.lib("dragonfly-reverb")
SSO = I.SSO
TAIL_S = 6.0
TARGET_LUFS = -14.0
CEIL_DBTP = -1.0


# ------------------------------------------------------------------ helpers
def db(x):
    return 10 ** (x / 20)


def pan_st(x, pan, out=None):
    """Constant-power pan of a stereo signal (balance); into `out` if given (may be x)."""
    a = (pan + 1) * math.pi / 4
    if out is None:
        return x * np.array([math.cos(a), math.sin(a)], np.float32) * 1.414
    np.multiply(x, np.array([math.cos(a), math.sin(a)], np.float32), out=out)
    out *= 1.414
    return out


def humanize(ns, ms=6.0, vel=0.06, seed=1):
    rng = np.random.default_rng(seed)
    out = []
    for n in ns:
        m = dict(n)
        m["t"] = max(0.0, n["t"] + rng.normal(0, ms / 1000))
        m["vel"] = float(np.clip(n["vel"] * (1 + rng.normal(0, vel)), 0.05, 1.0))
        out.append(m)
    return out


def place(buf, x, at):
    i = int(at * SR)
    if i >= len(buf):
        return
    j = min(len(buf), i + len(x))
    buf[i:j] += x[: j - i]


def plugin(name):
    return I.load_vst3(DRAGON / f"{name}.vst3")


_SFZ = {}


def sfz(path):
    key = str(path)
    if key not in _SFZ:
        _SFZ[key] = I.SfzInstrument(path)
    return _SFZ[key]


def sso(rel):
    return SSO / rel


# ------------------------------------------------------------------ part renderers
def _surge_part(spec, ns):
    """The Surge XT patch of a part, its parameters and the notes it is sent."""
    params = {"a_pitch_bend_up_range": "12 semitones", "a_pitch_bend_down_range": "12 semitones",
              "b_pitch_bend_up_range": "12 semitones", "b_pitch_bend_down_range": "12 semitones"}
    params.update(spec.get("params", {}))
    # Surge (via pedalboard) drops note-ons at exactly t=0
    ns = [dict(n, t=max(n["t"], 0.002)) for n in ns]
    return I.SurgePatch(spec["patch"], params), ns


def r_surge(spec, ns, total):
    patch, ns = _surge_part(spec, ns)
    return patch.render(ns, total=total)


def r_sfz(spec, ns, total):
    return sfz(spec["sfz"]).render(ns, total=total)


def r_sf2(spec, ns, total):
    return I.Sf2Instrument(spec["program"], spec.get("bank", 0)).render(ns, total=total)


def r_synth(spec, ns, total):
    buf = np.zeros((total, 2), np.float32)
    prev = None
    for i, n in enumerate(sorted(ns, key=lambda n: n["t"])):
        x = synth.render_note(spec["params"], n["note"], n["dur"], n["vel"], n.get("bends"), seed=i,
                              prev_pitch=prev)
        place(buf, x, n["t"])
        prev = n["note"]
    return buf


def r_drumsynth(spec, ns, total):
    buf = np.zeros((total, 2), np.float32)
    cache = {}
    for i, n in enumerate(ns):
        k = (n["kind"], round(n["vel"], 2), n.get("pitch", 0), i % 4)
        if k not in cache:
            cache[k] = synth.drum(n["kind"], n["vel"], seed=i % 4, flavor=spec.get("flavor", "modern"),
                                  pitch=n.get("pitch", 0))
        place(buf, cache[k], n["t"])
    return buf


RENDER = {"surge": r_surge, "sfz": r_sfz, "sf2": r_sf2, "synth": r_synth, "drumsynth": r_drumsynth}


def threads():
    """Threads a track's parts play in (generate.py sets TGMUSIC_THREADS for its render
    processes; 1 if unset): Surge XT parts play at once, each on an instance of its own
    (pedalboard lets go of the GIL while a plug-in plays), the other parts one after the other
    in their order, as always. The mix takes the parts in their order, whichever ends first."""
    try:
        return max(1, int(os.environ.get("TGMUSIC_THREADS", "1")))
    except ValueError:
        return 1


def rendered(played, total, pool=None, width=1):
    """Each (part, notes) of `played` rendered, in order: yields (part, notes, [audio]) (take
    the audio out of the list: then nothing else holds it). With a thread pool, up to `width`
    Surge XT parts are started ahead, each on an instance of its own (instruments.SURGE_POOL: at
    most `width`), and are taken in their turn; every other part is rendered here, in order,
    when its turn comes. An .sfz instrument is let go (its samples' memory with it) once the
    last part that plays it has played."""
    last_use = {}
    for i, (p, _) in enumerate(played):
        if p["spec"]["type"] == "sfz":
            last_use[str(p["spec"]["sfz"])] = i
    started, ahead = {}, 0
    try:
        for i, (p, ns) in enumerate(played):
            if pool is not None:
                # Start the Surge parts ahead that fit: an instance free, at most `width` waiting.
                ahead = max(ahead, i + 1)
                while ahead < len(played) and len(started) < width:
                    q, qns = played[ahead]
                    if q["spec"]["type"] == "surge":
                        patch, qns = _surge_part(q["spec"], qns)
                        f = patch.start(qns, total, pool, wait=False)
                        if f is None:
                            break
                        started[ahead] = f
                    ahead += 1
            kind = p["spec"]["type"]
            if i in started:
                x = started.pop(i).result()
            elif pool is not None and kind == "surge":
                patch, sns = _surge_part(p["spec"], ns)
                x = patch.start(sns, total, pool).result()
            else:
                x = RENDER[kind](p["spec"], ns, total)
            if kind == "sfz" and last_use.get(str(p["spec"]["sfz"])) == i:
                _SFZ.pop(str(p["spec"]["sfz"]), None)
            box = [x]
            del x
            yield p, ns, box
    finally:
        for f in started.values():
            f.cancel()
        for f in started.values():
            if not f.cancelled():
                try:
                    f.result()
                except Exception:  # noqa: BLE001 - the part's own error is raised where it is taken
                    pass
# generate.py --plan-notes (tgmusic/plan.py): PLAN(part, notes, total) is given each part with
# the notes its instrument would get, in place of rendering it; then render_song stops (nothing
# is mixed or written).
PLAN = None
# TGMUSIC_DIGESTS=1 (checking repeatability): the SHA-256 of each part as rendered goes into the
# track's .json ("part_sha256": {part: [instrument type, sha256]}).
DIGESTS = os.environ.get("TGMUSIC_DIGESTS") == "1"

CALIBRATION = config.JsonCache("pitch_calibration.json")


def spec_key(spec):
    return json.dumps({k: (config.rel_key(v) if k == "sfz" else str(v)) for k, v in spec.items()}, sort_keys=True)


def octave_offset(spec):
    """Measured octave offset of an instrument (some factory patches sound an octave low).
    The values measured when the generator was made ship in data/pitch_calibration.json; any
    other instrument is measured once and cached. The renderer transposes the part so the
    listener hears the written register."""
    if spec["type"] not in ("surge", "sfz", "sf2"):
        return 0
    k = spec_key(spec)
    val = CALIBRATION.get(k)
    if val is None:
        offs = []
        for note in (55, 62, 67):
            x = RENDER[spec["type"]](spec, [{"t": 0.05, "dur": 0.6, "note": note, "vel": 0.8}], int(1.2 * SR)).mean(1)
            seg = x[int(0.25 * SR): int(0.55 * SR)]
            f = yin_f0(seg) if np.abs(seg).max() > 1e-4 else None
            if f:
                offs.append(12 * np.log2(f / 440) + 69 - note)
        val = int(round(np.median(offs) / 12) * 12) if offs else 0
        CALIBRATION.put(k, val)
    return val


# ------------------------------------------------------------------ effects
def gated_reverb(x, spb):
    """80s gated reverb (for synthwave snares)."""
    rev = plugin("DragonflyPlateReverb")
    _set(rev, dry_level=0, wet_level=100, decay_s=1.6, predelay_ms=0, width=120)
    wet = rev(x.T.copy(), SR).T
    env = np.abs(x).max(1)
    from scipy.ndimage import maximum_filter1d
    gate = maximum_filter1d((env > 0.02).astype(np.float32), int(0.28 * SR), origin=-int(0.14 * SR))
    gate = np.convolve(gate, np.ones(480) / 480, "same")
    return x + wet * gate[:, None] * 0.9


def _set(p, **kw):
    for k, v in kw.items():
        if k in p.parameters:
            setattr(p, k, v)


def duck_curve(n, kick_times, depth=0.5, attack=0.004, release=0.18):
    g = np.ones(n, np.float32)
    t = np.arange(int(release * 5 * SR)) / SR
    shape = 1 - depth * np.where(t < attack, t / attack, np.exp(-(t - attack) / release))
    for kt in kick_times:
        i = int(kt * SR)
        j = min(n, i + len(shape))
        if i < n:
            g[i:j] = np.minimum(g[i:j], shape[: j - i])
    return g


def chain(fx):
    return pb.Pedalboard(fx)


# ------------------------------------------------------------------ style recipes
def kicks_t(notes):
    return sorted(n["t"] for n in A.drums(notes, "kick"))


def drum_events(meta, notes, extra_hats=False, crash=True):
    """Original drum pattern as abstract events (kind, t, vel)."""
    ev = []
    for kind in ("kick", "snare", "hat", "ohat", "tom", "perc"):
        src = A.drums(notes, kind)
        if not src:
            continue
        vmax = max(n["vel"] for n in src)
        for n in src:
            ev.append({"kind": kind, "t": n["t"], "vel": 0.55 + 0.45 * n["vel"] / vmax,
                       "pitch": (n.get("drum_pitch") or 60) - 60, "beat": n["beat"]})
    if extra_hats and not A.drums(notes, "hat"):
        for s, e in A.active_ranges(meta, A.drums(notes, "kick") + A.drums(notes, "snare"), gap_beats=4):
            s = math.floor(s)
            for g in A.grid_notes(meta, s, e, 0.5, 0, 0.6, accent_every=2, accent=1.25):
                ev.append({"kind": "hat", "t": g["t"], "vel": min(1, g["vel"]), "pitch": 0,
                           "beat": g["t"] / meta["spb"]})
    if crash:
        for b in A.section_beats(meta, notes):
            ev.append({"kind": "crash", "t": b * meta["spb"], "vel": 0.9, "pitch": 0, "beat": b})
    return sorted(ev, key=lambda e: e["t"])


def map_drums(ev, keymap, dur=0.6):
    out = []
    for e in ev:
        k = keymap.get(e["kind"])
        if k is None:
            continue
        if callable(k):
            k = k(e)
        out.append({"t": e["t"], "dur": dur, "note": k, "vel": e["vel"]})
    return out


RUSTY = {"kick": 36, "snare": 38, "hat": 42, "ohat": 46, "crash": 49, "ride": 51,
         "tom": lambda e: [41, 43, 45, 47][int(np.clip((e["pitch"] + 6) // 4, 0, 3))]}


def parts_for(style, song, meta, notes, ch=None):
    spb = meta["spb"]
    ch = ch or A.chords(meta, notes)
    counter = A.to_notes(A.by_role(notes, "counter"), vel_min=0.5, keep_bends=False)
    lead = A.to_notes(A.by_role(notes, "lead"), vel_min=0.55)
    lead2 = A.to_notes(A.by_role(notes, "lead2"), vel_min=0.5)
    bass = A.to_notes(A.by_role(notes, "bass"), vel_min=0.6, keep_bends=False)
    arp = A.to_notes(A.by_role(notes, "arp"), vel_min=0.45, keep_bends=False)
    arp2 = A.to_notes(A.by_role(notes, "arp2"), vel_min=0.4, keep_bends=False)
    chord = A.to_notes(A.by_role(notes, "chord"), vel_min=0.5)
    riff = A.to_notes(A.by_role(notes, "riff"), vel_min=0.6, keep_bends=False)
    pulse = A.to_notes(A.by_role(notes, "pulse"), vel_min=0.4)
    kt = kicks_t(notes)
    P = []

    def add(name, spec, ns, **mix):
        if ns:
            P.append(dict(name=name, spec=spec, notes=ns, **mix))

    if style == "faithful":
        # Same parts, same voicing, same echo: each SNES sample replaced by a hi-fi equivalent.
        add("lead", {"type": "surge", "patch": "Leads/Crisp PWM.fxp"}, lead, gain=-3, pan=0.0, echo=0.22, rev=0.12)
        add("lead2", {"type": "surge", "patch": "Leads/Square.fxp"}, lead2, gain=-6, pan=0.25, echo=0.2, rev=0.12)
        add("bass", {"type": "surge", "patch": "Basses/Rubber Bass.fxp"}, bass, gain=-2, pan=0.0, rev=0.02)
        add("arp", {"type": "surge", "patch": "Plucks/Square Pop.fxp"}, arp, gain=-8, pan=-0.25, echo=0.25, rev=0.1)
        add("arp2", {"type": "surge", "patch": "Plucks/Pure Square.fxp"}, arp2, gain=-12, pan=0.35, echo=0.25, rev=0.1)
        add("chord", {"type": "surge", "patch": "Polysynths/Jupiter-8.fxp"}, chord, gain=-9, pan=0.15, echo=0.15, rev=0.15)
        add("counter", {"type": "surge", "patch": "Leads/Square.fxp"}, counter, gain=-9, pan=-0.3, echo=0.2, rev=0.12)
        add("riff", {"type": "surge", "patch": "Leads/Synth Guitar 1.fxp"}, riff, gain=-7, pan=-0.2, echo=0.1, rev=0.05)
        add("pulse", {"type": "sfz", "sfz": sso("Percussion/Marimba Hits.sfz")}, pulse, gain=-10, pan=-0.3, echo=0.2, rev=0.1)
        ev = drum_events(meta, notes, extra_hats=False, crash=False)
        add("drums", {"type": "sfz", "sfz": I.DRUMS / "02-basic.sfz"}, map_drums(ev, RUSTY), gain=-1, bus="drums", rev=0.06)
        add("drums_snap", {"type": "drumsynth", "flavor": "tight"},
            [e for e in ev if e["kind"] in ("kick", "snare")], gain=-9, bus="drums")
    elif style == "synthwave":
        add("lead", {"type": "surge", "patch": "Leads/Moogy Saw.fxp"}, lead, gain=-3, pan=0.0, delay=0.18, rev=0.18)
        add("lead_layer", {"type": "surge", "patch": "Polysynths/Super Jupiter.fxp"},
            A.to_notes(A.by_role(notes, "lead"), transpose=12, vel_min=0.4), gain=-14, pan=0.0, delay=0.2, rev=0.25)
        add("lead2", {"type": "surge", "patch": "Leads/Sync Lead.fxp"}, lead2, gain=-7, pan=0.2, delay=0.2, rev=0.2)
        add("counter", {"type": "surge", "patch": "Leads/Sync Lead.fxp"}, counter, gain=-11, pan=-0.3, delay=0.25, rev=0.2)
        add("bass", {"type": "surge", "patch": "Basses/Bass 2.fxp"}, bass, gain=-2, duck=0.35)
        add("sub", {"type": "synth", "params": dict(layers=[dict(wave=3)], cutoff=300, a=0.003, d=0.1, s=1, r=0.05, gain=0.6)},
            [dict(n, note=n["note"] - 12 if n["note"] >= 40 else n["note"]) for n in bass], gain=-6, duck=0.5)
        add("arp", {"type": "surge", "patch": "Plucks/The 1980s.fxp"}, arp, gain=-8, pan=-0.3, delay=0.3, rev=0.15, duck=0.3)
        add("arp2", {"type": "surge", "patch": "Plucks/Hasselhoff.fxp"}, arp2, gain=-11, pan=0.35, delay=0.3, rev=0.15)
        add("chord", {"type": "surge", "patch": "Brass/OB-8 Jump.fxp"}, chord, gain=-9, pan=0.0, rev=0.2, duck=0.3)
        add("riff", {"type": "surge", "patch": "Polysynths/Megasynth 1.fxp"}, riff, gain=-8, pan=0.0, rev=0.1, duck=0.3)
        add("pulse", {"type": "surge", "patch": "Plucks/Delay Pops 1.fxp"}, pulse, gain=-12, pan=0.3, rev=0.15)
        add("pad", {"type": "surge", "patch": "Pads/MKS-70 Warm Pad.fxp"}, A.pad_notes(ch, 55, 74, vel=0.55),
            gain=-13, pan=0.0, rev=0.3, duck=0.55)
        ev = drum_events(meta, notes, extra_hats=True, crash=True)
        dr = [dict(e, kind={"tom": "tom", "perc": "shaker"}.get(e["kind"], e["kind"])) for e in ev]
        add("kick", {"type": "drumsynth", "flavor": "modern"}, [e for e in dr if e["kind"] == "kick"], gain=-2, bus="drums")
        add("snare", {"type": "drumsynth"}, [e for e in dr if e["kind"] == "snare"], gain=-5, bus="drums", gated=True)
        add("clap", {"type": "drumsynth"}, [dict(e, kind="clap") for e in dr if e["kind"] == "snare"], gain=-10, bus="drums", rev=0.2)
        add("hats", {"type": "drumsynth"}, [e for e in dr if e["kind"] in ("hat", "ohat", "shaker")], gain=-13, bus="drums", pan=0.2)
        add("toms", {"type": "drumsynth"}, [e for e in dr if e["kind"] == "tom"], gain=-14, bus="drums", rev=0.2)
        add("crash", {"type": "drumsynth"}, [e for e in dr if e["kind"] == "crash"], gain=-14, bus="drums", rev=0.2)
    elif style == "orchestral":
        lead_leg = A.legato(lead, 0.5, spb)
        add("lead_tpt", {"type": "sfz", "sfz": sso("Brass - Performance/Trumpets Sustain.sfz")}, lead_leg, gain=-2, pan=0.15, rev=0.3)
        add("lead_vln", {"type": "sfz", "sfz": sso("Strings - Performance/1st Violins Sustain.sfz")},
            [dict(n, note=n["note"] + 12 if n["note"] < 76 else n["note"]) for n in lead_leg], gain=-5, pan=-0.35, rev=0.32)
        add("lead_horn", {"type": "sfz", "sfz": sso("Brass - Performance/Horns Sustain.sfz")},
            [dict(n, note=n["note"] - 12) for n in lead_leg], gain=-8, pan=0.3, rev=0.35)
        add("lead2", {"type": "sfz", "sfz": sso("Woodwinds - Performance/Flutes Sustain.sfz")}, A.legato(lead2, 0.5, spb), gain=-5, pan=0.2, rev=0.3)
        add("counter", {"type": "sfz", "sfz": sso("Woodwinds - Performance/Clarinets Sustain.sfz")},
            A.legato(counter, 0.5, spb), gain=-7, pan=-0.25, rev=0.3)
        add("arp", {"type": "sfz", "sfz": sso("Strings - Performance/2nd Violins Staccato.sfz")}, arp, gain=-6, pan=0.25, rev=0.28)
        add("arp_harp", {"type": "sfz", "sfz": sso("Concert Harp.sfz")}, arp, gain=-12, pan=-0.3, rev=0.3)
        add("arp2", {"type": "sfz", "sfz": sso("Percussion/Glockenspiel.sfz")}, [dict(n, vel=n["vel"] * 0.7) for n in arp2], gain=-16, pan=0.3, rev=0.3)
        add("bass_vc", {"type": "sfz", "sfz": sso("Strings - Performance/Celli Staccato.sfz")},
            [dict(n, note=n["note"] + 12 if n["note"] < 36 else n["note"]) for n in bass], gain=-4, pan=-0.2, rev=0.22)
        add("bass_cb", {"type": "sfz", "sfz": sso("Strings - Performance/Basses Staccato.sfz")}, bass, gain=-6, pan=0.25, rev=0.2)
        add("chord", {"type": "sfz", "sfz": sso("Brass - Performance/Trombones Sustain.sfz")}, chord, gain=-8, pan=0.2, rev=0.3)
        add("riff", {"type": "sfz", "sfz": sso("Strings - Performance/Celli Marcato.sfz")}, riff, gain=-4, pan=-0.2, rev=0.25)
        add("riff_low", {"type": "sfz", "sfz": sso("Brass - Performance/Tuba Staccato.sfz")}, riff, gain=-10, pan=0.2, rev=0.25)
        add("strings_pad", {"type": "sfz", "sfz": sso("Strings - Performance/All Strings Sustain.sfz")},
            A.pad_notes(ch, 48, 72, vel=0.5, n=5), gain=-11, pan=0.0, rev=0.35)
        lead_rng = A.active_ranges(meta, A.by_role(notes, "lead"), gap_beats=8)
        choir = [n for n in A.pad_notes(ch, 55, 72, vel=0.45, n=3)
                 if any(s <= n["t"] / spb < e for s, e in lead_rng)]
        add("choir", {"type": "sfz", "sfz": sso("Chorus - Performance/Large Chorus.sfz")}, choir, gain=-16, pan=0.0, rev=0.4)
        add("pulse", {"type": "sfz", "sfz": sso("Percussion/Marimba Hits.sfz")}, pulse, gain=-12, pan=0.3, rev=0.25)
        ev = drum_events(meta, notes, extra_hats=False, crash=True)
        root_at = lambda t: next((c for c in reversed(ch) if c["t"] <= t + 1e-3), ch[0])
        timp = []
        for e in ev:
            if e["kind"] == "kick":
                r = root_at(e["t"])["bass"]
                timp.append({"t": e["t"], "dur": 0.8, "note": 36 + ((r - 0) % 12), "vel": e["vel"] * 0.95})
        add("timpani", {"type": "sfz", "sfz": sso("Percussion/Timpani.sfz")}, timp, gain=-3, bus="drums", rev=0.3)
        add("snare_orch", {"type": "sfz", "sfz": sso("Percussion/Bass Drum & Snare.sfz")},
            [{"t": e["t"], "dur": 0.5, "note": 49, "vel": e["vel"] * 0.85} for e in ev if e["kind"] == "snare"],
            gain=-6, bus="drums", rev=0.3)
        add("bassdrum", {"type": "sfz", "sfz": sso("Percussion/Bass Drum & Snare.sfz")},
            [{"t": e["t"], "dur": 0.8, "note": 48, "vel": 1.0} for e in ev if e["kind"] == "crash"], gain=-4, bus="drums", rev=0.35)
        add("cymbal", {"type": "sfz", "sfz": sso("Percussion/Cymbals & Tamtam.sfz")},
            [{"t": e["t"], "dur": 2.0, "note": 55, "vel": 0.9} for e in ev if e["kind"] == "crash"], gain=-10, bus="drums", rev=0.35)
        hyb = [e for e in ev if e["kind"] in ("kick", "snare")]
        add("hybrid", {"type": "drumsynth", "flavor": "808"}, [dict(e, vel=e["vel"] * 0.7) for e in hyb if e["kind"] == "kick"],
            gain=-10, bus="drums")
    else:
        raise ValueError(style)
    return P, ch, kt


def bus_fx(style):
    if style == "synthwave":
        return {"drums": [pb.Compressor(-14, 3, 3, 80)]}
    if style == "orchestral":
        return {"drums": [pb.Compressor(-18, 2, 20, 200)]}
    return {"drums": [pb.Compressor(-16, 2.5, 8, 120)]}


# "rock" is the room most city bands play in (bands.py), from a dropped rock style.
REVERB = {"faithful": ("DragonflyRoomReverb", dict(decay_s=1.2, size_m=18, predelay_ms=8, late_level=100, early_level=40)),
          "synthwave": ("DragonflyHallReverb", dict(decay_s=2.8, size_m=30, predelay_ms=20, late_level=100, early_level=20)),
          "rock": ("DragonflyRoomReverb", dict(decay_s=1.4, size_m=22, predelay_ms=10, late_level=100, early_level=60)),
          "orchestral": ("DragonflyHallReverb", dict(decay_s=2.6, size_m=38, predelay_ms=25, late_level=100, early_level=60))}


def render_song(song, style, version=None, dest=None, custom=None, formats=("ogg",), stem_dir=None):
    """Render song in style to <dest>/<song>__<style>[__<version>].* (formats: ogg, wav, mp3).

    custom: dict(parts_fn(meta, notes, ch, parts) -> parts, name, dest, reverb_style, bus_style, info,
    notes_fn) for the city versions. stem_dir: also write each part's stem there (FLAC)."""
    t0 = time.time()
    meta, notes = A.load(song)
    ch_over, changes = None, None
    if version:
        notes, ch_over, changes = RI.transform(song, version, meta, notes)
    if custom and custom.get("notes_fn"):
        notes = custom["notes_fn"](meta, notes)
    spb = meta["spb"]
    loops = meta["loops"]
    pass_n = int(round(meta["pass_s"] * SR))
    total = pass_n + int(TAIL_S * SR)
    parts, ch, kt = parts_for(style, song, meta, notes, ch_over)
    if custom:
        parts = custom["parts_fn"](meta, notes, ch, parts)
    if loops:
        # notes of the next pass that sound into the tail (keeps the loop seamless)
        kt = kt + [t + meta["pass_s"] for t in kt if t < TAIL_S]
    music = np.zeros((total, 2), np.float32)
    drums = np.zeros((total, 2), np.float32)
    rev_send = np.zeros((total, 2), np.float32)
    dly_send = np.zeros((total, 2), np.float32)
    echo_send = np.zeros((total, 2), np.float32)
    if stem_dir:
        stem_dir = Path(stem_dir)
        stem_dir.mkdir(parents=True, exist_ok=True)
    report, digests = [], {}
    # Every part's notes first, so that each .sfz instrument loads once with what all its
    # parts play (instruments.expect), then the parts in turn.
    played = []
    for p in parts:
        ns = p["notes"]
        if loops:
            ns = ns + [dict(n, t=n["t"] + meta["pass_s"]) for n in ns if n["t"] < TAIL_S]
        if p.get("bus") not in ("drums", "real"):
            off = octave_offset(p["spec"])
            if off:
                ns = [dict(n, note=int(np.clip(n["note"] - off, 0, 127))) for n in ns]
        played.append((p, ns))
    if PLAN is not None:
        for p, ns in played:
            PLAN(p, ns, total)
        return None
    for p, ns in played:
        if p["spec"]["type"] == "sfz":
            I.expect(p["spec"]["sfz"], ns, total)
    n_threads = threads()
    pool = None
    if n_threads > 1 and any(p["spec"]["type"] == "surge" for p, _ in played):
        from concurrent.futures import ThreadPoolExecutor
        I.SURGE_POOL.limit = n_threads
        pool = ThreadPoolExecutor(n_threads, thread_name_prefix="part")
    tmp = np.empty((total, 2), np.float32)  # x times a send's level
    try:
        for p, ns, box in rendered(played, total, pool, n_threads):
            x = np.nan_to_num(box.pop()[:total], copy=False)
            if len(x) < total:
                x = np.pad(x, ((0, total - len(x)), (0, 0)))
            if DIGESTS:
                digests[p["name"]] = [p["spec"]["type"], hashlib.sha256(np.ascontiguousarray(x, np.float32).tobytes()).hexdigest()]
            if p.get("gated"):
                x = gated_reverb(x, spb)
            peak = float(np.abs(x).max())
            if peak < 1e-6:
                report.append((p["name"], "SILENT"))
                continue
            # level-normalise each stem to -18 dBFS RMS (while playing) before its mix gain
            act = np.abs(x).max(1) > peak * 0.02
            rms = float(np.sqrt((x[act] ** 2).mean())) if act.any() else peak
            del act
            # (in place: the same products, one buffer fewer)
            x = np.ascontiguousarray(x, dtype=np.float32)
            x *= db(-18) / max(rms, 1e-6)
            x *= db(p.get("gain", 0))
            pan_st(x, p.get("pan", 0.0), out=x)
            if p.get("duck"):
                x *= duck_curve(total, kt, p["duck"])[:, None]
            (drums if p.get("bus") == "drums" else music)[:] += x
            for send, level in ((rev_send, p.get("rev", 0.0)), (dly_send, p.get("delay", 0.0)),
                                (echo_send, p.get("echo", 0.0))):
                if level:  # (a send of 0 adds only zeros)
                    np.multiply(x, level, out=tmp)
                    send += tmp
            report.append((p["name"], round(20 * math.log10(peak), 1)))
            if stem_dir:
                sf.write(stem_dir / f"{p['name']}.flac", x, SR, subtype="PCM_24")
    finally:
        if pool is not None:
            pool.shutdown(wait=True)
        I.SURGE_POOL.clear()  # the instances' memory back before the mix
    del tmp
    # The effects on this thread and in this order, as they always were: loading Dragonfly
    # Reverb turns flush-to-zero on for the thread that loads it (and the threads it starts
    # after), and what is computed after it here depends on that. Each send is let go once used.
    drums = chain(bus_fx(custom.get("bus_style", style) if custom else style)["drums"])(drums.T.copy(), SR).T
    # FX returns
    rname, rpar = REVERB[custom.get("reverb_style", style) if custom else style]
    rev = plugin(rname)
    _set(rev, dry_level=0, **rpar)
    _set(rev, wet_level=100)
    rev_ret = rev(rev_send.T.copy(), SR).T
    del rev_send
    rev_ret = chain([pb.HighpassFilter(250), pb.LowpassFilter(9000)])(rev_ret.T.copy(), SR).T
    dly = pb.Pedalboard([pb.Delay(delay_seconds=0.75 * spb, feedback=0.35, mix=1.0), pb.HighpassFilter(300), pb.LowpassFilter(6000)])
    dly_ret = dly(dly_send.T.copy(), SR).T
    del dly_send
    # SNES-style echo (160 ms, fed back lightly, low-passed like the FIR)
    echo = pb.Pedalboard([pb.Delay(delay_seconds=0.160, feedback=0.25, mix=1.0), pb.LowpassFilter(4500)])
    echo_ret = echo(echo_send.T.copy(), SR).T
    del echo_send
    # music + drums + rev_ret * 0.9 + dly_ret * 0.6 + echo_ret * 0.7, summed in that order in
    # place (the same sums, without a temporary array for each)
    mix = music
    mix += drums
    for ret, level in ((rev_ret, 0.9), (dly_ret, 0.6), (echo_ret, 0.7)):
        ret *= level
        mix += ret
    del music, drums, rev_ret, dly_ret, echo_ret, ret
    # loop: fold the tail back into the start, then master circularly (seamless)
    if loops:
        out = mix[:pass_n].copy()
        out[: total - pass_n] += mix[pass_n:]
    else:
        out = mix
    del mix
    master_fx = [pb.HighpassFilter(28), pb.LowShelfFilter(90, 1.0), pb.HighShelfFilter(10000, 1.5),
                 pb.Compressor(threshold_db=-12, ratio=2.0, attack_ms=25, release_ms=200)]
    pre = int(3 * SR)
    if loops:
        circ = np.concatenate([out[-pre:], out])
        del out
        y = chain(master_fx)(circ.T.copy(), SR).T[pre:]
        del circ
    else:
        y = chain(master_fx)(out.T.copy(), SR).T
        del out
    y, tp = master_loudness(y, loops, with_peak=True)
    name = f"{song}__{style}" + (f"__{version}" if version else "")
    if custom:
        name, dest = custom["name"], custom["dest"]
    dest = Path(dest)
    dest.mkdir(parents=True, exist_ok=True)
    meter = pyloudnorm.Meter(SR)
    lufs = meter.integrated_loudness(y)
    if tp is None:
        tp = true_peak_db(y)
    write_outputs(dest, name, y, loops, meta, formats)
    info = {"song": song, "style": style, "version": version or "original",
            "arrangement": RI.VERSIONS.get(version, "original arrangement, note for note"), "changes": changes,
            "title": meta["title"], "bpm": round(meta["bpm"], 3),
            "key": meta["key"], "seconds": round(len(y) / SR, 3), "loop": loops,
            "loop_start": 0, "loop_length": len(y) if loops else None,
            "lufs": round(lufs, 2), "true_peak_dbtp": round(tp, 2),
            "parts": report, "render_s": round(time.time() - t0, 1),
            **(custom.get("info", {}) if custom else {}),
            **({"part_sha256": digests} if DIGESTS else {}),
            "chords": [A.chord_name(c) + "@" + str(c["beat"]) for c in A.merge_chords(ch)][:400]}
    # Written last and under a temporary name: a track counts as done (jobs.is_done) only
    # once its .ogg and a complete .json are there, so a stopped run leaves no half track.
    tmp = dest / f".{name}.tmp.json"
    tmp.write_text(json.dumps(info, indent=1))
    os.replace(tmp, dest / f"{name}.json")
    print(f"{name}: {info['seconds']}s LUFS {lufs:.2f} TP {tp:.2f}, {len(report)} parts ({info['render_s']}s)", flush=True)
    return info


def true_peak_db(y):
    """The true peak (4x oversampled), in dBFS. Each channel on its own, at once in threads (the
    same samples as the whole array at once: resample_poly filters each channel by itself), its
    largest magnitude without an array of magnitudes (memory)."""
    from scipy.signal import resample_poly

    def peak(x):
        up = resample_poly(x, 4, 1, axis=0)
        return max(float(up.max()), -float(up.min()))
    if y.ndim == 2 and y.shape[1] > 1 and threads() > 1:
        from concurrent.futures import ThreadPoolExecutor
        with ThreadPoolExecutor(y.shape[1], thread_name_prefix="peak") as ex:
            top = max(ex.map(peak, [y[:, c] for c in range(y.shape[1])]))
    else:
        top = peak(y)
    return 20 * math.log10(max(1e-9, top))


def master_loudness(y, loops, with_peak=False):
    """y at TARGET_LUFS, its true peak at most CEIL_DBTP. with_peak: (y, its true peak in dB if
    it was measured on y as returned, else None)."""
    meter = pyloudnorm.Meter(SR)
    for _ in range(3):
        l = meter.integrated_loudness(y)
        y = y * db(TARGET_LUFS - l)
        lim = pb.Pedalboard([pb.Limiter(threshold_db=CEIL_DBTP - 0.8, release_ms=120)])
        if loops:
            pre = int(1 * SR)
            y = lim(np.concatenate([y[-pre:], y]).T.copy(), SR).T[pre:]
        else:
            y = lim(y.T.copy(), SR).T
    l = meter.integrated_loudness(y)
    y = y * db(TARGET_LUFS - l)
    tp = true_peak_db(y)
    if tp > CEIL_DBTP:
        y = y * db(CEIL_DBTP - tp)
        tp = None
    if with_peak:
        y32 = y.astype(np.float32)
        return y32, (tp if y32.dtype == y.dtype else None)
    return y.astype(np.float32)


def write_outputs(dest, name, y, loops, meta, formats=("ogg",)):
    """OGG for the game (Vorbis q7, LOOPSTART / LOOPLENGTH comments in samples); optionally the
    24-bit WAV master with a smpl loop chunk and an MP3 of the exact loop. Each file is written
    under a temporary name and then moved into place, so a stopped run leaves no half file."""
    import imageio_ffmpeg
    ff = imageio_ffmpeg.get_ffmpeg_exe()
    wav_tmp = dest / f".{name}.tmp.wav"
    sf.write(wav_tmp, y, SR, subtype="PCM_24")
    if loops:
        add_smpl_chunk(wav_tmp, 0, len(y) - 1)
    meta_args = ["-metadata", f"title={meta['title']} ({name.split('__', 1)[1].replace('__', ' ')})",
                 "-metadata", "artist=Barry Leitch (arr. remake)",
                 "-metadata", "comment=Rendered on this computer from your own ROM by tools/music; see CREDITS.txt"]
    loop_args = ["-metadata", "LOOPSTART=0", "-metadata", f"LOOPLENGTH={len(y)}"] if loops else []

    def encode(ext, codec):
        from .progress import full_speed
        tmp = dest / f".{name}.tmp.{ext}"
        cmd = [ff, "-y", "-loglevel", "error", "-i", str(wav_tmp), *codec, *meta_args, *(loop_args if ext == "ogg" else []),
               str(tmp)]
        p = subprocess.Popen(cmd)
        full_speed(p)  # (Windows: not throttled, as this process)
        if p.wait():
            raise subprocess.CalledProcessError(p.returncode, cmd)
        os.replace(tmp, dest / f"{name}.{ext}")

    if "ogg" in formats:
        encode("ogg", ["-c:a", "libvorbis", "-q:a", "7"])
    if "mp3" in formats:
        encode("mp3", ["-c:a", "libmp3lame", "-b:a", "320k"])
    if "wav" in formats:
        os.replace(wav_tmp, dest / f"{name}.wav")
    else:
        wav_tmp.unlink()


def add_smpl_chunk(path, start, end):
    import struct
    data = Path(path).read_bytes()
    smpl = struct.pack("<9I", 0, 0, int(1e9 / SR), 60, 0, 0, 0, 1, 0)
    smpl += struct.pack("<6I", 0, 0, start, end, 0, 0)
    chunk = b"smpl" + struct.pack("<I", len(smpl)) + smpl
    data = data + chunk
    data = data[:4] + struct.pack("<I", len(data) - 8) + data[8:]
    Path(path).write_bytes(data)


SONGS = ["01_title", "02_las_vegas", "03_hiroshima", "04_bordeaux", "05_frankfurt", "06_qualified", "07_ending"]
STYLES = ["faithful", "synthwave", "orchestral"]
