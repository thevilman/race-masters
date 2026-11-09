#!/usr/bin/env python3
"""Per-car-id check of the modern look's cars, from a TG_CAR_TRACE=1 log of tg_headless.

    TG_CAR_TRACE=1 build/tg_headless ... --shots3d WxH --shots-every 2 2> trace.txt
    python3 tools/qa/car_trace_check.py trace.txt [--every 2]

Stricter than the image check `cars/` (which sees hues on the picture): every car is
followed by its id, in each view, frame by frame.

- paint: within a race (CT lines without a gap of more than 120 frames) a car's paint may
  change only from the neutral silver drawn while the car palettes are still black (the
  race's fade-in) to its race colour, once. Any other change fails.
- level: the car's level of detail may not flip back within 10 frames (A-B-A), per view.

Exit 1 on a failure. Prints a line per race and a summary.
"""
import collections
import sys

NEUTRAL = 'ffb4b4bc'


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    if not args:
        print(__doc__)
        return 2
    rows = []
    for line in open(args[0], errors='replace'):
        if not line.startswith('CT '):
            continue
        f = line.split()
        d = dict(zip(f[3::2], f[4::2]))
        rows.append((int(f[1]), int(f[2]), int(d.get('view', 0)), d['paint'], int(d.get('lvl', -1))))
    if not rows:
        print('no CT lines (run with TG_CAR_TRACE=1)')
        return 1
    # races: runs of frames without a long gap
    races, cur, last = [], [], None
    for r in rows:
        if last is not None and r[0] - last > 120:
            races.append(cur)
            cur = []
        cur.append(r)
        last = r[0]
    races.append(cur)
    bad = 0
    for race in races:
        paint = collections.defaultdict(list)
        lvl = collections.defaultdict(list)
        for fr, car, view, p, lv in race:
            paint[car].append((fr, p))
            lvl[(car, view)].append((fr, lv))
        changes, flips = [], []
        for car, seq in paint.items():
            for k in range(1, len(seq)):
                a, b = seq[k - 1][1], seq[k][1]
                if a != b and not (a == NEUTRAL and all(x[1] == NEUTRAL for x in seq[:k])):
                    changes.append((car, seq[k][0], a, b))
        for key, seq in lvl.items():
            for k in range(2, len(seq)):
                if seq[k][1] == seq[k - 2][1] != seq[k - 1][1] and seq[k][0] - seq[k - 2][0] <= 10:
                    flips.append((key, seq[k - 1][0]))
        cars = len(paint)
        status = 'PASS' if not changes and not flips else 'FAIL'
        bad += status == 'FAIL'
        print(f'race frames {race[0][0]}-{race[-1][0]}: {cars} cars, {len(race)} drawn, '
              f'paint changes {len(changes)}, level A-B-A {len(flips)}: {status}')
        for c in changes[:10]:
            print('  paint: car %d at %d: %s -> %s' % c)
        for (car, view), fr in flips[:10]:
            print(f'  level flip: car {car} view {view} at {fr}')
    print('car_trace_check:', 'FAIL' if bad else 'PASS', f'({len(races)} races)')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
