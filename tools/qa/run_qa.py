#!/usr/bin/env python3
"""The modern-look QA harness: one command the whole team reruns after every change.

    python3 tools/qa/run_qa.py --quick     # regressions + one short recording (~2-3 min)
    python3 tools/qa/run_qa.py             # everything (see docs/qa/README.md for timings)
    python3 tools/qa/run_qa.py --only session-20261102-173420 --no-regressions

Checks (each prints PASS/FAIL with frame numbers; details in report.md / summary.json):
  regressions   race_1p / race_2p --port verify, pinned wram + frame hashes (fatal)
  replay-hash   each replay's end wram hash: the player's recorded "# end ... wram H", or
                a pinned native-mode baseline (tools/qa/baselines.json)
  leaks         non-black frames the player would see in the ORIGINAL graphics
  blank         modern frames that are one flat colour while the game shows something
  popping       sudden block changes in the skyline band / roadside not explained by camera
                motion (temporal spike or A-B-A flicker), see analyze.py
  cars          cars swapping colour in place between consecutive frames
  speed         modern-look scene build time per frame vs the 16.7 ms budget (+ hitches)
  sheets        contact sheets of every recording (not a check, always written)

Exit code: 1 if a regression or replay hash fails, 2 if only image/speed checks fail,
0 if everything passes. Images and reports go to the scratch QA dir, never the repo.
"""

from __future__ import annotations

import argparse
import concurrent.futures as cf
import datetime
import json
import os
import re
import shutil
import statistics
import subprocess
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import analyze  # noqa: E402

REPO = Path(__file__).resolve().parents[2]
TG = Path(os.environ.get("TG_QA_TOP", Path.home() / "Top Gear"))  # the player's game folder
ROM = Path(os.environ.get("TG_QA_ROM", TG / "Top Gear (USA).sfc"))
ART = os.environ.get("TG_QA_ART", str(TG / "art3d")).rstrip("/") + "/"
PLAYER_RECS = Path(os.environ.get("TG_QA_RECORDINGS", TG / "recordings"))
OUT = Path(os.environ.get("TG_QA_OUT", str(Path(__file__).resolve().parents[2] / "build" / "qa")))
HEADLESS = REPO / "build" / "tg_headless"
INPUTS = REPO / "tests" / "inputs"
BASELINES = Path(__file__).resolve().parent / "baselines.json"
SHOT_SIZE = "640x360"
BUDGET_MS = 1000.0 / 60.0

REGRESSIONS = [
    # name, frames, wram, frame  (from docs/MODERN-TEAM-PLAN.md / CMakeLists.txt)
    ("race_1p", 11000, "08f94c7b56cb0b8ac33b9c557c5c0fdd25c7ab80ee479d374a45a6253b5b2a8a",
     "94766e6c901d25c1fdb0f14cf24d3b8c78be1e25318146f0f3905f6d904537c9"),
    ("race_2p", 14000, "af867184fd380ccfc5327252cdb15d44ee7b6ce23c06b7aaa668bac6bf5f8592",
     "ca02793076b8845454d3cc4d949bda5bc49fe16d063e310331f86318ed644ba7"),
]

# Scripted recordings: (name, frames, port args, shots every, verify-mode wram or None).
# --native-from switches to the C code, whose loading is faster, so the verify-mode hashes
# in CMakeLists.txt do not apply; those runs are checked against tools/qa/baselines.json.
SCRIPTED = [
    ("race_1p", 11000, ["--port", "verify"], 4, REGRESSIONS[0][2]),
    ("race_2p", 14000, ["--port", "verify"], 4, REGRESSIONS[1][2]),
    ("attract", 7200, ["--port", "verify"], 4, "eb074942bd3f6b5efa7402979c2d6e56e4373739112cc711cfe448f4a8291cba"),
    ("country_s_america", 9762, ["--port", "verify", "--native-from", "6000"], 4, None),
    ("country_japan", 10242, ["--port", "verify", "--native-from", "6000"], 4, None),
    ("country_germany", 10402, ["--port", "verify", "--native-from", "6000"], 4, None),
    ("country_scandinavia", 10602, ["--port", "verify", "--native-from", "6000"], 4, None),
    ("country_france", 10562, ["--port", "verify", "--native-from", "6000"], 4, None),
    ("country_italy", 10522, ["--port", "verify", "--native-from", "6000"], 4, None),
    ("country_uk", 10242, ["--port", "verify", "--native-from", "6000"], 4, None),
    # Car contacts: rear pushes on a car stopped on the grid, curves pushing an unsteered car
    # (verify, CMake hashes), and the rear pushes with smooth motion in native mode, where the
    # pushes and the cars' weave keep the original's pace (baselines.json).
    ("rear_push", 9000, ["--port", "verify"], 4, "a7d236e61a2a77219df636aa69c219eb0e6bf28ba6d9b959e92c814f88c8b7df"),
    ("curve_push", 9000, ["--port", "verify"], 4, "6a3146d7e4b2f47ae4f1ff35434e157c49c10371cec634a877f79a83a84d1a85"),
    ("rear_push_smooth", 9000, ["--port", "native"], 4, None),
    # Chris's sessions were recorded against the original's timing: with --native-from 2000
    # they desync (chris_session_03 sat in name entry for ~100k frames instead of racing),
    # so they replay in verify mode, against their CMake hashes.
    ("chris_session_01", 54853, ["--port", "verify"], 30, "4ff4ffc25d58655992eaa28e1c404ff7f3a73e815903af5e62361bd135f76cd8"),
    ("chris_session_02", 17047, ["--port", "verify"], 4, "a7367821082e64bf84eee43439ddcc1dc23bea23a99d868e25e1acecd714b908"),
    ("chris_session_03", 304552, ["--port", "verify"], 60, "bc050a7e61dd3e96502383ceb71374eba269ceea30be1c99ccbc89c726722056"),
]

QUICK_REC = "session-20261102-173420"
QUICK_FRAMES = 3600  # title, menus, race start and the first ~20 s of the Las Vegas race


class Rec:
    def __init__(self, name, frames, port, every, expect=None, inp=None, expect_src=""):
        self.name, self.frames, self.port, self.every = name, frames, port, every
        self.expect, self.inp, self.expect_src = expect, inp, expect_src


def player_recordings():
    # A session's own end hash is the reference, unless baselines.json re-pins it: a change
    # that brings the C race code closer to the original changes what a recording made
    # before it replays to (the 2026-11-03 smooth-motion pacing fix: sessions with smooth
    # motion and a race).
    base = load_baselines()
    out = []
    for p in sorted(PLAYER_RECS.glob("session-*.txt")):
        if p.stat().st_size == 0:
            continue
        m = None
        for line in p.read_text(errors="replace").splitlines()[::-1]:
            m = re.match(r"#\s*end frame (\d+) wram ([0-9a-f]{64})", line.strip())
            if m:
                break
        if not m:
            continue
        if p.stem in base:
            out.append(Rec(p.stem, int(m.group(1)), ["--port", "native"], 2, base[p.stem], p, "baselines.json (re-pinned)"))
        else:
            out.append(Rec(p.stem, int(m.group(1)), ["--port", "native"], 2, m.group(2), p, "recorded end hash"))
    return out


def all_recordings(quick: bool):
    recs = player_recordings()
    if quick:
        r = next((r for r in recs if r.name == QUICK_REC), None)
        if r is None:
            return []
        name = r.name + f"@{QUICK_FRAMES}"
        return [Rec(name, QUICK_FRAMES, r.port, 2, load_baselines().get(name), r.inp, "baselines.json (native)")]
    base = load_baselines()
    out = []
    for name, frames, port, every, wram in SCRIPTED:
        inp = INPUTS / f"{name}.txt" if name != "attract" else None
        if wram:
            out.append(Rec(name, frames, port, every, wram, inp, "CMake verify hash"))
        else:
            out.append(Rec(name, frames, port, every, base.get(name), inp, "baselines.json (native)"))
    return out + recs


def load_baselines():
    try:
        return json.loads(BASELINES.read_text())
    except FileNotFoundError:
        return {}


# ---- running tg_headless --------------------------------------------------------------
_print_lock = threading.Lock()


def log(msg):
    with _print_lock:
        print(msg, flush=True)


def regression(name, frames, wram, frame):
    t0 = time.time()
    cmd = [str(HEADLESS), "--rom", str(ROM), "--port", "verify", "--frames", str(frames),
           "--input", str(INPUTS / f"{name}.txt"), "--expect-wram", wram, "--expect-frame", frame]
    p = subprocess.run(cmd, capture_output=True, text=True)
    got_w = re.search(r"^wram ([0-9a-f]{64})", p.stdout, re.M)
    got_f = re.search(r"^frame ([0-9a-f]{64})", p.stdout, re.M)
    bad_port = [ln.strip() for ln in p.stdout.splitlines() if re.search(r"mismatched [1-9]", ln)]
    ok = p.returncode == 0 and got_w and got_w.group(1) == wram and got_f and got_f.group(1) == frame
    res = {"name": name, "ok": bool(ok), "seconds": round(time.time() - t0, 1),
           "wram": got_w.group(1) if got_w else None, "frame": got_f.group(1) if got_f else None,
           "expect_wram": wram, "expect_frame": frame, "port_mismatches": bad_port[:10],
           "returncode": p.returncode}
    log(f"  regression {name}: {'PASS' if ok else '*** FAIL ***'} ({res['seconds']} s)")
    return res


TIME_RE = re.compile(r"draw ([\d.]+) ms, (\d+) triangles, (\d+) batches, look (\d), view (\d) of (\d), raster ([\d.]+) ms")


class TimingLog:
    """Reads the TG_V3_TIME lines while tg_headless runs: one entry per modern shot."""

    def __init__(self, path):
        self.path, self.pos, self.buf = path, 0, ""
        self.shots = []      # dict(draw, raster, tris, batches, views)
        self._cur = None

    def poll(self):
        try:
            with open(self.path, "r", errors="replace") as f:
                f.seek(self.pos)
                data = f.read()
                self.pos = f.tell()
        except FileNotFoundError:
            return
        self.buf += data
        *lines, self.buf = self.buf.split("\n")
        for ln in lines:
            m = TIME_RE.match(ln)
            if not m or int(m.group(4)) != 2:  # 2 = TG_V3_LOOK_MODERN
                continue
            draw, tris, batches, view, views, raster = float(m.group(1)), int(m.group(2)), int(m.group(3)), \
                int(m.group(5)), int(m.group(6)), float(m.group(7))
            if view == 0:
                self._cur = {"draw": 0.0, "raster": 0.0, "tris": 0, "batches": 0, "views": views}
                self.shots.append(self._cur)
            c = self._cur
            c["draw"] += draw
            c["raster"] += raster
            c["tris"] += tris
            c["batches"] += batches


SHOT_RE = re.compile(r"frame(\d{6})(_sel|_3dx|_3dm|_3d)?\.ppm$")


def replay(rec: Rec, outdir: Path, keep: bool):
    """Runs one recording with shots and analyses the frames while they are written."""
    t0 = time.time()
    work = outdir / "work" / rec.name
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    errpath = outdir / "logs" / f"{rec.name}.stderr"
    outpath = outdir / "logs" / f"{rec.name}.stdout"
    cmd = [str(HEADLESS), "--rom", str(ROM), *rec.port, "--frames", str(rec.frames),
           "--shots-every", str(rec.every), "--shots-dir", str(work), "--shots3d", SHOT_SIZE, "--art3d", ART]
    if rec.inp:
        cmd += ["--input", str(rec.inp)]
    env = dict(os.environ, TG_V3_TIME="1")
    sheet_every = max(60, (rec.frames // 192) // rec.every * rec.every or rec.every)
    an = analyze.Analyzer(rec.name, rec.every, sheet_every)
    tl = TimingLog(errpath)
    modern_seen = 0
    with open(outpath, "w") as fo, open(errpath, "w") as fe:
        proc = subprocess.Popen(cmd, stdout=fo, stderr=fe, env=env)
        done_frames = set()
        while True:
            running = proc.poll() is None
            groups = {}
            for e in os.scandir(work):
                m = SHOT_RE.match(e.name)
                if m:
                    groups.setdefault(int(m.group(1)), {})[m.group(2) or ""] = e.path
            if not groups:
                if not running:
                    break
                time.sleep(0.2)
                continue
            last = max(groups)
            ready = sorted(f for f in groups if f not in done_frames and (f < last or not running))
            tl.poll()
            for f in ready:
                g = groups[f]
                orig = analyze.load(g[""]) if "" in g else None
                sel = analyze.load(g["_sel"]) if "_sel" in g else None
                x3d = analyze.load(g["_3dx"]) if "_3dx" in g else None
                views = 1
                if x3d is not None:
                    if modern_seen < len(tl.shots):
                        views = tl.shots[modern_seen]["views"]
                    modern_seen += 1
                an.feed(f, orig, sel, x3d, views)
                done_frames.add(f)
                if not keep:
                    for p in g.values():
                        try:
                            os.remove(p)
                        except FileNotFoundError:
                            pass
            if not running and not ready:
                break
            if not ready:
                time.sleep(0.2)
    tl.poll()
    if not keep:
        shutil.rmtree(work, ignore_errors=True)
    stdout = outpath.read_text(errors="replace")
    got = re.search(r"^wram ([0-9a-f]{64})", stdout, re.M)
    s = an.summary()
    s["name"] = rec.name
    s["frames"] = rec.frames
    s["every"] = rec.every
    s["port"] = " ".join(rec.port)
    s["returncode"] = proc.returncode
    s["wram"] = got.group(1) if got else None
    s["expect_wram"] = rec.expect
    s["expect_src"] = rec.expect_src
    s["hash_ok"] = None if rec.expect is None else (s["wram"] == rec.expect)
    s["speed"] = speed_stats(tl.shots, an)
    s["sheets"] = analyze.contact_sheets(an.tiles, f"{rec.name} (every {sheet_every} frames + screen changes)",
                                         str(outdir / "sheets" / f"{rec.name}"))
    s["evidence"] = analyze.evidence_sheets(an.evidence, rec.name, str(outdir / "sheets" / f"{rec.name}_events"))
    s["seconds"] = round(time.time() - t0, 1)
    hs = "-" if s["hash_ok"] is None else ("ok" if s["hash_ok"] else "MISMATCH")
    log(f"  replay {rec.name}: {s['shots']} shots, leaks {s['leak_frames']}, pops {len(s['pops'])}, "
        f"car {len(s['car_events'])}, hash {hs} ({s['seconds']} s)")
    return s


def speed_stats(shots, an):
    """Frame-time stats of the modern look (scene build on the CPU; the software raster is
    reported separately and only as a relative number)."""
    if not shots:
        return None
    modern_frames = [f["frame"] for f in an.frames if f["src"] == "3d"]
    n = min(len(shots), len(modern_frames))
    rows = [(modern_frames[i], shots[i]) for i in range(n)]
    # the first modern frame after a non-modern one: a race (re)start
    starts = set()
    prev = None
    for f in an.frames:
        if f["src"] == "3d" and (prev is None or prev["src"] != "3d"):
            starts.add(f["frame"])
        prev = f
    steady = [s["draw"] for fr, s in rows if fr not in starts]
    allv = [s["draw"] for _, s in rows]
    if not steady:
        steady = allv
    srt = sorted(steady)

    def pct(p):
        return round(srt[min(len(srt) - 1, int(p / 100 * len(srt)))], 2)

    med = statistics.median(steady)
    worst1 = srt[-max(1, len(srt) // 100):]
    over = [(fr, round(s["draw"], 1)) for fr, s in rows if s["draw"] > BUDGET_MS]
    hitch = [(fr, round(s["draw"], 1)) for fr, s in rows if fr not in starts and s["draw"] > max(2 * med, 8.0)]
    return {
        "frames": n,
        "draw_ms": {"median": round(med, 2), "p95": pct(95), "p99": pct(99), "max": round(max(steady), 2),
                    "worst1pct_avg": round(sum(worst1) / len(worst1), 2)},
        "raster_ms_median": round(statistics.median(s["raster"] for _, s in rows), 1),
        "triangles_median": int(statistics.median(s["tris"] for _, s in rows)),
        "batches_median": int(statistics.median(s["batches"] for _, s in rows)),
        "batches_max": max(s["batches"] for _, s in rows),
        "race_start_ms": [(fr, round(s["draw"], 1)) for fr, s in rows if fr in starts],
        "stalls": [(fr, round(s["draw"], 1)) for fr, s in rows if s["draw"] > 100],
        "over_budget": over[:50],
        "over_budget_count": len(over),
        "hitches": hitch[:50],
        "hitch_count": len(hitch),
    }


# ---- report ---------------------------------------------------------------------------
def fmt_ranges(rs, limit=12):
    parts = [f"{a}" if a == b else f"{a}-{b}" for a, b in rs[:limit]]
    return ", ".join(parts) + (f" … (+{len(rs) - limit})" if len(rs) > limit else "")


def evaluate(regs, reps):
    """The named checks, each PASS/FAIL with the frames."""
    checks = []

    def add(name, ok, detail, fatal=False):
        checks.append({"check": name, "ok": ok, "detail": detail, "fatal": fatal})

    for r in regs:
        add(f"regression/{r['name']}", r["ok"],
            "wram+frame match" if r["ok"] else f"wram {r['wram']} frame {r['frame']} port {r['port_mismatches']}", True)
    for s in reps:
        n = s["name"]
        if s["hash_ok"] is not None:
            add(f"replay-hash/{n}", s["hash_ok"], f"wram {s['wram'][:8] if s['wram'] else None}… vs {s['expect_src']}", True)
        elif s["wram"]:
            add(f"replay-hash/{n}", True, f"no reference yet (wram {s['wram'][:8]}…; --update-baselines pins it)")
        add(f"leaks/{n}", not s["leaks"], "none" if not s["leaks"] else f"{s['leak_frames']} shots: frames {fmt_ranges(s['leaks'])}")
        add(f"blank/{n}", not s["blank_modern"], "none" if not s["blank_modern"] else f"frames {fmt_ranges(s['blank_modern'])}")
        if s["every"] > analyze.MAX_TEMPORAL_EVERY:
            s["pops"], s["car_events"], s["pop_pairs_checked"], s["car_pairs_checked"] = [], [], 0, 0
        if s["pop_pairs_checked"] == 0:
            add(f"popping/{n}", True, f"not checked (no consecutive modern shots; every {s['every']})")
        else:
            pops = [p for p in s["pops"] if not p.get("mirrors_original")]
            mir = len({p["frame"] for p in s["pops"]}) - len({p["frame"] for p in pops})
            fr = sorted({p["frame"] for p in pops})
            add(f"popping/{n}", not pops, f"{s['pop_pairs_checked']} frame triples, {mir} frames like the original; " +
                ("none" if not pops else f"{len(fr)} frames: {fmt_ranges(analyze.ranges(fr, s['every'], 1))}"))
        if s["car_pairs_checked"]:
            ce = s["car_events"]
            add(f"cars/{n}", not ce, f"{s['car_pairs_checked']} pairs; " +
                ("none" if not ce else "frames " + fmt_ranges(analyze.ranges([c['frame'] for c in ce], s['every'], 1))))
        sp = s["speed"]
        if sp:
            # Fail on a steady p99 over budget or any stall over 100 ms; single frames over
            # budget are listed but do not fail (parallel runs on a shared CPU add noise).
            ok = sp["draw_ms"]["p99"] <= BUDGET_MS and not sp["stalls"]
            add(f"speed/{n}", ok, f"draw median {sp['draw_ms']['median']} ms p99 {sp['draw_ms']['p99']} max "
                f"{sp['draw_ms']['max']}; over 16.7 ms: {sp['over_budget_count']} "
                f"{[f for f, _ in sp['over_budget'][:8]]}; stalls >100 ms {sp['stalls'][:4]}")
    return checks


def write_report(outdir, mode, regs, reps, checks, elapsed):
    lines = [f"# QA report ({mode}) {datetime.datetime.now():%Y-%m-%d %H:%M}", "",
             f"Commit `{git_head()}`, {elapsed:.0f} s.", "", "## Checks", "", "| check | result | detail |", "|---|---|---|"]
    for c in checks:
        lines.append(f"| {c['check']} | {'PASS' if c['ok'] else 'FAIL'} | {c['detail']} |")
    lines += ["", "## Replays", ""]
    for s in reps:
        lines += [f"### {s['name']}", "",
                  f"- shots {s['shots']} (every {s['every']}), shown: {s['by_source']}",
                  f"- contact sheets: {', '.join(s['sheets'])}",
                  f"- event sheets: {', '.join(s['evidence']) or '-'}"]
        if s["speed"]:
            sp = s["speed"]
            lines.append(f"- speed: {json.dumps(sp['draw_ms'])}, raster median {sp['raster_ms_median']} ms (software), "
                         f"triangles {sp['triangles_median']}, batches median {sp['batches_median']} max {sp['batches_max']}, "
                         f"race starts {sp['race_start_ms']}, hitches {sp['hitches'][:10]}")
        if s["pops"]:
            lines.append("- pops (* = the original does it too): " + "; ".join(
                f"{p['frame']} {p['kind']} {p['band']} x{p['blocks']}{'*' if p.get('mirrors_original') else ''}" for p in s["pops"][:60]))
        if s["car_events"]:
            lines.append("- car colour: " + "; ".join(f"{c['frame']} {c['old_hue']}°→{c['new_hue']}° x{c['blocks']}" for c in s["car_events"][:40]))
        lines.append("")
    (outdir / "report.md").write_text("\n".join(lines))


def git_head():
    try:
        return subprocess.run(["git", "-C", str(REPO), "rev-parse", "--short", "HEAD"], capture_output=True,
                              text=True).stdout.strip() + (" (dirty)" if subprocess.run(
                                  ["git", "-C", str(REPO), "status", "--porcelain", "--untracked-files=no"],
                                  capture_output=True, text=True).stdout.strip() else "")
    except Exception:
        return "?"


def compare_previous(mode, checks):
    hist = OUT / "history"
    hist.mkdir(parents=True, exist_ok=True)
    prev = sorted(hist.glob(f"{mode}-*.json"))
    now = {c["check"]: c["ok"] for c in checks}
    if prev:
        old = json.loads(prev[-1].read_text())
        newly_bad = [k for k, v in now.items() if not v and old.get(k, True)]
        fixed = [k for k, v in now.items() if v and old.get(k) is False]
        if newly_bad:
            log("  newly failing since the last run: " + ", ".join(newly_bad))
        if fixed:
            log("  newly passing since the last run: " + ", ".join(fixed))
    (hist / f"{mode}-{datetime.datetime.now():%Y%m%d-%H%M%S}.json").write_text(json.dumps(now, indent=1))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--quick", action="store_true", help="regressions + one short recording")
    ap.add_argument("--only", action="append", default=[], help="only recordings whose name contains this (repeatable)")
    ap.add_argument("--skip", action="append", default=[], help="skip recordings whose name contains this")
    ap.add_argument("--no-regressions", action="store_true")
    ap.add_argument("--no-build", action="store_true")
    ap.add_argument("--jobs", type=int, default=3, help="tg_headless processes at once (max 3: memory)")
    ap.add_argument("--keep-frames", action="store_true", help="keep the PPM shots in OUT/<run>/work")
    ap.add_argument("--update-baselines", action="store_true", help="pin the native-mode replay hashes of this run")
    ap.add_argument("--out", type=Path, default=None)
    a = ap.parse_args()
    jobs = max(1, min(3, a.jobs))
    mode = "quick" if a.quick else "full"
    t0 = time.time()
    for p, what in ((ROM, "ROM"), ):
        if not p.exists():
            sys.exit(f"{what} not found: {p}")
    if not a.no_build:
        b = subprocess.run(["cmake", "--build", str(REPO / "build"), "-j8", "--target", "tg_headless"],
                           capture_output=True, text=True)
        if b.returncode != 0:
            print(b.stdout[-3000:], b.stderr[-3000:])
            sys.exit("*** BUILD FAILED ***")
    outdir = a.out or OUT / "runs" / f"{mode}-{datetime.datetime.now():%Y%m%d-%H%M%S}"
    for d in ("sheets", "logs", "work"):
        (outdir / d).mkdir(parents=True, exist_ok=True)
    latest = OUT / f"latest-{mode}"
    if latest.is_symlink() or latest.exists():
        latest.unlink()
    latest.symlink_to(outdir)
    recs = all_recordings(a.quick)
    if a.only:
        recs = [r for r in recs if any(o in r.name for o in a.only)]
    recs = [r for r in recs if not any(s in r.name for s in a.skip)]
    log(f"QA {mode}: {len(recs)} recordings, {'no ' if a.no_regressions else ''}regressions, {jobs} jobs -> {outdir}")
    regs, reps = [], []
    with cf.ThreadPoolExecutor(max_workers=jobs) as ex:
        futs = []
        if not a.no_regressions:
            futs += [ex.submit(regression, *r) for r in REGRESSIONS]
        # longest first so the pool ends together
        futs += [ex.submit(replay, r, outdir, a.keep_frames) for r in sorted(recs, key=lambda r: -r.frames)]
        for f in futs:
            try:
                r = f.result()
            except Exception as e:  # a crashed analysis must not hide the others
                log(f"  *** harness error: {e!r}")
                continue
            (regs if "expect_frame" in r else reps).append(r)
    reps.sort(key=lambda s: s["name"])
    if a.update_baselines:
        base = load_baselines()
        for s in reps:
            if s["expect_src"].startswith("baselines") and s["wram"] and s["returncode"] in (0, 1):
                base[s["name"]] = s["wram"]
        BASELINES.write_text(json.dumps(base, indent=1, sort_keys=True) + "\n")
        log(f"  baselines written: {BASELINES}")
    checks = evaluate(regs, reps)
    st = subprocess.run([sys.executable, str(Path(__file__).resolve().parent / "selftest.py")], capture_output=True, text=True)
    checks.insert(0, {"check": "selftest/analyze", "ok": st.returncode == 0, "fatal": False,
                      "detail": "synthetic pop/flicker/car/leak cases found" if st.returncode == 0 else st.stdout[-400:]})
    elapsed = time.time() - t0
    (outdir / "summary.json").write_text(json.dumps({"mode": mode, "commit": git_head(), "seconds": round(elapsed),
                                                     "regressions": regs, "replays": reps, "checks": checks},
                                                    indent=1, default=str))
    write_report(outdir, mode, regs, reps, checks, elapsed)
    compare_previous(mode, checks)
    shutil.rmtree(outdir / "work", ignore_errors=True)
    print()
    print("=" * 78)
    for c in checks:
        print(f"{'PASS' if c['ok'] else 'FAIL'}  {c['check']:<44} {c['detail'][:150]}")
    print("=" * 78)
    fatal = [c for c in checks if not c["ok"] and c["fatal"]]
    other = [c for c in checks if not c["ok"] and not c["fatal"]]
    if fatal:
        print("*" * 78)
        print("*** REGRESSION / HASH FAILURE: the game itself changed. Do not commit. ***")
        for c in fatal:
            print(f"***   {c['check']}: {c['detail']}")
        print("*" * 78)
    print(f"{len(checks) - len(fatal) - len(other)} passed, {len(fatal)} fatal, {len(other)} failing image/speed checks;"
          f" {elapsed:.0f} s. Report: {outdir / 'report.md'}")
    sys.exit(1 if fatal else (2 if other else 0))


if __name__ == "__main__":
    main()
