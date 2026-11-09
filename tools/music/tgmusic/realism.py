"""Swap General-MIDI soundfont voices for real multi-sampled instruments (CC0 / CC-BY libraries).

When the generator was made, listening-model scores (MS-CLAP, audiobox-aesthetics) showed GM-soundfont timbres +
the SNES arp figure are what make the band renders read as '16-bit game music'. This module maps GM programs -> SFZ (VSCO 2 CE, VCSL, MTG Solo Sax,
Naked Drums), probes each instrument's playable range (cached) and splits short notes onto staccato samples.
"""
import numpy as np

from . import config

VS = config.lib("vsco2-ce")
VC = config.lib("vcsl")
SAX = config.lib("mtg-solo-sax") / "MTG Solo Saxophones"
DRUM_KIT = config.lib("naked-drums") / "Wilkinson Audio" / "Naked Drums" / "User" / "Naked Drums GM.sfz"
RANGES = config.JsonCache("sfz_ranges.json")

# GM program -> (sustain sfz, staccato sfz or None)
UPGRADE = {
    0: (VC / "Chordophones/Zithers/Grand Piano, Steinway B.sfz", None),
    1: (VC / "Chordophones/Zithers/Grand Piano, Kawai.sfz", None),
    6: (VC / "Chordophones/Zithers/Harpsichord, Flemish - 8'.sfz", None),
    9: (VS / "Glockenspiel.sfz", None),
    11: (VC / "Idiophones/Struck Idiophones/Vibraphone - Soft Mallets.sfz", None),
    12: (VS / "Marimba.sfz", None),
    13: (VS / "Xylophone.sfz", None),
    14: (VS / "TubularBells.sfz", None),
    40: (VS / "SViolinVib.sfz", VS / "SViolinSpic.sfz"),
    41: (VS / "ViolaEnsSusVib.sfz", VS / "ViolaEnsSpic.sfz"),
    42: (VS / "CelloEnsSusVib.sfz", VS / "CelloEnsSpic.sfz"),
    43: (VS / "ContrabassSusVB.sfz", VS / "ContrabassSpic.sfz"),
    44: (VS / "ViolinEnsTrem.sfz", None),
    45: (VS / "ViolinEnsPizz.sfz", None),
    46: (VS / "Harp.sfz", None),
    47: (VS / "Timpani.sfz", None),
    48: (VS / "ViolinEnsSusVib.sfz", VS / "ViolinEnsSpic.sfz"),
    49: (VS / "ViolinEnsSusVib-Quiet.sfz", None),
    56: (VS / "TrumpetSusVib.sfz", VS / "TrumpetStac.sfz"),
    57: (VS / "TromboneSus.sfz", VS / "TromboneStac.sfz"),
    58: (VS / "TubaSus.sfz", VS / "TubaStac.sfz"),
    59: (VS / "TrumpetHarmonMuteSus.sfz", None),
    60: (VS / "FHornSus.sfz", VS / "FHornStac.sfz"),
    64: (SAX / "MTG Soprano Sax.sfz", None),
    65: (SAX / "MTG Alto Sax.sfz", None),
    66: (SAX / "MTG Tenor Sax.sfz", None),
    67: (SAX / "MTG Baritone Sax.sfz", None),
    68: (VS / "OboeSusVib.sfz", VS / "OboeStac.sfz"),
    70: (VS / "BassoonSus.sfz", VS / "BassoonStac.sfz"),
    71: (VS / "ClarinetSus.sfz", VS / "ClarinetStac.sfz"),
    72: (VS / "PiccoloSus.sfz", VS / "PiccoloStac.sfz"),
    73: (VS / "FluteSusVib.sfz", VS / "FluteStac.sfz"),
    74: (VC / "Aerophones/Edge-blown Aerophones/Baroque Alto Recorder - Sustain.sfz",
         VC / "Aerophones/Edge-blown Aerophones/Baroque Alto Recorder - Staccato.sfz"),
    79: (VC / "Aerophones/Edge-blown Aerophones/Ocarina, Typical - Sus.sfz", None),
    107: (VC / "Chordophones/Zithers/Dan Tranh - Normal.sfz", None),
    108: (VC / "Idiophones/Plucked Idiophones/Kalimba, Kenya.sfz", None),
}
# section instruments get a second, detuned/delayed double for ensemble width
ENSEMBLE = {48, 49, 56, 57, 60, 61}
STAC_S = 0.2   # notes shorter than this (seconds) use the staccato sample set
ACOUSTIC_KITS = {0, 8, 16, 32}   # GS bank-128 kits replaced by the real Naked Drums kit

def playable(path):
    """Sorted list of MIDI notes the sfz actually sounds (shipped in data/sfz_ranges.json for the
    instruments used; anything else is probed once and cached)."""
    key = config.rel_key(path)
    val = RANGES.get(key)
    if val is None:
        from . import instruments as I
        sr = I.SR
        notes = list(range(21, 109))
        step = 0.6
        ns = [{"t": i * step + 0.01, "dur": 0.35, "note": n, "vel": 0.8} for i, n in enumerate(notes)]
        y = I.SfzInstrument(path).render(ns, total=int((len(notes) * step + 1) * sr))
        m = np.abs(y).max(1) if y.ndim == 2 else np.abs(y)
        en = [float(m[int(i * step * sr): int((i * step + 0.4) * sr)].max()) for i in range(len(notes))]
        top = max(en) + 1e-9
        val = [n for n, e in zip(notes, en) if e > top * 0.02]
        RANGES.put(key, val)
    return val


def fold(note, ok):
    if not ok or note in ok:
        return note
    lo, hi = ok[0], ok[-1]
    while note < lo:
        note += 12
    while note > hi:
        note -= 12
    if note in ok:
        return note
    return min(ok, key=lambda k: abs(k - note))


def upgrade(name, spec, notes, mix):
    """Return list of (name, spec, notes, mix) replacing a GM sf2 part with real samples where available."""
    if spec.get("type") != "sf2":
        return [(name, spec, notes, mix)]
    prog, bank = spec.get("program"), spec.get("bank", 0)
    if bank == 128:
        if mix.get("bus") == "drums" and prog in ACOUSTIC_KITS and name in ("drums", "drive"):
            return [(name, {"type": "sfz", "sfz": str(DRUM_KIT)}, notes, mix)]
        return [(name, spec, notes, mix)]
    if prog not in UPGRADE or mix.get("bus") == "drums":
        return [(name, spec, notes, mix)]
    # The libraries are fetched before rendering (generate.py); a missing file is an error,
    # not a silent fallback to the GM voice (that would change the render).
    sus, stac = UPGRADE[prog]
    out = []
    groups = [(name, sus, [n for n in notes if not stac or n["dur"] >= STAC_S])]
    if stac:
        groups.append((name + "_stac", stac, [n for n in notes if n["dur"] < STAC_S]))
    else:
        groups[0] = (name, sus, notes)
    for nm, path, ns in groups:
        if not ns:
            continue
        ok = playable(path)
        ns = [dict(n, note=fold(n["note"], ok)) for n in ns]
        out.append((nm, {"type": "sfz", "sfz": str(path)}, ns, dict(mix, bus="real")))
        if prog in ENSEMBLE and nm == name:
            rng = np.random.default_rng(len(ns))
            dbl = [dict(n, t=n["t"] + 0.012 + abs(rng.normal(0, 0.008)), vel=n["vel"] * 0.85) for n in ns]
            p = mix.get("pan", 0.0)
            out.append((nm + "_dbl", {"type": "sfz", "sfz": str(path)}, dbl,
                        dict(mix, bus="real", gain=mix.get("gain", 0) - 4, pan=float(np.clip(-p - 0.35 * np.sign(p or 1), -1, 1)))))
    return out or [(name, spec, notes, mix)]
