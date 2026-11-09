#!/usr/bin/env python3
"""Self-test of the QA image checks on synthetic frames (no ROM, no tg_headless).

    python3 tools/qa/selftest.py

Each case builds a short sequence of 640x360 "modern" frames and an "original" frame,
feeds them to analyze.Analyzer and checks that the right event (and only it) is found.
Run it after changing analyze.py.
"""

import sys
from pathlib import Path

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))
import analyze  # noqa: E402

W, H = 640, 360


def scene(t, car_colour=(200, 40, 30), extra=None, gate=False, shift=0):
    im = Image.new("RGB", (W, H))
    d = ImageDraw.Draw(im)
    for y in range(H):  # sky then ground gradient
        c = (90 + y // 6, 140 + y // 8, 210) if y < 150 else (170 - (y - 150) // 5, 140 - (y - 150) // 6, 120)
        d.line([(0, y), (W, y)], fill=c)
    d.polygon([(300 + shift, 150), (340 + shift, 150), (560 + shift, 360), (80 + shift, 360)], fill=(70, 70, 75))
    x = 300 + t * 2 + shift  # a car drifting slowly to the right
    d.rectangle([x, 190, x + 50, 225], fill=car_colour)
    if extra:
        d.rectangle(extra, fill=(30, 120, 40))
    return im


def orig(t, jump=False):
    im = Image.new("RGB", (256, 224), (60, 120, 200))
    ImageDraw.Draw(im).rectangle([100 + t + (30 if jump else 0), 120, 150 + t + (30 if jump else 0), 200], fill=(200, 160, 120))
    return im


def run(frames):
    an = analyze.Analyzer("selftest", 2, 60)
    for i, (m, o) in enumerate(frames):
        an.feed(100 + 2 * i, o, None, m)
    return an


def case(name, frames, want_pop=None, want_car=False, want_none=False):
    an = run(frames)
    pops = [p for p in an.pops if not p["mirrors_original"]]
    ok = True
    if want_none:
        ok = not pops and not an.car_events
    if want_pop:
        ok = ok and any(p["kind"].startswith(want_pop[0]) and p["band"] == want_pop[1] for p in pops)
    if want_car:
        ok = ok and bool(an.car_events)
    print(f"{'ok  ' if ok else 'FAIL'} {name}: pops {[(p['frame'], p['kind'], p['band']) for p in an.pops]} cars {an.car_events}")
    return ok


def main():
    n = 10
    good = True
    # 1. smooth motion only: nothing flagged
    good &= case("smooth", [(scene(t), orig(t)) for t in range(n)], want_none=True)
    # 2. a tree appears at once in the skyline band and stays: a pop
    good &= case("skyline pop", [(scene(t, extra=[200, 60, 240, 110] if t >= 5 else None), orig(t)) for t in range(n)],
                 want_pop=("pop", "skyline"))
    # 3. something on the roadside for one frame only (A-B-A): a flicker
    good &= case("roadside flicker", [(scene(t, extra=[20, 200, 120, 260] if t == 5 else None), orig(t)) for t in range(n)],
                 want_pop=("flicker", "roadside"))
    # 4. the car turns blue in place: a car-colour event
    good &= case("car colour", [(scene(t, car_colour=(30, 60, 210) if t >= 5 else (200, 40, 30)), orig(t)) for t in range(n)],
                 want_car=True)
    # 5. the same pop while the original jumps too: explained by the original, not a failure
    good &= case("pop like the original", [(scene(t, extra=[200, 60, 240, 110] if t >= 5 else None), orig(t, jump=t >= 5))
                                          for t in range(n)], want_none=True)
    # 6. a near object sweeping out of the picture in perspective (faster and bigger each
    # shot): motion, not a pop (motion compensation)
    def sweep(t):
        im = scene(t)
        x0, s0 = 400 + t * t * 3, 20 + t * 2
        ImageDraw.Draw(im).rectangle([x0, 120 - s0, x0 + s0, 120 + s0], fill=(40, 110, 50))
        ImageDraw.Draw(im).rectangle([x0 + 4, 120 - s0 + 4, x0 + s0 // 2, 120], fill=(140, 90, 40))
        return im
    good &= case("near object sweeping by", [(sweep(t), orig(t)) for t in range(n)], want_none=True)
    # 7. leak: no modern frame, original not black
    an = analyze.Analyzer("leak", 2, 60)
    an.feed(2, orig(0), None, None)
    an.feed(4, Image.new("RGB", (256, 224)), None, None)
    ok = an.leaks == [2]
    print(f"{'ok  ' if ok else 'FAIL'} leak: {an.leaks}")
    good &= ok
    print("selftest", "PASSED" if good else "FAILED")
    sys.exit(0 if good else 1)


if __name__ == "__main__":
    main()
