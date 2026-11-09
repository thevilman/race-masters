#!/usr/bin/env python3
"""tools/qa/check_results.py NAME INPUT PORT FRAME [extra tg_headless args...]

Replays INPUT to FRAME (a frame where the RESULTS page is up), then checks the results
table numerically:
  - the original's printed text (read off BG1 by the menu capture, TG_MENU_DUMP),
  - the game's tables in WRAM ($1F12 order, $1F26 + 2*driver times in frames),
  - what the modern table prints (same formula as view3d.c race_time()).
Prints a per-row table and PASS/FAIL lines; exit 1 if a check fails.

Checked (2026-11-02): session-20261102-195339 native 8400, session-20261030-095942
native 7400, chris_session_02 verify 8880: 30/30 rows identical."""
import os, re, subprocess, sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
TG = Path(os.environ.get("TG_QA_TOP", Path.home() / "Top Gear"))  # the player's game folder
ROM = str(Path(os.environ.get("TG_QA_ROM", TG / "Top Gear (USA).sfc")))
ART = os.environ.get("TG_QA_ART", str(TG / "art3d")).rstrip("/") + "/"
OUT = os.path.join(os.environ.get("TG_QA_OUT", str(Path(__file__).resolve().parents[2] / "build" / "qa")), "results-check")
TOP = str(REPO)

name, inp, port, frame = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
extra = sys.argv[5:]
d = os.path.join(OUT, name)
os.makedirs(d, exist_ok=True)
wram = os.path.join(d, "wram.bin")
env = dict(os.environ, TG_MENU_DUMP="1")
cmd = [os.path.join(TOP, "build/tg_headless"), "--rom", ROM, "--port", port, "--input", inp, "--frames", str(frame),
       "--shots-every", "1", "--shots-from", str(frame), "--shots-dir", d, "--shots3d", "480x270", "--art3d", ART,
       "--dump-wram", wram] + extra
r = subprocess.run(cmd, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
dump = r.stderr

# The original's text at that frame: items by row.
items = re.findall(r"item \d+ r(\d+) c(\d+) big(\d) \[([^\]]*)\]", dump)
scroll = re.search(r"scroll (\d+)", dump)
rows = {}
for row, col, big, text in items:
    if big == "0":
        rows.setdefault(int(row), []).append((int(col), text))
orig = []
for row in sorted(rows):
    cells = [t for c, t in sorted(rows[row])]
    if len(cells) >= 3 and "PT" in cells[-1]:
        orig.append(cells)

m = open(wram, "rb").read()
r16 = lambda a: m[a] | m[a + 1] << 8
two = m[0x1F04] & 1
order = [m[0x1F12 + i] % 20 for i in range(10)]
times = [r16(0x1F26 + 2 * d) for d in range(20)]

def fmt(frames, plus):  # view3d.c race_time()
    sec, hund = frames // 60, (frames % 60) * 100 // 60
    if plus and sec < 60:
        return "+%d.%02d" % (sec, hund)
    return "%s%d:%02d.%02d" % ("+" if plus else "", sec // 60, sec % 60, hund)

def game_fmt(frames):  # MenuFormatTime: M'SS"HH, as the original prints it (digits only)
    sec, hund = frames // 60, (frames % 60) * 100 // 60
    return "%d %02d %02d" % (sec // 60, sec % 60, hund)

ok = True
win = times[order[0]]
print("%s frame %d (BG1 scroll %s, %s player)" % (name, frame, scroll.group(1) if scroll else "?", 2 if two else 1))
print("| pos | driver id | original's name | original's figure | WRAM frames | our TIME | our GAP | gap = original? |")
print("|---|---|---|---|---|---|---|---|")
prev = -1
for p in range(10):
    dr = order[p]
    t = times[dr]
    o = orig[p] if p < len(orig) else ["?", "?", "?"]
    ofig = o[1]
    want = game_fmt(t if p == 0 else t - win)
    # the original's figure, as digits (the OCR loses ' " + and leading zeros vary)
    od = [int(x) for x in ofig.split()] if re.fullmatch(r"[\d ]+", ofig) else None
    wd = [int(x) for x in want.split()]
    same = od == wd
    ok &= same
    if t < prev: ok = False
    prev = t
    print("| %d | %d | %s | %s%s | %d | %s | %s | %s |" % (p + 1, dr, o[0], "" if p == 0 else "+", ofig, t, fmt(t, False),
          "WINNER" if p == 0 else fmt(t - win, True), "yes" if same else "NO (game %s)" % want))
print("%s  original's figures = WRAM times (winner's time, others' gap to the winner)" % ("PASS" if ok else "FAIL"))
mono = all(times[order[i]] <= times[order[i + 1]] for i in range(9))
print("%s  times non-decreasing in finishing order" % ("PASS" if mono else "FAIL"))
print("%s  original lists the drivers in $1F12 order (%d rows read)" % ("PASS" if len(orig) == 10 else "FAIL", len(orig)))
sys.exit(0 if ok and mono and len(orig) == 10 else 1)
