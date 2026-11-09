#!/usr/bin/env python3
"""Contact sheets of every course in the modern look, for a look by eye.

    python3 tools/qa/course_sheets.py [--jobs N] [--from F --to T --every E] [--size WxH] [courses...]

Replays the player's session-20261030-081901 (a 1-player first race) with
`--set-course 1900:N` (the course, $1F06 bits 0-4, set before the race loads; course
numbers as in docs/retro/CITIES.md), takes a modern shot and the original's picture every
E frames from F to T, and writes, under $TG_QA_OUT/courses/<time>/:
  sheet_cNN.png   the modern shots of course N (3 a row) with the original's picture of the
                  same frame as a small inset in the corner (for "is this the right place")
  all.png         2 shots of every course, 4 a row
The PPMs are deleted once the sheet is made. At most 3 tg_headless at once (memory).
Limit: --set-course changes the course only; the skyline picture and roadside pictures
the original shows belong to the course the recording loaded, so the learned skyline is
not that course's (see BUGS.md, "Not tested")."""
import argparse
import os
import subprocess
import sys
import time
from pathlib import Path

from PIL import Image, ImageDraw

REPO = Path(__file__).resolve().parents[2]
TG = Path(os.environ.get("TG_QA_TOP", Path.home() / "Top Gear"))  # the player's game folder
ROM = str(Path(os.environ.get("TG_QA_ROM", TG / "Top Gear (USA).sfc")))
ART = os.environ.get("TG_QA_ART", str(TG / "art3d")).rstrip("/") + "/"
REC = str(Path(os.environ.get("TG_QA_RECORDINGS", TG / "recordings")) / "session-20261030-081901.txt")
OUT = Path(os.environ.get("TG_QA_OUT", str(Path(__file__).resolve().parents[2] / "build" / "qa")))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--jobs", type=int, default=3)
    ap.add_argument("--from", dest="frm", type=int, default=2600)
    ap.add_argument("--to", type=int, default=5000)
    ap.add_argument("--every", type=int, default=200)
    ap.add_argument("--size", default="960x540")
    ap.add_argument("courses", nargs="*", type=int)
    a = ap.parse_args()
    jobs = max(1, min(3, a.jobs))
    courses = a.courses or list(range(32))
    out = OUT / "courses" / time.strftime("%Y%m%d-%H%M%S")
    out.mkdir(parents=True, exist_ok=True)
    print(f"courses {courses} -> {out}", flush=True)
    t0 = time.time()
    queue, running = list(courses), []
    while queue or running:
        while queue and len(running) < jobs:
            c = queue.pop(0)
            d = out / f"c{c:02d}"
            d.mkdir(exist_ok=True)
            log = open(d / "log.txt", "w")
            p = subprocess.Popen([str(REPO / "build/tg_headless"), "--rom", ROM, "--port", "native", "--input", REC,
                                  "--frames", str(a.to + 1), "--set-course", f"1900:{c}", "--shots-every", str(a.every),
                                  "--shots-from", str(a.frm), "--shots-dir", str(d) + "/", "--shots3d", a.size,
                                  "--art3d", ART], stdout=log, stderr=subprocess.STDOUT)
            running.append((c, p))
        time.sleep(0.5)
        for c, p in list(running):
            if p.poll() is not None:
                running.remove((c, p))
                print(f"course {c} rc {p.returncode} {time.time() - t0:.0f}s", flush=True)
    thumbs = []
    for c in courses:
        d = out / f"c{c:02d}"
        shots = sorted(f for f in os.listdir(d) if f.endswith("_3dx.ppm"))
        if not shots:
            print(f"course {c}: no shots")
            continue
        ims = []
        for f in shots:
            im = Image.open(d / f).convert("RGB")
            orig = d / f.replace("_3dx", "")
            if orig.exists():
                o = Image.open(orig).convert("RGB")
                ow = im.width // 4
                im.paste(o.resize((ow, o.height * ow // o.width)), (im.width - ow, 0))
            ims.append((f[5:11], im))
        W, H = ims[0][1].size
        cols = 3
        sheet = Image.new("RGB", (W * cols, H * ((len(ims) + cols - 1) // cols)))
        dr = ImageDraw.Draw(sheet)
        for i, (fr, im) in enumerate(ims):
            x, y = (i % cols) * W, (i // cols) * H
            sheet.paste(im, (x, y))
            dr.text((x + 6, y + 4), f"c{c:02d} {fr}", fill=(255, 255, 0))
        sheet.save(out / f"sheet_c{c:02d}.png")
        for k in (len(ims) // 3, 2 * len(ims) // 3):
            thumbs.append((c, ims[k][1].resize((480, 270))))
        for f in os.listdir(d):
            if f.endswith(".ppm"):
                os.remove(d / f)
    if thumbs:
        ov = Image.new("RGB", (480 * 4, 270 * ((len(thumbs) + 3) // 4)))
        dr = ImageDraw.Draw(ov)
        for i, (c, im) in enumerate(thumbs):
            ov.paste(im, ((i % 4) * 480, (i // 4) * 270))
            dr.text(((i % 4) * 480 + 4, (i // 4) * 270 + 2), f"c{c:02d}", fill=(255, 255, 0))
        ov.save(out / "all.png")
    print(f"done {time.time() - t0:.0f}s: {out}")


if __name__ == "__main__":
    sys.exit(main())
