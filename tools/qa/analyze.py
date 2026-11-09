"""Per-frame image checks for the QA harness (tools/qa/run_qa.py).

One Analyzer per replay. tg_headless writes, per shot frame N:
  frameNNNNNN.ppm      the original picture (256x224, or wider in widescreen)
  frameNNNNNN_sel.ppm  a redrawn menu / title (when one is up)
  frameNNNNNN_3dx.ppm  the modern 3D look (in races); _3d/_3dm are debug looks, ignored
The player sees _sel if present, else _3dx, else the original.

Checks fed here (see docs/qa/README.md for the reasoning and the limits):
  leaks     a non-black frame the player would see in the ORIGINAL graphics
  blank     a modern frame that is a single flat colour while the original is not
  popping   per-block temporal spikes / A-B-A flicker in the skyline band and roadside
  cars      saturated-colour blocks that swap hue in place between consecutive frames
"""

from __future__ import annotations

import numpy as np
from PIL import Image, ImageDraw

AW, AH = 320, 180  # analysis resolution of the modern picture
BS = 10            # block size (pixels at analysis resolution) -> 32 x 18 blocks
GW, GH = AW // BS, AH // BS

# HUD panels of the modern look, as fractions of one view (x0, y0, x1, y1). Measured on
# 640x360 renders of the 1-player HUD; padded a little. Masked out of popping/car checks.
HUD_RECTS = [
    (0.00, 0.00, 0.21, 0.23),  # map / lap panel, top left
    (0.40, 0.00, 0.60, 0.11),  # course title, top centre
    (0.74, 0.00, 1.00, 0.19),  # position / time, top right
    (0.00, 0.76, 0.22, 1.00),  # nitro / fuel, bottom left
    (0.72, 0.72, 1.00, 1.00),  # gear / speed, bottom right
    (0.40, 0.13, 0.60, 0.36),  # race messages (countdown, GO, 19TH ...) which blink by design
]
PLAYER_CAR = (0.27, 0.58, 0.73, 1.00)  # the player's own car and its nitro flames
SKY_BAND = (0.10, 0.43)   # y range of the skyline band (fraction of view height)
SIDE_BAND = (0.43, 0.84)  # y range of the roadside band
CAR_ZONE = (0.12, 0.36, 0.88, 0.97)  # where other cars are drawn (x0, y0, x1, y1)

POP_HI = 18.0     # mean |grey diff| in a block that counts as a big change (0..255)
POP_RATIO = 3.0   # a spike: the change is this many times the one before and after
POP_FLOOR = 3.0   # ...with the neighbours' change counted as at least this
FLICKER_LO = 6.0  # A-B-A: frame t+1 is this close to frame t-1 in that block
CUT_FRAC = 0.30   # more than this fraction of blocks changing = a cut / fade, skipped
ORIG_CUT = 30.0   # the original picture changing this much in one step = a cut / jump
SKY_SHIFT = 8     # camera compensation search for the skyline band, +-pixels in x
SKY_SHIFT_Y = 4   # ... and in y

MC_X, MC_Y = 32, 12  # motion compensation: a block's content is looked for this far (px) in the other shot
MC_TOL = 8.0          # ...and found when its mean |grey diff| there is under max(MC_TOL, MC_REL x its change)
MC_REL = 0.35

MAX_TEMPORAL_EVERY = 4  # popping / car checks need shots at most this many frames apart

THUMB = (192, 108)


def load(path):
    try:
        im = Image.open(path)
        im.load()
        return im.convert("RGB")
    except Exception:
        return None


def nonblack(img: Image.Image) -> bool:
    a = np.asarray(img)
    return float((a.max(axis=2) > 40).mean()) > 0.02


def visible_thumb(img: Image.Image) -> Image.Image:
    """Fit into THUMB keeping the aspect (the original is 8:7, the modern 16:9)."""
    t = Image.new("RGB", THUMB, (0, 0, 0))
    im = img.copy()
    im.thumbnail(THUMB, Image.BILINEAR)
    t.paste(im, ((THUMB[0] - im.width) // 2, (THUMB[1] - im.height) // 2))
    return t


def view_rows(views: int):
    return [(k * AH // views, (k + 1) * AH // views) for k in range(views)]


def build_masks(views: int):
    """Block masks (GH x GW bool) for: sky band, side band, car zone, all valid."""
    pix_valid = np.ones((AH, AW), bool)
    sky = np.zeros((AH, AW), bool)
    side = np.zeros((AH, AW), bool)
    car = np.zeros((AH, AW), bool)
    for (r0, r1) in view_rows(views):
        h = r1 - r0

        def rect(fr, target, val=True):
            x0, y0, x1, y1 = fr
            target[r0 + int(y0 * h): r0 + int(np.ceil(y1 * h)), int(x0 * AW): int(np.ceil(x1 * AW))] = val

        for r in HUD_RECTS:
            rect(r, pix_valid, False)
        rect(PLAYER_CAR, pix_valid, False)
        rect((0, SKY_BAND[0], 1, SKY_BAND[1]), sky)
        rect((0, SIDE_BAND[0], 1, SIDE_BAND[1]), side)
        rect(CAR_ZONE, car)
    car_valid = pix_valid.copy()
    # the player's car may change colour too: keep it in the car check
    for (r0, r1) in view_rows(views):
        h = r1 - r0
        x0, y0, x1, y1 = PLAYER_CAR
        car_valid[r0 + int(y0 * h): r1, int(x0 * AW): int(np.ceil(x1 * AW))] = True
        for r in HUD_RECTS:
            rx0, ry0, rx1, ry1 = r
            car_valid[r0 + int(ry0 * h): r0 + int(np.ceil(ry1 * h)), int(rx0 * AW): int(np.ceil(rx1 * AW))] = False

    def blocks(m):
        return m.reshape(GH, BS, GW, BS).mean(axis=(1, 3))

    valid = blocks(pix_valid)
    return {
        "pix_valid": pix_valid,
        "sky": (blocks(sky & pix_valid) >= 0.6),
        "side": (blocks(side & pix_valid) >= 0.6),
        "car": (blocks(car & car_valid) >= 0.6),
        "sky_pix": sky & pix_valid,
        "valid": valid >= 0.6,
    }


def block_mean(a):
    return a.reshape(GH, BS, GW, BS).mean(axis=(1, 3))


def to_hsv_blocks(rgb: np.ndarray):
    """Per block: dominant hue bin 0..11 of the saturated pixels, or -1."""
    f = rgb.astype(np.float32) / 255.0
    mx = f.max(axis=2)
    mn = f.min(axis=2)
    d = mx - mn
    s = np.where(mx > 0, d / np.maximum(mx, 1e-6), 0)
    r, g, b = f[..., 0], f[..., 1], f[..., 2]
    hue = np.zeros_like(mx)
    nz = d > 1e-6
    rm = nz & (mx == r)
    gm = nz & (mx == g) & ~rm
    bm = nz & ~rm & ~gm
    hue[rm] = ((g - b)[rm] / d[rm]) % 6
    hue[gm] = ((b - r)[gm] / d[gm]) + 2
    hue[bm] = ((r - g)[bm] / d[bm]) + 4
    ang = hue * (np.pi / 3)
    sat = (s >= 0.40) & (mx >= 0.25)
    frac = block_mean(sat.astype(np.float32))
    c = block_mean(np.where(sat, np.cos(ang), 0))
    si = block_mean(np.where(sat, np.sin(ang), 0))
    a = np.arctan2(si, c) % (2 * np.pi)
    bins = np.round(a / (np.pi / 6)).astype(int) % 12
    bins[frac < 0.35] = -1
    return bins


def best_match(src, blk, y0, x0):
    """The smallest mean |diff| of the block blk (BS x BS, placed at y0, x0) against src
    shifted by up to MC_X / MC_Y pixels (step 2): where the block's content went or came from."""
    best = 1e9
    for dy in range(-MC_Y, MC_Y + 1, 2):
        yy = y0 + dy
        if yy < 0 or yy + BS > AH:
            continue
        rows = src[yy:yy + BS]
        for dx in range(-MC_X, MC_X + 1, 2):
            xx = x0 + dx
            if xx < 0 or xx + BS > AW:
                continue
            e = float(np.abs(rows[:, xx:xx + BS] - blk).mean())
            if e < best:
                best = e
    return best


def moved(prev, cur, y, x, change):
    """Motion compensation (block matching): the block's new content was nearby in the shot
    before AND its old content is still nearby in this shot: something that moved (a near
    object sweeping by in perspective), not something that appeared or vanished."""
    tol = max(MC_TOL, MC_REL * change)
    y0, x0 = y * BS, x * BS
    new = cur[y0:y0 + BS, x0:x0 + BS]
    old = prev[y0:y0 + BS, x0:x0 + BS]
    came = best_match(prev, new, y0, x0) < tol
    stays = best_match(cur, old, y0, x0) < tol
    # By the picture's sides and bottom a thing can leave (or enter) the picture within one
    # step: there one of the two is enough (what it uncovers was nearby, or it was).
    edge = x0 < MC_X or x0 + BS > AW - MC_X or y0 + BS > AH - MC_Y
    return (came or stays) if edge else (came and stays)


def solid(hit):
    sq = hit[:-1, :-1] & hit[1:, :-1] & hit[:-1, 1:] & hit[1:, 1:]
    run = hit[:, :-3] & hit[:, 1:-2] & hit[:, 2:-1] & hit[:, 3:]
    return bool(sq.any() or run.any())


def hue_dist(a, b):
    d = np.abs(a - b) % 12
    return np.minimum(d, 12 - d)


class Analyzer:
    def __init__(self, name: str, every: int, sheet_every: int):
        self.name = name
        self.every = every
        self.sheet_every = sheet_every
        self.frames = []        # per shot: dict(frame, src, nonblack)
        self.tiles = []         # (frame, label, thumb, flag)
        self.last_src = None
        self.leaks = []
        self.blanks = []
        self.pops = []          # dict(frame, kind, band, blocks, score)
        self.car_events = []    # dict(frame, blocks, old, new)
        self.evidence = []      # (kind, frame, before, after, boxes)
        self.masks = {}
        self.hist = []          # modern-frame history: dict(frame, grey, rgb, hue, views)
        self.D = []             # (frame, block diff array, cut, sky_shift) aligned with hist[1:]
        self.pop_checked_pairs = 0
        self.pop_moved = 0       # spiking blocks explained by motion (block matching), not counted
        self.car_checked_pairs = 0
        self.views_of = None    # callback frame -> views (from the timing log)

    # ---- feeding --------------------------------------------------------------------
    def feed(self, frame: int, orig, sel, x3d, views: int = 1):
        nb = orig is not None and nonblack(orig)
        if sel is not None:
            src, vis = "sel", sel
        elif x3d is not None:
            src, vis = "3d", x3d
        else:
            src, vis = "orig", orig
        leak = src == "orig" and nb
        blank = False
        if src == "3d" and nb:
            a = np.asarray(x3d.resize((160, 90)))
            blank = float(a.std()) < 3.0
        self.frames.append({"frame": frame, "src": src, "nonblack": nb, "leak": leak, "blank": blank})
        if leak:
            self.leaks.append(frame)
        if blank:
            self.blanks.append(frame)
        # contact sheet: regular samples, every change of what is shown, every leak start
        changed = src != self.last_src
        prev_leak = len(self.frames) > 1 and self.frames[-2]["leak"]
        if vis is not None and (frame % self.sheet_every < self.every or changed or (leak and not prev_leak)):
            tag = {"sel": "MENU", "3d": "3D", "orig": "ORIGINAL"}[src]
            self.tiles.append((frame, tag, visible_thumb(vis), "leak" if leak else ("blank" if blank else "")))
        self.last_src = src
        if src == "3d" and self.every <= MAX_TEMPORAL_EVERY:
            self._modern(frame, x3d, views, orig)
        else:
            self.hist.clear()
            self.D.clear()

    # ---- modern-frame temporal checks ----------------------------------------------
    def _modern(self, frame, img, views, orig=None):
        small = img.resize((AW, AH), Image.BOX)
        rgb = np.asarray(small)
        grey = rgb.astype(np.float32) @ np.array([0.299, 0.587, 0.114], np.float32)
        if views not in self.masks:
            self.masks[views] = build_masks(views)
        m = self.masks[views]
        og = None
        if orig is not None:  # the original picture below its top HUD rows, for "mirrors the original"
            o = np.asarray(orig.resize((128, 112), Image.BOX)).astype(np.float32)
            og = (o @ np.array([0.299, 0.587, 0.114], np.float32))[28:]
        cur = {"frame": frame, "grey": grey, "rgb": rgb, "hue": to_hsv_blocks(rgb), "views": views, "img": small, "og": og}
        if self.hist and (frame - self.hist[-1]["frame"] != self.every or self.hist[-1]["views"] != views):
            self.hist.clear()
            self.D.clear()
        if self.hist:
            prev = self.hist[-1]
            d, shift = self._blockdiff(prev["grey"], grey, m)
            cut = float((d[m["valid"]] >= POP_HI).mean()) > CUT_FRAC if m["valid"].any() else False
            od = float(np.abs(og - prev["og"]).mean()) if og is not None and prev["og"] is not None else 0.0
            self.D.append({"frame": frame, "d": d, "cut": cut, "shift": shift, "od": od})
            self._cars(prev, cur, m, cut)
        self.hist.append(cur)
        if len(self.hist) > 4:
            self.hist.pop(0)
        if len(self.D) > 3:
            self.D.pop(0)
        if len(self.D) >= 3 and len(self.hist) >= 4:
            self._pops(m)
        self._trim_evidence()

    def _blockdiff(self, a, b, m):
        """Block mean |b - a|; the skyline band is compared after the best small shift
        (camera yaw / pitch), the rest as is."""
        d = block_mean(np.abs(b - a))
        sky = m["sky_pix"]
        best, bs = None, (0, 0)
        rows = np.where(sky.any(axis=1))[0]
        if len(rows):
            y0, y1 = rows[0], rows[-1] + 1
            for dy in range(-SKY_SHIFT_Y, SKY_SHIFT_Y + 1):
                if y0 + dy < 0 or y1 + dy > AH:
                    continue
                for dx in range(-SKY_SHIFT, SKY_SHIFT + 1, 2):
                    pa = a[y0:y1, max(0, -dx): AW - max(0, dx)]
                    pb = b[y0 + dy:y1 + dy, max(0, dx): AW - max(0, -dx)]
                    mk = sky[y0:y1, max(0, -dx): AW - max(0, dx)]
                    e = float(np.abs(pb - pa)[mk].mean()) if mk.any() else 0.0
                    if best is None or e < best:
                        best, bs = e, (dx, dy)
            dx, dy = bs
            sh = np.full_like(a, np.nan)
            # a shifted by the camera motion, to compare with b
            src = a[max(0, -dy): AH - max(0, dy), max(0, -dx): AW - max(0, dx)]
            sh[max(0, dy): AH - max(0, -dy), max(0, dx): AW - max(0, -dx)] = src
            diff = np.abs(b - sh)
            diff = np.where(np.isnan(diff), np.abs(b - a), diff)
            ds = block_mean(diff)
            d = np.where(m["sky"], ds, d)
        return d, bs

    def _pops(self, m):
        # Evaluate the middle change D[-2] (between hist[-3] and hist[-2]) with its
        # neighbours D[-3] (before) and D[-1] (after).
        d0, d1, d2 = self.D[-3], self.D[-2], self.D[-1]
        if d0["cut"] or d1["cut"] or d2["cut"]:
            return
        self.pop_checked_pairs += 1
        a, b, c = d0["d"], d1["d"], d2["d"]
        spike = (b >= POP_HI) & (b >= POP_RATIO * np.maximum(np.maximum(a, c), POP_FLOOR))
        # Moving geometry is not a pop: each spiking block whose change block matching
        # explains (its content moved, within MC_X / MC_Y) is dropped.
        g0, g1 = self.hist[-3]["grey"], self.hist[-2]["grey"]
        for y, x in zip(*np.nonzero(spike & m["valid"])):
            if moved(g0, g1, int(y), int(x), float(b[y, x])):
                spike[y, x] = False
                self.pop_moved += 1
        # A-B-A flicker: two big changes and frame t+1 back to frame t-1
        h1, h3 = self.hist[-3], self.hist[-1]
        back = block_mean(np.abs(h3["grey"] - h1["grey"]))
        # Does the original picture do the same at this step? (whole picture, no mapping)
        o0, o1, o2 = d0["od"], d1["od"], d2["od"]
        oback = float(np.abs(h3["og"] - h1["og"]).mean()) if h3["og"] is not None and h1["og"] is not None else 99.0
        # The modern view can trail the original by a frame or two, so a cut or jump in the
        # original one shot before or after also counts (a cut: mean change >= ORIG_CUT).
        mirrored = {"pop": (o1 >= 4.0 and o1 >= 2.5 * max(o0, o2, 0.5)) or max(o0, o1, o2) >= ORIG_CUT,
                    "flicker": o1 >= 4.0 and o2 >= 4.0 and oback <= 0.5 * min(o1, o2)}
        flick = (b >= POP_HI) & (c >= POP_HI) & (back <= FLICKER_LO)
        g2 = self.hist[-1]["grey"]
        for y, x in zip(*np.nonzero(flick & m["valid"])):  # a thing passing through, seen once
            if moved(g0, g1, int(y), int(x), float(b[y, x])) and moved(g1, g2, int(y), int(x), float(c[y, x])):
                flick[y, x] = False
                self.pop_moved += 1
        for kind, mask in (("pop", spike), ("flicker", flick)):
            for band in ("sky", "side"):
                hit = mask & m[band]
                n = int(hit.sum())
                if n == 0 or (band == "side" and n < 2) or (kind == "flicker" and n < 2):
                    continue
                if kind == "flicker" and not solid(hit):
                    # thin things (a roadside post passing close by is seen in one shot only:
                    # temporal aliasing of the 2-4 frame sampling) are not a flicker; need a
                    # 2x2 patch of blocks or a run of 4 in a row
                    continue
                score = float(b[hit].sum())
                ys, xs = np.nonzero(hit)
                if band == "side" and n >= 6 and (xs.max() - xs.min() + 1) >= GW // 2 and (ys.max() - ys.min()) <= 4:
                    kind_l = kind + ":wide-band"  # a whole strip of ground at once (texture jump)
                else:
                    kind_l = kind
                ev = {"frame": d1["frame"], "kind": kind_l, "band": "skyline" if band == "sky" else "roadside",
                      "blocks": n, "score": round(score, 1), "mirrors_original": mirrored[kind]}
                self.pops.append(ev)
                boxes = [(int(x) * BS, int(y) * BS) for y, x in zip(*np.nonzero(hit))]
                if mirrored[kind]:
                    continue  # the original does it too: not the modern look's fault, no evidence
                self.evidence.append((f"{kind_l}/{ev['band']}", d1["frame"], self.hist[-3]["img"],
                                      self.hist[-2]["img"], boxes, score))

    def _cars(self, prev, cur, m, cut):
        if cut:
            return
        self.car_checked_pairs += 1
        h0, h1 = prev["hue"], cur["hue"]
        zone = m["car"]
        both = zone & (h0 >= 0) & (h1 >= 0) & (hue_dist(h0, h1) >= 2)
        if not both.any():
            return
        cand = []
        for y, x in zip(*np.nonzero(both)):
            ys, xs = slice(max(0, y - 2), y + 3), slice(max(0, x - 2), x + 3)
            n0, n1 = h0[ys, xs], h1[ys, xs]
            new_before = ((n0 >= 0) & (hue_dist(n0, h1[y, x]) <= 1)).any()
            # the new colour came from nowhere nearby (a car that moved in would have been
            # within 2 blocks); the old colour may still be near (bunched packs)
            if not new_before:
                cand.append((y, x))
        if len(cand) < 2:
            return
        # need two adjacent candidate blocks with the same old -> new swap
        cs = set(cand)
        groups = []
        for (y, x) in cand:
            for (yy, xx) in ((y + 1, x), (y, x + 1), (y + 1, x + 1), (y + 1, x - 1)):
                if (yy, xx) in cs and hue_dist(h0[y, x], h0[yy, xx]) <= 1 and hue_dist(h1[y, x], h1[yy, xx]) <= 1:
                    groups.append(((y, x), (yy, xx)))
        if not groups:
            return
        blocks = sorted({p for g in groups for p in g})
        (y, x) = blocks[0]
        ev = {"frame": cur["frame"], "blocks": len(blocks), "old_hue": int(h0[y, x]) * 30, "new_hue": int(h1[y, x]) * 30}
        self.car_events.append(ev)
        self.evidence.append(("car colour", cur["frame"], prev["img"], cur["img"],
                              [(x * BS, y * BS) for (y, x) in blocks], float(len(blocks))))

    def _trim_evidence(self):
        if len(self.evidence) > 300:  # keep memory bounded: the strongest events per kind
            self.evidence.sort(key=lambda e: -e[5])
            kinds = {}
            keep = []
            for e in self.evidence:
                if kinds.get(e[0], 0) < 40:
                    kinds[e[0]] = kinds.get(e[0], 0) + 1
                    keep.append(e)
            self.evidence = keep

    # ---- results --------------------------------------------------------------------
    def summary(self):
        return {
            "shots": len(self.frames),
            "by_source": {s: sum(1 for f in self.frames if f["src"] == s) for s in ("sel", "3d", "orig")},
            "leaks": ranges(self.leaks, self.every),
            "leak_frames": len(self.leaks),
            "blank_modern": ranges(self.blanks, self.every),
            "pops": self.pops,
            "pop_pairs_checked": self.pop_checked_pairs,
            "pop_blocks_moved": self.pop_moved,
            "car_events": self.car_events,
            "car_pairs_checked": self.car_checked_pairs,
        }


def ranges(frames, every, gap=3):
    out = []
    for f in frames:
        if out and f - out[-1][1] <= every * gap:
            out[-1][1] = f
        else:
            out.append([f, f])
    return out


# ---- contact sheets ------------------------------------------------------------------
COLS = 8
ROWS = 8
LABEL_H = 14


def contact_sheets(tiles, title, path_prefix):
    """Pages of 8x8 thumbnails; a red border = original-graphics leak, orange = blank."""
    paths = []
    per = COLS * ROWS
    tw, th = THUMB
    for p in range(0, len(tiles), per):
        page = tiles[p:p + per]
        rows = (len(page) + COLS - 1) // COLS
        sheet = Image.new("RGB", (COLS * (tw + 4) + 4, 22 + rows * (th + LABEL_H + 4)), (24, 24, 28))
        dr = ImageDraw.Draw(sheet)
        dr.text((6, 5), f"{title}  page {p // per + 1}  (red=ORIGINAL leak, orange=blank modern)", fill=(230, 230, 230))
        for i, (frame, tag, im, flag) in enumerate(page):
            x = 4 + (i % COLS) * (tw + 4)
            y = 22 + (i // COLS) * (th + LABEL_H + 4)
            sheet.paste(im, (x, y))
            col = {"leak": (255, 40, 40), "blank": (255, 160, 0)}.get(flag)
            if col:
                dr.rectangle([x - 2, y - 2, x + tw + 1, y + th + 1], outline=col, width=2)
            dr.text((x + 2, y + th + 1), f"{frame}  {tag}", fill=col or (200, 200, 200))
        path = f"{path_prefix}_{p // per + 1:02d}.png"
        sheet.save(path)
        paths.append(path)
    return paths


def evidence_sheets(evidence, title, path_prefix, limit=24):
    """Before / after pairs of the strongest temporal events, the blocks boxed in red."""
    if not evidence:
        return []
    # the strongest few of each kind, so every kind of event has an example
    kinds = sorted({e[0] for e in evidence})
    per_kind = max(2, limit // max(1, len(kinds)))
    ev = []
    for k in kinds:
        ev += sorted((e for e in evidence if e[0] == k), key=lambda e: -e[5])[:per_kind]
    ev.sort(key=lambda e: (e[0], e[1]))
    W, H = AW * 2 + 12, AH + 18
    paths = []
    per = 8
    for p in range(0, len(ev), per):
        page = ev[p:p + per]
        sheet = Image.new("RGB", (W * 2, 20 + ((len(page) + 1) // 2) * H), (24, 24, 28))
        dr = ImageDraw.Draw(sheet)
        dr.text((6, 4), f"{title}: before | after (red boxes = flagged blocks)", fill=(230, 230, 230))
        for i, (kind, frame, before, after, boxes, score) in enumerate(page):
            ox = (i % 2) * W + 4
            oy = 20 + (i // 2) * H
            sheet.paste(before, (ox, oy + 14))
            a = after.copy()
            da = ImageDraw.Draw(a)
            for (x, y) in boxes:
                da.rectangle([x, y, x + BS - 1, y + BS - 1], outline=(255, 0, 0))
            sheet.paste(a, (ox + AW + 4, oy + 14))
            dr.text((ox, oy), f"{kind}  frame {frame}  score {score:.0f}", fill=(255, 210, 120))
        path = f"{path_prefix}_{p // per + 1:02d}.png"
        sheet.save(path)
        paths.append(path)
    return paths
