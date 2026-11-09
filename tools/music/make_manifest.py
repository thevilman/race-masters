#!/usr/bin/env python3
"""Make tgmusic/data/samples.tsv, the files of each sample library the generator plays (for
maintainers: after changing an instrument in the code or a library's pin).

    python3 tools/music/make_manifest.py --cache DIR

1. The instruments the code can load: tgmusic/catalog.py (a scan of the generator's source).
2. Each library's files at its pin (tgmusic/libraries.py), names and sizes, without
   downloading them: a repository's tree from GitHub's API (git/trees/<commit>?recursive=1,
   without a token), a release zip's central directory (HTTP range requests). Kept in
   DIR/api, so running again asks nothing.
3. Each .sfz, and what it includes, read as sfizz reads it (tgmusic/sfz.py): the samples it
   plays, each looked up in the listing (as sfizz looks: a case difference is reported).
   Samples only regions needing a controller value play are left out (sfz.playable(): the
   generator sends no controller message, e.g. a piano's sustain-pedal samples).
4. The libraries' licence, readme and credit files: those in the library's top folder or in
   a folder holding a file played.
5. Every file fetched once (with the generator's own downloader) into DIR/libs/<library>/,
   the layout the generator reads, to take its SHA-256 (so `generate.py --cache DIR` finds
   them in place). Files there already are reused; so is the SHA-256 of a file the list
   already has at the same source and size (not fetched again).

Writes the list, then prints for each library its files and size, and the longest path.
Standard library only.
"""
import argparse
import hashlib
import json
import posixpath
import re
import sys
import urllib.request
from pathlib import Path

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from tgmusic import catalog  # noqa: E402
from tgmusic import libraries as L  # noqa: E402
from tgmusic import sfz  # noqa: E402

API = "https://api.github.com"
DOC = re.compile(r"(licen[cs]e|copying|readme|credits?|authors|notice|attribution)([ ._-].*)?", re.I)
DOC_MAX = 1 << 20  # a licence or readme bigger than this is something else (a manual, a picture)
HEADER = """\
# The files of the sample libraries the generator plays, and their licence, readme and credit
# files: only these are downloaded (tgmusic/libraries.py). Made by tools/music/make_manifest.py
# from the generator's code (tgmusic/catalog.py) and the pins in libraries.py; do not edit.
# "@<library>  <source>" (a repository at a commit, or a release zip), then one line a file:
# sha256, size in bytes, path in the source. Tab-separated.
"""


def listing(spec, cache):
    """{path: size} of every file of a listed spec's source (cached in cache/api)."""
    api = cache / "api"
    api.mkdir(parents=True, exist_ok=True)
    if "repo" in spec:
        f = api / f"{spec['repo'].replace('/', '_')}@{spec['commit']}.tree.json"
        if not f.exists():
            url = f"{API}/repos/{spec['repo']}/git/trees/{spec['commit']}?recursive=1"
            print(f"  listing {url}", flush=True)
            req = urllib.request.Request(url, headers={"User-Agent": L.UA, "Accept": "application/vnd.github+json"})
            with urllib.request.urlopen(req, timeout=60) as r:
                f.write_bytes(r.read())
        d = json.loads(f.read_text())
        if d.get("truncated"):
            raise SystemExit(f"GitHub's listing of {spec['repo']} is truncated")
        return {e["path"]: e["size"] for e in d["tree"] if e["type"] == "blob"}
    f = api / (spec["zip"].rsplit("/", 1)[-1] + ".members.json")
    if not f.exists():
        print(f"  listing {spec['zip']}", flush=True)
        members, _ = L.zip_members(spec["zip"], spec["bytes"])
        f.write_text(json.dumps({n: i.file_size for n, i in members.items() if not i.is_dir()}, indent=0))
    return json.loads(f.read_text())


def source_path(spec, disk):
    """Where a file of the library's folder comes from in its source (the reverse of the map)."""
    for src, dst in spec.get("map", {}).items():
        if disk.startswith(dst):
            return src + disk[len(dst):]
    return disk


class Library:
    """One listed library: its source's files, and fetching them into cache/libs/<lib>."""

    def __init__(self, lib, spec, cache):
        self.lib, self.spec, self.cache = lib, spec, cache
        self.sizes = listing(spec, cache)
        self.dirs = {}  # folder -> names in it
        for p in self.sizes:
            parts = p.split("/")
            for i in range(len(parts)):
                self.dirs.setdefault("/".join(parts[:i]), set()).add(parts[i])
        self.members = None

    def exists(self, p):
        return p in self.sizes or p in self.dirs

    def listdir(self, d):
        return sorted(self.dirs.get(d, ()))

    def dest(self, path):
        return self.cache / "libs" / self.lib / L.disk_path(self.spec, path)

    def fetch(self, paths):
        """Fetch the files not in place yet (sizes checked) into cache/libs."""
        todo = [p for p in paths if not (self.dest(p).is_file() and self.dest(p).stat().st_size == self.sizes[p])]
        if not todo:
            return
        print(f"    fetching {len(todo)} files of {self.lib} ({sum(self.sizes[p] for p in todo) / 1e6:.0f} MB)", flush=True)
        if "repo" in self.spec:
            jobs = [lambda stop, count, p=p: L._get_raw(L.raw_url(self.spec, p), self.dest(p), self.sizes[p], None, stop,
                                                         count) for p in todo]
        else:
            if self.members is None:
                self.members, self.cd = L.zip_members(self.spec["zip"], self.spec["bytes"])
            dests = {p: (self.dest(p), self.sizes[p], None) for p in todo}
            runs = L._zip_runs(self.members, self.cd, [self.members[p] for p in todo])
            jobs = [lambda stop, count, run=run: L._get_zip_run(self.spec["zip"], run, dests, stop, count) for run in runs]
        L._parallel(jobs, None, lambda got: None)

    def read(self, p):
        if p not in self.sizes:
            return None
        self.fetch([p])
        return self.dest(p).read_bytes()


def resolve(lb, entries):
    """The source paths an instrument library needs for its entry files, the errors found
    (a file missing) and notes (what sfizz warns about too; case differences it gets over)."""
    need, problems, notes, crossed = set(), [], [], set()
    for disk in entries:
        entry = source_path(lb.spec, disk)
        if entry not in lb.sizes:
            problems.append(f"{lb.lib}: {entry} (named in the code) is not in the source")
            continue
        need.add(entry)
        if not entry.lower().endswith(".sfz"):
            continue

        def read(p, entry=entry):
            if p in lb.sizes:
                return lb.read(p)
            hit = sfz.find(p, lb.exists, lb.listdir)
            if hit is not None and hit in lb.sizes:
                problems.append(f"{lb.lib}: {entry} includes {p}, which is {hit} (Linux: sfizz won't open it)")
                return lb.read(hit)
            return None
        res = sfz.deps(entry, read)
        need.update(res.files)
        crossed.update(res.crossed)
        problems += [f"{lb.lib}: {m}" for m in res.missing]
        notes += [f"{lb.lib}: {m}" for m in res.problems]
        samples = sfz.playable(res)
        out = set(res.samples) - set(samples)
        if out:
            size = sum(lb.sizes.get(sfz.find(s, lb.exists, lb.listdir) or s, 0) for s in out)
            notes.append(f"{lb.lib}: {entry}: {len(out)} samples left out ({size / 1e6:.0f} MB): only regions "
                         "needing a controller value never sent play them")
        for s in samples:
            hit = sfz.find(s, lb.exists, lb.listdir)
            if hit is None or hit not in lb.sizes:
                problems.append(f"{lb.lib}: {entry}: sample {s} is not in the source")
                continue
            if hit != s:
                notes.append(f"{lb.lib}: {entry}: sample {s} is {hit} (sfizz finds it, ignoring case)")
            need.add(hit)
    # A '..' steps out of a folder: it must be there (it is if it holds a file played).
    dirs = {posixpath.dirname(p) for p in need}
    dirs = {d for p in dirs for d in [p] + [p.rsplit("/", k)[0] for k in range(1, p.count("/") + 1)]}
    for d in sorted(crossed):
        real = sfz.find(d, lb.exists, lb.listdir) if d else d
        if real not in dirs and real != "":
            problems.append(f"{lb.lib}: a path steps out of {d}/ with '..', which holds no file played")
    # The licence, readme and credit files of the folders involved.
    for p, size in lb.sizes.items():
        folder, name = posixpath.split(p)
        if DOC.fullmatch(name) and size <= DOC_MAX and (folder == "" or folder in dirs):
            need.add(p)
    # Two files whose names differ only in case can't both be on Windows.
    seen = {}
    for p in sorted(need):
        k = L.disk_path(lb.spec, p).lower()
        if k in seen:
            problems.append(f"{lb.lib}: {seen[k]} and {p} differ only in case (one file on Windows)")
        seen[k] = p
    return need, problems, notes


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cache", required=True, help="where the listings and the files fetched go (DIR/api, DIR/libs)")
    ap.add_argument("--out", default=str(L.SAMPLES), help="the list to write (default tgmusic/data/samples.tsv)")
    args = ap.parse_args()
    cache = Path(args.cache).resolve()
    entries, unknown = catalog.instrument_files()
    if unknown:
        raise SystemExit("tgmusic/catalog.py does not understand these instrument names:\n" +
                         "\n".join(f"  {w}: {t}" for w, t in unknown))
    lines, problems, notes, report = [HEADER.rstrip("\n")], [], [], []
    # The SHA-256 the list has already ({(source, path): (size, sha256)}): not fetched again.
    known = {}
    if Path(args.out).exists():
        for lib, m in L.manifest(Path(args.out)).items():
            known.update({(m["source"], p): (size, sha) for p, size, sha in m["files"]})
    for lib, spec in L.LIBRARIES.items():
        listed = [f for f in spec["files"] if L.is_listed(f)]
        if not listed:
            for disk in entries.get(lib, []):
                names = {f.get("name") or f["url"].rsplit("/", 1)[-1] for f in L.files(lib) or []}
                if disk not in names:
                    problems.append(f"{lib}: {disk} (named in the code) is not one of its downloads")
            continue
        (f,) = listed
        print(f"{lib}: {L.source_of(f)}", flush=True)
        lb = Library(lib, f, cache)
        need, probs, nts = resolve(lb, entries.get(lib, []))
        problems += probs
        notes += nts
        src = L.source_of(f)
        reuse = {p: known[(src, p)][1] for p in need if known.get((src, p), (None,))[0] == lb.sizes[p]}
        lb.fetch(sorted(p for p in need if p not in reuse))
        lines.append(f"@{lib}\t{src}")
        total = 0
        for p in sorted(need):
            if p in reuse:
                lines.append(f"{reuse[p]}\t{lb.sizes[p]}\t{p}")
                total += lb.sizes[p]
                continue
            data = lb.dest(p).read_bytes()
            if len(data) != lb.sizes[p]:
                raise SystemExit(f"{lib}: {p} is {len(data)} bytes, the listing says {lb.sizes[p]}")
            lines.append(f"{hashlib.sha256(data).hexdigest()}\t{len(data)}\t{p}")
            total += len(data)
        whole = sum(lb.sizes.values())
        longest = max((f"{lib}/{L.disk_path(f, p)}" for p in need), key=len)
        report.append((lib, len(entries.get(lib, [])), len(need), total, len(lb.sizes), whole, longest))
    Path(args.out).write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"\nwrote {args.out}\n")
    print(f"{'library':16s} {'entries':>7s} {'files':>6s} {'MB':>8s}   of the source: {'files':>6s} {'MB':>8s}")
    for lib, n_entries, n, total, n_all, whole, _ in report:
        print(f"{lib:16s} {n_entries:7d} {n:6d} {total / 1e6:8.1f}   {'':15s} {n_all:6d} {whole / 1e6:8.1f}")
    print(f"{'all':16s} {'':7s} {sum(r[2] for r in report):6d} {sum(r[3] for r in report) / 1e6:8.1f}")
    longest = max((r[6] for r in report), key=len)
    print(f"\nlongest path under <libs>: {len(longest)} characters: {longest}")
    for title, items in (("Notes (as sfizz reads them; no file missing)", notes), ("PROBLEMS", problems)):
        if items:
            print(f"\n{title}:")
            for p in items:
                print("  " + p)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
