#!/usr/bin/env python3
"""The GL renderer's shaders checked without the game (no window, no player's PC).

    python3 tools/qa/gl_check.py [--build build] [--out DIR]

src/gl3d.c's GLSL (the shaded materials' program and the basic fallback) is pulled out of the
source, compiled into tools/qa/gl_check.c and run on an offscreen EGL context (Mesa: the GPU's
own driver - on WSL the D3D12 one - and llvmpipe), on the showcase pictures of the five cars
(`tg_headless --showcase` with TG_SHOWCASE_DUMP=1: 5 cars x 5 angles, their triangles as
drawn, batch by batch with their pictures: the cars' textures (tgcar version 3) with their
mip levels and tint masks, the race number's font), whole and seen through by screen door
(TG_SHOWCASE_DOOR=7, `door_*`: the ordered dither a rival hiding the player's car is drawn
with, which must leave out the same pixels in both). Each picture is compared with
tg_v3_raster's of the same triangles (the per-pixel lighting and trilinear texture sampling of
headless renders, shade_mat and sample_mip in src/mod3d/view3d.c, which the shader and the GPU
repeat). Prints the differences per picture (mean / p95 of the largest channel difference
over the car's pixels, the share over 24) and writes $OUT/gl_check/sheet.png
(software | material shader | basic + CPU bake).

Exit 1 if a shader doesn't compile or link, or a material-shader picture's mean difference is
over 3 or more than 2 % of its pixels differ by over 24. The basic fallback (the materials lit
at the vertices on the CPU) is only reported for textured cars: it lights the paint at the
corners and leaves the rest of the texture unlit, so it is not meant to match. Needs gcc, EGL and GL headers and libraries (Mesa),
numpy and PIL; ROM and art as the other QA tools (TG_QA_ROM, TG_QA_ART, TG_QA_TOP).
"""
import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

REPO = Path(__file__).resolve().parents[2]
TG = Path(os.environ.get("TG_QA_TOP", Path.home() / "Top Gear"))  # the player's game folder
ROM = str(Path(os.environ.get("TG_QA_ROM", TG / "Top Gear (USA).sfc")))
ART = os.environ.get("TG_QA_ART", str(TG / "art3d")).rstrip("/") + "/"
OUT = Path(os.environ.get("TG_QA_OUT", str(Path(__file__).resolve().parents[2] / "build" / "qa")))


def shaders(dst):
    src = (REPO / "src/gl3d.c").read_text()
    out = []
    for name in ("kVert", "kFrag", "kVertMat", "kFragMat"):
        m = re.search(r"static const char\* %s =\s*((?:\s*\"(?:[^\"\\]|\\.)*\")+)\s*;" % name, src)
        if not m:
            sys.exit("gl_check: %s not found in src/gl3d.c" % name)
        out.append("static const char* %s = %s;\n" % (name, m.group(1)))
    dst.write_text("".join(out))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", default=str(REPO / "build"))
    ap.add_argument("--out", default=str(OUT / "gl_check"))
    a = ap.parse_args()
    out, build = Path(a.out), Path(a.build)
    out.mkdir(parents=True, exist_ok=True)
    shaders(out / "gl_check_shaders.h")
    exe = out / "gl_check"
    libs = [str(build / n) for n in ("libtgmod3d.a", "libtgcore.a", "liblakesnes.a")]
    subprocess.run(["gcc", "-O1", "-std=c11", "-Wall", "-I", str(out), "-I", str(REPO / "src"), "-I", str(REPO / "src/mod3d"),
                    "-o", str(exe), str(REPO / "tools/qa/gl_check.c")] + libs + ["-lEGL", "-lGL", "-lm"], check=True)
    # The showcase (it is drawn after the replay: a short one, the modern look on; its exit
    # status is the port's report on a replay cut short, not the pictures'): whole, and seen
    # through by screen door (TG_SHOWCASE_DOOR: 7 of 16 pixels left out, as a rival hiding the
    # player's car is drawn), whose dither must leave out the same pixels in GL.
    sets = (("", "showcase", {}), ("door_", "showcase_door", {"TG_SHOWCASE_DOOR": "7"}))
    for _, name, extra in sets:
        shots = out / name
        shots.mkdir(exist_ok=True)
        for old in shots.glob("car*"):
            old.unlink()
        subprocess.run([str(build / "tg_headless"), "--rom", ROM, "--port", "verify", "--frames", "600", "--input",
                        str(REPO / "tests/inputs/race_1p.txt"), "--shots3d", "640x360", "--art3d", ART, "--showcase", str(shots)],
                       env=dict(os.environ, TG_SHOWCASE_DUMP="1", TG_SHOWCASE_SIZE="960x600", **extra),
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if len(list(shots.glob("car*.v3"))) != 25:
            sys.exit("gl_check: tg_headless --showcase wrote no dumps")
    bad, rows = False, []
    for (prefix, name, _), t, k in [(s, t, k) for s in sets for t in range(5) for k in (0, 35, 90, 150, 215)]:
        base = out / name / f"car{t}_{k:03d}"
        sw = np.asarray(Image.open(f"{base}.ppm").convert("RGB")).astype(int)
        line, pics = f"{prefix}car{t}_{k:03d}", {}
        if prefix:  # the screen door was applied: the picture is not the whole car's
            whole = np.asarray(Image.open(out / "showcase" / f"car{t}_{k:03d}.ppm").convert("RGB")).astype(int)
            seen = (np.abs(whole - sw).max(2) > 24).mean() * 100
            line += f"  seen through {seen:4.1f}% px"
            bad |= seen < 1
        for mode, env in (("mat", {}), ("mat-llvmpipe", {"LIBGL_ALWAYS_SOFTWARE": "1"}), ("basic", {})):
            dst = out / f"{mode}_{prefix}car{t}_{k:03d}.ppm"
            r = subprocess.run([str(exe), f"{base}.v3", str(dst), "basic" if mode == "basic" else "mat"],
                               env=dict(os.environ, **env), capture_output=True, text=True)
            log = [l for l in r.stderr.splitlines() if l.strip() and not l.startswith(("renderer:", "libEGL warning"))]
            if not dst.exists() or log or (r.returncode not in (0, -11)):  # (-11: WSL's D3D12 driver at exit, after the picture)
                print(f"{line} {mode}: FAIL exit {r.returncode}\n  " + "\n  ".join(log))
                bad = True
                continue
            if k == 0 and t == 0:
                print(mode + ":", " ".join(l[10:] for l in r.stderr.splitlines() if l.startswith("renderer:")))
            gl = np.asarray(Image.open(dst).convert("RGB")).astype(int)
            mask = (sw.sum(2) > 0) | (gl.sum(2) > 0)
            d = np.abs(gl - sw).max(2)[mask]
            over = (d > 24).mean() * 100
            line += f"  {mode} mean {d.mean():4.2f} p95 {np.percentile(d, 95):3.0f} >24 {over:4.2f}%"
            if mode != "basic":
                bad |= d.mean() > 3 or over > 2
            pics[mode] = dst
        print(line)
        if k in (35, 150) and "mat" in pics and "basic" in pics:
            rows.append((prefix + base.name, [Image.open(f"{base}.ppm"), Image.open(pics["mat"]), Image.open(pics["basic"])]))
    if rows:
        crop = (130, 90, 830, 530)
        w, h = crop[2] - crop[0], crop[3] - crop[1]
        sheet = Image.new("RGB", (3 * w + 16, len(rows) * (h + 18) + 4), (20, 20, 20))
        dr = ImageDraw.Draw(sheet)
        for i, (name, ims) in enumerate(rows):
            y = 4 + i * (h + 18)
            for j, (im, lab) in enumerate(zip(ims, ("software (tg_v3_raster)", "GL material shader", "GL basic + CPU bake"))):
                sheet.paste(im.convert("RGB").crop(crop), (4 + j * (w + 4), y + 14))
                dr.text((4 + j * (w + 4), y), f"{name}  {lab}", fill=(230, 230, 230))
        sheet.resize((sheet.width // 2, sheet.height // 2), Image.LANCZOS).save(out / "sheet.png")
        print("sheet:", out / "sheet.png")
    print("gl_check:", "FAIL" if bad else "PASS")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
