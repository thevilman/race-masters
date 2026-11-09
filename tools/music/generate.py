#!/usr/bin/env python3
"""Generate the game's optional modern music on your own computer, from your own ROM.

    python3 tools/music/generate.py --rom "Top Gear (USA).sfc" --out "<game folder>/audio-modern"

The launcher's "Build music" button runs this (after setup_tools.py); it can also be run by hand.

1. Notes: tools/audio plays every song of your ROM on the emulated sound chip and logs the
   notes (MIDI); nothing of the game's music is in this repository.
2. Plan: each track is arranged (tgmusic/plan.py) without rendering, for the notes each of its
   instruments will play; kept in the cache, never in the repository or the output.
3. Instruments: from free sample libraries and plug-ins, only the samples those notes can play
   (tgmusic/needs.py) of the files the styles may play (tgmusic/data/samples.tsv: what you
   agree to) are downloaded, from their official sources (pinned versions, SHA-256 checked)
   into a cache folder; see THIRD_PARTY.md.
4. Render: each chosen style is arranged and rendered to Ogg Vorbis files with the loop tags
   the game reads (LOOPSTART / LOOPLENGTH), in the folders src/audio/music.c looks in.

Styles (--styles, comma separated; the game's Music option in brackets):
  faithful    the 7 songs, each SNES instrument replaced by a modern one   [FAITHFUL, the default]
  synthwave   the 7 songs as synthwave                                     [SYNTHWAVE]
  orchestral  the 7 songs as an orchestra                                  [ORCHESTRAL]
  A           each city's race tune with local instruments (postcard)      [POSTCARD]
  B           each city's tune re-arranged for a local band                [LOCAL BAND]
  C           each city's tune covered by a local genre band               [CITY BAND]
  D           C at racing speed with a racing drive under it               [MODERN]
The city styles play the faithful title, Qualified and Ending, so faithful is always made
for the title at least (the game uses the folder only when it has the faithful title).

Stopping is safe at any moment (Ctrl+C, the launcher's Cancel, closing the launcher): finished
tracks, downloads (even half-done ones) and unpacked libraries are kept, and the next run goes
on from there. Nothing is written into the repository. Deterministic: the same ROM, libraries
and options give the same notes and arrangement; see README.md for the details.
"""
import argparse
import os
import shutil
import sys
import time
from pathlib import Path

sys.dont_write_bytecode = True  # no __pycache__ in the repository
if sys.version_info < (3, 10):
    sys.exit("tools/music needs Python 3.10 or newer")
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from tgmusic import progress as P  # noqa: E402 (standard library only)

# The source checkout this runs from, if it is one (a release's music-tools folder is not): no
# output, cache or work folder may be inside it.
REPO = HERE.parents[1] if (HERE.parents[1] / "CMakeLists.txt").is_file() and (HERE.parents[1] / "src").is_dir() else None

ALIASES = {"modern": "D", "race": "D", "band": "C", "local": "B", "postcard": "A"}
ALL_STYLES = ["faithful", "synthwave", "orchestral", "A", "B", "C", "D"]
SONGS = ["01_title", "02_las_vegas", "03_hiroshima", "04_bordeaux", "05_frankfurt", "06_qualified", "07_ending"]
CONSENT = HERE / "tgmusic" / "data" / "consent.txt"
# Disk, in MB, for what the launcher shows (tools/music/README.md): the libraries in
# <cache>/libs if every listed file were fetched (every style; the files as downloaded, the
# plug-ins unpacked: 3,168 MB on Windows, 3,197 on Linux, from data/samples.tsv; a build
# usually fetches about a third, libraries.TYPICAL_MB), the music (all 149 tracks, measured),
# the most free space a whole run may need at once (the notes are read, and their work files
# pruned to 7 MB, before anything is downloaded: the libraries at most, the biggest archive
# while it unpacks, the tools and the music).
LIBS_UNPACKED_MB, MUSIC_ALL_MB, PEAK_MB = 3300, 700, 4600
WORK_MB = 1700  # tools/audio's output while it runs (then 7 MB are kept)


def data_root():
    """The per-user folder the launcher and the game share (%LOCALAPPDATA%\\race-masters,
    ~/.local/share/race-masters)."""
    if os.name == "nt":
        base = os.environ.get("LOCALAPPDATA") or str(Path.home() / "AppData" / "Local")
    else:
        xdg = os.environ.get("XDG_DATA_HOME")
        base = xdg if xdg and os.path.isabs(xdg) else str(Path.home() / ".local" / "share")
    return Path(base) / "race-masters"


def default_cache():
    # The cache of earlier versions of the generator, if there is one (no 10 GB again).
    if os.name == "nt":
        legacy = Path(os.environ.get("LOCALAPPDATA") or Path.home() / "AppData" / "Local") / "topgear-music"
    else:
        legacy = Path(os.environ.get("XDG_CACHE_HOME") or Path.home() / ".cache") / "topgear-music"
    if (legacy / "libs").is_dir():
        return legacy
    return data_root() / "music-tools" / "cache"


def outside_repo(path, what):
    p = Path(path).resolve()
    if REPO is None:
        return p
    try:
        p.relative_to(REPO)
    except ValueError:
        return p
    raise SystemExit(f"{what} ({p}) is inside the repository: the music and the libraries must stay out of git. "
                     "Choose a folder outside it.")


def parse_styles(s):
    out = []
    for x in s.split(","):
        x = x.strip()
        if not x:
            continue
        if x == "all":
            return list(ALL_STYLES)
        x = ALIASES.get(x.lower(), x if x in ("A", "B", "C", "D") else x.lower())
        if x.upper() in ("A", "B", "C", "D"):
            x = x.upper()
        if x not in ALL_STYLES:
            raise SystemExit(f"--styles: unknown style {x!r} (one of {', '.join(ALL_STYLES)}, or all)")
        if x not in out:
            out.append(x)
    return out


def norm(s):
    return s.strip().lower().replace(" ", "_").replace("-", "_")


# A track as the launcher shows it: "Las Vegas (race)", "Title (faithful)".
STYLE_WORDS = {"faithful": "faithful", "synthwave": "synthwave", "orchestral": "orchestral", "A": "postcard",
               "B": "local band", "C": "city band", "D": "race"}


def shown(job):
    kind, a, b = job
    name = a[3:].replace("_", " ").title() if kind == "variation" else a[1]
    return f"{name} ({STYLE_WORDS[b]})"


# --- What the launcher shows before asking (tgmusic/data/consent.txt) ------------------------

def consent_text():
    """The launcher's list of what a build downloads and keeps, from libraries.py and
    setup_tools.py (one record per line, tab-separated; the launcher reads it at run time)."""
    import setup_tools as S
    from tgmusic import libraries as L
    win, lin = "win32-x86_64", "linux-x86_64"
    rows = ["# What the launcher shows before it asks to build the modern music (src/launcher.c reads it).",
            "# Made from tgmusic/libraries.py and setup_tools.py by: python3 tools/music/generate.py --write-consent",
            "# (the music_tools test checks it is up to date). Tab-separated; sizes in MB: Windows, Linux.",
            "# lib: the most a build downloads of it (every file data/samples.tsv lists); typical: what a",
            "# build of one style usually downloads of it (libraries.TYPICAL_MB; the build works out its own).",
            f"tracks\t{len(SONGS) * 3 + 32 * 4}"]
    for lib in L.needed(ALL_STYLES):
        s = L.LIBRARIES[lib]
        rows.append("\t".join(["lib", lib, s["name"], s["licence"].split(" (")[0], s["homepage"],
                               str(L.size_mb(lib, win)), str(L.size_mb(lib, lin))]))
    for st in ALL_STYLES:
        rows.append("\t".join(["style", st, ",".join(L.STYLE_LIBS[st])]))
    for lib in L.needed(ALL_STYLES):
        for st in ALL_STYLES:
            if lib in L.TYPICAL_MB and lib in L.STYLE_LIBS[st]:
                rows.append("\t".join(["typical", lib, st, str(min(L.TYPICAL_MB[lib].get(st, 0), L.size_mb(lib, win)))]))
    rows.append("\t".join(["tool", f"Python {S.PYTHON_EMBED['version']} (python.org, Windows only), pip {S.PIP['version']} "
                           f"and {S.PACKAGES} Python packages (PyPI), pinned and checked", "PSF-2.0, MIT, BSD, Apache-2.0, "
                           "GPL-3.0 (pedalboard)", "https://pypi.org", str(S.TOOLS_MB[win]), str(S.TOOLS_MB[lin])]))
    rows += [f"disk\tlibs\t{LIBS_UNPACKED_MB}", f"disk\ttools\t{S.TOOLS_INSTALLED_MB}", f"disk\tmusic\t{MUSIC_ALL_MB}",
             f"disk\tpeak\t{PEAK_MB}", f"time\ttrack_s\t{int(P.Eta.TRACK_S)}", f"time\tnotes_s\t{int(P.Eta.NOTES_S)}",
             f"time\trate_mbs\t{int(P.Eta.RATE / 1e6)}"]
    return "\n".join(rows) + "\n"


# --- Rendering ------------------------------------------------------------------------------

def render_all(pending, out, formats, jobs, results, label, prog, eta, start_done, total, log, room=None):
    """Each track in its own Python process (`generate.py --worker`), up to `jobs` at a time, a
    new one only when the memory available holds it (room(pids of those running):
    progress.room_for_track). A process that dies (e.g. out of memory) fails its track only; the
    others go on. A cancel stops the processes running (their tracks are not written: each file
    appears only once complete). Returns (the jobs that failed, the samples the render
    processes' safety net fetched)."""
    import json
    import subprocess
    if room is None:
        always = min(jobs, P.always_at_once())
        print(f"  up to {jobs} at a time, {always} whatever the memory free, more while it holds them", flush=True)

        def room(pids):
            return P.room_for_track(pids, always=always)
    results.mkdir(parents=True, exist_ok=True)
    queue, running, failed, n, late = list(pending), {}, [], 0, 0
    last_beat, held = 0.0, None
    logf = P.open_log(log) if log else None
    try:
        while queue or running:
            prog.check()
            while queue and len(running) < jobs:
                if not room([p.pid for p in running]):
                    if held != len(running):  # (said once for each number running it waits at)
                        held = len(running)
                        print(f"  {held} track(s) rendering: the next waits for memory to be free "
                              f"({P.memory_mb()[1] or 0:.0f} MB available)", flush=True)
                    break
                job = queue.pop(0)
                rf = results / f"{os.getpid()}-{len(queue)}.json"
                rf.unlink(missing_ok=True)
                cmd = [sys.executable, *P.PYFLAGS, str(Path(__file__).resolve()), "--worker",
                       json.dumps([job, str(out), list(formats)]), "--result", str(rf)]
                kw = dict(stdout=logf, stderr=subprocess.STDOUT) if logf else {}
                running[subprocess.Popen(cmd, stdin=subprocess.DEVNULL, **kw, **P.popen_kwargs())] = (job, rf, time.time())
            if queue and eta.track_times:  # measured: at the number running (fewer while memory is short)
                eta.jobs = max(1, len(running))
            prog.cancelled.wait(0.5)
            if time.time() - last_beat >= 5:
                last_beat = time.time()
                prog.emit("render", done=start_done + n, of=total, running=" / ".join(shown(j) for j, _, _ in running.values()),
                          eta_s=eta.seconds())
            for p in [p for p in running if p.poll() is not None]:
                job, rf, t0 = running.pop(p)
                n += 1
                try:
                    res = json.loads(rf.read_text())
                    rf.unlink()
                except (OSError, ValueError):
                    res = {"error": f"the render process ended with code {p.returncode} (out of memory? try fewer --jobs)",
                           "mb": 0}
                err = res["error"] or (None if p.returncode == 0 else f"exit code {p.returncode}")
                secs = time.time() - t0
                eta.tracks_left -= 1
                if not err:
                    eta.track_times.append(secs)
                late += res.get("fetched", 0)
                print(f"  [{start_done + n}/{total}] {label(job)}: {'FAILED' if err else 'done'} in {secs:.0f} s"
                      f" (peak {res['mb']:.0f} MB)" + (f"; the safety net fetched {res['fetched']} samples the plan didn't "
                                                       "foresee" if res.get("fetched") else ""), flush=True)
                prog.emit("track", i=start_done + n, of=total, label=shown(job), status="failed" if err else "done",
                          secs=secs, eta_s=eta.seconds())
                if err:
                    print(err, flush=True)
                    failed.append(job)
    except BaseException:
        # Cancelled, Ctrl+C or an error: the tracks being rendered stop (none is half written).
        for p in running:
            P.stop_process(p)
        raise
    finally:
        if logf:
            logf.close()
    return failed, late


def worker(argv):
    """generate.py --worker '<job json>' --result FILE: render one job (environment from the parent;
    TGMUSIC_THREADS: threads its parts play in)."""
    import json
    P.die_with_parent()
    P.full_speed()
    job, out, formats = json.loads(argv[argv.index("--worker") + 1])
    kind, a, b = job
    job = (kind, tuple(a) if isinstance(a, list) else a, b)
    from tgmusic import jobs as J
    from tgmusic import needs
    _, secs, mb, err = J.run((job, out, tuple(formats)))
    Path(argv[argv.index("--result") + 1]).write_text(json.dumps({"seconds": secs, "mb": mb, "error": err,
                                                                  "fetched": len(needs.FETCHED)}))


def plan_worker(argv):
    """generate.py --plan-worker '[<job>, <out>, <shipped only>]' --result FILE: the notes of
    one job (plan.py)."""
    import json
    P.die_with_parent()
    P.full_speed()
    job, out, shipped_only = json.loads(argv[argv.index("--plan-worker") + 1])
    kind, a, b = job
    from tgmusic import plan
    res = plan.plan_job((kind, tuple(a) if isinstance(a, list) else a, b), out, shipped_only)
    Path(argv[argv.index("--result") + 1]).write_text(json.dumps(res))


def plan_jobs(jobs):
    """Tracks planned at once: planning takes about a second and 0.2 GB a track."""
    return max(jobs, min(6, (os.cpu_count() or 2) // 2))


def plan_tracks(todo, cache, jobs, prog, shipped_only, store=None, on_track=None):
    """The plan of each job of `todo` (plan.plan_job), in todo's order, each in a process of
    its own as in a build, `jobs` at a time. store (needs.PlanStore): plans kept from before
    are reused and each new one is kept. on_track(n done, of, job, plan) after each."""
    import json
    import subprocess
    results = cache / "results"
    results.mkdir(parents=True, exist_ok=True)
    tracks = [store.get(job) if store else None for job in todo]
    queue = [(i, job) for i, job in enumerate(todo) if tracks[i] is None]
    running, n = {}, len(todo) - len(queue)
    if on_track and n:
        on_track(n, len(todo), None, None)
    try:
        while queue or running:
            prog.check()
            while queue and len(running) < jobs:
                i, job = queue.pop(0)
                rf = results / f"plan-{os.getpid()}-{i}.json"
                rf.unlink(missing_ok=True)
                cmd = [sys.executable, *P.PYFLAGS, str(Path(__file__).resolve()), "--plan-worker",
                       json.dumps([job, str(cache / "plan-out"), shipped_only]), "--result", str(rf)]
                running[subprocess.Popen(cmd, stdin=subprocess.DEVNULL, **P.popen_kwargs())] = (i, job, rf)
            prog.cancelled.wait(0.2)
            for p in [p for p in running if p.poll() is not None]:
                i, job, rf = running.pop(p)
                n += 1
                try:
                    res = json.loads(rf.read_text())
                    rf.unlink()
                except (OSError, ValueError):
                    res = {"job": json.loads(json.dumps(job)), "error": f"the planning process ended with code {p.returncode}",
                           "parts": [], "misses": [], "probes": []}
                res.pop("seconds", None)
                tracks[i] = res
                if store and not res["error"]:
                    store.put(job, res)
                if on_track:
                    on_track(n, len(todo), job, res)
    except BaseException:
        for p in running:
            P.stop_process(p)
        raise
    return tracks


def plan_notes(args, rom, todo, cache, prog):
    """--plan-notes FILE: every note each instrument of the tracks is sent, worked out by the
    arrangement code itself (tgmusic/plan.py), each track in a process of its own as in a
    build, --jobs at a time. No audio is rendered and no library is needed: only the notes from
    the ROM (read first if they aren't yet). FILE (JSON) holds what the ROM's notes become:
    keep it out of the repository, to yourself. With --yes, also what a build of each style
    downloads of the sample libraries (fetching their instrument definitions, about 0.5 MB,
    to read them): the numbers for libraries.TYPICAL_MB."""
    import json
    from tgmusic import extract, plan
    from tgmusic import jobs as J
    dest = outside_repo(args.plan_notes, "--plan-notes")
    lock = P.hold_lock(cache / "build.lock")
    if lock is None:
        raise SystemExit(f"Another build is using {cache} right now; wait for it to finish (or stop it) first.")
    t0 = time.time()
    print("Notes from your ROM", flush=True)
    extract.extract(rom, keep_all=args.keep_extracted, jobs=os.cpu_count() or 4, tg_audio=args.tg_audio, progress=prog)
    jobs = plan_jobs(1) if args.jobs == "auto" else int(args.jobs)  # (planning takes about 0.2 GB a track)
    print(f"Planning {len(todo)} tracks ({jobs} at a time; nothing is rendered)", flush=True)
    t1 = time.time()

    def told(n, of, job, res):
        if job is None:
            return
        sfz_notes = sum(x["notes"] for x in res["parts"] if x["type"] == "sfz")
        print(f"  [{n}/{of}] {J.label(job)}: {'FAILED' if res['error'] else 'planned'}, {len(res['parts'])} parts, "
              f"{sfz_notes} .sfz notes" + (f", {len(res['misses'])} table misses" if res["misses"] else ""), flush=True)
        if res["error"]:
            print(res["error"], flush=True)
    tracks = plan_tracks(todo, cache, jobs, prog, True, on_track=told)
    errors = [shown(tuple(t["job"])) for t in tracks if t["error"]]
    misses = sorted({json.dumps(m, sort_keys=True) for t in tracks for m in t["misses"]})
    data = {"about": "generate.py --plan-notes: the notes each instrument of these tracks is sent (tgmusic/plan.py). "
                     "Made from the notes of a ROM: keep it to yourself.",
            "styles": sorted({j[2] for j in todo}), "tracks": tracks, "sfz": plan.summary(tracks),
            "misses": [json.loads(m) for m in misses], "errors": errors}
    tmp = dest.with_name(dest.name + ".tmp")
    tmp.write_text(json.dumps(data, sort_keys=True, indent=0), encoding="utf-8")
    os.replace(tmp, dest)
    n_on = sum(c for v in data["sfz"].values() for _, _, c in v)
    print(f"\nWrote {dest}: {len(tracks)} tracks, {len(data['sfz'])} .sfz instruments, {n_on} .sfz notes "
          f"({time.time() - t1:.0f} s planning, {time.time() - t0:.0f} s in all)", flush=True)
    for m in data["misses"]:
        print(f"  not in the shipped table {m['table']} (a build would measure it): {m['key']}", flush=True)
    if errors:
        print(f"{len(errors)} track(s) failed: {', '.join(errors)}", flush=True)
    if args.yes and not errors:
        # What a build of each style downloads of the sample libraries (for TYPICAL_MB).
        from tgmusic import libraries as L
        from tgmusic import needs
        libs = Path(os.environ["TGMUSIC_LIBS"])
        table = {}
        for st in [s for s in ALL_STYLES if any(t["job"][2] == s for t in tracks)]:
            want = needs.wanted([t for t in tracks if t["job"][2] == st], libs, prog)
            for lib, paths in want.items():
                table.setdefault(lib, {})[st] = -(-L.wanted_bytes(lib, paths) // 1000000)  # MB, rounded up
        want = needs.wanted(tracks, libs, prog)
        print("\nWhat a build downloads of the sample libraries, MB (libraries.TYPICAL_MB):")
        print("TYPICAL_MB = " + json.dumps(table, sort_keys=True))
        for lib in sorted(want):
            print(f"  {lib:16s} all these tracks: {L.wanted_bytes(lib, want[lib]) / 1e6:8.1f} MB of {L.size_mb(lib)}")
    return P.EXIT_TRACKS if errors or misses else P.EXIT_OK


def remove_stale_temp(out):
    """A run stopped mid-track leaves its temporary files (.<name>.tmp.*): remove them."""
    for p in Path(out).glob("*/*/.*.tmp.*"):
        try:
            p.unlink()
        except OSError:
            pass


def room(cache, out, need_cache, need_out, prog, log_path):
    """Whether the cache's drive has need_cache MB free and the output's need_out (both, if
    they are one drive); if not, says so (an error "disk")."""
    free_c, free_o = free_mb(cache), free_mb(out)
    same = free_c is not None and free_o is not None and abs(free_c - free_o) < 1
    checks = [(cache, need_cache + (need_out if same else 0), free_c)] + ([] if same else [(out, need_out, free_o)])
    for where, want_mb, have_mb in checks:
        if have_mb is not None and have_mb < want_mb:
            prog.emit("error", code="disk", text=f"Not enough disk space: about {want_mb / 1000:.1f} GB is needed "
                      f"for {where} and {have_mb / 1000:.1f} GB is free.", log=log_path)
            print(f"Not enough disk space for {where}: about {want_mb / 1000:.1f} GB needed, "
                  f"{have_mb / 1000:.1f} GB free.", flush=True)
            return False
    return True


def free_mb(path):
    p = Path(path)
    while not p.exists() and p.parent != p:
        p = p.parent
    try:
        return shutil.disk_usage(p).free / 1e6
    except OSError:
        return None


def error_code(e):
    import socket
    import urllib.error
    from tgmusic import libraries as L
    if isinstance(e, L.HashMismatch):
        return "hash"
    if isinstance(e, (urllib.error.URLError, socket.timeout, ConnectionError, TimeoutError)):
        return "network"
    if isinstance(e, OSError) and getattr(e, "errno", None) == 28:
        return "disk"
    return "other"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rom", help="your Top Gear (USA) ROM (.sfc / .smc)")
    ap.add_argument("--out", help="the audio-modern folder next to the game (made if missing)")
    ap.add_argument("--styles", help="what to make (default: faithful,D, or all with --plan-notes; 'all' for every style)")
    ap.add_argument("--stages", default="all",
                    help="cities for the city styles: names or countries, comma separated (e.g. rio,las_vegas or usa); default all")
    ap.add_argument("--songs", default="all", help="songs for faithful/synthwave/orchestral (e.g. 01_title,02_las_vegas); default all 7")
    ap.add_argument("--jobs", default="auto",
                    help="tracks rendered at once, at most (default auto: half the logical processors, as many as the "
                         "memory holds; beyond a third of that, a track starts only when the memory available holds it)")
    ap.add_argument("--threads", default="auto",
                    help="threads each track's parts play in (Surge XT parts play at once; default auto: the track's "
                         "share of the processors, 2-4)")
    ap.add_argument("--cache", default=os.environ.get("TGMUSIC_CACHE"),
                    help=f"where the libraries, downloads and work files go (default {default_cache()}, or $TGMUSIC_CACHE)")
    ap.add_argument("--work", help="the work folder for the notes from the ROM (default <cache>/work)")
    ap.add_argument("--tg-audio", help="tools/audio's tg_audio program (default: the one beside the music tools in a "
                                       "release, else built with CMake)")
    ap.add_argument("--formats", default="ogg", help="ogg (what the game plays), plus wav (24-bit master) and/or mp3")
    ap.add_argument("--force", action="store_true", help="render tracks again even if they are in --out already")
    ap.add_argument("--yes", action="store_true", help="agree to download the libraries listed without asking")
    ap.add_argument("--keep-downloads", action="store_true", help="keep the downloaded archives in <cache>/downloads")
    ap.add_argument("--keep-extracted", action="store_true", help="keep all of tools/audio's output in the work folder")
    ap.add_argument("--clean-after", action="store_true",
                    help="when every track is made, delete the libraries and work files (about 4 GB; downloaded again "
                         "only to render more)")
    ap.add_argument("--dry-run", action="store_true",
                    help="read the notes and plan the tracks, show what would be made and downloaded, then stop (with "
                         "--yes it fetches the instruments' definitions, about 0.5 MB, for the exact size)")
    ap.add_argument("--list", action="store_true", help="list the cities and songs, then stop")
    ap.add_argument("--plan-notes", metavar="FILE",
                    help="render nothing: write FILE (JSON) with every note each instrument of the tracks would be "
                         "sent (needs only --rom; no --out, no libraries)")
    ap.add_argument("--progress-json", action="store_true",
                    help="progress as JSON lines on standard output (for the launcher; tgmusic/progress.py); "
                         "the rest goes to a log file; stop on 'cancel' or end of standard input")
    ap.add_argument("--write-consent", action="store_true", help=argparse.SUPPRESS)
    ap.add_argument("--check-consent", action="store_true", help=argparse.SUPPRESS)
    args = ap.parse_args()
    for name in ("jobs", "threads"):
        v = getattr(args, name)
        if v != "auto" and not (str(v).isdigit() and int(v) >= 1):
            ap.error(f"--{name}: auto or a number from 1")

    if args.write_consent or args.check_consent:
        text = consent_text()
        if args.write_consent:
            CONSENT.write_text(text, encoding="utf-8")
            print(f"wrote {CONSENT}")
            return P.EXIT_OK
        if CONSENT.read_text(encoding="utf-8") != text:
            print(f"{CONSENT} is out of date: run generate.py --write-consent", file=sys.stderr)
            return P.EXIT_ERROR
        print("consent.txt is up to date")
        return P.EXIT_OK

    prog = P.Progress()
    log_path = None
    if args.progress_json:
        # Standard output carries only the JSON lines; everything else goes to the log.
        try:
            cache_for_log = Path(args.cache or default_cache())
            logs = cache_for_log / "logs"
            logs.mkdir(parents=True, exist_ok=True)
        except OSError as e:
            sys.stdout.write('{"ev":"error","code":"disk","text":%s}\n' % __import__("json").dumps(str(e)))
            return P.EXIT_ERROR
        log_path = logs / time.strftime("build-%Y%m%d-%H%M%S.log")
        for old in sorted(logs.glob("build-*.log"))[:-4]:
            old.unlink(missing_ok=True)
        json_out = sys.stdout
        logf = P.open_log(log_path)  # (the render processes append too)
        sys.stdout = sys.stderr = logf
        prog = P.Progress(json_out)
        prog.watch_stdin()
    try:
        return run(args, ap, prog, log_path)
    except P.Cancelled:
        print("\nStopped. Finished tracks and downloads are kept: run again to go on.", flush=True)
        prog.emit("cancelled")
        return P.EXIT_CANCELLED
    except KeyboardInterrupt:
        print("\nStopped. Finished tracks and downloads are kept: run again to go on.", flush=True)
        return P.EXIT_CANCELLED
    except SystemExit as e:
        if e.code in (None, 0):
            raise
        text = e.code if isinstance(e.code, str) else f"exit {e.code}"
        if not prog.json:
            raise
        print(text, flush=True)
        prog.emit("error", code=error_code(e), text=text, log=log_path)
        return P.EXIT_ERROR
    except Exception as e:  # noqa: BLE001 - reported to the launcher, the traceback in the log
        import traceback
        if not prog.json:
            raise
        traceback.print_exc()
        prog.emit("error", code=error_code(e), text=f"{type(e).__name__}: {e}", log=log_path)
        return P.EXIT_ERROR


def run(args, ap, prog, log_path):
    cache = outside_repo(args.cache or default_cache(), "--cache")
    work = outside_repo(args.work or cache / "work", "--work")
    libs = cache / "libs"
    os.environ["TGMUSIC_WORK"] = str(work)
    os.environ["TGMUSIC_LIBS"] = str(libs)
    os.environ["NUMBA_CACHE_DIR"] = str(cache / "numba")
    os.environ["PYTHONDONTWRITEBYTECODE"] = "1"

    from tgmusic import libraries as L
    try:
        from tgmusic import jobs as J
        cities = J.cities()  # loads the arrangements and the players (numpy, pedalboard, ...)
    except ImportError as e:
        if os.name == "nt" and "DLL load failed" in str(e):
            prog.emit("error", code="vcredist", text="A package couldn't load a Windows library: install the Microsoft "
                      "Visual C++ Redistributable (x64), then try again.", log=log_path)
            print(e, flush=True)
            return P.EXIT_ERROR
        raise SystemExit(f"the music tools' Python packages are missing or broken ({e}): run setup_tools.py") from e

    if args.list:
        print("Cities (--stages takes a city or a country):")
        for country, city, tune in cities:
            print(f"  {J.slug(country):14s} {J.slug(city):14s} race tune {SONGS[tune]}")
        print("Songs (--songs):", ", ".join(SONGS))
        return P.EXIT_OK
    if not args.rom or not (args.out or args.plan_notes):
        ap.error("--rom and --out are needed (--plan-notes: --rom)")
    rom = Path(args.rom).resolve()
    if not rom.is_file():
        raise SystemExit(f"--rom: {rom} not found")
    out = outside_repo(args.out, "--out") if args.out else None
    formats = tuple(f.strip().lower() for f in args.formats.split(",") if f.strip())
    if "ogg" not in formats or any(f not in ("ogg", "wav", "mp3") for f in formats):
        raise SystemExit("--formats: ogg plus optionally wav, mp3")

    styles = parse_styles(args.styles or ("all" if args.plan_notes else "faithful,D"))
    songs = SONGS if args.songs == "all" else [s for s in SONGS if any(norm(x) in (s, s[3:]) for x in args.songs.split(","))]
    if args.stages == "all":
        chosen = cities
    else:
        want = {norm(x) for x in args.stages.split(",")}
        chosen = [c for c in cities if J.slug(c[0]) in want or J.slug(c[1]) in want]
        unknown = want - {J.slug(c[0]) for c in cities} - {J.slug(c[1]) for c in cities}
        if unknown:
            raise SystemExit(f"--stages: unknown {', '.join(sorted(unknown))} (see --list)")

    todo = []
    if "faithful" not in styles:
        todo.append(("variation", "01_title", "faithful"))  # the game needs it to use the folder
    for st in styles:
        if st in J.VARIATION_STYLES:
            todo += [("variation", s, st) for s in songs]
        else:
            todo += [("band" if st in ("C", "D") else "stage", (c[0], c[1]), st) for c in chosen]
    todo.sort(key=lambda j: j != ("variation", "01_title", "faithful"))  # the title first
    if args.plan_notes:
        return plan_notes(args, rom, todo, cache, prog)
    pending = [j for j in todo if args.force or not J.is_done(out, j)]
    used = L.needed(sorted({j[2] for j in todo}))  # for CREDITS.txt
    need = L.needed(sorted({j[2] for j in pending}))
    no_build = L.unsupported(need)
    if no_build:
        raise SystemExit(f"{', '.join(L.LIBRARIES[x]['name'] for x in no_build)}: no build pinned for this system "
                         f"({L.host_platform()}); the generator runs on Windows x64 and Linux x86-64")
    # What may be downloaded: at most what data/samples.tsv lists of these styles' libraries (what
    # the player agrees to), usually much less (TYPICAL_MB); the plan says exactly, before
    # anything is downloaded.
    missing = [lib for lib in need if L.missing_bytes(libs, lib) > 0]
    most_mb = sum(L.missing_bytes(libs, x) for x in missing) / 1e6

    def usual(lib):  # what the usual download leaves to fetch of a library, given what is there
        miss = L.missing_bytes(libs, lib) / 1e6
        if not L.LIBRARIES[lib].get("by_note"):
            return miss
        return min(miss, max(0.0, L.typical_mb(lib, sorted({j[2] for j in pending})) - (L.size_mb(lib) - miss)))
    usual_mb = sum(usual(x) for x in missing)
    from tgmusic import extract, needs
    notes_done = extract.done(work, extract.rom_sha256(rom))
    jobs, threads = P.concurrency(args.jobs, args.threads)
    os.environ["TGMUSIC_THREADS"] = str(threads)  # (the render processes read it)
    eta = P.Eta(download_bytes=usual_mb * 1e6, notes=bool(pending) and not notes_done, tracks=len(pending), jobs=jobs,
                plan=len(pending), plan_jobs=plan_jobs(jobs))

    print(f"ROM:    {rom}\nOutput: {out}\nCache:  {cache}")
    print(f"Tracks: {len(todo)} ({len(todo) - len(pending)} already there, {len(pending)} to render, up to {jobs} at a "
          f"time, the parts of each in {threads} threads)")
    for j in pending[:40]:
        print(f"  - {J.label(j)}")
    if len(pending) > 40:
        print(f"  ... and {len(pending) - 40} more")
    if missing:
        print("\nInstrument libraries to download from (once; into the cache folder, never into the repository).\n"
              "They are free to use for making music; this generator uses them only to render on this computer:")
        print(L.describe(missing))
        print(f"  up to {most_mb / 1000:.1f} GB in all, usually about {usual_mb / 1000:.1f} GB: only the samples the notes "
              "of these tracks can play, worked out from your ROM's notes before anything is downloaded (archives are "
              "deleted once unpacked); licences: tools/music/THIRD_PARTY.md")
    if pending:
        print(f"\nTime: about {P.fmt_eta(eta.seconds())} ({jobs} track(s) at a time; finished tracks are kept if it stops)")
    prog.emit("plan", tracks=len(todo), done=len(todo) - len(pending), todo=len(pending), download_mb=round(usual_mb),
              download_max_mb=round(most_mb), libs=len(missing), jobs=jobs, eta_s=eta.seconds(), out=str(out),
              dry_run=args.dry_run)
    if missing and pending and not args.yes and not args.dry_run:
        if prog.json or not sys.stdin.isatty():
            prog.emit("error", code="consent", text="Agree to the downloads first (--yes).")
            print("\nRun again with --yes to agree to these downloads.", flush=True)
            return P.EXIT_ERROR
        if input("\nDownload them now? [y/N] ").strip().lower() not in ("y", "yes"):
            print("Stopped: nothing downloaded.")
            return P.EXIT_CANCELLED
    if args.dry_run and not pending:
        prog.emit("done", tracks=len(todo), failed=0, mb=0, minutes=0, dry_run=True)
        return P.EXIT_OK
    # Enough room for the notes (the work files while they are read) and the tracks? The
    # libraries are checked once the plan says what they take.
    need_out = len(pending) * 6
    if pending and not args.dry_run and not room(cache, out, 0 if notes_done else WORK_MB, need_out, prog, log_path):
        return P.EXIT_ERROR

    lock = P.hold_lock(cache / "build.lock")
    if lock is None:
        raise SystemExit(f"Another build is using {cache} right now; wait for it to finish (or stop it) first.")
    P.lower_priority()
    P.kill_children_with_me()
    t_all = time.time()
    if not args.dry_run:
        out.mkdir(parents=True, exist_ok=True)
        remove_stale_temp(out)
    failed, late = [], 0
    if pending:
        print("\n[1/4] Notes from your ROM", flush=True)
        prog.emit("stage", stage="notes", n=1, of=4, text="Reading the notes from your ROM", eta_s=eta.seconds())
        t0 = time.time()

        def beat(elapsed):
            eta.notes_left = max(30.0, P.Eta.NOTES_S - elapsed)
            prog.emit("notes", elapsed_s=elapsed, eta_s=eta.seconds())

        extract.extract(rom, keep_all=args.keep_extracted, jobs=os.cpu_count() or 4, tg_audio=args.tg_audio,
                        progress=prog, heartbeat=beat)
        eta.notes_left = 0
        print(f"  done ({time.time() - t0:.0f} s)", flush=True)

        print(f"[2/4] Planning {len(pending)} tracks: the notes each instrument will play", flush=True)
        prog.emit("stage", stage="plan", n=2, of=4, text="Working out what to download", eta_s=eta.seconds())
        t0 = time.time()
        store = needs.PlanStore(work / "plan", needs.plan_key(work, libs, False))

        def planned(n, of, job, res):
            eta.plan_left = (of - n) * P.Eta.PLAN_S / plan_jobs(jobs)
            prog.emit("planning", i=n, of=of, eta_s=eta.seconds())
            if job is not None and res["error"]:
                print(f"  {J.label(job)}: couldn't plan it (rendering it will say why):\n{res['error']}", flush=True)

        tracks = plan_tracks(pending, cache, plan_jobs(jobs), prog, False, store, planned)
        eta.plan_left = 0
        misses = {m["key"] for t in tracks for m in t["misses"]}
        print(f"  done ({time.time() - t0:.0f} s)" + (f"; {len(misses)} instrument values not in the shipped tables "
                                                    "will be measured here first" if misses else ""), flush=True)

        print("[3/4] Instrument libraries", flush=True)
        prog.emit("stage", stage="libraries", n=3, of=4, text="Getting the instrument libraries", eta_s=eta.seconds())
        t0 = time.time()
        planned_ok = [t for t in tracks if not t["error"]]
        if args.dry_run and not args.yes and needs.definitions_missing(planned_ok, libs):
            print("  The exact size needs the instruments' definition files (about 0.5 MB): run with --yes to fetch them.",
                  flush=True)
            prog.emit("done", tracks=len(todo), failed=0, mb=0, minutes=0, dry_run=True)
            return P.EXIT_OK
        want = needs.wanted(planned_ok, libs, prog)
        wants = {lib: want.get(lib, set()) if L.LIBRARIES[lib].get("by_note") else None for lib in need}
        get = [lib for lib in need if (wants[lib] is None or wants[lib]) and not L.is_ready(libs, lib, wants[lib])]
        exact = sum(L.missing_bytes(libs, lib, wants[lib]) for lib in get)
        all_total = sum(L.wanted_bytes(lib, wants[lib]) for lib in get)
        eta.download_left = exact
        eta.unpack_left_gb = sum(L.archive_mb(lib) for lib in get if L.missing_bytes(libs, lib, set()) > 0) / 1000
        print(f"  {exact / 1e6:.0f} MB to download (up to {most_mb:.0f} MB if every listed sample were played)", flush=True)
        for lib in need:
            if wants[lib] is not None:
                print(f"    {lib}: {len(wants[lib])} files, {L.wanted_bytes(lib, wants[lib]) / 1e6:.0f} MB of "
                      f"{L.size_mb(lib)} MB", flush=True)
        prog.emit("plan", tracks=len(todo), done=len(todo) - len(pending), todo=len(pending), download_mb=round(exact / 1e6),
                  download_max_mb=round(most_mb), libs=len(get), jobs=jobs, eta_s=eta.seconds(), out=str(out),
                  dry_run=args.dry_run, planned=True)
        if args.dry_run:
            prog.emit("done", tracks=len(todo), failed=0, mb=0, minutes=0, dry_run=True)
            return P.EXIT_OK
        if not room(cache, out, exact / 1e6 + max([L.archive_mb(x) for x in get] or [0]), need_out, prog, log_path):
            return P.EXIT_ERROR
        done_bytes = [0.0]
        meas = [None, 0.0]  # when and at how many bytes the speed was last measured
        last = [0.0]
        for idx, lib in enumerate(get, 1):
            lib_bytes = L.wanted_bytes(lib, wants[lib])

            def on_bytes(got, total, lib=lib, idx=idx, lib_bytes=lib_bytes):
                now = time.time()
                cur = done_bytes[0] + got
                eta.download_left = max(0.0, all_total - cur)
                # The speed from what arrives from now on (not what a resumed download had).
                if meas[0] is None or cur < meas[1]:
                    meas[0], meas[1] = now, cur
                elif now - meas[0] > 3:
                    r = max(1e5, (cur - meas[1]) / (now - meas[0]))
                    eta.rate = r if eta.rate is None else 0.7 * eta.rate + 0.3 * r
                    meas[0], meas[1] = now, cur
                if now - last[0] >= 0.5:
                    last[0] = now
                    prog.emit("download", lib=lib, name=L.LIBRARIES[lib]["name"], i=idx, of=len(get), got=got,
                              total=lib_bytes, all_got=cur, all_total=all_total, rate=eta.rate or 0,
                              eta_s=eta.seconds())

            def on_unpack(lib=lib, idx=idx):
                prog.emit("unpack", lib=lib, name=L.LIBRARIES[lib]["name"], i=idx, of=len(get), eta_s=eta.seconds())

            L.fetch(lib, libs, cache / "downloads", keep_downloads=args.keep_downloads, progress=prog,
                    on_bytes=on_bytes, on_unpack=on_unpack, want=wants[lib])
            done_bytes[0] += lib_bytes
            eta.download_left = max(0.0, all_total - done_bytes[0])
            eta.unpack_left_gb = max(0.0, eta.unpack_left_gb - L.archive_mb(lib) / 1000)
        print(f"  ready ({time.time() - t0:.0f} s)", flush=True)

        print(f"[4/4] Rendering {len(pending)} tracks", flush=True)
        prog.emit("stage", stage="render", n=4, of=4, text=f"Rendering {len(pending)} tracks", eta_s=eta.seconds())
        needs.clear_views(libs)
        failed, late = render_all(pending, out, formats, jobs, cache / "results", J.label, prog, eta,
                                  len(todo) - len(pending), len(todo), log_path)
    # every library any track in the folder was played on (CC-BY asks for credit if shared)
    (out / "CREDITS.txt").write_text(L.credits(L.needed(J.present_styles(out)) or used))
    size = sum(p.stat().st_size for p in out.rglob("*") if p.is_file())
    minutes = (time.time() - t_all) / 60
    print(f"\nFinished in {minutes:.1f} min; {out} holds {size / 1e6:.0f} MB.")
    if late:
        print(f"The safety net fetched {late} samples while rendering that the plan didn't foresee (see above); "
              "the music is complete.", flush=True)
    if failed:
        text = f"{len(failed)} track(s) failed: " + ", ".join(shown(j) for j in failed) + ". Retry renders them again."
        prog.emit("error", code="tracks", text=text, log=log_path)
        print(text, flush=True)
        if not prog.json:
            print("Run again to try them again (the others are kept).", file=sys.stderr)
        return P.EXIT_TRACKS
    if args.clean_after and pending:
        # Everything asked for is made: the libraries and work files (the notes, the plan) can go.
        for d in (libs, cache / "downloads", work, cache / "numba", cache / "results"):
            shutil.rmtree(d, ignore_errors=True)
        print(f"Deleted the instrument libraries and work files in {cache} (--clean-after).")
    prog.emit("done", tracks=len(todo), failed=0, mb=size / 1e6, minutes=minutes, late_samples=late)
    print("Start the game and pick the music in the options (Tab) or the launcher.")
    return P.EXIT_OK


if __name__ == "__main__":
    if "--worker" in sys.argv:
        worker(sys.argv)
    elif "--plan-worker" in sys.argv:
        plan_worker(sys.argv)
    else:
        sys.exit(main())
