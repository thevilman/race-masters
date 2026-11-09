#!/usr/bin/env python3
"""Summary of a frame-time log written by the game (Shift+F2, --perf-log FILE or
TG_PERF_LOG=FILE; see docs/qa/README.md, "Frame-time log").

    python3 tools/qa/perf_report.py recordings/perf-20261103-201500.csv

Prints, for the race pictures: frame time (median, p95, p99, max, the worst-1 % average
as CapFrameX's "1 % low"), where the time goes (game, capture, scene build, GL submit,
present/vsync wait, GPU), texture uploads, and the first 10 s of each race against the
rest (the start-of-race slowdown), plus the 15 worst frames with their breakdown.
"""

import csv
import sys

COLS = ["game_ms", "capture_ms", "build_ms", "gl_ms", "present_ms", "gpu_ms"]


def stats(v):
    v = sorted(v)
    n = len(v)
    if not n:
        return "-"
    worst = v[-max(1, n // 100):]
    return (f"med {v[n // 2]:6.2f}  p95 {v[int(n * .95)]:6.2f}  p99 {v[min(n - 1, int(n * .99))]:6.2f}  "
            f"max {v[-1]:7.2f}  1%low {sum(worst) / len(worst):6.2f}")


def block(name, rows):
    if not rows:
        return
    print(f"\n{name}: {len(rows)} pictures")
    print(f"  frame_ms    {stats([float(r['frame_ms']) for r in rows])}")
    for c in COLS:
        vals = [float(r[c]) for r in rows if float(r[c]) >= 0]
        print(f"  {c:11s} {stats(vals)}")
    up = [int(r["upload_kb"]) for r in rows]
    print(f"  uploads     {sum(up) / 1024:.1f} MB in {sum(1 for u in up if u)} pictures, max {max(up) / 1024:.2f} MB")
    print(f"  triangles   {stats([float(r['triangles']) for r in rows])}")
    print(f"  over 16.7 ms: {sum(1 for r in rows if float(r['frame_ms']) > 16.7)}, over 33 ms: "
          f"{sum(1 for r in rows if float(r['frame_ms']) > 33.4)}")


def from_headless(rows):
    """tg_headless --perf-log writes frame,in_race,capture_ms,build_ms,triangles,batches,
    vertex_bytes,upload_bytes (CPU side only: no GL, present or GPU). Map it onto the game's
    columns: t_ms from the game frame at 60 a second, frame_ms = capture + build, the rest -1
    (not measured; stats() skips negative values) so the same report applies."""
    out = []
    for r in rows:
        cap, build = float(r["capture_ms"]), float(r["build_ms"])
        out.append({"t_ms": int(r["frame"]) * 1000.0 / 60.0, "frame": r["frame"], "in_race": r["in_race"],
                    "game_frames": "1", "game_ms": -1, "capture_ms": cap, "build_ms": build, "gl_ms": -1,
                    "present_ms": -1, "gpu_ms": -1, "frame_ms": cap + build, "triangles": r["triangles"],
                    "upload_kb": int(r["upload_bytes"]) // 1024, "width": "headless", "height": "cpu"})
    return out


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    rows = list(csv.DictReader(line for line in open(sys.argv[1]) if not line.startswith("#")))
    if not rows:
        print("empty log")
        sys.exit(1)
    if "t_ms" not in rows[0]:
        rows = from_headless(rows)
        print("tg_headless log: frame_ms = capture + build (CPU only; no GL, present or GPU)")
    print(f"{sys.argv[1]}: {len(rows)} pictures, window {rows[-1]['width']}x{rows[-1]['height']}")
    race = [r for r in rows if r["in_race"] == "1"]
    block("all pictures", rows)
    # races: runs of in_race pictures; the first 10 s of each against the rest
    starts, prev = [], "0"
    for r in rows:
        if r["in_race"] == "1" and prev == "0":
            starts.append(float(r["t_ms"]))
        prev = r["in_race"]
    first = [r for r in race if any(s <= float(r["t_ms"]) < s + 10000 for s in starts)]
    rest = [r for r in race if r not in first]
    print(f"\nraces: {len(starts)}")
    block("race, first 10 s of each", first)
    block("race, after", rest)
    print("\nworst 15 pictures (t_ms frame: frame_ms = game + capture + build + gl + present; gpu, uploads, triangles):")
    for r in sorted(rows, key=lambda r: -float(r["frame_ms"]))[:15]:
        print(f"  {float(r['t_ms']):9.0f} {r['frame']:>6} race={r['in_race']}: {float(r['frame_ms']):7.2f} = "
              f"{float(r['game_ms']):.1f} + {float(r['capture_ms']):.1f} + {float(r['build_ms']):.1f} + "
              f"{float(r['gl_ms']):.1f} + {float(r['present_ms']):.1f};  gpu {float(r['gpu_ms']):.1f}, "
              f"{r['upload_kb']} KB, {r['triangles']} tris, {r['game_frames']} game frames")


if __name__ == "__main__":
    main()
