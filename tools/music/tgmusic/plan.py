"""generate.py --plan-notes: the notes each instrument of a track is sent, without rendering
audio and without the instrument libraries.

A track is arranged by the generator's own code (jobs.run -> render.render_song, with
stages.py, personality.py, realism.py), each track in a fresh process as in a build;
render.PLAN is given each part with the notes its instrument would get, in place of rendering
it, and render_song stops there (nothing is mixed, mastered or written). For an .sfz
instrument the notes kept are the note-ons instruments.sfz_events() sends sfizz: (key,
velocity 1-127).

The arrangement reads two tables measured on the instruments: render.octave_offset
(data/pitch_calibration.json) and realism.playable (data/sfz_ranges.json). A build finds the
shipped values there (and those measured on this computer before) and measures an
instrument they lack by playing it a few notes. Planning, the instruments are stand-ins that
play silence and write nothing into the tables: a value missing is reported as a miss, with
the notes measuring it plays ("probes"), which a build's downloads include; the rest of that
track's plan may then differ from what the build plays (the safety net covers it). For
--plan-notes, only the shipped values count (a fresh computer); a build also reads its own.

generate.py runs plan_job in a process of its own for each track (as it renders them).
"""
import hashlib
import json
import os
from collections import Counter

import numpy as np

from . import config

_parts, _misses, _probes = [], [], []


def _digest(x):
    """The SHA-256 of x as JSON (numpy numbers as Python ones)."""
    return hashlib.sha256(json.dumps(x, sort_keys=True, default=lambda o: o.item()).encode()).hexdigest()


def _sfz_notes(notes, total):
    """(note-ons [[key, velocity, count]], digest of every event, notes that last no time)."""
    from . import instruments as I
    ev = [(int(t), int(kind), int(key), int(vel)) for t, kind, key, vel in I.sfz_events(notes, total)]
    ons = Counter((key, vel) for _, kind, key, vel in ev if kind == 1)
    return sorted([k, v, c] for (k, v), c in ons.items()), _digest(ev), sum(1 for n in notes if n["dur"] <= 0)


def _record(part, notes, total):
    """render.PLAN: one part of a track and the notes its instrument would be sent."""
    spec = part["spec"]
    kind = spec["type"]
    out = {"name": part["name"], "type": kind}
    if kind == "sfz":
        out["instrument"] = config.rel_key(spec["sfz"])
        out["on"], out["digest"], out["no_length"] = _sfz_notes(notes, total)
        out["notes"] = sum(c for _, _, c in out["on"])
        bad = sorted({k for k, _, _ in out["on"] if not 0 <= k <= 127})
        if bad:
            out["bad_keys"] = bad  # sfizz (pysfizz) refuses these: the render would fail
    else:
        if kind == "sf2":
            out["instrument"] = f"program {spec['program']} bank {spec.get('bank', 0)}"
        elif kind == "surge":
            out["instrument"] = spec["patch"]
        out["notes"] = len(notes)
        out["digest"] = _digest(notes)
    _parts.append(out)


class _Silent:
    """In place of SfzInstrument / Sf2Instrument / SurgePatch: plays silence, and notes what
    it was asked to play (only a measurement for a table miss asks it anything)."""
    kind = None

    def __init__(self, what, *args, **kwargs):
        self.what = config.rel_key(what) if isinstance(what, (str, os.PathLike)) else what

    def render(self, notes, tail=3.0, total=None, **kwargs):
        from . import instruments as I
        n = total or I._length(notes, tail)
        on, digest, _ = _sfz_notes(notes, n)
        _probes.append({"type": self.kind, "instrument": self.what, "on": on, "digest": digest})
        return np.zeros((n, 2), np.float32)


class _SilentSfz(_Silent):
    kind = "sfz"


class _SilentSf2(_Silent):
    kind = "sf2"


class _SilentSurge(_Silent):
    kind = "surge"


def _get(original, shipped_only):
    def get(self, key):
        """config.JsonCache.get (from the shipped table only if shipped_only); a miss is noted."""
        if shipped_only:
            if self._shipped is None:
                self._shipped = self._read(config.DATA / self.name)
            val = self._shipped.get(key)
        else:
            val = original(self, key)
        if val is None:
            _misses.append({"table": self.name, "key": key})
        return val
    return get


def install(shipped_only=True):
    """Arrange, don't render (in a process of its own: this changes the modules for good)."""
    from . import instruments as I
    from . import render as R
    R.PLAN = _record
    I.SfzInstrument, I.Sf2Instrument, I.SurgePatch = _SilentSfz, _SilentSf2, _SilentSurge
    config.JsonCache.get = _get(config.JsonCache.get, shipped_only)
    config.JsonCache.put = lambda self, key, value: None


def plan_job(job, out, shipped_only=True):
    """The notes of one job (jobs.run's arguments; `out` is only named, never written):
    {"job", "error", "parts": [{name, type, instrument, notes, digest, [on]}], "misses",
    "probes", "seconds"}."""
    from . import jobs as J
    install(shipped_only)
    del _parts[:], _misses[:], _probes[:]
    _, secs, _, err = J.run((job, str(out), ("ogg",)))
    return {"job": [job[0], list(job[1]) if isinstance(job[1], tuple) else job[1], job[2]], "error": err,
            "parts": list(_parts), "misses": list(_misses), "probes": list(_probes), "seconds": round(secs, 2)}


def summary(tracks):
    """{instrument: [[key, velocity, count]]} of the .sfz instruments over all tracks."""
    on = {}
    for t in tracks:
        for p in t["parts"]:
            if p["type"] == "sfz":
                c = on.setdefault(p["instrument"], Counter())
                for k, v, n in p["on"]:
                    c[(k, v)] += n
    return {inst: sorted([k, v, n] for (k, v), n in c.items()) for inst, c in sorted(on.items())}
