"""The free instrument libraries and effects the generator plays the notes on.

Each one is downloaded from its official source at a pinned version into the cache folder
(never into the repository), checked against pinned SHA-256s, into <libs>/<id>/. Nothing here
is redistributed: the libraries are used only to render audio on the player's own computer.
Licences and what they allow: tools/music/THIRD_PARTY.md.

Two kinds of download:
  * The sample libraries (and Surge XT's patches): only the files the generator can play, listed
    with their size and SHA-256 in data/samples.tsv (made by tools/music/make_manifest.py from
    the generator's code, tgmusic/catalog.py): the most a build downloads. Each is fetched on
    its own: from the library's GitHub repository at the pinned commit
    (raw.githubusercontent.com), or out of a pinned release zip with HTTP range requests (the
    whole zip only if the server can't send parts). Of the libraries marked by_note, a build
    fetches only the files the notes it renders can play (fetch(want=...), worked out by
    needs.py from the plan of the tracks); the listed files a build fetched stay for the next.
  * The plug-ins and the SoundFont: whole files, as before. An archive is checked by
        sha256       of the downloaded file (release assets and raw files are byte-stable), or
        tree_sha256  of the unpacked files (sha256 over the lines "<sha256 of file>  <relative
                     path>\\n" of every kept file, sorted by path), then unpacked.

Maintainers updating a pin: TGMUSIC_PIN=1 accepts an archive whose hashes are not pinned yet
(or differ) and prints the values to pin; for a sample library change its commit (or zip)
here, then run make_manifest.py.
"""
import concurrent.futures
import hashlib
import http.client
import json
import os
import platform
import posixpath
import re
import shutil
import ssl
import struct
import sys
import tarfile
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import zipfile
import zlib
from pathlib import Path

from . import progress as P

GH = "https://github.com"
RAW = "https://raw.githubusercontent.com"
SAMPLES = Path(__file__).resolve().parent / "data" / "samples.tsv"

LIBRARIES = {
    "surge-xt": dict(
        name="Surge XT 1.3.4 (synthesizer plug-in and the factory patches used)",
        licence="GPL-3.0-or-later", homepage="https://surge-synthesizer.github.io",
        # The plug-in's build for the computer it runs on, then the patches played (the same
        # files for every system), from the source repository at the release's tag
        # (release_xt_1.3.4; byte for byte the files of the release's 253 MB content pack).
        platform_files={
            "linux-x86_64": [
                dict(url=f"{GH}/surge-synthesizer/releases-xt/releases/download/1.3.4/surge-xt-linux-1.3.4-pluginsonly.tar.gz",
                     sha256="dd431b75f5fa197c4bffa6ca27ca46970f0a94c834119bb1db7decdeec4c28db", strip=0,
                     keep=["Surge XT.vst3"], mb=97),
            ],
            "win32-x86_64": [
                dict(url=f"{GH}/surge-synthesizer/releases-xt/releases/download/1.3.4/surge-xt-win64-1.3.4-pluginsonly.zip",
                     sha256="564e162c560af07ad4ed47fe1bfcd827cf97a575de30d06c48249aad2e7c35e6", strip=0,
                     keep=["Surge XT.vst3"], mb=49),
            ],
        },
        files=[dict(repo="surge-synthesizer/surge", commit="f7b97c682ade0b87da85ca5968b63d5c7c98e68d",
                    map={"resources/data/": "Surge Synth Team/SurgeXTData/"})]),
    "dragonfly-reverb": dict(
        name="Dragonfly Reverb 3.2.10 (reverb plug-ins)",
        licence="GPL-3.0-or-later", homepage="https://github.com/michaelwillis/dragonfly-reverb",
        platform_files={
            "linux-x86_64": [
                dict(url=f"{GH}/michaelwillis/dragonfly-reverb/releases/download/3.2.10/dragonfly-reverb-3.2.10-linux-x86_64.tar.xz",
                     sha256="59f1593120de82e31dd194e2ab1536af14fec2b3ec6ec45abf6d9c8ac02ef430", strip=1,
                     keep=["DragonflyHallReverb.vst3", "DragonflyPlateReverb.vst3", "DragonflyRoomReverb.vst3"], mb=3),
            ],
            "win32-x86_64": [
                dict(url=f"{GH}/michaelwillis/dragonfly-reverb/releases/download/3.2.10/dragonfly-reverb-3.2.10-win64.zip",
                     sha256="ca5f35f9dcd9a33f06490d3c92f04061a1a798cb81257d5ca5851bdda0824c66", strip=1,
                     keep=["DragonflyHallReverb.vst3", "DragonflyPlateReverb.vst3", "DragonflyRoomReverb.vst3"], mb=22),
            ],
        },
        files=[]),
    "generaluser-gs": dict(
        name="GeneralUser GS 2.0.3 (General MIDI SoundFont) by S. Christian Collins",
        licence="GeneralUser GS License v2.0 (free use in music, private or commercial)",
        homepage="https://www.schristiancollins.com/generaluser",
        files=[
            dict(url=f"{RAW}/mrbumpy409/GeneralUser-GS/684543d5e5efaef08d02be50dcda8d552478fa60/GeneralUser-GS.sf2",
                 sha256="9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe", name="GeneralUser-GS.sf2", mb=32),
            dict(url=f"{RAW}/mrbumpy409/GeneralUser-GS/684543d5e5efaef08d02be50dcda8d552478fa60/documentation/LICENSE.txt",
                 sha256="7b32efefdf95ce38a043799f0659853ddc00fbaa14d8c50f0aca16b9b8b405be", name="LICENSE.txt", mb=0),
        ]),
    "sso": dict(
        name="Sonatina Symphonic Orchestra (SFZ, peastman/sso at 32bbdb16)",
        licence="CC Sampling Plus 1.0", homepage="https://github.com/peastman/sso", by_note=True,
        files=[dict(repo="peastman/sso", commit="32bbdb169aef636b8216029a2e056424ba7c2abb")]),
    "big-rusty-drums": dict(
        name="Karoryfer Big Rusty Drums 1.100 (SFZ)",
        licence="CC0-1.0", homepage="https://github.com/sfzinstruments/karoryfer.big-rusty-drums", by_note=True,
        files=[dict(zip=f"{GH}/sfzinstruments/karoryfer.big-rusty-drums/releases/download/v1.100/Big_Rusty_Drums_1100.zip",
                    sha256="d4a9990acd19376d91ce446dae415c81428728b5adebb6a88eddbb3a6aac8744", bytes=619600370)]),
    "vsco2-ce": dict(
        name="VSCO 2 Community Edition (SFZ, at 6dd651d5) by Versilian Studios",
        licence="CC0-1.0", homepage="https://github.com/sgossner/VSCO-2-CE", by_note=True,
        files=[dict(repo="sgossner/VSCO-2-CE", commit="6dd651d55dde97fd4028699be9d4481f26917891")]),
    "vcsl": dict(
        name="Versilian Community Sample Library (SFZ, at dfcf4a49)",
        licence="CC0-1.0", homepage="https://github.com/sgossner/VCSL", by_note=True,
        files=[dict(repo="sgossner/VCSL", commit="dfcf4a4918771eee884b96ad4493de82ef84daf6")]),
    "mtg-solo-sax": dict(
        name="MTG Solo Saxophones (SFZ by kinwie from the MTG/freesound samples, at b494d256)",
        licence="CC-BY-4.0", homepage="https://github.com/sfzinstruments/MTG.SoloSax", by_note=True,
        files=[dict(repo="sfzinstruments/MTG.SoloSax", commit="b494d256549b3d088fdec176ce82867f8a1f58b2")]),
    "naked-drums": dict(
        name="Wilkinson Audio Naked Drums (SFZ by kinwie, at 407732a5)",
        licence="CC-BY-4.0", homepage="https://github.com/sfzinstruments/WilkinsonAudio.NakedDrums", by_note=True,
        files=[dict(repo="sfzinstruments/WilkinsonAudio.NakedDrums", commit="407732a548606ac715ad8f3fbac63d9f4df77c0d")]),
}

# What each style plays on (render.py, stages.py, personality.py, realism.py).
_FAITHFUL = ["surge-xt", "sso", "big-rusty-drums", "dragonfly-reverb"]
_BAND = ["generaluser-gs", "surge-xt", "dragonfly-reverb", "vsco2-ce", "vcsl", "mtg-solo-sax", "naked-drums"]
STYLE_LIBS = {
    "faithful": _FAITHFUL,
    "synthwave": ["surge-xt", "dragonfly-reverb"],
    "orchestral": ["sso", "dragonfly-reverb"],
    "A": _FAITHFUL + ["generaluser-gs"],
    "B": _FAITHFUL + ["generaluser-gs"],
    "C": _BAND,
    "D": _BAND,
}

# What a build of one style usually downloads of each by_note library, in MB (rounded up): the
# samples the notes of Top Gear (USA) can play there, measured with
#   generate.py --rom ROM --plan-notes FILE --styles all --yes
# (no notes, only these sizes). An estimate for the launcher to show ("usually about"); a build
# works out its own download from the notes it reads.
TYPICAL_MB = {"big-rusty-drums": {"faithful": 5, "A": 2, "B": 10}, "mtg-solo-sax": {"C": 54, "D": 54},
              "naked-drums": {"C": 134, "D": 134}, "sso": {"faithful": 3, "orchestral": 132, "A": 0, "B": 0},
              "vcsl": {"C": 323, "D": 313}, "vsco2-ce": {"C": 273, "D": 280}}

STAMP = ".tgmusic-ok"
# The listed files of a library in place and checked (their SHA-256 taken once): lines
# "<sha256>\t<size>\t<path in the source>". A build that wants them doesn't read them again.
CHECKED = ".tgmusic-files"
TMP = ".tgpart"  # a file being downloaded, beside where it goes (+ the process id in a render process)
# A library's licence, readme and credit files (make_manifest.py lists them; a build fetches
# those of the top folder and of the folders it fetches files of).
DOC = re.compile(r"(licen[cs]e|copying|readme|credits?|authors|notice|attribution)([ ._-].*)?", re.I)
DOC_MAX = 1 << 20  # a licence or readme bigger than this is something else (a manual, a picture)
UA = "race-masters-music-tools"  # a neutral User-Agent: no personal data in requests
WORKERS = 6  # files downloaded at once (measured: 7 MB/s with 4, 13 MB/s with 6)
TIMEOUT = 30  # seconds without data before a connection counts as lost
RETRY_S = (2, 5, 10, 20, 40)  # waits before trying a file again (network trouble, 429, 5xx)
BLOCK = 1 << 18


def host_platform():
    m = platform.machine().lower()
    m = {"amd64": "x86_64", "x64": "x86_64"}.get(m, m)
    return f"{sys.platform.replace('linux2', 'linux')}-{m}"


PLATFORM_NAMES = {"linux-x86_64": "Linux x86-64", "win32-x86_64": "Windows x64"}


# --- The list of files (data/samples.tsv) ------------------------------------------------------

_MANIFEST = {}


def manifest(path=None):
    """{library: {"source": "<owner>/<repo>@<commit>" or the zip's URL, "files": [(path, size,
    sha256)], "digest": sha256 of its lines}} from data/samples.tsv (or `path`). A file's path
    is its path in the repository or zip."""
    path = path or SAMPLES
    key = str(path)
    if key not in _MANIFEST:
        out, cur = {}, None
        for line in Path(path).read_text(encoding="utf-8").splitlines():
            if not line or line.startswith("#"):
                continue
            f = line.split("\t")
            if f[0].startswith("@"):
                cur = out[f[0][1:]] = {"source": f[1], "files": [], "digest": hashlib.sha256()}
            else:
                cur["files"].append((f[2], int(f[1]), f[0]))
            cur["digest"].update(line.encode() + b"\n")
        for v in out.values():
            v["digest"] = v["digest"].hexdigest()
        _MANIFEST[key] = out
    return _MANIFEST[key]


def is_listed(spec):
    """A spec whose files are those of data/samples.tsv (a repository or a zip)."""
    return "repo" in spec or "zip" in spec


def source_of(spec):
    return f"{spec['repo']}@{spec['commit']}" if "repo" in spec else spec["zip"]


def _listing(lib, spec):
    m = manifest().get(lib)
    if m is None or m["source"] != source_of(spec):
        raise RuntimeError(f"tgmusic/data/samples.tsv has no files of {lib} at {source_of(spec)}: run make_manifest.py")
    return m


def listed(lib, spec):
    """(path, size, sha256) of the files of a listed spec."""
    return _listing(lib, spec)["files"]


def disk_path(spec, path):
    """Where a listed file goes in the library's folder (spec's map: source prefix -> folder)."""
    for src, dst in spec.get("map", {}).items():
        if path.startswith(src):
            return dst + path[len(src):]
    return path


def raw_url(spec, path):
    return f"{RAW}/{spec['repo']}/{spec['commit']}/{urllib.parse.quote(path, safe='/')}"


def files(lib, plat=None):
    """The files of a library for a platform (default: this computer): its build of a plug-in
    first, then the files every system shares. None if the library has no build for it."""
    spec = LIBRARIES[lib]
    pf = spec.get("platform_files")
    if pf is None:
        return list(spec["files"])
    mine = pf.get(plat or host_platform())
    return None if mine is None else list(mine) + list(spec["files"])


def spec_mb(lib, f):
    if is_listed(f):
        return round(sum(size for _, size, _ in listed(lib, f)) / 1e6)
    return f.get("mb", 0)


def size_mb(lib, plat=None):
    """What a library downloads, in MB."""
    fs = files(lib, plat)
    return sum(spec_mb(lib, f) for f in fs) if fs else 0


def typical_mb(lib, styles, plat=None):
    """About what a build of these styles downloads of a library, in MB: of a by_note library
    the most one of the styles takes (TYPICAL_MB), of the others all of it."""
    t = TYPICAL_MB.get(lib)
    if not t:
        return size_mb(lib, plat)
    return min(size_mb(lib, plat), max([t.get(s, 0) for s in styles] or [0]))


def archive_mb(lib, plat=None):
    """The biggest archive of a library (it is kept while it unpacks), in MB."""
    return max([f.get("mb", 0) for f in files(lib, plat) or [] if "strip" in f] or [0])


def unsupported(libs, plat=None):
    """The libraries of `libs` with no build for this platform (empty: all fine)."""
    return [lib for lib in libs if files(lib, plat) is None]


def needed(styles):
    out = []
    for s in styles:
        for lib in STYLE_LIBS[s]:
            if lib not in out:
                out.append(lib)
    return out


def _part_id(lib, f):
    """What a library part is made from: its pins (for a listed part, also its list)."""
    if is_listed(f):
        return json.dumps({"source": source_of(f), "map": f.get("map"), "sha256": f.get("sha256"), "bytes": f.get("bytes"),
                           "files": _listing(lib, f)["digest"]}, sort_keys=True)
    return json.dumps({k: f.get(k) for k in ("url", "sha256", "tree_sha256", "strip", "keep", "name")}, sort_keys=True)


def _fingerprint(lib):
    # An archive part gives the same line as in earlier versions' stamps, so parts already in
    # place are kept (_parts_in_place).
    return json.dumps([json.loads(_part_id(lib, f)) for f in files(lib) or []], sort_keys=True)


def is_ready(libs_dir, lib, want=None):
    """Whether a library is in place: whole (want None), or with the listed files `want`
    (paths in the source; see fetch) and its other parts."""
    final = Path(libs_dir) / lib
    try:
        if (final / STAMP).read_text() == _fingerprint(lib):
            return True
        if want is None:
            return False
        in_place = _parts_in_place(final)
        for f in files(lib) or []:
            if not is_listed(f):
                if _part_id(lib, f) not in in_place:
                    return False
                continue
            if _part_id(lib, f) in in_place:
                continue
            checked = _checked(final)
            for path, size, sha in listed(lib, f):
                if path in want and (checked.get(path) != sha or long_path(final / disk_path(f, path)).stat().st_size != size):
                    return False
        return True
    except (OSError, RuntimeError):
        return False


def _checked(final):
    """{path in the source: sha256} of the listed files a fetch found in place and checked."""
    out = {}
    try:
        for line in (Path(final) / CHECKED).read_text(encoding="utf-8").splitlines():
            f = line.split("\t")
            if len(f) == 3:
                out[f[2]] = f[0]
    except OSError:
        pass
    return out


def _write_checked(final, entries):
    """CHECKED := entries [(path, size, sha256)], written under another name and renamed."""
    final = Path(final)
    tmp = final / (CHECKED + f".{os.getpid()}" + TMP)
    tmp.write_text("".join(f"{sha}\t{size}\t{p}\n" for p, size, sha in sorted(entries)), encoding="utf-8")
    os.replace(tmp, final / CHECKED)


def listed_part(lib):
    """The listed part of a library (its files in data/samples.tsv), or None."""
    return next((f for f in LIBRARIES[lib]["files"] if is_listed(f)), None)


def docs(lib, paths):
    """The listed licence, readme and credit files of a library that go with the files `paths`
    (paths in the source): those of its top folder and of the folders holding one of them."""
    spec = listed_part(lib)
    dirs = {""}
    for p in paths:
        d = posixpath.dirname(p)
        while d:
            dirs.add(d)
            d = posixpath.dirname(d)
    return {p for p, size, _ in listed(lib, spec) if DOC.fullmatch(posixpath.basename(p)) and size <= DOC_MAX
            and posixpath.dirname(p) in dirs}


def missing_bytes(libs_dir, lib, want=None, plat=None):
    """What fetch(lib, want=want) would download, in bytes (a listed file there of the right
    size counts as there; a whole-file part not in place counts its pinned size)."""
    final = Path(libs_dir) / lib
    try:
        if (final / STAMP).read_text() == _fingerprint(lib):
            return 0
    except (OSError, RuntimeError):
        pass
    in_place = _parts_in_place(final)
    out = 0
    for f in files(lib, plat) or []:
        if _part_id(lib, f) in in_place:
            continue
        if not is_listed(f):
            out += f.get("mb", 0) * 1e6
            continue
        for path, size, _ in listed(lib, f):
            if want is not None and path not in want:
                continue
            try:
                if long_path(final / disk_path(f, path)).stat().st_size == size:
                    continue
            except OSError:
                pass
            out += size
    return int(out)


def wanted_bytes(lib, want=None):
    """The bytes of a library's listed files `want` (all if None) and of its whole-file parts."""
    out = 0
    for f in files(lib) or []:
        if is_listed(f):
            out += sum(size for path, size, _ in listed(lib, f) if want is None or path in want)
        else:
            out += f.get("mb", 0) * 1e6
    return int(out)


def _parts_in_place(final):
    """The parts an earlier (complete) fetch of this library put in place, from its stamp."""
    try:
        return {json.dumps(p, sort_keys=True) for p in json.loads((final / STAMP).read_text())}
    except (OSError, ValueError, TypeError):
        return set()


def describe(libs):
    lines = []
    for lib in libs:
        s = LIBRARIES[lib]
        lines.append(f"  - {s['name']}\n      licence: {s['licence']}\n      source:  {s['homepage']}"
                     f"  (download about {size_mb(lib)} MB)")
    return "\n".join(lines)


# --- Paths ---------------------------------------------------------------------------------

def long_path(p):
    """p as Windows' file functions take it when it is long: a path of 260 characters or more
    fails there unless the system allows long paths, or it is written \\\\?\\C:\\... (absolute,
    backslashes). Elsewhere (and when short) p itself. (With the default folders the longest
    path of the libraries is about 190 characters: this is for a deeper cache folder.)"""
    if os.name != "nt":
        return Path(p)
    return Path(win_long(os.path.abspath(str(p))))


def win_long(s):
    """An absolute Windows path in its \\\\?\\ form if it is 240 characters or more."""
    if len(s) < 240 or s.startswith("\\\\?\\"):
        return s
    return "\\\\?\\UNC\\" + s[2:] if s.startswith("\\\\") else "\\\\?\\" + s


def _sha256_file(path, bufsize=1 << 20):
    h = hashlib.sha256()
    with open(long_path(path), "rb") as f:
        for b in iter(lambda: f.read(bufsize), b""):
            h.update(b)
    return h.hexdigest()


def tree_sha256(root):
    root = Path(root)
    files_ = sorted((p.relative_to(root).as_posix(), p) for p in root.rglob("*") if p.is_file() and p.name != STAMP)
    h = hashlib.sha256()
    for rel, p in files_:
        h.update(f"{_sha256_file(p)}  {rel}\n".encode())
    return h.hexdigest()


def _check(progress):
    if progress is not None:
        progress.check()


# --- Whole files (the plug-ins, the SoundFont, a zip without ranges) -------------------------

def _download(url, dest, progress=None, on_bytes=None):
    try:
        return _download_once(url, dest, progress, on_bytes)
    except urllib.error.HTTPError as e:
        if e.code != 416:  # 416: the part kept is not a beginning of this file: start again
            raise
        dest.with_name(dest.name + ".part").unlink(missing_ok=True)
        dest.with_name(dest.name + ".part.json").unlink(missing_ok=True)
        return _download_once(url, dest, progress, on_bytes)


def _download_once(url, dest, progress=None, on_bytes=None):
    """url -> dest (via dest.part); returns the file's sha256.

    A stopped download goes on from where it stopped next time: dest.part is kept, with the
    server's validator (ETag / Last-Modified) in dest.part.json, and the rest is asked for
    with Range + If-Range. A server that sends the whole file instead (or whose file changed)
    starts it again. on_bytes(got, total) after each block; progress.check() between blocks
    raises Cancelled (the part is kept)."""
    dest.parent.mkdir(parents=True, exist_ok=True)
    part = dest.with_name(dest.name + ".part")
    meta = dest.with_name(dest.name + ".part.json")
    h = hashlib.sha256()
    have = part.stat().st_size if part.exists() else 0
    validator = None
    if have:
        try:
            validator = json.loads(meta.read_text()).get("validator")
        except (OSError, ValueError):
            validator = None
        if not validator:
            have = 0
    headers = {"User-Agent": UA}
    if have:
        headers["Range"] = f"bytes={have}-"
        headers["If-Range"] = validator
    t0 = time.time()
    req = urllib.request.Request(url, headers=headers)
    with urllib.request.urlopen(req, timeout=60) as r:
        resumed = have and r.status == 206
        if resumed:
            # Hash what is already there, then append.
            with open(part, "rb") as f:
                for b in iter(lambda: f.read(1 << 20), b""):
                    h.update(b)
            mode, got = "ab", have
            rng = r.headers.get("Content-Range", "")
            total = int(rng.rsplit("/", 1)[-1]) if "/" in rng and not rng.endswith("*") else 0
        else:
            mode, got = "wb", 0
            total = int(r.headers.get("Content-Length") or 0)
            v = r.headers.get("ETag") or r.headers.get("Last-Modified")
            if v:
                meta.write_text(json.dumps({"url": url, "validator": v}))
            else:
                meta.unlink(missing_ok=True)
        shown = got
        with open(part, mode) as f:
            while True:
                _check(progress)
                b = r.read(1 << 20)
                if not b:
                    break
                f.write(b)
                h.update(b)
                got += len(b)
                if on_bytes:
                    on_bytes(got, total)
                if progress is None or not progress.json:
                    if got - shown >= (64 << 20):
                        shown = got
                        pct = f" of {total / 1e6:.0f}" if total else ""
                        rate = (got - (have if resumed else 0)) / 1e6 / max(time.time() - t0, 1e-3)
                        print(f"      {got / 1e6:.0f}{pct} MB ({rate:.1f} MB/s)", flush=True)
    if total and got != total:
        raise OSError(f"the download of {url} stopped at {got} of {total} bytes")
    os.replace(part, dest)
    meta.unlink(missing_ok=True)
    return h.hexdigest()


def _members(names, strip, keep):
    """(member name, relative path) for the archive members to unpack."""
    for n in names:
        parts = [p for p in n.replace("\\", "/").split("/") if p not in ("", ".")]
        if len(parts) <= strip:
            continue
        rel = "/".join(parts[strip:])
        if any(p == ".." for p in parts) or rel.startswith("/") or ":" in parts[0]:
            raise RuntimeError(f"unsafe path in archive: {n}")
        if keep is not None and not any(rel == k or rel.startswith(k) for k in keep):
            continue
        yield n, rel


def _unpack(archive, dest, strip, keep, progress=None):
    dest = Path(dest)
    name = archive.name
    if name.endswith(".zip"):
        with zipfile.ZipFile(archive) as z:
            infos = {i.filename: i for i in z.infolist()}
            for n, rel in _members(infos, strip, keep):
                _check(progress)
                if infos[n].is_dir():
                    continue
                out = dest / rel
                out.parent.mkdir(parents=True, exist_ok=True)
                with z.open(infos[n]) as src, open(out, "wb") as dst:
                    shutil.copyfileobj(src, dst, 1 << 20)
    elif name.endswith((".tar.gz", ".tgz", ".tar.xz")):
        with tarfile.open(archive) as t:
            infos = {m.name: m for m in t.getmembers()}
            for n, rel in _members(infos, strip, keep):
                _check(progress)
                m = infos[n]
                if m.isdir():
                    continue
                if not m.isfile():
                    raise RuntimeError(f"unexpected link or device in archive: {n}")
                out = dest / rel
                out.parent.mkdir(parents=True, exist_ok=True)
                with t.extractfile(m) as src, open(out, "wb") as dst:
                    shutil.copyfileobj(src, dst, 1 << 20)
                if m.mode & 0o111:
                    out.chmod(0o755)
    else:
        raise RuntimeError(f"unknown archive type: {name}")


class HashMismatch(SystemExit):
    """A download whose bytes and contents both differ from the pinned values."""


# --- Listed files, one by one ----------------------------------------------------------------

class _Short(OSError):
    """A response that ended early, or a file of the wrong size: worth trying again."""


class NoRanges(Exception):
    """The server sends whole files only (it ignores Range)."""


def _transient(e):
    """Network trouble worth trying again for (not a missing file or a full disk)."""
    if isinstance(e, urllib.error.HTTPError):
        return e.code in (408, 425, 429) or e.code >= 500
    return isinstance(e, (_Short, urllib.error.URLError, ConnectionError, TimeoutError, http.client.HTTPException,
                          ssl.SSLError))


def _retrying(fn, stop, what):
    """fn() until it works, waiting RETRY_S between tries on network trouble (or what the
    server asks: Retry-After). A stop (cancel) during a wait raises Cancelled."""
    for attempt in range(len(RETRY_S) + 1):
        try:
            return fn()
        except BaseException as e:  # noqa: BLE001 - re-raised unless worth another try
            if not _transient(e) or attempt == len(RETRY_S):
                raise
            wait = RETRY_S[attempt]
            after = getattr(e, "headers", None) and e.headers.get("Retry-After")
            if after and after.strip().isdigit():
                wait = min(max(wait, int(after)), 120)
            print(f"    {what}: {e}; trying again in {wait} s", flush=True)
            if stop.wait(wait):
                raise P.Cancelled() from e


def _open(url, start=None, end=None):
    """A GET of url (bytes start..end-1 if given; NoRanges if the server sends it all)."""
    headers = {"User-Agent": UA}
    if start is not None:
        headers["Range"] = f"bytes={start}-{end - 1}"
    try:
        r = urllib.request.urlopen(urllib.request.Request(url, headers=headers), timeout=TIMEOUT)
    except urllib.error.HTTPError as e:
        e.msg = f"{e.msg} ({url})"  # say which file
        raise
    if start is not None and r.status != 206:
        r.close()
        raise NoRanges(url)
    return r


def _read_exact(r, n, stop):
    out = bytearray()
    while len(out) < n:
        if stop.is_set():
            raise P.Cancelled()
        b = r.read(min(BLOCK, n - len(out)))
        if not b:
            raise _Short("the connection closed early")
        out += b
    return bytes(out)


class _Writer:
    """One listed file being written: to dest.<process id> + TMP (so render processes fetching
    the same file at once don't share one), checked, then renamed to dest (sha None: not
    checked, for make_manifest.py, which takes it). Should the rename fail because another
    process has just put the same file there (Windows can't replace a file in use), that file
    is kept once it checks."""

    def __init__(self, dest, size, sha, count):
        self.dest, self.size, self.sha, self.count = long_path(dest), size, sha, count
        self.tmp = self.dest.with_name(f"{self.dest.name}.{os.getpid()}{TMP}")
        self.dest.parent.mkdir(parents=True, exist_ok=True)
        self.f = open(self.tmp, "wb")
        self.h, self.got = hashlib.sha256(), 0

    def write(self, b):
        self.f.write(b)
        self.h.update(b)
        self.got += len(b)
        self.count(len(b))

    def finish(self, what):
        self.f.close()
        if self.got != self.size:
            raise _Short(f"{what}: {self.got} bytes, not {self.size}")
        if self.sha is not None and self.h.hexdigest() != self.sha:
            raise HashMismatch(f"{what}: sha256 {self.h.hexdigest()} does not match the pinned {self.sha} "
                               "(not kept; try again, or report it)")
        try:
            os.replace(self.tmp, self.dest)
        except OSError:
            if not _file_ok(self.dest, self.size, self.sha):
                raise
            self.tmp.unlink(missing_ok=True)

    def drop(self):
        self.f.close()
        self.count(-self.got)
        self.tmp.unlink(missing_ok=True)


def _file_ok(path, size, sha):
    """Whether a file is there with this size and (unless sha is None) this sha256."""
    try:
        return long_path(path).stat().st_size == size and (sha is None or _sha256_file(path) == sha)
    except OSError:
        return False


def _get_raw(url, dest, size, sha, stop, count):
    """One file of a repository -> dest, checked against its size and sha256."""
    def once():
        w = _Writer(dest, size, sha, count)
        try:
            with _open(url) as r:
                while True:
                    if stop.is_set():
                        raise P.Cancelled()
                    b = r.read(BLOCK)
                    if not b:
                        break
                    w.write(b)
            w.finish(url)
        except BaseException:
            w.drop()
            raise
    _retrying(once, stop, url.rsplit("/", 1)[-1])


class _ZipIndex:
    """A zip's members read from its central directory with range requests (zipfile does the
    reading; this file object fetches only the bytes it asks for)."""

    def __init__(self, url, size, stop):
        self.url, self.size, self.pos, self.stop = url, size, 0, stop

    def seek(self, off, whence=0):
        self.pos = off if whence == 0 else self.pos + off if whence == 1 else self.size + off
        return self.pos

    def tell(self):
        return self.pos

    def read(self, n=-1):
        end = self.size if n is None or n < 0 else min(self.size, self.pos + n)
        if end <= self.pos:
            return b""

        def once():
            with _open(self.url, self.pos, end) as r:
                total = r.headers.get("Content-Range", "").rsplit("/", 1)[-1]
                if total.isdigit() and int(total) != self.size:
                    raise NoRanges(f"{self.url} is {total} bytes, not the {self.size} pinned")
                return _read_exact(r, end - self.pos, self.stop)
        b = _retrying(once, self.stop, self.url.rsplit("/", 1)[-1])
        self.pos = end
        return b


def zip_members(url, size, stop=None):
    """({name: ZipInfo}, where its central directory starts) of a zip on a server, read with
    range requests."""
    with zipfile.ZipFile(_ZipIndex(url, size, stop or threading.Event())) as z:
        return {i.filename: i for i in z.infolist()}, z.start_dir


def _zip_runs(members, cd, wanted, cap=32 << 20):
    """The wanted members in runs that lie one after the other in the zip (one request each):
    [(start, end, [(info, end of its bytes)])]. A member's bytes end where the next one (or
    the central directory, at cd) starts."""
    starts = sorted(i.header_offset for i in members.values())
    nxt = {a: b for a, b in zip(starts, starts[1:] + [None])}
    runs = []
    for info in sorted(wanted, key=lambda i: i.header_offset):
        end = nxt[info.header_offset] or cd
        if runs and runs[-1][1] == info.header_offset and runs[-1][1] - runs[-1][0] < cap:
            runs[-1][1] = end
            runs[-1][2].append((info, end))
        else:
            runs.append([info.header_offset, end, [(info, end)]])
    return runs


def _get_zip_run(url, run, dests, stop, count):
    """Members lying one after the other in a zip -> their files, in one range request (after
    an interruption, again from the first not done)."""
    done = [0]

    def once():
        items = run[2][done[0]:]
        start = items[0][0].header_offset
        with _open(url, start, run[1]) as r:
            pos = start
            for info, end in items:
                hdr = _read_exact(r, 30, stop)
                if hdr[:4] != b"PK\x03\x04":
                    raise _Short(f"{info.filename}: no zip header at {pos}")
                n, e = struct.unpack("<HH", hdr[26:30])
                _read_exact(r, n + e, stop)
                if info.compress_type not in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED) or info.flag_bits & 1:
                    raise RuntimeError(f"{info.filename}: compression {info.compress_type} is not supported")
                inflate = zlib.decompressobj(-15) if info.compress_type == zipfile.ZIP_DEFLATED else None
                path, size, sha = dests[info.filename]
                w = _Writer(path, size, sha, count)
                try:
                    left, crc = info.compress_size, 0
                    while left:
                        b = _read_exact(r, min(BLOCK, left), stop)
                        left -= len(b)
                        b = inflate.decompress(b) if inflate else b
                        crc = zlib.crc32(b, crc)
                        w.write(b)
                    if inflate:
                        b = inflate.flush()
                        crc = zlib.crc32(b, crc)
                        w.write(b)
                    if crc != info.CRC:
                        raise _Short(f"{info.filename}: CRC does not match")
                    w.finish(f"{url} {info.filename}")
                except BaseException:
                    w.drop()
                    raise
                skip = end - (pos + 30 + n + e + info.compress_size)  # a data descriptor
                if skip:
                    _read_exact(r, skip, stop)
                pos = end
                done[0] += 1
    _retrying(once, stop, run[2][done[0]][0].filename.rsplit("/", 1)[-1])


def _parallel(jobs, progress, report):
    """Run jobs(stop, count) on WORKERS threads. report(bytes so far) about 4 times a second
    (from this thread). A cancel, or a job's error, stops the others at their next block and
    is raised here."""
    stop = threading.Event()
    lock = threading.Lock()
    got = [0]

    def count(n):
        with lock:
            got[0] += n

    if not jobs:
        return
    ex = concurrent.futures.ThreadPoolExecutor(min(WORKERS, len(jobs)), thread_name_prefix="fetch")
    futures = [ex.submit(job, stop, count) for job in jobs]
    try:
        pending = set(futures)
        while pending:
            done, pending = concurrent.futures.wait(pending, timeout=0.25,
                                                    return_when=concurrent.futures.FIRST_EXCEPTION)
            report(got[0])
            for f in done:
                if f.exception() is not None:
                    raise f.exception()
            _check(progress)
    except BaseException:
        stop.set()
        for f in futures:
            f.cancel()
        raise
    finally:
        ex.shutdown(wait=True)  # the running jobs see the stop at their next block
    report(got[0])


def _fetch_listed(lib, spec, final, downloads, keep_downloads, progress, on_bytes, want=None):
    """The listed files of one library part (those in `want` if given) into final/: those there
    already and checked are kept (a file in CHECKED, of its size, isn't read again), the others
    fetched WORKERS at a time. on_bytes(bytes of these files there). Returns whether every
    listed file of the part is now there."""
    every = listed(lib, spec)
    entries = every if want is None else [e for e in every if e[0] in want]
    checked = _checked(final)
    total = sum(size for _, size, _ in entries)
    todo, have, ok_now = [], 0, []
    for path, size, sha in entries:
        _check(progress)
        dest = final / disk_path(spec, path)
        try:
            ok = long_path(dest).stat().st_size == size and (checked.get(path) == sha or _sha256_file(dest) == sha)
        except OSError:
            ok = False
        if ok:
            have += size
            ok_now.append((path, size, sha))
        else:
            todo.append((path, size, sha, dest))

    def record():
        # The files checked before (still listed so, and there) and those of this fetch.
        new = {p for p, _, _ in ok_now}
        keep = [(p, size, sha) for p, size, sha in every if p not in new and checked.get(p) == sha
                and _exists(final / disk_path(spec, p), size)]
        _write_checked(final, ok_now + keep)
        return len(ok_now) + len(keep) == len(every)
    if on_bytes:
        on_bytes(have)
    if not todo:
        return record()
    mb = sum(t[1] for t in todo) / 1e6
    print(f"    {len(todo)} of its {len(entries)} files to fetch ({mb:.0f} MB of {total / 1e6:.0f})", flush=True)
    report = (lambda got: on_bytes(have + got)) if on_bytes else (lambda got: None)
    _get_listed(lib, spec, todo, downloads, keep_downloads, progress, report)
    ok_now += [(p, size, sha) for p, size, sha, _ in todo]
    return record()


def _exists(path, size):
    try:
        return long_path(path).stat().st_size == size
    except OSError:
        return False


def _get_listed(lib, spec, todo, downloads, keep_downloads, progress, report):
    """Fetch [(path in the source, size, sha256, dest)] of a listed part, checked."""
    if "repo" in spec:
        jobs = [lambda stop, count, t=t: _get_raw(raw_url(spec, t[0]), t[3], t[1], t[2], stop, count) for t in todo]
        _parallel(jobs, progress, report)
        return
    try:
        members, cd = zip_members(spec["zip"], spec["bytes"], progress.cancelled if progress else None)
        missing = [t[0] for t in todo if t[0] not in members]
        if missing:
            raise RuntimeError(f"{spec['zip']} has no {missing[0]} (and {len(missing) - 1} more): run make_manifest.py")
        dests = {t[0]: (t[3], t[1], t[2]) for t in todo}
        runs = _zip_runs(members, cd, [members[t[0]] for t in todo])
        print(f"    from {spec['zip'].rsplit('/', 1)[-1]} in {len(runs)} range requests", flush=True)
        _parallel([lambda stop, count, run=run: _get_zip_run(spec["zip"], run, dests, stop, count) for run in runs],
                  progress, report)
    except NoRanges as e:
        print(f"    {e}: the server sends only whole files, so the whole zip is downloaded", flush=True)
        _fetch_whole_zip(lib, spec, todo, downloads, keep_downloads, progress, report)


def _fetch_whole_zip(lib, spec, todo, downloads, keep_downloads, progress, report):
    name = spec["zip"].rsplit("/", 1)[-1]
    arch = downloads / lib / name
    scale = sum(t[1] for t in todo) / spec["bytes"]
    digest = (_sha256_file(arch) if arch.exists() else
              _download(spec["zip"], arch, progress, lambda got, total: report(int(got * scale))))
    if digest != spec["sha256"]:
        arch.unlink(missing_ok=True)
        raise HashMismatch(f"{name}: sha256 {digest} does not match the pinned {spec['sha256']} (removed; try again)")
    with zipfile.ZipFile(arch) as z:
        for path, size, sha, dest in todo:
            _check(progress)
            w = _Writer(dest, size, sha, lambda n: None)
            try:
                with z.open(path) as src:
                    for b in iter(lambda: src.read(1 << 20), b""):
                        w.write(b)
                w.finish(f"{name} {path}")
            except BaseException:
                w.drop()
                raise
    if not keep_downloads:
        arch.unlink(missing_ok=True)


def _prune(lib, final, keep_top):
    """Delete what a library's folder holds besides its listed files and its archives' folders
    (keep_top), e.g. the rest of a whole library an earlier version unpacked, or a file a
    stopped download left. Every listed file is kept, also one this build doesn't want: an
    earlier build fetched it, and a later one may want it again."""
    keep = {STAMP, CHECKED}
    for f in files(lib) or []:
        if is_listed(f):
            keep |= {disk_path(f, p) for p, _, _ in listed(lib, f)}
    n = mb = 0
    root = long_path(final)
    for d, dirs, names in os.walk(root, topdown=False):
        rel = Path(d).relative_to(root).as_posix()
        rel = "" if rel == "." else rel + "/"
        for name in names:
            if rel + name not in keep and (rel + name).split("/", 1)[0] not in keep_top:
                p = Path(d) / name
                mb += p.stat().st_size / 1e6
                p.unlink()
                n += 1
        if rel and rel.split("/", 1)[0] not in keep_top and not os.listdir(d):
            os.rmdir(d)
    if n:
        print(f"    removed {n} files the generator doesn't play ({mb:.0f} MB)", flush=True)


def fetch(lib, libs_dir, downloads_dir, keep_downloads=False, progress=None, on_bytes=None, on_unpack=None, want=None):
    """Download, check and put in place one library (no-op when it is already there).
    want: of its listed files, only these (paths in the source; None: all of them). The other
    listed files there are kept. on_bytes(got, None): bytes of the library there so far (its
    files wanted); on_unpack() before unpacking an archive.

    Stopping at any point is safe: a library counts as there only once its stamp is written,
    after every file is in place and checked (the stamp names the parts in place; a listed part
    counts once all its files are). A whole-file download goes on next time (_download); an
    archive is unpacked beside the library's folder (again from the kept archive next time)
    and moved in at the end; a listed file appears only once checked, and the files already
    there are kept."""
    libs_dir, downloads_dir = Path(libs_dir), Path(downloads_dir)
    if is_ready(libs_dir, lib, want):
        return
    spec = LIBRARIES[lib]
    fs = files(lib)
    if fs is None:
        plats = ", ".join(PLATFORM_NAMES.get(p, p) for p in spec["platform_files"])
        raise SystemExit(f"{spec['name']} is pinned for {plats} only (this is {host_platform()}); "
                         "see tools/music/README.md")
    pin = os.environ.get("TGMUSIC_PIN") == "1"
    final = libs_dir / lib
    staging = libs_dir / f".{lib}.unpacking"
    shutil.rmtree(staging, ignore_errors=True)
    staging.mkdir(parents=True)
    listed_lib = any(is_listed(f) for f in fs)
    # A library with listed files is completed in its folder (what is there and checked is
    # kept); one of whole files only is replaced whole, as it always was.
    in_place = _parts_in_place(final) if listed_lib else set()
    print(f"  {spec['name']}\n    licence {spec['licence']}, {spec['homepage']}", flush=True)
    downloaded = []
    keep_top = set()
    complete = []  # the parts in place (for the stamp)
    before = 0  # bytes of this library's earlier files: on_bytes counts the whole library
    for f in fs:
        if is_listed(f):
            final.mkdir(parents=True, exist_ok=True)
            if _fetch_listed(lib, f, final, downloads_dir, keep_downloads, progress,
                             (lambda got, b=before: on_bytes(b + got, None)) if on_bytes else None, want):
                complete.append(f)
            before += sum(size for p, size, _ in listed(lib, f) if want is None or p in want)
            continue
        complete.append(f)
        fname = f.get("name") or f["url"].rsplit("/", 1)[-1]
        if "strip" in f:  # what an archive puts in the folder is not pruned (_prune)
            keep_top |= {k.rstrip("/").split("/")[0] for k in f.get("keep") or ["*"]}
        else:
            keep_top.add(fname)
        if _part_id(lib, f) in in_place:  # put in place by an earlier fetch: still there
            before += f.get("mb", 0) * 1e6
            if on_bytes:
                on_bytes(before, None)
            continue
        arch = downloads_dir / lib / fname
        if arch.exists():
            digest = _sha256_file(arch)
            if on_bytes:
                on_bytes(before + arch.stat().st_size, None)
        else:
            print(f"    downloading {f['url']}", flush=True)
            digest = _download(f["url"], arch, progress,
                               (lambda got, total, b=before: on_bytes(b + got, None)) if on_bytes else None)
        before += arch.stat().st_size if arch.exists() else 0
        downloaded.append(arch)
        bytes_ok = f.get("sha256") is not None and digest == f["sha256"]
        if "strip" not in f:  # a plain file: its bytes must match
            if pin and not bytes_ok:
                print(f"    PIN {lib} {fname} sha256={digest}")
            elif not bytes_ok:
                arch.unlink()
                shutil.rmtree(staging, ignore_errors=True)
                raise HashMismatch(f"{fname}: sha256 {digest} does not match the pinned {f['sha256']} (removed; try again)")
            shutil.copyfile(arch, staging / fname)
            continue
        part = staging / f".part{len(downloaded)}"
        print(f"    unpacking {fname}", flush=True)
        if on_unpack:
            on_unpack()
        _unpack(arch, part, f["strip"], f.get("keep"), progress)
        tree = None
        if not bytes_ok:
            tree = tree_sha256(part)
            tree_ok = f.get("tree_sha256") is not None and tree == f["tree_sha256"]
            if pin:
                print(f"    PIN {lib} {fname} sha256={digest} tree_sha256={tree}", flush=True)
            elif not tree_ok:
                shutil.rmtree(staging, ignore_errors=True)
                arch.unlink()
                raise HashMismatch(f"{fname}: neither the download (sha256 {digest}) nor its contents (tree {tree}) "
                                   "match the pinned values (removed; try again, or report it)")
            else:
                print("    (the archive's bytes changed upstream; its contents match the pinned tree)")
        for p in part.iterdir():
            shutil.move(str(p), str(staging / p.name))
        part.rmdir()
    if listed_lib:
        # What was unpacked goes in now, in place of what it replaces.
        final.mkdir(parents=True, exist_ok=True)
        for p in staging.iterdir():
            target = final / p.name
            if target.is_dir():
                shutil.rmtree(target)
            elif target.exists():
                target.unlink()
            os.replace(p, target)
        staging.rmdir()
        if "*" not in keep_top:
            _prune(lib, final, keep_top)
    else:
        shutil.rmtree(final, ignore_errors=True)
        os.replace(staging, final)
    # The whole fingerprint once everything is there (as earlier versions wrote it), else the
    # parts in place.
    (final / STAMP).write_text(_fingerprint(lib) if len(complete) == len(fs) else
                               json.dumps([json.loads(_part_id(lib, f)) for f in complete], sort_keys=True))
    if not keep_downloads:
        for a in downloaded:
            a.unlink(missing_ok=True)
        shutil.rmtree(downloads_dir / lib, ignore_errors=True)


def fetch_files(lib, paths, libs_dir, progress=None):
    """Of a library's listed files `paths` (paths in the source), those not in place (of their
    size) fetched now, each checked against its SHA-256: the instrument definitions a build
    reads before working out what to download, and what the safety net of a render process
    (needs.py) finds missing. Safe with several processes at once (_Writer). Returns
    [(path, size)] fetched."""
    spec = listed_part(lib)
    final = Path(libs_dir) / lib
    info = {p: (size, sha) for p, size, sha in listed(lib, spec)}
    todo = []
    for p in sorted(set(paths)):
        if p not in info:
            raise RuntimeError(f"{lib}: {p} is not in tgmusic/data/samples.tsv")
        size, sha = info[p]
        dest = final / disk_path(spec, p)
        if not _exists(dest, size):
            todo.append((p, size, sha, dest))
    if todo:
        _get_listed(lib, spec, todo, Path(libs_dir).parent / "downloads", False, progress, lambda got: None)
    return [(t[0], t[1]) for t in todo]


def credits(libs):
    """Text for CREDITS.txt in the output folder (CC-BY needs attribution if a render is shared)."""
    out = ["The music in this folder was rendered on this computer by tools/music/generate.py from the",
           "notes of your own Top Gear (USA) ROM. It is an arrangement of the game's compositions: keep it",
           "to yourself (do not share or publish it).", "",
           "Instruments and effects used:"]
    for lib in libs:
        s = LIBRARIES[lib]
        out.append(f"  {s['name']}: {s['licence']}, {s['homepage']}")
    return "\n".join(out) + "\n"
