"""What a build downloads of the sample libraries (those marked by_note in libraries.py),
worked out on the player's computer from the plan of the tracks it renders, and what a render
process makes sure of before it loads an .sfz instrument. Standard library only.

The build (generate.py): the notes from the ROM -> the plan of each track (plan.py: the notes
each of its instruments gets) -> wanted(): for every .sfz instrument the plan plays, its
definition files (the .sfz and what it #includes), the samples its notes can play
(sfz.note_samples; also what measuring an instrument missing from the shipped tables plays),
and its library's licence and readme files -> only those are downloaded
(libraries.fetch(want=...)), out of data/samples.tsv, the most a build may download. The plan
and the notes stay in the cache folder.

A render process (instruments.SfzInstrument, through prepare()): before sfizz loads an
instrument, the samples the notes it will play can trigger are checked to be there. A missing
one is fetched then, from samples.tsv and checked (the safety net: with the plan it doesn't
happen; each is logged), because sfizz drops a region whose sample is missing when it loads.
sfizz then loads the instrument from a folder of links to just those files, every sample read
whole into memory (<control> hint_ram_based): nothing is streamed while it plays, so the same
notes give the same audio every time, and only the samples the track plays take memory.
"""
import hashlib
import itertools
import json
import os
import shutil
from pathlib import Path

from . import config
from . import libraries as L
from . import sfz

FETCHED = []  # (library, path, size): what the safety net fetched in this process
_LIBS = {}
_COUNT = itertools.count()
_AUDIO = (".wav", ".flac", ".ogg", ".aif", ".aiff", ".mp3")


def _definitions(lb):
    """The listed files of a library that are neither sound nor a licence or readme: the .sfz
    files and what they #include (.txt and the like), about 0.5 MB for all. (One named like a
    readme that is #included after all is fetched when it is read.)"""
    return [p for p in lb.files if not p.lower().endswith(_AUDIO) and not L.DOC.fullmatch(p.rsplit("/", 1)[-1])]


class Library:
    """A by_note library's listed files (data/samples.tsv), in its folder under libs_dir."""

    def __init__(self, lib, libs_dir):
        self.lib, self.spec = lib, L.listed_part(lib)
        self.root = Path(libs_dir) / lib
        self.files = {p: size for p, size, _ in L.listed(lib, self.spec)}
        self.dirs = {}
        for p in self.files:
            parts = p.split("/")
            for i in range(len(parts)):
                self.dirs.setdefault("/".join(parts[:i]), set()).add(parts[i])
        self.parsed = {}

    def exists(self, p):
        return p in self.files or p in self.dirs

    def listdir(self, d):
        return sorted(self.dirs.get(d, ()))

    def disk(self, p):
        return self.root / L.disk_path(self.spec, p)

    def there(self, p):
        return L._exists(self.disk(p), self.files[p])

    def read(self, p, fetch):
        """A definition file's bytes (fetched first if it isn't there); None if it isn't listed
        (sfizz, opening it as written, wouldn't find it either)."""
        if p not in self.files:
            return None
        if not self.there(p):
            fetch([p])
        return self.disk(p).read_bytes()

    def instrument(self, entry, fetch):
        """sfz.deps() of an instrument of this library (read once)."""
        if entry not in self.parsed:
            self.parsed[entry] = sfz.deps(entry, lambda p: self.read(p, fetch))
        return self.parsed[entry]

    def needs(self, entry, notes, fetch):
        """The listed files an instrument needs to play the note-ons `notes` ((key, velocity)):
        its definition files and the samples sfz.note_samples gives, as sfizz finds them."""
        res = self.instrument(entry, fetch)
        out = {f for f in res.files if f in self.files}
        for s in sfz.note_samples(res, notes):
            hit = sfz.find(s, self.exists, self.listdir)
            if hit in self.files:
                out.add(hit)
            else:  # not in the list: make_manifest.py would have said so
                print(f"    {self.lib}: {entry}: sample {s} is not in tgmusic/data/samples.tsv", flush=True)
        return out


def planned_notes(tracks):
    """{"<library>:<instrument>": set of (key, velocity)} of the .sfz instruments the planned
    tracks (plan.plan_job results) load: their parts' note-ons and what measuring one missing
    from the shipped tables plays."""
    out = {}
    for t in tracks:
        for p in t["parts"]:
            if p["type"] == "sfz":
                out.setdefault(p["instrument"], set()).update((k, v) for k, v, _ in p["on"])
        for p in t.get("probes", ()):
            if p.get("type") == "sfz":
                out.setdefault(p["instrument"], set()).update((k, v) for k, v, _ in p["on"])
    return out


def wanted(tracks, libs_dir, progress=None):
    """{library: set of paths in its source} the planned tracks need of the by_note libraries:
    each instrument's definition files and the samples its notes can play, and the library's
    licence and readme files. The definition files are fetched first, all at once (they are
    small: about 0.5 MB for all), to be read."""
    by_lib = {}
    for inst, notes in sorted(planned_notes(tracks).items()):
        lib, _, entry = inst.partition(":")
        if L.LIBRARIES.get(lib, {}).get("by_note"):
            by_lib.setdefault(lib, {})[entry] = notes
    out = {}
    for lib, insts in by_lib.items():
        lb = Library(lib, libs_dir)

        def fetch(paths, lib=lib):
            return L.fetch_files(lib, paths, libs_dir, progress)
        fetch(_definitions(lb))
        want = set()
        for entry, notes in insts.items():
            want |= lb.needs(entry, notes, fetch)
        out[lib] = want | L.docs(lib, want)
    return out


def definitions_missing(tracks, libs_dir):
    """The bytes of instrument definition files that wanted() would fetch first."""
    out = 0
    for lib in {inst.partition(":")[0] for inst in planned_notes(tracks)}:
        if L.LIBRARIES.get(lib, {}).get("by_note"):
            lb = Library(lib, libs_dir)
            out += sum(lb.files[p] for p in _definitions(lb) if not lb.there(p))
    return out


# --- The plans of a build, kept in the work folder -----------------------------------------

def plan_key(work, libs_dir, shipped_only):
    """What a track's plan depends on: the generator's code and tables, the notes from the
    ROM (<work>/events), and (unless shipped_only) the values measured on this computer."""
    h = hashlib.sha256(b"plan 1\n" + (b"shipped\n" if shipped_only else b"local\n"))
    here = Path(__file__).resolve().parent
    files = sorted(here.glob("*.py")) + sorted((here / "data").glob("*.json")) + sorted((Path(work) / "events").glob("*.json"))
    if not shipped_only:
        files += sorted((Path(libs_dir) / "calibration").glob("*.json"))
    for p in files:
        h.update(p.name.encode() + b"\n" + p.read_bytes() + b"\n")
    return h.hexdigest()


class PlanStore:
    """The plans of a build's tracks, one file each in `folder` (<work>/plan), so that a build
    stopped while planning (or rendering) plans only what it hasn't yet. Plans made under
    another key (plan_key) are dropped."""

    def __init__(self, folder, key):
        self.folder = Path(folder)
        try:
            same = (self.folder / "key.txt").read_text() == key
        except OSError:
            same = False
        if not same:
            shutil.rmtree(self.folder, ignore_errors=True)
            self.folder.mkdir(parents=True, exist_ok=True)
            (self.folder / "key.txt").write_text(key)

    def _path(self, job):
        return self.folder / (hashlib.sha256(json.dumps(job).encode()).hexdigest()[:32] + ".json")

    def get(self, job):
        try:
            res = json.loads(self._path(job).read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return None
        return res if res.get("job") == json.loads(json.dumps(job)) and not res.get("error") else None

    def put(self, job, res):
        p = self._path(job)
        tmp = p.with_name(p.name + f".{os.getpid()}.tmp")
        tmp.write_text(json.dumps(res), encoding="utf-8")
        os.replace(tmp, p)


# --- A render process: the safety net and the instrument's folder --------------------------

def _library(lib):
    if lib not in _LIBS:
        _LIBS[lib] = Library(lib, config.libs_dir())
    return _LIBS[lib]


def _safety(lib, paths):
    """Fetch what the plan didn't foresee (logged: it shouldn't happen)."""
    got = L.fetch_files(lib, paths, config.libs_dir())
    for p, size in got:
        FETCHED.append((lib, p, size))
        print(f"    safety net: fetched {lib}: {p} ({size / 1e6:.1f} MB), which the plan didn't foresee", flush=True)
    return got


def where(path):
    """(library, instrument path in it) of an .sfz file of a by_note library, else None."""
    try:
        rel = Path(path).relative_to(config.libs_dir()).as_posix()
    except ValueError:
        return None
    lib, _, entry = rel.partition("/")
    return (lib, entry) if L.LIBRARIES.get(lib, {}).get("by_note") and entry else None


def needed(path, notes):
    """The listed files the instrument at `path` needs for the note-ons `notes` (None: not a
    by_note library's)."""
    w = where(path)
    if w is None:
        return None
    lib, entry = w
    return _library(lib).needs(entry, notes, lambda ps: _safety(lib, ps))


class _Loading:
    """What sfizz loads for one instrument (prepare()): .root, the .sfz to load, which reads
    every sample whole into memory; .files, the listed files it loads (None: not a by_note
    library's). Cleaned up when the `with` ends (sfizz has read everything by then)."""

    def __init__(self, root, files, remove):
        self.root, self.files, self._remove = root, files, remove

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        for p in self._remove:
            if p.is_dir():
                shutil.rmtree(p, ignore_errors=True)
            else:
                p.unlink(missing_ok=True)
        return False


def _ram(folder, include, tag):
    """A small .sfz in `folder` that loads `include` (in the same folder, so its paths read the
    same) with every sample read whole into memory."""
    w = Path(folder) / f".{tag}.ram.sfz"
    w.write_text(f'<control> hint_ram_based=1\n#include "{include}"\n', encoding="utf-8")
    return w


def prepare(path, notes):
    """Before sfizz loads the instrument at `path` to play the note-ons `notes`: every file
    they need is there (the safety net fetches a missing one), and sfizz gets a folder of links
    to just those (libs/.views/<process>-<n>), with every sample read whole into memory. Where
    links can't be made (a drive without them), or with TGMUSIC_SFZ_WHOLE=1 (for checking that
    the render is that of a full download), the instrument's own folder, all its samples there
    read into memory."""
    path = Path(path)
    tag = f"{os.getpid()}-{next(_COUNT)}"
    w = where(path)
    if w is None:
        return _Loading(_ram(path.parent, path.name, tag), None, [path.parent / f".{tag}.ram.sfz"])
    lib, entry = w
    lb = _library(lib)
    need = lb.needs(entry, notes, lambda ps: _safety(lib, ps))
    missing = [p for p in need if not lb.there(p)]
    if missing:
        _safety(lib, missing)
    view = config.libs_dir() / ".views" / tag
    try:
        if os.environ.get("TGMUSIC_SFZ_WHOLE") == "1":  # checking: every sample there, as a full download
            raise OSError("TGMUSIC_SFZ_WHOLE")
        for p in need:
            dst = view / lib / L.disk_path(lb.spec, p)
            dst.parent.mkdir(parents=True, exist_ok=True)
            os.link(L.long_path(lb.disk(p)), L.long_path(dst))
        root = view / lib / entry
        return _Loading(_ram(root.parent, root.name, tag), need, [view])
    except OSError:
        shutil.rmtree(view, ignore_errors=True)
    every = {f for f in (sfz.find(s, lb.exists, lb.listdir) for s in lb.instrument(entry, None).samples)
             if f in lb.files and lb.there(f)}
    return _Loading(_ram(path.parent, path.name, tag), every | need, [path.parent / f".{tag}.ram.sfz"])


def clear_views(libs_dir):
    """The link folders of render processes that ended without removing theirs."""
    shutil.rmtree(Path(libs_dir) / ".views", ignore_errors=True)
