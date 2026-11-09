#!/usr/bin/env python3
"""The cars' models, level 3 (see docs/CARS.md), paint stage: the atlas and the .tgcar file.

    python3 tools/cars/paint_cars.py WORK OUT_DIR [--cars rosso,...] [--preview DIR]

Reads what build_cars.py (Blender) left in WORK/<car>/ - the game mesh's levels and wheel with
their UVs, and the bakes into the atlas (position, normal, albedo, paint mask, material ids,
ambient occlusion) - and paints each car's texture from them, texel by texel:

- materials made in code at every texel from where it is on the car (its position and normal):
  tyre tread with grooves and sipes, sidewall lettering round the rim, the rim's machined face
  and polished lip, lug nuts and the centre badge, a cross-drilled brake disc with its pad
  track and vented edge, multi-reflector headlamps and honeycomb tail lamps (each lamp unit
  found as a cluster), reverse lights, grille mesh in the intakes, carbon twill, seat fabric
  with stitching, carpet, a frit band round the glass, the number plate;
- the full model's detail baked in (shut lines, seams, gaskets, louvres, slats, bolts) and
  its ambient occlusion, a little road grime low down;
- each car's livery, projected onto the body (stripes over the top, a sash, pinstripes, the
  race number's roundel or panel, invented sponsors - no real brand), in the repo's fonts
  (Barlow Condensed, Oxanium: SIL OFL);
- the paint itself left to the game: where the body is paint the texel holds only its shading
  (rgb ~ 1, AO, lines) and alpha says "paint here" - the renderer multiplies in the car's
  colour (a rival's is the game's own, the colour lock), as BeamNG / SCS / Outerra paint masks;
- every empty texel filled from its nearest island (edge padding, so mipmaps don't bleed) and
  a white block for corners drawn in their plain colour.

Then the .tgcar, version 3 (src/mod3d/view3d.c reads 1-3): "TGCR", u32 3, u32 levels; per level
u32 flags (bit 0: it has an interior - the glass is see-through), u32 triangles, corners of 32
bytes (3 f32 position, 3 f32 normal, u8 class, u8 r g b, u16 u v); the wheel (left front, around
its hub), its fixed part (the caliper: steers, doesn't spin), the right wheel (mirrored, its
sidewall reading the right way); 4 hubs; the race number's place (f32 along, up, out, radius,
u32 shape, u32 digit colour); the white block (f32 u v); the atlas (u32 w h, ARGB8888 rows,
the first at v = 0); the ink mask (u32 1, then w x h u8: where the contrasting decals are -
the game makes a white-ink and a black-ink texture from it and picks by the car's paint).
"""
import argparse
import json
import math
import os
import struct
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage

REPO = Path(__file__).resolve().parents[2]
F_ITALIC = str(REPO / 'third_party/barlow/BarlowCondensed-ExtraBoldItalic.ttf')
F_BOLD = str(REPO / 'third_party/barlow/BarlowCondensed-Bold.ttf')
F_MED = str(REPO / 'third_party/barlow/BarlowCondensed-Medium.ttf')
F_TECH = str(REPO / 'third_party/oxanium/Oxanium[wght].ttf')


def to_srgb(x):
    x = np.clip(x, 0, 1)
    return np.where(x <= .0031308, x * 12.92, 1.055 * np.power(x, 1 / 2.4) - .055)


def smooth(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def text_img(text, font, px, track=0):
    """Text as coverage (0-1), tightly cropped: (array H x W)."""
    f = ImageFont.truetype(font, px)
    if 'Oxanium' in font:
        try:
            f.set_variation_by_axes([700])
        except OSError:  # (a FreeType without variable fonts: the default weight)
            f = ImageFont.truetype(font, px)
    w = int(sum(f.getlength(c) + track for c in text)) + px
    im = Image.new('L', (w + 8, int(px * 1.6)), 0)
    d = ImageDraw.Draw(im)
    x = 4
    for c in text:
        d.text((x, int(px * .2)), c, font=f, fill=255)
        x += f.getlength(c) + track
    a = np.asarray(im, np.float32) / 255
    ys, xs = np.nonzero(a > .02)
    return a[ys.min():ys.max() + 1, xs.min():xs.max() + 1]


def sample(img, u, v):
    """Bilinear sample of a 2D array at u (across, 0-1) v (down, 0-1); 0 outside."""
    h, w = img.shape[:2]
    x, y = u * w - .5, v * h - .5
    x0, y0 = np.floor(x).astype(int), np.floor(y).astype(int)
    fx, fy = x - x0, y - y0
    out = np.zeros(u.shape + img.shape[2:], np.float32)
    for dy in (0, 1):
        for dx in (0, 1):
            xi, yi = x0 + dx, y0 + dy
            ok = (xi >= 0) & (xi < w) & (yi >= 0) & (yi < h)
            wgt = (fx if dx else 1 - fx) * (fy if dy else 1 - fy)
            val = np.zeros(u.shape + img.shape[2:], np.float32)
            val[ok] = img[yi[ok], xi[ok]]
            out += val * (wgt[..., None] if img.ndim == 3 else wgt)
    return out


class Car:
    def __init__(self, work, name):
        self.name = name
        d = os.path.join(work, name)
        self.meta = json.load(open(os.path.join(d, 'meta.json')))
        ld = lambda n: np.load(os.path.join(d, 'bake_%s.npy' % n)).astype(np.float32)
        pos, nrm, alb, msk, idm, ao = (ld(n) for n in ('pos', 'nrm', 'albedo', 'mask', 'id', 'ao'))
        self.W = pos.shape[1]
        self.P = (pos[..., :3] - .5) * 8
        n = nrm[..., :3] * 2 - 1
        self.N = n / np.maximum(np.linalg.norm(n, axis=-1, keepdims=True), 1e-6)
        self.alb = to_srgb(alb[..., :3])
        self.paint = msk[..., 0]
        self.role = np.rint(idm[..., 0] * 32).astype(int) - 1
        self.ao = np.clip(ao[..., 0], 0, 1)
        self.roles = self.meta['roles']
        self.R = {r: self.role == i for i, r in enumerate(self.roles)}
        self.levels = [np.load(os.path.join(d, 'lod%d.npz' % i)) for i in range(3)]
        self.wheel = np.load(os.path.join(d, 'wheel.npz'))
        self.colour = {r: to_srgb(np.array(c)) for r, c in self.meta['colour'].items()}
        dr = self.meta['drawing']
        self.cy = (dr[0][0] + dr[-1][0]) / 2
        self.kind = self.meta['kind']
        self.rgb = self.alb.copy()
        self.tint = np.zeros(self.P.shape[:2], np.float32)
        self.ink = np.zeros(self.P.shape[:2], np.float32)

    # --- coverage of the UV islands (the triangles themselves) ---
    def coverage(self):
        W = self.W
        im = Image.new('L', (W, W), 0)
        d = ImageDraw.Draw(im)
        for m in self.levels[:1] + [self.wheel]:
            for t in m['uv']:
                d.polygon([(float(u * W), float(v * W)) for u, v in t], fill=255)
        return np.asarray(im) > 0


def lamp_units(car, sel, cell=.03):
    """Lamp units: texels of sel grouped by where they are on the car (3D voxels joined)."""
    P = car.P[sel]
    q = np.floor(P / cell).astype(int)
    keys = {}
    for k in map(tuple, q):
        keys.setdefault(k, len(keys))
    parent = list(range(len(keys)))

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a
    for k, i in keys.items():
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    j = keys.get((k[0] + dx, k[1] + dy, k[2] + dz))
                    if j is not None:
                        parent[find(i)] = find(j)
    lab = np.array([find(keys[k]) for k in map(tuple, q)])
    return lab


def plane_axes(n):
    """Two axes across a face whose normal is n (the larger parts of the others)."""
    a = np.argmax(np.abs(n))
    return [i for i in range(3) if i != a]


# --- The procedural materials -------------------------------------------------------------------

def paint_wheel(car):
    m = car.meta
    hx, hy = m['lf']
    P, N = car.P, car.N
    lx = P[..., 0] - hx          # outward (the left front wheel: vehicles.py +x)
    dy, dz = P[..., 1] - hy, P[..., 2] - .32
    r = np.hypot(dy, dz)
    th = np.arctan2(dz, dy)
    rgb = car.rgb
    near = (np.abs(lx) < .2) & (r < .34)
    # Tyre: tread grooves and sipes; sidewall lettering.
    tyre = car.R['rubber'] & near
    base = np.array([.115, .115, .125])
    rgb[tyre] = base
    tread = tyre & (r > .3) & (np.abs(lx) < .085)
    g = np.zeros_like(r)
    for c in (-.05, 0, .05):
        g = np.maximum(g, 1 - smooth(.004, .007, np.abs(lx - c)))
    rib = np.floor((lx + .075) / .05)
    ph = (th / (2 * np.pi) * 64 + rib * .5) % 1
    sipe = (1 - smooth(.03, .07, np.abs(ph - .5))) * (np.abs(lx) < .07)
    k = 1 - .55 * np.maximum(g, sipe * .8)
    rgb[tread] *= k[tread][:, None]
    side = tyre & (lx > .1) & (N[..., 0] > .3)
    # the sidewall's text, along a strip that goes round clockwise as seen from outside
    strip = make_sidewall_strip()
    s = ((np.pi / 2 - th) / (2 * np.pi)) % 1
    t = (.300 - r) / (.300 - .258)
    cov = sample(strip, s, t) * (t > 0) * (t < 1)
    lettering = side & (cov > 0)
    rgb[lettering] = rgb[lettering] * (1 - cov[lettering, None]) + np.array([.62, .62, .64]) * cov[lettering, None]
    ring = side & (np.abs(r - .307) < .002)
    rgb[ring] *= 1.4
    # Rim: machined face, polished lip, darker spoke sides, lug nuts.
    rim = car.R['rim'] & near
    face = rim & (N[..., 0] > .55)
    rgb[rim] = np.array([.55, .57, .6])
    rgb[face] = np.array([.80, .82, .85]) * (.94 + .06 * np.sin(r[face] * 1400))[:, None]
    lip = rim & (r > .197)
    rgb[lip] = np.array([.93, .94, .96])
    hub = rim & (r < .06) & (N[..., 0] > .5)
    for i in range(5):
        a = 2 * np.pi * i / 5 + .2
        d = np.hypot(dy - .036 * np.cos(a), dz - .036 * np.sin(a))
        nut = hub & (d < .009)
        rgb[nut] = np.array([.38, .39, .42]) + (.5 * (1 - smooth(.002, .005, d[nut])))[:, None]
    dark = car.R['rimdark'] & near
    rgb[dark] = np.array([.13, .135, .15])
    cap = dark & (r < .046) & (N[..., 0] > .5)
    # the badge: a chevron (V) in silver, a ring
    ax, ay = dy / .04, dz / .04
    chev = (np.abs(np.abs(ax) * .9 - (ay + .55)) < .17) & (ay > -.6) & (ay < .55)
    ringb = np.abs(r - .038) < .003
    rgb[cap & (chev | ringb)] = np.array([.82, .84, .87])
    # Brake disc: pad track, concentric marks, cross-drilled holes; vented edge; dark hat.
    disc = car.R['rotor'] & near
    rgb[disc] = np.array([.22, .225, .235])
    flat = disc & (np.abs(N[..., 0]) > .6) & (r > .086)
    track = flat & (r > .098) & (r < .166)
    rgb[flat] = np.array([.36, .365, .37])
    rgb[track] = (np.array([.52, .52, .53]) * (.93 + .07 * np.sin(r[track] * 3000))[:, None])
    holes = np.zeros_like(r)
    for rr, cnt in ((.143, 18), (.119, 15)):
        a = np.round((th - .1) / (2 * np.pi) * cnt) * 2 * np.pi / cnt + .1
        d = np.hypot(dy - rr * np.cos(a), dz - rr * np.sin(a))
        holes = np.maximum(holes, 1 - smooth(.0045, .0065, d))
    rgb[track] *= (1 - .85 * holes[track])[:, None]
    edge = disc & (np.abs(N[..., 0]) < .4) & (r > .16)
    vane = ((th / (2 * np.pi) * 40) % 1) < .45
    rgb[edge & vane] = .07
    hat = disc & (r < .086)
    rgb[hat] = np.array([.16, .16, .17])
    # Caliper: a light label on its face.
    cal = car.R['caliper']
    lab = cal & (N[..., 0] > .5) & (np.abs(dz - .025) < .018) & (np.abs(dy - .128) < .011)
    rgb[lab] = np.array([.95, .95, .95])


_strip = []
BRAND = 'RACE MASTERS'  # the project's own mark on the tyres and the bodywork (TG_APP_NAME)


def make_sidewall_strip():
    if _strip:
        return _strip[0]
    H, L = 90, 4200
    im = Image.new('L', (L, H), 0)
    d = ImageDraw.Draw(im)
    big = ImageFont.truetype(F_BOLD, 74)
    small = ImageFont.truetype(F_MED, 40)
    for k in range(2):  # twice round
        x0 = k * L // 2
        d.text((x0 + 120, 4), BRAND, font=big, fill=255)
        d.text((x0 + 120 + int(big.getlength(BRAND)) + 60, 26), 'PERFORMANCE RADIAL  •  225/45 ZR16  •  TUBELESS', font=small, fill=200)
    _strip.append(np.asarray(im, np.float32) / 255)
    return _strip[0]


def paint_lamps(car):
    rgb, tint = car.rgb, car.tint
    P, N = car.P, car.N
    for role, kind in (('lamp', 'head'), ('red', 'tail'), ('amber', 'amber')):
        sel = car.R[role] & ~((np.abs(P[..., 0] - car.meta['lf'][0]) < .2) & (np.hypot(P[..., 1] - car.meta['lf'][1], P[..., 2] - .32) < .34))
        if not sel.any():
            continue
        lab = lamp_units(car, sel)
        idx = np.argwhere(sel)
        Ps, Ns = P[sel], N[sel]
        val = np.ones(len(Ps), np.float32)
        clear = np.zeros(len(Ps), bool)
        for u in np.unique(lab):
            m = lab == u
            n = Ns[m].mean(0)
            a0, a1 = plane_axes(n)
            q0, q1 = Ps[m][:, a0], Ps[m][:, a1]
            lo0, hi0, lo1, hi1 = q0.min(), q0.max(), q1.min(), q1.max()
            w, h = hi0 - lo0, hi1 - lo1
            if w < h:  # the long axis first
                q0, q1, lo0, hi0, lo1, hi1, w, h = q1, q0, lo1, hi1, lo0, hi0, h, w
            c1 = (lo1 + hi1) / 2
            if kind == 'head':
                nb = max(1, int(round(w / max(h, .03))))
                R = min(w / nb, h) * .45
                best = np.full(q0.shape, 9.0)
                for i in range(nb):
                    c0 = lo0 + (i + .5) * w / nb
                    best = np.minimum(best, np.hypot(q0 - c0, q1 - c1))
                rr = best / max(R, 1e-3)
                v = np.where(rr < .42, 1.0, np.where(rr < .55, .3, np.where(rr < .62, .9, .55 + .35 * smooth(1.25, .6, rr))))
                v *= .96 + .04 * np.sin((q1 - lo1) / .004)  # the lens's fluting
                val[m] = v
            elif kind == 'tail':
                # honeycomb of small cells; the inner end a clear reverse light
                cs = .021
                x, y = q0 / cs, q1 / (cs * .866)
                row = np.floor(y)
                xx = x + (row % 2) * .5
                d = np.hypot(xx - np.floor(xx) - .5, (y - row - .5) * .866)
                v = .5 + .5 * (1 - smooth(.25, .5, d))
                border = np.minimum.reduce([q0 - lo0, hi0 - q0, q1 - lo1, hi1 - q1])
                v = np.where(border < .006, .38, v)
                val[m] = v
                # (the reverse light: at the end nearest the car's middle)
                mid_end = lo0 if abs(lo0) < abs(hi0) else hi0
                inner = (np.abs(q0 - mid_end) < w * .18) & (w > .2) & (a0 == 0)
                clear[np.where(m)[0][inner]] = True
            else:
                val[m] = .85 + .15 * np.sin((q0 - lo0) / .003)
        flat = idx
        if kind in ('head', 'tail'):
            rgb[flat[:, 0], flat[:, 1]] = val[:, None]
            tint[flat[:, 0], flat[:, 1]] = 1  # (tinted by the vertex: the brake lights brighten)
            cl = flat[clear]
            rgb[cl[:, 0], cl[:, 1]] = np.array([.80, .80, .82]) * val[clear][:, None]
            tint[cl[:, 0], cl[:, 1]] = 0
        else:
            rgb[flat[:, 0], flat[:, 1]] *= val[:, None]


def carbon(a, b, p=.006):
    i, j = np.floor(a / p), np.floor(b / p)
    tw = ((i + np.floor(j / 2)) % 2) < 1
    s = np.where(tw, np.sin((a / p % 1) * np.pi), np.sin((b / p % 1) * np.pi))
    return .075 + .06 * s * np.where(tw, 1, .8)


def paint_materials(car):
    rgb, P, N = car.rgb, car.P, car.N
    R = car.R
    # Intakes: a fine diamond mesh.
    blk = R['black'] & (P[..., 2] < car.meta['cabin'][7] - .03)  # (intakes, not the windows' frames)
    if blk.any():
        idx = np.argwhere(blk)
        Ps, Ns = P[blk], N[blk]
        ax = np.argmax(np.abs(Ns), 1)
        a = np.where(ax == 0, Ps[:, 1], Ps[:, 0])
        b = np.where(ax == 2, Ps[:, 1], Ps[:, 2])
        u, v = (a + b) / .016, (a - b) / .016
        line = np.minimum(np.abs(u - np.round(u)), np.abs(v - np.round(v)))
        val = np.where(line < .14, .17, .035)
        rgb[idx[:, 0], idx[:, 1]] = val[:, None] * np.array([1, 1, 1.08])
    # Carbon: the made carbon parts, and satin trim low down (splitters, valances).
    cb = R['carbon'] | (R['trim'] & (P[..., 2] < .3))
    if cb.any():
        idx = np.argwhere(cb)
        Ps, Ns = P[cb], N[cb]
        ax = np.argmax(np.abs(Ns), 1)
        a = np.where(ax == 0, Ps[:, 1], Ps[:, 0])
        b = np.where(ax == 2, Ps[:, 1], Ps[:, 2])
        rgb[idx[:, 0], idx[:, 1]] = carbon(a, b)[:, None] * np.array([1, 1.02, 1.1])
    # Glass: a dark frit band round each pane (and the screen's shade band at the top).
    gl = R['glass']
    if gl.any():
        rgb[gl] = np.array([.11, .14, .17])
        # (a ceramic band, solid at the edge and thinning out: a halftone too fine to draw)
        dist = ndimage.distance_transform_edt(gl | (car.role < 0))
        k = smooth(3.5, 8, dist)
        rgb[gl] = rgb[gl] * k[gl][:, None] + np.array([.02, .02, .025]) * (1 - k[gl])[:, None]
        cab = car.meta['cabin']
        shade = gl & (P[..., 1] < cab[1]) & (P[..., 1] > cab[1] - .14) & (np.abs(P[..., 0]) < cab[4])
        rgb[shade] *= .45
    # Interior: carpet and plastics; seats with an insert, pleats and coloured stitching.
    it = R['interior']
    noise = np.random.default_rng(7).random(P.shape[:2]).astype(np.float32)
    rgb[it] = np.array([.075, .078, .085]) * (.85 + .3 * noise[it])[:, None]
    fab = R['fabric']
    accent = {0: (.95, .95, .95), 1: (.85, .1, .1), 2: (.9, .7, .25), 3: (.85, .12, .1)}[car.kind]
    if car.name == 'amber':
        accent = (.95, .55, .1)
    xs = np.abs(np.abs(P[..., 0]) - .34)
    ins = fab & (xs < .09)
    rgb[fab] = np.array([.07, .07, .075]) * (.9 + .2 * noise[fab])[:, None]
    pleat = .8 + .2 * np.abs(np.sin((P[..., 2] + P[..., 1]) / .022))
    rgb[ins] = np.array([.12, .12, .125]) * pleat[ins][:, None]
    st = fab & (np.abs(xs - .09) < .004)
    rgb[st] = np.array(accent)
    # Roll bar satin; chrome exhaust tips (a dark inside).
    rgb[R['rollbar']] = np.array([.42, .43, .45])
    rgb[R['chrome']] = np.array([.85, .86, .88])
    # Number plate: an invented plate, read from behind.
    pl = R['plate']
    if pl.any():
        rear = car.meta['drawing'][-1][0]
        img = text_img('RMS · %02d' % (17 + car.kind * 11 + (car.name == 'amber') * 5), F_BOLD, 120, 6)
        hgt, wid = .052, .052 * img.shape[1] / img.shape[0]
        u = (-P[..., 0] + wid / 2) / wid
        v = (.48 + hgt / 2 - P[..., 2]) / hgt
        cov = sample(img, u, v) * pl
        rgb[pl] = np.array([.86, .86, .8])
        rgb[:] = rgb * (1 - cov[..., None]) + np.array([.08, .08, .1]) * cov[..., None]
        edge = pl & ((np.abs(np.abs(P[..., 0]) - .16) < .004) | (np.abs(np.abs(P[..., 2] - .48) - .04) < .004))
        rgb[edge] = .2


# --- Liveries -----------------------------------------------------------------------------------

SPONSORS = {  # invented
    'rosso': ('ZEPHYRA', 'KORVAX OIL', 'NIMBUS'),
    'glacier': ('HALCYON', 'STRATO', 'NIMBUS'),
    'violet': ('MERIDIA', 'ZEPHYRA', 'KORVAX OIL'),
    'azure': ('STRATO', 'MERIDIA', 'HALCYON'),
    'amber': ('KORVAX OIL', 'STRATO', 'ZEPHYRA'),
}


INK, PAINT = 'ink', 'paint'


def put(car, sel, cov, colour):
    """A decal over the body: covers paint (no longer tinted) and what is under it. INK: a
    decal in the contrasting ink, white on dark paint, black on light (the game makes both
    textures from the ink mask and picks by the car's paint: a rival's paint is the game's
    choice); PAINT: takes the ink away (a chequer's paint cells)."""
    cov = np.clip(cov, 0, 1) * sel
    m = cov > 0
    if isinstance(colour, str):
        car.ink[m] = car.ink[m] * (1 - cov[m]) + (cov[m] if colour == INK else 0)
        return
    c = np.array(colour, np.float32)
    car.rgb[m] = car.rgb[m] * (1 - cov[m, None]) + c * cov[m, None]
    car.tint[m] *= 1 - cov[m]
    car.ink[m] *= 1 - cov[m]


def side_text(car, text, font, y0, zc, height, colour, sides=(-1, 1), sel=None, track=2):
    """Text on the car's sides, read from outside: its left edge at y0 (the front end on the
    left side, as it is read), centred on height zc."""
    img = text_img(text, font, 160, track)
    wid = height * img.shape[1] / img.shape[0]
    P, N = car.P, car.N
    for s in sides:
        on = (P[..., 0] * s > .3) & (N[..., 0] * s > .25) & (sel if sel is not None else car.R['paint'] | (car.tint > .5))
        # left side (+x, the game's left): reads front to back (+y); right side back to front
        u = (P[..., 1] - y0) / wid if s > 0 else (y0 + wid - P[..., 1]) / wid
        v = (zc + height / 2 - P[..., 2]) / height
        put(car, on, sample(img, u, v), colour)
    return wid


def top_text(car, text, font, yc, xc, height, colour, from_rear=True, zmin=0):
    """Text on a top surface (above zmin: a wing, not the deck under it), read from behind (the
    chase camera) or from the front."""
    img = text_img(text, font, 160, 2)
    wid = height * img.shape[1] / img.shape[0]
    P, N = car.P, car.N
    on = (N[..., 2] > .5) & (car.R['paint'] | (car.tint > .5)) & (P[..., 2] > zmin)
    if from_rear:  # seen from behind: right is -x, up is -y (forward)
        u = (xc + wid / 2 - P[..., 0]) / wid
        v = (P[..., 1] - (yc - height / 2)) / height
    else:
        u = (P[..., 0] - (xc - wid / 2)) / wid
        v = (yc + height / 2 - P[..., 1]) / height
    put(car, on, sample(img, u, v), colour)


def number_plate(car, shape, ring, fill):
    """The race number's roundel (or panel) on both doors; the digits are the game's (drawn
    there by the renderer, each car its own)."""
    y, z = car.meta['number']
    P, N = car.P, car.N
    for s in (-1, 1):
        on = (P[..., 0] * s > .3) & (N[..., 0] * s > .3) & car.R['paint']
        if shape == 0:
            d = np.hypot(P[..., 1] - y, P[..., 2] - z)
            put(car, on, 1 - smooth(.150, .154, d), ring)
            put(car, on, 1 - smooth(.136, .140, d), fill)
        else:
            dy, dz = np.abs(P[..., 1] - y) - .16, np.abs(P[..., 2] - z) - .12
            d = np.hypot(np.maximum(dy, 0), np.maximum(dz, 0)) + np.minimum(np.maximum(dy, dz), 0)
            put(car, on, 1 - smooth(.016, .02, d), ring)
            put(car, on, 1 - smooth(.004, .008, d), fill)


def livery(car):
    P, N = car.P, car.N
    paint = car.R['paint']
    top = paint & (N[..., 2] > .25) & (np.abs(N[..., 0]) < .75)
    x, y, z = P[..., 0], P[..., 1], P[..., 2]
    W, D = (.95, .95, .95), (.06, .06, .07)
    sp = SPONSORS[car.name]
    dr = car.meta['drawing']
    front, rear = dr[0][0], dr[-1][0]
    ax0, ax1 = car.meta['axles']
    if car.name == 'rosso':
        # twin white stripes nose to tail, fine dark edges; sponsors under the number and on
        # the sills, the edition on the tail
        for c in (-.13, .13):
            d = np.abs(x - c)
            put(car, top, 1 - smooth(.075, .079, d), D)
            put(car, top, 1 - smooth(.066, .070, d), INK)
        number_plate(car, 0, D, W)
        side_text(car, sp[0], F_ITALIC, -.05 - .17, .30, .075, INK)
        side_text(car, sp[1], F_BOLD, -.42, .205, .05, INK)
    elif car.name == 'glacier':
        # a black sash slanting down the doors and a red pinstripe at its edge
        sash = (y - .12) + (z - .5) * .9
        on = paint & (np.abs(N[..., 0]) > .25)
        put(car, on, smooth(-.004, .004, sash) * (1 - smooth(.27, .278, sash)), INK)
        put(car, on, (1 - smooth(.003, .006, np.abs(sash - .295))), (.85, .08, .1))
        top_bonnet = top & (y < -1.3)
        put(car, top_bonnet, 1 - smooth(.25, .254, np.abs(x)), INK)
        number_plate(car, 0, D, W)
        side_text(car, sp[0], F_ITALIC, -.62, .205, .055, W, sel=car.R['trim'] | car.R['lower'])
        side_text(car, sp[1], F_BOLD, ax0 + .45, .45, .06, INK)
        top_text(car, sp[0], F_ITALIC, 2.08, 0, .12, INK, zmin=1.0)
    elif car.name == 'violet':
        # gold pinstripe along the shoulder, two on the bonnet
        gold = (.92, .72, .28)
        on = paint & (np.abs(N[..., 0]) > .3)
        zs = np.interp(y, [p[0] for p in dr], [p[2] for p in dr]) - .07
        put(car, on, 1 - smooth(.004, .007, np.abs(z - zs)), gold)
        for c in (-.2, .2):
            put(car, top & (y < -.75), 1 - smooth(.004, .007, np.abs(x - c)), gold)
        number_plate(car, 0, gold, W)
        side_text(car, sp[0], F_TECH, .62 - .55, .29, .06, gold)
        side_text(car, sp[1], F_BOLD, ax0 + .42, .40, .06, INK)
        top_text(car, BRAND, F_TECH, rear - .3, 0, .06, gold)
    elif car.name == 'amber':
        # a wide black stripe over the top with a chequered band at its end
        d = np.abs(x)
        put(car, top, 1 - smooth(.17, .174, d), INK)
        chk = top & (d < .17) & (y > .95) & (y < 1.1)
        cell = ((np.floor(x / .034) + np.floor(y / .034)) % 2) < 1
        put(car, chk & cell, np.ones_like(x), PAINT)
        number_plate(car, 0, D, W)
        side_text(car, sp[0], F_ITALIC, -.3, .28, .07, INK)
        side_text(car, sp[1], F_BOLD, ax0 + .42, .45, .06, INK)
    elif car.name == 'azure':
        # a white line along the two-tone edge; an endurance number panel
        on = paint & (np.abs(N[..., 0]) > .3)
        zl = np.interp(y, [-2.4, -.99, .86, 2.4], [.40, .40, .44, .48]) + .03
        put(car, on, 1 - smooth(.006, .009, np.abs(z - zl)), INK)
        number_plate(car, 1, D, W)
        side_text(car, sp[0], F_ITALIC, ax0 + .45, .56, .08, INK)
        side_text(car, BRAND, F_TECH, -.6, .31, .06, (.85, .87, .9), sel=car.R['lower'])
        top_text(car, sp[1], F_ITALIC, 2.3, 0, .1, INK, zmin=.9)


def finish(car, cover):
    """AO, grime, then fill every texel outside the islands from its nearest island."""
    rgb, tint = car.rgb, car.tint
    ao = car.ao
    # (the paint keeps the game's colour on open panels - the colour lock, judged from the chase
    # camera: its occlusion is lighter, and its shading set so the open panels sit at 1)
    paint = tint > .5
    if paint.any():  # (open panels at 1: the bake's hemisphere sees a little of the car itself)
        ao = np.clip(ao / max(np.percentile(ao[paint], 90), .5), 0, 1)
    k = np.where(paint, 1 - .25 * (1 - ao), 1 - .75 * (1 - ao))
    rgb *= k[..., None]
    low = smooth(.4, .12, car.P[..., 2]) * (car.role >= 0) * ~car.R['glass'] * ~paint
    rgb *= (1 - .07 * low)[..., None]
    if paint.any():
        lum = rgb[paint].mean(1)
        top = np.percentile(lum, 75)
        if top > .5:
            rgb[paint] = np.clip(rgb[paint] / top, 0, 1)
    have = car.role >= 0
    idx = ndimage.distance_transform_edt(~have, return_distances=False, return_indices=True)
    rgb[:] = rgb[idx[0], idx[1]]
    tint[:] = tint[idx[0], idx[1]]
    car.ink[:] = car.ink[idx[0], idx[1]]
    # a white block (the colour of the corner, untextured) where no island is
    free = ~ndimage.binary_dilation(cover, iterations=10)
    W = car.W
    for by in range(0, W - 32, 16):
        for bx in range(0, W - 32, 16):
            if free[by:by + 32, bx:bx + 32].all():
                rgb[by:by + 32, bx:bx + 32] = 1
                tint[by:by + 32, bx:bx + 32] = 1
                car.ink[by:by + 32, bx:bx + 32] = 0
                return ((bx + 16) / W, (by + 16) / W)
    rgb[:8, :8] = 1
    tint[:8, :8] = 1
    return (4 / W, 4 / W)


# --- The file ------------------------------------------------------------------------------------

def corners(car, pos, nrm, uv, role, centre=True, origin=(0, 0, 0)):
    """Triangles in the game's frame (x right, y up, z forward): 32-byte corners."""
    cls = car.meta['class']
    roles = car.roles
    out = []
    for t in range(len(pos)):
        r = roles[role[t]]
        k = cls[r]
        if r == 'paint':
            c = car.colour['paint']
        elif r == 'lamp':
            c = np.array([1, .97, .9])
        elif r == 'red':
            c = np.array([.82, .1, .06])
        else:
            c = car.colour[r]
        rgbb = [int(round(float(np.clip(v, 0, 1)) * 255)) for v in c]
        n = nrm[t]
        for kk in range(3):
            p = pos[t, kk] - np.array(origin)
            gx, gy, gz = -p[0], p[2], -(p[1] - (car.cy if centre else 0))
            u, v = uv[t, kk]
            out.append(struct.pack('<6f4B2H', gx, gy, gz, -n[0], n[2], -n[1], k, *rgbb,
                                   int(round(np.clip(u, 0, 1) * 65535)), int(round(np.clip(v, 0, 1) * 65535))))
    return out


def write(car, out_dir, white, px):
    m = car.meta
    data = [b'TGCR', struct.pack('<II', 3, 3)]
    tris = []
    for lvl, L in enumerate(car.levels):
        roles = [car.roles[r] for r in L['role']]
        glass = np.array([r == 'glass' for r in roles])
        order = np.concatenate([np.nonzero(~glass)[0], np.nonzero(glass)[0]])  # the glass last (see-through)
        flags = 1 if lvl < 2 and (L['grp'] == 1).any() else 0
        c = corners(car, L['pos'][order], L['nrm'][order], L['uv'][order], L['role'][order])
        data.append(struct.pack('<II', flags, len(order)))
        data += c
        tris.append(len(order))
    wh = car.wheel
    hx, hy = m['lf']
    origin = (hx, hy, .32)
    spin = wh['grp'] != 3
    for part in (spin, ~spin):
        c = corners(car, wh['pos'][part], wh['nrm'][part], wh['uv'][part], wh['role'][part], centre=False, origin=origin)
        data.append(struct.pack('<I', len(c) // 3))
        data += c
    # The right wheel: mirrored; its outer sidewall takes the texture of the point opposite
    # front to back (a right wheel is a left one turned round: its lettering reads right).
    pos, nrm, uv, role = wh['pos'][spin], wh['nrm'][spin], wh['uv'][spin].copy(), wh['role'][spin]
    key = lambda p: (round(float(p[0]), 4), round(float(p[1]), 4), round(float(p[2]), 4))
    at = {}
    for t in range(len(pos)):
        for k in range(3):
            at[key(pos[t, k])] = uv[t, k]
    rub = car.roles.index('rubber')
    outward = nrm[:, 0] > .5  # (vehicles.py +x: the left wheel's outside)
    for t in np.nonzero((role == rub) & outward)[0]:
        new = [at.get(key((pos[t, k, 0], 2 * hy - pos[t, k, 1], pos[t, k, 2]))) for k in range(3)]
        if all(n is not None for n in new):
            uv[t] = np.array(new)
    mp = pos.copy()
    mp[..., 0] = 2 * hx - mp[..., 0]
    mn = nrm.copy()
    mn[:, 0] *= -1
    c = corners(car, mp, mn, uv, role, centre=False, origin=(hx, hy, .32))
    data.append(struct.pack('<I', len(c) // 3))
    data += c
    hubs = []
    for (x, y) in m['hubs']:
        hubs.append((-x, .32, -(y - car.cy)))
    hubs.sort(key=lambda g: (-g[2], g[0]))  # front first, left first
    for g in hubs:
        data.append(struct.pack('<3f', *g))
    ny, nz = m['number']
    shape = 1 if car.name == 'azure' else 0
    data.append(struct.pack('<4fII', -(ny - car.cy), nz, m['number_x'], .14 if shape == 0 else .12, shape, 0xFF12161C))
    data.append(struct.pack('<2f', *white))
    data.append(struct.pack('<II', car.W, car.W))
    data.append(px.tobytes())
    data.append(struct.pack('<I', 1))  # the ink mask: u8 per texel
    data.append((np.clip(car.ink, 0, 1) * 255 + .5).astype(np.uint8).tobytes())
    path = os.path.join(out_dir, 'car-%s.tgcar' % car.name)
    open(path, 'wb').write(b''.join(data))
    return path, tris, len(wh['pos'][spin]), int((~spin).sum())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('work')
    ap.add_argument('out')
    ap.add_argument('--cars', default='rosso,glacier,violet,azure,amber')
    ap.add_argument('--preview', default=None)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    for name in a.cars.split(','):
        car = Car(a.work, name)
        cover = car.coverage()
        # the starting point: the baked albedo; paint where the mask says, its shading only
        pc = car.colour['paint']
        mask = np.clip(car.paint, 0, 1)
        car.tint = mask.copy()
        denom = 1 - mask[..., None] + mask[..., None] * pc
        car.rgb = np.clip(car.alb / np.maximum(denom, 1e-3), 0, 1)
        paint_wheel(car)
        paint_lamps(car)
        paint_materials(car)
        livery(car)
        white = finish(car, cover)
        a8 = (np.clip(car.tint, 0, 1) * 255 + .5).astype(np.uint32)
        c8 = (np.clip(car.rgb, 0, 1) * 255 + .5).astype(np.uint32)
        px = (a8 << 24) | (c8[..., 0] << 16) | (c8[..., 1] << 8) | c8[..., 2]
        path, tris, wt, ct = write(car, a.out, white, px.astype('<u4'))
        print('%s: levels %s, wheel %d + caliper %d, atlas %d, white %s -> %s' % (name, tris, wt, ct, car.W, white, path))
        if a.preview:
            os.makedirs(a.preview, exist_ok=True)
            Image.fromarray((np.clip(car.rgb, 0, 1) * 255).astype(np.uint8)[::-1]).save(os.path.join(a.preview, name + '_rgb.png'))
            Image.fromarray((np.clip(car.tint, 0, 1) * 255).astype(np.uint8)[::-1]).save(os.path.join(a.preview, name + '_tint.png'))
            shown = car.rgb * (1 - car.tint[..., None] + car.tint[..., None] * pc)
            ink = .06 if (pc * [.2126, .7152, .0722]).sum() > .6 else .95
            shown = shown * (1 - car.ink[..., None]) + ink * car.ink[..., None]
            Image.fromarray((np.clip(shown, 0, 1) * 255).astype(np.uint8)[::-1]).save(os.path.join(a.preview, name + '_painted.png'))


if __name__ == '__main__':
    main()
