#!/usr/bin/env python3
"""AI-upscales the pictures tg_headless --export-art wrote (menubg-*.bmp: the menus'
photographs) with Real-ESRGAN (realesrgan-ncnn-vulkan, x4plus) into the game's art3d
folder, where they replace the originals. The pictures come from your ROM: they stay on
your machine. Usage: upscale_art.py EXPORT_DIR ART3D_DIR PATH_TO_realesrgan-ncnn-vulkan"""
import subprocess, sys, tempfile
from pathlib import Path
from PIL import Image

src, dst, exe = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3]
dst.mkdir(parents=True, exist_ok=True)
for f in sorted(src.glob("menubg-*.bmp")):
    with tempfile.TemporaryDirectory(dir=Path(exe).parent) as t:
        i, o = Path(t) / "in.png", Path(t) / "out.png"
        Image.open(f).convert("RGB").save(i)
        subprocess.run([exe, "-i", str(i), "-o", str(o), "-n", "realesrgan-x4plus"], check=True, cwd=Path(exe).parent)
        Image.open(o).convert("RGBA").save(dst / f.name)
        print(f.name, "upscaled")
