#!/usr/bin/env python3
"""The five cars' .tgcar files (version 3), from the specialist's design: both stages in one go.

    python3 tools/cars/make_cars.py --blender BLENDER --vehicles VEHICLES_PY --out ART_CARS_DIR
                                    [--work DIR] [--cars rosso,glacier,violet,azure,amber] [--preview DIR]

BLENDER: a Blender 4.2 LTS (run headless, -b; CPU baking, no GPU needed); VEHICLES_PY: the
specialist remake's vehicles.py (the player's inspiration/6-specialist-remake/); ART_CARS_DIR:
where car-<name>.tgcar go (an art3d folder's cars/). WORK keeps the bakes and meshes between the
stages (default: a work/ folder beside ART_CARS_DIR). Reproducible: the same inputs give the
same files (no randomness but fixed seeds). See docs/CARS.md.
"""
import argparse
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--blender', required=True)
    ap.add_argument('--vehicles', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--work', default=None)
    ap.add_argument('--cars', default='rosso,glacier,violet,azure,amber')
    ap.add_argument('--preview', default=None, help='also write each atlas as PNG here')
    ap.add_argument('--tex', default='1024')
    a = ap.parse_args()
    work = a.work or os.path.join(os.path.dirname(os.path.abspath(a.out)), 'work')
    subprocess.run([a.blender, '-b', '--factory-startup', '--python-exit-code', '1', '--python', str(HERE / 'build_cars.py'), '--',
                    '--vehicles', a.vehicles, '--out', work, '--cars', a.cars, '--tex', a.tex], check=True)
    cmd = [sys.executable, str(HERE / 'paint_cars.py'), work, a.out, '--cars', a.cars]
    if a.preview:
        cmd += ['--preview', a.preview]
    subprocess.run(cmd, check=True)


if __name__ == '__main__':
    main()
