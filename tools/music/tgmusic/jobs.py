"""What to render and where: one job = one track, rendered in a fresh process (generate.py
--worker), so a track does not depend on what was rendered before it, the memory of its
instruments is given back when it is done, and a process that dies fails only its own track."""
import time
import traceback
from pathlib import Path

VARIATION_STYLES = ("faithful", "synthwave", "orchestral")  # variations/<song>/<song>__<style>
CITY_STYLES = ("A", "B", "C", "D")  # stages/ (A, B), stages-band/ (C), stages-race/ (D)
FOLDER = {"A": "stages", "B": "stages", "C": "stages-band", "D": "stages-race"}
CITY_LABEL = {"A": "postcard", "B": "local band", "C": "city band", "D": "race (modern)"}


def slug(s):
    return s.lower().replace(" ", "_")


def cities():
    """[(country, city, tune)] for the 32 courses, in the game's order."""
    from .stages import STAGES
    return [(s["country"], s["city"], s["tune"]) for s in STAGES]


def output_path(out, job):
    """The .ogg a job writes (its .json is written last, next to it)."""
    kind, a, b = job
    if kind == "variation":
        return Path(out) / "variations" / a / f"{a}__{b}.ogg"
    country, city = a
    return Path(out) / FOLDER[b] / slug(country) / f"{slug(country)}__{slug(city)}__{b}.ogg"


def is_done(out, job):
    p = output_path(out, job)
    return p.exists() and p.with_suffix(".json").exists()


def present_styles(out):
    """The styles that have at least one track in the output folder (for CREDITS.txt)."""
    out = Path(out)
    found = [s for s in VARIATION_STYLES if any(out.glob(f"variations/*/*__{s}.ogg"))]
    found += [s for s in CITY_STYLES if any(out.glob(f"{FOLDER[s]}/*/*__{s}.ogg"))]
    return found


def label(job):
    kind, a, b = job
    if kind == "variation":
        return f"{a} {b}"
    return f"{a[1]} {b} ({CITY_LABEL[b]})"


def run(args):
    """Worker: render one job. Returns (job, seconds, peak memory MB, error or None)."""
    job, out, formats = args
    kind, a, b = job
    t0 = time.time()
    err = None
    try:
        if kind == "variation":
            from . import render
            render.render_song(a, b, dest=Path(out) / "variations" / a, formats=formats)
        elif b in ("A", "B"):
            from . import stages
            st = next(s for s in stages.STAGES if (s["country"], s["city"]) == a)
            stages.render_stage(st, b, out, formats)
        else:
            from . import bands, personality
            prof = next(p for p in bands.BANDS if (p["country"], p["city"]) == a)
            personality.render_band(prof, b, out, formats)
    except Exception:
        err = traceback.format_exc()
    from .progress import peak_mb
    return job, time.time() - t0, peak_mb(), err
