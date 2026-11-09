#!/usr/bin/env python3
"""The README's screenshots of the modern look, and the proof that nothing in them comes from
the game's pictures.

    python3 tools/qa/readme_shots.py [--write] [--jobs 2] [names...]

Each shot is one tg_headless run that ends at its frame, with:
- TG_V3_PICS=1: every textured batch of the modern view's draw list, by its picture slot. Only
  the slots the mod makes may appear: its procedural textures (TEX_*), its fonts (FONT_PIC,
  from Barlow), the car models' atlases (CAR_TEX, from tools/cars) and the liveries made from a
  paint colour (LIVERY_PIC). A picture learned from the game's screen (CAR_PIC, REAR_PIC,
  ART_PIC, SKY_PIC, HUD_PIC, SIDE_*, MENU_*, ...) fails the shot.
- --export-art: what the view had learned from the screen when the run ended. The far
  skyline's outline layer is untextured but its heights trace the game's skyline picture, so
  a shot fails if a skyline had been learned (sky-*.bmp). It is only learned while the
  player's car moves and is past the start gantry: the grid and the first second after GO.
- --art3d with only the art folder's cars/ (so a menu photo or picture upscaled from the game
  that is in the folder is never loaded); no --v3-cache (nothing learned in an earlier run).
The original picture, the two picture looks' shots and the export are deleted as soon as the
run ends. Writes $TG_QA_OUT/readme/<time>/NAME.png (the 1920x1080 render averaged down to
1600x900, under ~600 KB in full colour) and proof.txt; --write copies them to docs/screenshots.
Exit 1 if a shot fails. The launcher's screenshot is the launcher's own (docs/qa/README.md)."""
import argparse
import glob
import os
import re
import shutil
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from PIL import Image

REPO = Path(__file__).resolve().parents[2]
TG = Path(os.environ.get("TG_QA_TOP", Path.home() / "Top Gear"))  # the player's game folder
ROM = str(Path(os.environ.get("TG_QA_ROM", TG / "Top Gear (USA).sfc")))
ART = Path(os.environ.get("TG_QA_ART", str(TG / "art3d")))
RECS = Path(os.environ.get("TG_QA_RECORDINGS", TG / "recordings"))
OUT = Path(os.environ.get("TG_QA_OUT", str(Path(__file__).resolve().parents[2] / "build" / "qa")))
# name: recording, course (--set-course 1900:N), frame
SHOTS = {
    "rio-grid": ("session-20261030-081901.txt", 4, 2700),        # the grid, Christ the Redeemer
    "new-york-night": ("session-20261030-081901.txt", 2, 2700),  # the grid at night
    "las-vegas-start": ("session-20261030-081901.txt", 0, 2840),  # the pack through the first bend
}
MADE = {"TEX_GRAIN", "TEX_ASPHALT", "TEX_CHEVRON_L", "TEX_CHEVRON_R", "TEX_WARNING", "TEX_GLOW", "TEX_CLOUD",
        "TEX_CLOUD2", "TEX_ATLAS", "LIVERY_PIC", "FONT_PIC", "CAR_TEX"}


def slots():
    """Picture slot -> its name, from view3d.c's defines."""
    src = (REPO / "src/mod3d/view3d.c").read_text().split("#define PIC_COUNT")[0]
    env = {}
    for name, expr in re.findall(r"#define ([A-Z_0-9]+) (\([^)\n]*\)|\d+)", src):
        try:
            env[name] = eval(expr, {}, dict(env))
        except Exception:
            pass
    starts = sorted((v, k) for k, v in env.items() if k.endswith(("_PIC", "_SRC", "_HI", "_BG", "_TEX", "_SMOOTH")) or k.startswith("TEX_"))
    starts.append((env["CAR_TEX"] + 10, "END"))
    return {i: n for (a, n), (b, _) in zip(starts, starts[1:]) for i in range(a, b)}


def shoot(name, out, names, art):
    rec, course, frame = SHOTS[name]
    d = out / name
    (d / "shots").mkdir(parents=True, exist_ok=True)
    (d / "export").mkdir(exist_ok=True)
    cmd = [str(REPO / "build/tg_headless"), "--rom", ROM, "--port", "native", "--input", str(RECS / rec), "--frames",
           str(frame + 1), "--set-course", "1900:%d" % course, "--shots-every", "1", "--shots-from", str(frame), "--shots-dir",
           str(d / "shots") + "/", "--shots3d", "1920x1080", "--art3d", str(art) + "/", "--export-art", str(d / "export") + "/"]
    with open(d / "log.txt", "w") as log:
        subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, env=dict(os.environ, TG_V3_PICS="1"))
    for p in glob.glob(str(d / "shots/*.ppm")):  # the original picture and the picture looks: deleted
        if not p.endswith("_3dx.ppm"):
            os.remove(p)
    shot = d / "shots" / ("frame%06d_3dx.ppm" % frame)
    have = shot.exists()
    if have:
        Image.open(shot).convert("RGB").resize((1600, 900), Image.BOX).save(out / (name + ".png"), optimize=True)
    for p in glob.glob(str(d / "shots/*.ppm")):
        os.remove(p)
    used = {}
    for line in open(d / "log.txt"):
        if line.startswith("pics:"):
            for tok in line.split()[1:]:
                pid, tris = map(int, tok.split("/"))
                used[names.get(pid, "?%d" % pid)] = used.get(names.get(pid, "?%d" % pid), 0) + tris
    learned = sorted(os.path.basename(p) for p in glob.glob(str(d / "export/*")))
    shutil.rmtree(d / "export")  # (pictures from the game's screen: only their names are kept)
    sky = [e for e in learned if e.startswith("sky-")]
    bad = sorted(k for k in used if k not in MADE)
    ok = have and not bad and not sky
    return ok, "%s %s: frame %d course %d of %s; drawn %s; not made by the mod: %s; skyline learned: %s; %d other pictures " \
               "learned and not drawn" % ("PASS" if ok else "FAIL", name, frame, course, rec,
                                          ", ".join("%s %d tris" % kv for kv in sorted(used.items())), bad or "none",
                                          sky or "none", len(learned) - len(sky))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--write", action="store_true", help="copy the shots to docs/screenshots")
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("names", nargs="*")
    a = ap.parse_args()
    todo = a.names or list(SHOTS)
    out = OUT / "readme" / time.strftime("%Y%m%d-%H%M%S")
    art = out / "art3d"
    art.mkdir(parents=True)
    (art / "cars").symlink_to(ART / "cars")
    names = slots()
    with ThreadPoolExecutor(max(1, min(2, a.jobs))) as ex:
        results = list(ex.map(lambda n: shoot(n, out, names, art), todo))
    with open(out / "proof.txt", "w") as f:
        for ok, line in results:
            print(line)
            f.write(line + "\n")
    if all(ok for ok, _ in results) and a.write:
        for n in todo:
            shutil.copy(out / (n + ".png"), REPO / "docs/screenshots" / (n + ".png"))
        print("written to docs/screenshots")
    print(out)
    sys.exit(0 if all(ok for ok, _ in results) else 1)


if __name__ == "__main__":
    main()
