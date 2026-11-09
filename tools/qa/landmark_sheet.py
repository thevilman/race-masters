#!/usr/bin/env python3
"""Every course's landmark as seen from the grid, for a look by eye.

    python3 tools/qa/landmark_sheet.py [--frame 2700] [--size 1920x1080] [--jobs 2] [courses...]

Replays session-20261030-081901 with `--set-course 1900:N` (as course_sheets.py) to one modern
shot at FRAME (the grid: every landmark is placed in view of it, QA-007) and writes, under
$TG_QA_OUT/landmarks/<time>/: cNN.png (the whole shot) and all.png (the top half of each, 4 a
row, labelled with the course and its landmark). TG_LM_INFO=1 in the environment adds each
course's landmark direction and light to its log.txt. At most 2 tg_headless at once."""
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
NAMES = ["Las Vegas: the Strip", "Los Angeles: the hillside sign", "New York: Empire State", "San Francisco: Golden Gate",
         "Rio: Cristo Redentor", "Machu Picchu: peak", "Chichen Itza: pyramid", "Rain Forest: kapok",
         "Tokyo: Tokyo Tower", "Hiroshima: torii, dome", "Yokohama: Bay Bridge", "Kyoto: pagoda",
         "Munich: Frauenkirche", "Cologne: cathedral", "Black Forest: church", "Frankfurt: towers",
         "Stockholm: City Hall", "Copenhagen: spiral spire", "Helsinki: cathedral", "Oslo: ski jump",
         "Paris: Eiffel Tower", "Nice: Negresco dome", "Bordeaux: chateau", "Monaco: the Rock",
         "Pisa: Leaning Tower", "Rome: Colosseum", "Sicily: Etna", "Florence: Duomo",
         "London: Big Ben", "Sheffield: works", "Loch Ness: castle", "Stonehenge"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frame", type=int, default=2700)
    ap.add_argument("--size", default="1920x1080")
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("courses", nargs="*", type=int)
    a = ap.parse_args()
    courses = a.courses or list(range(32))
    out = OUT / "landmarks" / time.strftime("%Y%m%d-%H%M%S")
    out.mkdir(parents=True, exist_ok=True)
    print(f"courses {courses} -> {out}", flush=True)
    queue, running, t0 = list(courses), [], time.time()
    while queue or running:
        while queue and len(running) < max(1, min(2, a.jobs)):
            c = queue.pop(0)
            d = out / f"c{c:02d}"
            d.mkdir(exist_ok=True)
            log = open(d / "log.txt", "w")
            # (shots are taken on frames that are a multiple of --shots-every)
            p = subprocess.Popen([str(REPO / "build/tg_headless"), "--rom", ROM, "--port", "native", "--input", REC,
                                  "--frames", str(a.frame + 1), "--set-course", f"1900:{c}", "--shots-every", "1",
                                  "--shots-from", str(a.frame), "--shots-dir", str(d) + "/", "--shots3d", a.size,
                                  "--art3d", ART], stdout=log, stderr=subprocess.STDOUT)
            running.append((c, p))
        time.sleep(0.5)
        for c, p in list(running):
            if p.poll() is not None:
                running.remove((c, p))
                print(f"course {c} rc {p.returncode} {time.time() - t0:.0f}s", flush=True)
    tiles = []
    for c in courses:
        f = out / f"c{c:02d}" / f"frame{a.frame:06d}_3dx.ppm"
        if not f.exists():
            print(f"course {c}: no shot")
            continue
        im = Image.open(f).convert("RGB")
        im.save(out / f"c{c:02d}.png")
        top = im.crop((0, 0, im.width, im.height // 2))
        tiles.append((c, top.resize((960, top.height * 960 // top.width), Image.LANCZOS)))
        for q in (out / f"c{c:02d}").glob("*.ppm"):
            q.unlink()
    if not tiles:
        return 1
    W, H = tiles[0][1].size
    cols = 4
    sheet = Image.new("RGB", (W * cols, (H + 18) * ((len(tiles) + cols - 1) // cols)), (0, 0, 0))
    dr = ImageDraw.Draw(sheet)
    for i, (c, im) in enumerate(tiles):
        x, y = (i % cols) * W, (i // cols) * (H + 18)
        sheet.paste(im, (x, y + 18))
        dr.text((x + 4, y + 3), f"c{c:02d} {NAMES[c]}", fill=(255, 255, 0))
    sheet.save(out / "all.png")
    print(out / "all.png")
    return 0


if __name__ == "__main__":
    sys.exit(main())
