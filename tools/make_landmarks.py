#!/usr/bin/env python3
"""Bakes the skyline landmarks modelled in 3D into src/mod3d/landmark_meshes.h.

A landmark on the skyline is far away in one fixed direction (lm_place in view3d_stages.inc):
like the sun it turns with the camera but is always seen from the same side. So each one is
modelled here as a real 3D object (lofts, ellipsoids, boxes, a terrain height field, in
metres or in units of its height on the screen H), looked at once from that side
(orthographic, a few degrees from below as from the road), and baked as a flat drawing that
still knows its shape: per corner its place on the screen (in H), its surface normal (in the
view's frame: x right, y up, z towards the viewer), its colour (albedo, ambient occlusion
baked in), its material and how much distance haze it takes. The game lights each corner
with the scene's own light (kLight, turned into the view frame for the course's direction),
hazes it and draws the triangles as background, in an order that is right for this view.

The technique is the far layer of a "3D skybox" (Valve's Source engine: distant scenery is
real geometry drawn behind the world with the camera's rotation only) taken one step further:
since the far landmark is only ever seen from one side, its hidden-surface problem is solved
here, offline. Triangles hidden from that side are dropped (an id buffer at ~2000 pixels per
H), and the rest are put in a painter's order taken from the same id buffer (wherever a
triangle is in front of another at a sample, it is drawn after it; a topological sort, ties
and cycles by depth), so the game needs no depth buffer for it and it stays in the skyline's
layers (behind the near hills and the mist).

Everything is original, procedural work (no third-party model). References (researched
2026-11-03) used for the proportions and the look: Christ the Redeemer: figure 30 m on an 8 m
pedestal, arms 28 m across, Art Deco, soapstone over concrete, faces east, new floodlights at
its foot since 2010 (en.wikipedia.org/wiki/Christ_the_Redeemer_(statue)); head 3.75 m, hands
3.2 m (Paul Landowski's studio, paul-landowski.com/en/?p=381); the long pleated tunic under a
wide mantle ending ~8 m above the feet, the sleeves hanging under the arms to the hips, the
darker ribbed pedestal with a pale top (the Wikimedia Commons photo
"Christ_the_Redeemer_-_Cristo_Redentor.jpg", looked at, not traced); Corcovado's granite
peak (~710 m, a near-vertical rocky formation, Tijuca forest; "corcovado" = hunchback:
en.wikipedia.org/wiki/Corcovado); Sugarloaf (Pao de Acucar) 396 m, a bare granite dome beside
the lower, wooded Morro da Urca, joined by the cable car. Technique: the 3D skybox
(developer.valvesoftware.com/wiki/3D_Skybox: far geometry drawn behind the world, not culled
with it, its own fog); the order from the id buffer is a painter's algorithm made exact the
way Newell's is, by the overlaps themselves rather than by depth alone.

Run: tools/make_landmarks.py [--preview DIR] [out]   (numpy; Pillow for --preview)"""
import argparse
import heapq
import math
import sys
from pathlib import Path

import numpy as np

OUT = Path("src/mod3d/landmark_meshes.h")
DEBUG_DIR = None  # --debug: pictures of the samples drawn out of order

# Materials (TG landmark shading in view3d.c, lm_light): 0 ground (forest, rock), 1 stone (the
# statue: floodlit at night), 2 built (pedestal, terraces, towers: floodlit too), 3 a cable (a
# dark line), 4 a lamp (lit windows, a clock face: glows at night, lit as built by day).
MAT_GROUND, MAT_STONE, MAT_BUILT, MAT_CABLE, MAT_LAMP = 0, 1, 2, 3, 4


# --- small geometry kit ---------------------------------------------------------------------

def normalize(v):
    n = np.linalg.norm(v, axis=-1, keepdims=True)
    return v / np.maximum(n, 1e-12)


class Part:
    """A piece of a model: corners (positions, normals or None for smooth, albedo, material,
    haze) and triangles (counter-clockwise seen from outside)."""

    def __init__(self, P, T, albedo, mat, N=None, fog=None):
        self.P = np.asarray(P, float)
        self.T = np.asarray(T, np.int64).reshape(-1, 3)
        a = np.asarray(albedo, float)
        self.A = np.broadcast_to(a, self.P.shape).copy() if a.ndim == 1 else a
        self.M = np.full(len(self.P), mat, np.int64) if np.ndim(mat) == 0 else np.asarray(mat)
        self.N = None if N is None else np.asarray(N, float)
        self.F = np.zeros(len(self.P)) if fog is None else np.broadcast_to(np.asarray(fog, float), (len(self.P),)).copy()

    def smooth(self):
        N = np.zeros_like(self.P)
        a, b, c = self.P[self.T[:, 0]], self.P[self.T[:, 1]], self.P[self.T[:, 2]]
        fn = np.cross(b - a, c - a)
        for k in range(3):
            np.add.at(N, self.T[:, k], fn)
        self.N = normalize(N)
        return self

    def transformed(self, R=np.eye(3), s=1.0, t=(0, 0, 0)):
        p = Part(self.P @ R.T * s + np.asarray(t, float), self.T, self.A, self.M, None if self.N is None else self.N @ R.T, self.F)
        return p


def grid_tris(nv, nu, wrap_u=False, flip=False):
    idx = np.arange(nv * nu).reshape(nv, nu)
    T = []
    cols = nu if wrap_u else nu - 1
    for i in range(nv - 1):
        for j in range(cols):
            a, b, c, d = idx[i, j], idx[i, (j + 1) % nu], idx[i + 1, (j + 1) % nu], idx[i + 1, j]
            if flip:
                T += [(a, c, b), (a, d, c)]
            else:
                T += [(a, b, c), (a, c, d)]
    return np.array(T, np.int64)


def orient_out(part, centre_fn):
    """Flip the winding if most faces point inwards (towards centre_fn(p))."""
    P, T = part.P, part.T
    a, b, c = P[T[:, 0]], P[T[:, 1]], P[T[:, 2]]
    fn = np.cross(b - a, c - a)
    m = (a + b + c) / 3
    if np.sum(np.einsum("ij,ij->i", fn, m - centre_fn(m))) < 0:
        part.T = part.T[:, [0, 2, 1]]
    return part


def rot_x(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])


def rot_y(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])


def rot_z(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])


def ellipsoid(c, r, albedo, mat, nu=16, nv=10, R=np.eye(3), squash=None):
    """An ellipsoid (smooth normals from its own shape)."""
    th = np.linspace(0, 2 * np.pi, nu, endpoint=False)
    ph = np.linspace(-np.pi / 2, np.pi / 2, nv)
    pts, nrm = [], []
    for p in ph:
        for t in th:
            u = np.array([math.cos(p) * math.cos(t), math.sin(p), math.cos(p) * math.sin(t)])
            pts.append(u * r)
            nrm.append(u / np.asarray(r, float))
    P = np.array(pts) @ R.T + c
    N = normalize(np.array(nrm) @ R.T)
    T = grid_tris(nv, nu, wrap_u=True, flip=True)
    part = Part(P, T, albedo, mat, N)
    return orient_out(part, lambda m: np.broadcast_to(np.asarray(c, float), m.shape))


def box(c, h, albedo, mat, R=np.eye(3), cell=2.4):
    """A box, centre c, half sizes h, flat faces (its own corners per face), each face cut into
    cells of about `cell` (small triangles keep the painter's order right where it meets
    other surfaces)."""
    c = np.asarray(c, float)
    h = np.asarray(h, float)
    P, N, T = [], [], []
    for ax in range(3):
        for sgn in (-1, 1):
            n = np.zeros(3)
            n[ax] = sgn
            u, v = [k for k in range(3) if k != ax]
            nu = max(1, int(round(2 * h[u] / cell)))
            nv = max(1, int(round(2 * h[v] / cell)))
            base = len(P)
            for j in range(nv + 1):
                for i in range(nu + 1):
                    p = np.zeros(3)
                    p[ax] = sgn * h[ax]
                    p[u] = -h[u] + 2 * h[u] * i / nu
                    p[v] = -h[v] + 2 * h[v] * j / nv
                    P.append(p)
                    N.append(n)
            e_u, e_v = np.zeros(3), np.zeros(3)
            e_u[u], e_v[v] = 1, 1
            ccw = np.dot(np.cross(e_u, e_v), n) > 0
            for j in range(nv):
                for i in range(nu):
                    a = base + j * (nu + 1) + i
                    b, cc, d = a + 1, a + nu + 2, a + nu + 1
                    T += [(a, b, cc), (a, cc, d)] if ccw else [(a, cc, b), (a, d, cc)]
    P = np.array(P) @ R.T + c
    N = np.array(N) @ R.T
    return Part(P, T, albedo, mat, N)


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def vnoise(x, y, seed=0):
    """Value noise on a lattice (smooth), -1..1."""
    xi, yi = np.floor(x).astype(np.int64), np.floor(y).astype(np.int64)
    xf, yf = x - xi, y - yi

    def h(i, j):
        n = (i * 374761393 + j * 668265263 + seed * 1442695041) & 0xFFFFFFFF
        n = ((n ^ (n >> 13)) * 1274126177) & 0xFFFFFFFF
        return ((n ^ (n >> 16)) & 0xFFFF) / 32767.5 - 1

    u, v = xf * xf * (3 - 2 * xf), yf * yf * (3 - 2 * yf)
    return (h(xi, yi) * (1 - u) + h(xi + 1, yi) * u) * (1 - v) + (h(xi, yi + 1) * (1 - u) + h(xi + 1, yi + 1) * u) * v


def fbm(x, y, oct=4, seed=0):
    s, a, tot = 0.0, 1.0, 0.0
    for o in range(oct):
        s = s + a * vnoise(x * 2 ** o, y * 2 ** o, seed + o)
        tot += a
        a *= 0.5
    return s / tot


def smax(a, b, k):
    """Smooth maximum; the blend narrows to nothing near the ground (no lift of the plain)."""
    k = k * np.clip(np.minimum(a, b) / 0.04, 0, 1) + 1e-9
    h = np.clip(0.5 + 0.5 * (a - b) / k, 0, 1)
    return a * h + b * (1 - h) + k * h * (1 - h)


def crest(X, Z, xs, hs, zc, wf, wb, p=2.6, q=0.6):
    """A mountain whose silhouette seen from +z is the curve (xs, hs) exactly: across it (z) a
    steep section (1 - t^p)^q, wf deep towards the viewer, wb away, round the line zc."""
    Hc = np.interp(X, xs, hs, left=0, right=0)
    w = np.where(Z > zc, wf, wb)
    t = np.abs(Z - zc) / w
    return Hc * np.clip(1 - t ** p, 0, 1) ** q


# --- terrain -----------------------------------------------------------------------------------

def steps(*spans):
    """A non-uniform grid: spans of (start, end, step), joined."""
    out = []
    for (a, b, d) in spans:
        n = max(1, int(round((b - a) / d)))
        out.append(np.linspace(a, b, n + 1)[:-1])
    out.append(np.array([spans[-1][1]]))
    return np.concatenate(out)


def heightfield(hfn, matfn, xs, zs, fog_fn=None):
    """A height field y = h(x, z) on a grid (xs, zs), smooth normals, colours by
    matfn(x, z, h, slope, ao)."""
    X, Z = np.meshgrid(xs, zs)  # rows along z (back to front), columns along x
    Hh = hfn(X, Z)
    gx = np.gradient(Hh, xs, axis=1)
    gz = np.gradient(Hh, zs, axis=0)
    N = normalize(np.stack([-gx, np.ones_like(Hh), -gz], -1))
    slope = np.hypot(gx, gz)
    # ambient occlusion: how far a corner lies under the mean of its surroundings (cavities dark)
    k = 3
    pad = np.pad(Hh, k, mode="edge")
    blur = np.zeros_like(Hh)
    for i in range(-k, k + 1):
        for j in range(-k, k + 1):
            blur += pad[k + i:k + i + Hh.shape[0], k + j:k + j + Hh.shape[1]]
    blur /= (2 * k + 1) ** 2
    ao = np.clip(1 + (Hh - blur) * 9, 0.62, 1.08)
    alb, mat = matfn(X, Z, Hh, slope, ao)
    P = np.stack([X, Hh, Z], -1).reshape(-1, 3)
    T = grid_tris(len(zs), len(xs), flip=False)
    part = Part(P, T, alb.reshape(-1, 3), mat if np.ndim(mat) == 0 else mat.reshape(-1), N.reshape(-1, 3))
    # winding: the normals face up; make the triangles agree
    a, b, c = part.P[part.T[:, 0]], part.P[part.T[:, 1]], part.P[part.T[:, 2]]
    if np.mean(np.cross(b - a, c - a)[:, 1]) < 0:
        part.T = part.T[:, [0, 2, 1]]
    if fog_fn is not None:
        part.F = fog_fn(P[:, 0], P[:, 1], P[:, 2])
    # drop the flat plain round the foot (under the near hills and the mist)
    keep = np.max(part.P[part.T][:, :, 1], axis=1) > 0.004
    part.T = part.T[keep]
    return part


FOREST = np.array([0.12, 0.25, 0.12])
FOREST_LIGHT = np.array([0.22, 0.34, 0.15])
GRANITE = np.array([0.52, 0.50, 0.46])
GRANITE_DARK = np.array([0.33, 0.32, 0.30])
GRANITE_WARM = np.array([0.60, 0.55, 0.49])


def ground_colour(X, Z, Hh, slope, ao, rock, seed):
    """Forest and bare granite mixed by `rock` (0-1), with the forest's mottle and the rock's
    vertical rain streaks; ambient occlusion multiplied in."""
    mott = fbm(X * 38, Z * 38, 4, seed)
    patch = fbm(X * 9, Z * 9, 3, seed + 7)
    forest = FOREST[None, None] * (1 + 0.28 * mott[..., None]) + (FOREST_LIGHT - FOREST)[None, None] * smoothstep(0.1, 0.6, patch)[..., None]
    streak = fbm(X * 45, Hh * 4, 3, seed + 11)  # long down the face, thin across
    warm = smoothstep(0.0, 0.7, fbm(X * 6, Hh * 6, 2, seed + 13))
    rockc = GRANITE[None, None] + (GRANITE_DARK - GRANITE)[None, None] * smoothstep(-0.1, 0.5, streak)[..., None] \
        + (GRANITE_WARM - GRANITE)[None, None] * warm[..., None] * 0.6
    rock = np.clip(rock, 0, 1)[..., None]
    col = forest * (1 - rock) + rockc * rock
    return col * ao[..., None]


def corcovado():
    """Corcovado in units of H (its foot at y 0, the summit platform's rock at 0.585): a granite
    dome with near-vertical faces (the "hunchback": steepest to the right), its summit rounded,
    the Tijuca ridge falling away to the left in knobs and saddles, wooded lower slopes and a
    forested spur in front."""
    TOP = 0.585

    def core(X, Z):
        # the peak's own outline: a long steep convex rise from the saddle on the left, the
        # summit's small flat (the terraces' rock), the near-vertical drop on the right; its
        # face to the viewer sheer and cut by gullies on the right, the west slope gentler
        xs = [-0.5, -0.36, -0.29, -0.21, -0.15, -0.105, -0.075, 0.0, 0.07, 0.09, 0.105, 0.118, 0.13, 0.148, 0.17, 0.21]
        hs = [0.0, 0.26, 0.335, 0.41, 0.47, 0.53, 0.575, TOP, 0.58, 0.55, 0.49, 0.41, 0.31, 0.18, 0.07, 0.0]
        gul = 1 + 0.2 * fbm(X * 34 + 7, np.zeros_like(X), 3, 4)
        wf = np.interp(X, [-0.42, -0.24, -0.1, 0.0, 0.2], [0.34, 0.24, 0.15, 0.13, 0.12]) * gul
        wb = np.interp(X, [-0.42, -0.1, 0.2], [0.3, 0.22, 0.2])
        Hc = np.interp(X, xs, hs, left=0, right=0)
        w = np.where(Z > 0, wf, wb)
        t = np.abs(Z) / w
        lump = 1 + 0.03 * fbm(X * 9 + 3, Z * 9, 2, 2)
        return Hc * np.clip(1 - t ** 2.8, 0, 1) ** 0.55 * lump

    def ridge(X, Z):
        xs = [-1.6, -1.32, -1.08, -0.9, -0.74, -0.61, -0.5, -0.4, -0.3, -0.2, -0.1, 0.02]
        hs = [0.0, 0.055, 0.11, 0.15, 0.215, 0.275, 0.3, 0.27, 0.285, 0.33, 0.25, 0.0]
        zr = -0.05 + 0.07 * X
        bump = 1 + 0.08 * fbm(X * 11, Z * 11, 3, 5)
        return crest(X, Z - zr, xs, hs, 0.0, 0.14, 0.2, 2.0, 0.9) * bump

    def right(X, Z):
        xs = [0.0, 0.1, 0.19, 0.3, 0.42, 0.56, 0.72, 0.9, 1.02]
        hs = [0.0, 0.2, 0.17, 0.185, 0.13, 0.09, 0.045, 0.012, 0.0]
        bump = 1 + 0.1 * fbm(X * 12, Z * 12, 3, 6)
        return crest(X, Z, xs, hs, 0.03, 0.16, 0.16, 2.0, 0.9) * bump

    def spur(X, Z):  # a wooded spur in front of the cliff's foot
        return 0.17 * np.exp(-((X + 0.06) / 0.2) ** 2 - ((Z - 0.12) / 0.08) ** 2) * (1 + 0.1 * fbm(X * 14, Z * 14, 2, 8))

    def back(X, Z):  # the Tijuca massif behind, low and far
        return 0.2 * np.exp(-((X + 0.8) / 0.42) ** 2 - ((Z + 0.38) / 0.12) ** 2)

    def hfn(X, Z):
        h = smax(core(X, Z), ridge(X, Z), 0.04)
        h = smax(h, right(X, Z), 0.04)
        h = smax(h, spur(X, Z), 0.05)
        h = smax(h, back(X, Z), 0.04)
        # the forest's canopy: a fine roughness where it is not bare rock
        forest = 1 - smoothstep(0.38, 0.48, core(X, Z))
        return h + 0.0045 * fbm(X * 50, Z * 50, 3, 9) * forest

    def matfn(X, Z, Hh, slope, ao):
        # bare granite where it is steep (the cliffs), forest where it holds (the west slope,
        # the crest, the foot), the line between ragged; a few trees in the summit's cracks
        rock = smoothstep(2.0, 3.4, slope) + 0.5 * fbm(X * 24, Hh * 16, 3, 21)
        rock *= smoothstep(0.08, 0.2, Hh)
        rock = np.clip(rock, 0, 1)
        col = ground_colour(X, Z, Hh, slope, ao, rock, 3)
        return col, MAT_GROUND

    def fog(x, y, z):
        return 0.16 + 0.5 * smoothstep(0.32, 0.0, y) + 0.12 * smoothstep(-0.1, -0.4, z)

    xs = steps((-1.62, -0.45, 0.042), (-0.45, 0.24, 0.012), (0.24, 1.04, 0.042))
    zs = steps((-0.52, -0.1, 0.06), (-0.1, 0.05, 0.025), (0.05, 0.2, 0.017), (0.2, 0.4, 0.04))
    return heightfield(hfn, matfn, xs, zs, fog), TOP


def sugarloaf():
    """Sugarloaf and Morro da Urca in units of H, from their silhouette as seen across the bay:
    the tall bare granite loaf (sheer both sides, the round top leaning a little inland,
    trees on its crown and its foot) beside the lower, wooded, flat-topped Urca; foot at y 0."""
    TOP = 0.36
    xs = [-0.72, -0.62, -0.53, -0.44, -0.36, -0.29, -0.235, -0.2, -0.17, -0.15, -0.13, -0.11, -0.085, -0.055,
          -0.02, 0.02, 0.06, 0.09, 0.115, 0.13, 0.142, 0.16, 0.19]
    hs = [0.0, 0.05, 0.11, 0.152, 0.17, 0.168, 0.135, 0.095, 0.07, 0.085, 0.17, 0.245, 0.3, 0.338,
          TOP, 0.357, 0.335, 0.295, 0.23, 0.16, 0.09, 0.03, 0.0]

    def hfn(X, Z):
        Hc = np.interp(X, xs, hs, left=0, right=0)
        loaf = smoothstep(-0.17, -0.13, X)
        wf = (0.15 * (1 - loaf) + 0.13 * loaf) * (1 + 0.15 * fbm(X * 40, np.zeros_like(X), 2, 33) * loaf)
        wb = 0.15 * (1 - loaf) + 0.15 * loaf
        w = np.where(Z > 0, wf, wb)
        t = np.abs(Z) / w
        p = 2.0 * (1 - loaf) + 2.6 * loaf
        q = 0.85 * (1 - loaf) + 0.5 * loaf
        h = Hc * np.clip(1 - t ** p, 0, 1) ** q
        return h + 0.003 * fbm(X * 50, Z * 50, 3, 31) * (1 - loaf * 0.7)

    def matfn(X, Z, Hh, slope, ao):
        loaf = smoothstep(-0.17, -0.13, X)
        rock = loaf * smoothstep(1.6, 2.6, slope) * smoothstep(0.03, 0.07, Hh) * (1 - smoothstep(0.325, 0.35, Hh))
        rock += 0.25 * fbm(X * 25, Hh * 25, 2, 41) * loaf
        col = ground_colour(X, Z, Hh, slope, ao, np.clip(rock, 0, 1), 17)
        return col, MAT_GROUND

    def fog(x, y, z):
        return 0.24 + 0.46 * smoothstep(0.22, 0.0, y)

    part = heightfield(hfn, matfn, steps((-0.72, -0.18, 0.026), (-0.18, 0.2, 0.013)), steps((-0.22, 0.0, 0.04), (0.0, 0.18, 0.018)), fog)
    return part, TOP


def cable(p0, p1, sag, width, n=16):
    """A cable as a ribbon facing the viewer (z), sagging between p0 and p1."""
    P, T = [], []
    for i in range(n + 1):
        t = i / n
        x = p0[0] + (p1[0] - p0[0]) * t
        y = p0[1] + (p1[1] - p0[1]) * t - sag * 4 * t * (1 - t)
        z = p0[2] + (p1[2] - p0[2]) * t
        P += [(x, y + width / 2, z), (x, y - width / 2, z)]
    for i in range(n):
        a = 2 * i
        T += [(a, a + 1, a + 3), (a, a + 3, a + 2)]
    N = np.tile([0.0, 0.0, 1.0], (len(P), 1))
    return Part(P, T, (0.16, 0.16, 0.17), MAT_CABLE, N, fog=0.45)


# --- the statue --------------------------------------------------------------------------------

SOAPSTONE = np.array([0.84, 0.815, 0.75])
PEDESTAL = np.array([0.56, 0.52, 0.46])  # darker, weathered stone (the reference photo's block)
PEDESTAL_TOP = np.array([0.82, 0.80, 0.75])
TERRACE = np.array([0.80, 0.78, 0.72])
PAVING = np.array([0.56, 0.54, 0.50])


def loft(rings, albedo, mat, centre_fn, wrap=True):
    P = np.concatenate(rings)
    part = Part(P, grid_tris(len(rings), len(rings[0]), wrap_u=wrap), albedo, mat)
    return orient_out(part, centre_fn).smooth()


def section(th, a, b, ex=2 / 2.6):
    """A rounded-rectangle section (superellipse): theta 0 = +x, pi/2 = front (+z)."""
    c, s = np.cos(th), np.sin(th)
    return a * np.sign(c) * np.abs(c) ** ex, b * np.sign(s) * np.abs(s) ** ex


def statue_parts():
    """Christ the Redeemer in metres, feet at the origin, facing +z (x to the viewer's right).
    As the statue: a long tunic falling in deep straight pleats like a fluted column, flaring a
    little at the feet; over it the wide mantle from the shoulders to ~8 m above the feet, its
    hem slanting across, soft folds down it; the arms straight out at 24 m, the mantle's
    sleeves hanging under them from the wrists to the body's sides at ~18 m; open hands, palms
    to the front, thumbs up; the head bowed a little, the hair framing the face to the
    shoulders."""
    parts = []
    axis = lambda m: np.stack([np.zeros(len(m)), m[:, 1], np.zeros(len(m))], -1)
    # angles: dense over the front half (seen), sparse behind
    th = np.concatenate([np.linspace(-0.25, np.pi + 0.25, 46), np.linspace(np.pi + 0.25, 2 * np.pi - 0.25, 7)[1:-1]])
    front_w = np.clip(np.sin(th) + 0.25, 0, 1)

    # the tunic: (height, half width, half depth); pleats all round, deepest low down
    tun = [(0.0, 3.35, 2.15), (0.5, 3.22, 2.08), (1.6, 3.08, 1.98), (3.5, 3.0, 1.92), (6.0, 2.97, 1.9), (9.0, 2.95, 1.9), (11.0, 2.9, 1.85)]
    rings = []
    for (y, a, b) in tun:
        x, z = section(th, a, b)
        amp = np.interp(y, [0, 2, 9, 11], [0.36, 0.3, 0.26, 0.2])
        pl = (0.5 + 0.5 * np.cos((th - np.pi / 2) * 11.0)) ** 1.6 * amp * front_w
        r = 1 + pl / np.maximum(np.hypot(x, z), 0.3)
        rings.append(np.stack([x * r, np.full_like(x, y), z * r], -1))
    tunic = loft(rings, SOAPSTONE, MAT_STONE, axis)
    # the pleats' valleys darker (cavities)
    val = (0.5 + 0.5 * np.cos((np.arctan2(tunic.P[:, 2], tunic.P[:, 0]) - np.pi / 2) * 11.0)) ** 1.6
    tunic.A = tunic.A * (0.74 + 0.26 * val)[:, None]
    parts.append(tunic)

    # the mantle: hem slanting from 8.6 m (viewer's left) to 6.2 m (right); a rolled edge
    # tucked in under it; shoulders square under the cloth, then round to the neck
    def hem(x):
        return 7.4 - 1.2 * np.clip(x / 4.6, -1, 1)
    ys_up = [(12.0, 4.5, 2.3), (16.0, 4.6, 2.28), (19.0, 4.7, 2.25), (22.0, 4.78, 2.2), (23.4, 4.78, 2.1),
             (24.3, 4.5, 1.95), (24.9, 3.75, 1.72), (25.3, 2.6, 1.42), (25.6, 1.5, 1.1), (25.8, 0.9, 0.85)]
    rings = []
    x0, z0 = section(th, 1, 1)
    # the rolled hem (from inside, up to the edge)
    for (k, a, b) in ((-0.4, 2.9, 1.82), (0.0, 4.15, 2.22)):
        rings.append(np.stack([x0 * a, hem(x0 * a) + k, z0 * b], -1))
    for t in np.linspace(0.12, 1.0, 5):  # from the hem up to 12 m, the slant easing out
        a, b = 4.18 + 0.32 * t, 2.25 + 0.05 * t
        xx, zz = x0 * a, z0 * b
        yy = hem(xx) + (12.0 - hem(xx)) * t
        rings.append(np.stack([xx, yy, zz], -1))
    for (y, a, b) in ys_up:
        rings.append(np.stack([x0 * a, np.full_like(x0, y), z0 * b], -1))
    # soft folds down the mantle, a little slanted, deeper towards the hem
    for i, R_ in enumerate(rings):
        y = R_[:, 1]
        amp = np.interp(y, [6, 9, 15, 21, 24], [0.48, 0.42, 0.32, 0.2, 0.0])
        ph = (th - np.pi / 2) * 5.0 + y * 0.14
        f = (0.5 + 0.5 * np.cos(ph)) ** 2 * amp * front_w
        rr = np.hypot(R_[:, 0], R_[:, 2])
        k = 1 + f / np.maximum(rr, 0.3)
        rings[i] = np.stack([R_[:, 0] * k, y, R_[:, 2] * k], -1)
    mantle = loft(rings, SOAPSTONE, MAT_STONE, axis)
    ang = np.arctan2(mantle.P[:, 2], mantle.P[:, 0])
    val = (0.5 + 0.5 * np.cos((ang - np.pi / 2) * 5.0 + mantle.P[:, 1] * 0.14)) ** 2
    mantle.A = mantle.A * (0.8 + 0.2 * val)[:, None]
    # (the rolled edge's underside in shade)
    under = mantle.P[:, 1] < hem(mantle.P[:, 0]) + 0.05
    mantle.A[under] *= 0.8
    parts.append(mantle)

    # The arms, straight out from the shoulders to the wrists (the mantle's sleeves over them)
    for side in (-1, 1):
        xs = np.array([3.4, 4.6, 6.0, 7.5, 9.0, 10.2, 10.85])
        ry = np.array([1.25, 1.2, 1.12, 1.03, 0.94, 0.86, 0.76])
        rz = np.array([1.15, 1.1, 1.02, 0.95, 0.88, 0.8, 0.7])
        yc = np.interp(xs, [3, 11], [23.95, 23.85])
        th2 = np.linspace(0, 2 * np.pi, 12, endpoint=False)
        rings = [np.stack([np.full_like(th2, side * x), yy + a * np.sin(th2), 0.1 + b * np.cos(th2)], -1)
                 for x, a, b, yy in zip(xs, ry, rz, yc)]
        parts.append(loft(rings, SOAPSTONE, MAT_STONE, lambda m: np.stack([m[:, 0], np.full(len(m), 23.9), np.full(len(m), 0.1)], -1)))
        # the sleeve hanging under the arm: a lens of cloth from the arm's underside down to an
        # edge running from the wrist to the body's side at ~18 m
        ns, nphi = 7, 10
        S = np.linspace(0.0, 1.0, ns)
        phi = np.linspace(0, 2 * np.pi, nphi, endpoint=False)
        rings = []
        for s_ in S:
            xt = 4.2 + (10.3 - 4.2) * s_
            ytop = np.interp(xt, xs, yc - ry * 0.45)
            ybot = 17.6 + (23.1 - 17.6) * s_ ** 0.9 - 0.4 * math.sin(math.pi * s_)
            t = (1 - np.cos(phi)) / 2  # 0 at the top, 1 at the lower edge
            fr = np.sin(phi)  # + front, - back
            yy = ytop + (ybot - ytop) * t
            xx = xt - 0.2 * t * (1 - s_)
            th_ = (0.5 + 0.35 * (1 - s_)) * (1 - t ** 3) + 0.08
            folds = 0.1 * t * np.sin(s_ * 7.0 + 0.5) * (fr > 0)
            rings.append(np.stack([side * xx, yy, 0.1 + th_ * fr + folds], -1))
        drape = loft(rings, SOAPSTONE * 0.97, MAT_STONE, lambda m: np.stack([m[:, 0], m[:, 1], np.full(len(m), 0.1)], -1))
        parts.append(drape)
        # the hand, open, the palm to the front and a little down, fingers together, out
        hx = np.array([10.7, 11.3, 12.2, 13.1, 13.8, 14.2])
        hh = np.array([0.66, 0.86, 0.84, 0.72, 0.52, 0.22])  # half height
        ht = np.array([0.46, 0.4, 0.33, 0.27, 0.21, 0.12])  # half thickness
        th3 = np.linspace(0, 2 * np.pi, 10, endpoint=False)
        rings = [np.stack([np.full_like(th3, side * x), 23.8 + a_ * np.sin(th3), 0.15 + b_ * np.cos(th3)], -1) for x, a_, b_ in zip(hx, hh, ht)]
        c0 = np.array([0, 23.8, 0.15])
        rings = [(r_ - c0) @ rot_x(0.3).T + c0 for r_ in rings]
        parts.append(loft(rings, SOAPSTONE, MAT_STONE, lambda m: np.stack([m[:, 0], np.full(len(m), 23.8), np.full(len(m), 0.15)], -1)))
        parts.append(ellipsoid((side * 11.25, 24.75, 0.4), (0.62, 0.32, 0.26), SOAPSTONE, MAT_STONE, 8, 5, rot_z(side * 0.75)))  # thumb

    # The head, bowed a little, the hair framing the face down to the shoulders, the beard.
    bow = rot_x(0.22)
    parts.append(ellipsoid((0, 26.2, -0.1), (0.85, 0.9, 0.8), SOAPSTONE, MAT_STONE, 10, 6))  # neck
    parts.append(ellipsoid((0, 27.7, -0.35), (1.52, 2.15, 1.3), SOAPSTONE * 0.92, MAT_STONE, 12, 8, bow))  # the hair, to the shoulders
    parts.append(ellipsoid((0, 28.0, 0.2), (1.08, 1.72, 1.28), SOAPSTONE, MAT_STONE, 12, 8, bow))  # the face, bearded
    return parts


def pedestal_parts():
    """The Art Deco pedestal (8 m, the chapel inside): a stepped base, the block with ribs down
    every face, a cornice, a plinth; under it the summit's terraces with their parapets.
    Metres, the statue's feet at y 0."""
    parts = [
        box((0, -7.6, 0), (5.6, 0.4, 5.6), PEDESTAL_TOP * 0.9, MAT_BUILT),
        box((0, -4.15, 0), (4.75, 3.05, 4.75), PEDESTAL, MAT_BUILT),
        box((0, -0.8, 0), (5.15, 0.3, 5.15), PEDESTAL_TOP, MAT_BUILT),
        box((0, -0.25, 0), (4.2, 0.25, 4.2), PEDESTAL_TOP, MAT_BUILT),
    ]
    for k in range(5):  # ribs down each face (the fluted Art Deco block)
        o = -3.6 + k * 1.8
        for (cx, cz, hx, hz) in ((o, 4.9, 0.28, 0.15), (o, -4.9, 0.28, 0.15), (4.9, o, 0.15, 0.28), (-4.9, o, 0.15, 0.28)):
            parts.append(box((cx, -4.15, cz), (hx, 2.9, hz), PEDESTAL * 1.12, MAT_BUILT, cell=6))
    # the summit's terraces (white parapets): the upper one round the pedestal, the viewing
    # deck a step under it, set into the rock
    parts.append(box((0, -8.7, 0.3), (7.4, 0.7, 6.6), TERRACE, MAT_BUILT))
    parts.append(box((0.3, -10.5, 1.0), (9.6, 1.1, 8.4), TERRACE * 0.96, MAT_BUILT))
    return parts


# --- assembling, hidden surfaces, order --------------------------------------------------------

def merge(parts):
    P, N, A, M, F, T = [], [], [], [], [], []
    base = 0
    for p in parts:
        if p.N is None:
            p.smooth()
        P.append(p.P)
        N.append(p.N)
        A.append(p.A)
        M.append(p.M)
        F.append(p.F)
        T.append(p.T + base)
        base += len(p.P)
    return Part(np.concatenate(P), np.concatenate(T), np.concatenate(A), np.concatenate(M), np.concatenate(N), np.concatenate(F))


def bake(part, elev, res=2000):
    """Looks at the model from +z, `elev` radians from below (orthographic). Returns 2D corners
    (x, y in H), normals (view frame, unturned by elev), albedo, material, haze and the triangles
    that show, in a painter's order (far first)."""
    up = np.array([0.0, math.cos(elev), math.sin(elev)])
    fwd = np.array([0.0, math.sin(elev), -math.cos(elev)])
    P = part.P
    u = P[:, 0]
    v = P @ up
    d = P @ fwd
    T = part.T
    # faces turned away are dropped (by the triangle's own winding)
    a, b, c = P[T[:, 0]], P[T[:, 1]], P[T[:, 2]]
    fn = np.cross(b - a, c - a)
    front = fn @ (-fwd) > 0
    # 2D area (degenerate ones go)
    ua, va = u[T[:, 0]], v[T[:, 0]]
    ub, vb = u[T[:, 1]], v[T[:, 1]]
    uc, vc = u[T[:, 2]], v[T[:, 2]]
    area2 = (ub - ua) * (vc - va) - (uc - ua) * (vb - va)
    keep = front & (np.abs(area2) > 1e-10)
    T = T[keep]
    # id buffer: the front-most triangle at every sample (pass 1), then for every triangle
    # that shows, the ones in front of it somewhere (pass 2: it is drawn before them)
    umin, vmax = u.min(), v.max()
    px = (u - umin) * res
    py = (vmax - v) * res
    W = int(math.ceil((u.max() - umin) * res)) + 2
    Hh = int(math.ceil((vmax - v.min()) * res)) + 2
    zbuf = np.full((Hh, W), np.inf, np.float32)
    idb = np.full((Hh, W), -1, np.int64)

    def frags(ti):
        i, j, k = T[ti]
        x0, x1 = int(math.floor(min(px[i], px[j], px[k]))), int(math.ceil(max(px[i], px[j], px[k])))
        y0, y1 = int(math.floor(min(py[i], py[j], py[k]))), int(math.ceil(max(py[i], py[j], py[k])))
        x0, y0 = max(x0, 0), max(y0, 0)
        x1, y1 = min(x1, W - 1), min(y1, Hh - 1)
        if x1 < x0 or y1 < y0:
            return None
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        ax_, ay_, bx_, by_, cx_, cy_ = px[i], py[i], px[j], py[j], px[k], py[k]
        den = (by_ - cy_) * (ax_ - cx_) + (cx_ - bx_) * (ay_ - cy_)
        if abs(den) < 1e-12:
            return None
        l0 = ((by_ - cy_) * (gx - cx_) + (cx_ - bx_) * (gy - cy_)) / den
        l1 = ((cy_ - ay_) * (gx - cx_) + (ax_ - cx_) * (gy - cy_)) / den
        l2 = 1 - l0 - l1
        inside = (l0 >= -1e-9) & (l1 >= -1e-9) & (l2 >= -1e-9)
        if not inside.any():
            return None
        dd = (l0 * d[i] + l1 * d[j] + l2 * d[k])[inside]
        return gy[inside].astype(np.int64), gx[inside].astype(np.int64), dd

    for ti in range(len(T)):
        f = frags(ti)
        if f is None:
            continue
        yy, xx, dd = f
        better = dd < zbuf[yy, xx]
        zbuf[yy[better], xx[better]] = dd[better]
        idb[yy[better], xx[better]] = ti
    visible = np.zeros(len(T), bool)
    ids = np.unique(idb)
    visible[ids[ids >= 0]] = True
    ea_l, eb_l = [], []
    for ti in np.nonzero(visible)[0]:
        yy, xx, dd = frags(ti)
        own = idb[yy, xx]
        m = (own != ti) & (own >= 0) & (dd > zbuf[yy, xx] + 1e-5)
        if m.any():
            o = np.unique(own[m])
            ea_l.append(np.full(len(o), ti, np.int64))
            eb_l.append(o)
    ea = np.concatenate(ea_l) if ea_l else np.zeros(0, np.int64)
    eb = np.concatenate(eb_l) if eb_l else np.zeros(0, np.int64)
    # topological sort, far first among the ready ones; a cycle broken at its farthest triangle
    cen = (d[T[:, 0]] + d[T[:, 1]] + d[T[:, 2]]) / 3
    vis = np.nonzero(visible)[0]
    indeg = np.zeros(len(T), np.int64)
    np.add.at(indeg, eb, 1)
    succ = [[] for _ in range(len(T))]
    for x, y in zip(ea, eb):
        succ[x].append(y)
    heap = [(-cen[t], t) for t in vis if indeg[t] == 0]
    heapq.heapify(heap)
    done = np.zeros(len(T), bool)
    out = []
    remaining = set(vis.tolist())
    cycles = 0
    while remaining:
        if not heap:
            t = max(remaining, key=lambda q: cen[q])
            cycles += 1
            heapq.heappush(heap, (-cen[t], t))
            indeg[t] = 0
        _, t = heapq.heappop(heap)
        if done[t]:
            continue
        done[t] = True
        remaining.discard(t)
        out.append(t)
        for y in succ[t]:
            indeg[y] -= 1
            if indeg[y] == 0 and not done[y]:
                heapq.heappush(heap, (-cen[y], y))
    order_ = np.array(out, np.int64)
    # check: painted in this order, is every sample's last triangle the front-most one?
    paint = np.full((Hh, W), -1, np.int64)
    for ti in order_:
        yy, xx, dd = frags(ti)
        paint[yy, xx] = ti
    bad = (paint != idb) & (idb >= 0)
    wrong = int(np.sum(bad))
    if DEBUG_DIR:
        from PIL import Image
        im = np.zeros((Hh, W, 3), np.uint8)
        im[idb >= 0] = (60, 60, 60)
        im[bad] = (255, 40, 40)
        DEBUG_DIR.mkdir(parents=True, exist_ok=True)
        Image.fromarray(im).save(DEBUG_DIR / f"order_{len(T)}.png")
    T = T[order_]
    used = np.unique(T)
    remap = np.full(len(P), -1, np.int64)
    remap[used] = np.arange(len(used))
    res_ = dict(xy=np.stack([u[used], v[used]], -1), n=part.N[used], a=part.A[used], m=part.M[used], f=part.F[used],
                t=remap[T], cycles=cycles, dropped=int(len(part.T) - len(T)), wrong=wrong, res=res)
    return res_


# --- lighting (the game's lm_light in view3d.c, line for line, for the previews) --------------

def light(mesh, L, night, haze, horizon):
    n = mesh["n"]
    a = mesh["a"]
    m = mesh["m"]
    y = mesh["xy"][:, 1]
    L = normalize(np.asarray(L, float))
    out = np.zeros_like(a)
    for i in range(len(a)):
        out[i] = lm_light_py(a[i], n[i], m[i], mesh["f"][i], y[i], L, night, haze, horizon)
    return np.clip(out, 0, 1)


def lm_light_py(alb, n, mat, fog, y, L, night, haze, horizon):
    nx, ny, nz = n
    ndl = nx * L[0] + ny * L[1] + nz * L[2]
    if mat == MAT_CABLE:
        c = alb * (0.35 if night else 1.0)
    elif mat == MAT_LAMP and night:
        c = np.array([1.0, 0.82, 0.5])
    elif not night:
        # as the stage props are lit (lit(): 0.78 in shade up to 1.08 in the sun), with a
        # hemisphere for the form (sky blue from above, the ground's bounce from below), the
        # sun wrapped a little round organic shapes, and a rim where the sun grazes an edge
        wrap = 0.3 if mat == MAT_GROUND else 0.15
        dif = max(0.0, (ndl + wrap) / (1 + wrap))
        hemi = 0.5 + 0.5 * ny
        amb = np.array([0.62, 0.62, 0.56]) * (1 - hemi) + np.array([0.70, 0.76, 0.86]) * hemi
        sun = np.array([1.0, 0.95, 0.86])
        c = alb * (amb * (0.86 + 0.14 * max(0.0, nz)) + sun * dif * (0.42 if mat == MAT_GROUND else 0.6))
        rim = (1 - max(0.0, nz)) ** 3 * max(0.0, ndl + 0.25) * (0.3 if mat != MAT_GROUND else 0.1)
        c = c + rim * sun * (0.3 + 0.7 * max(0.0, -L[2]))
    else:
        if mat in (MAT_STONE, MAT_BUILT, MAT_LAMP):
            f1 = max(0.0, nx * -0.3 + ny * -0.25 + nz * 0.92)
            f2 = max(0.0, nx * 0.45 + ny * -0.2 + nz * 0.87)
            c = alb * (np.array([0.10, 0.12, 0.2]) + np.array([1.0, 0.97, 0.9]) * (0.62 * f1 + 0.5 * f2))
        else:
            moon = max(0.0, nx * 0.4 + ny * 0.8 + nz * 0.45)
            glow = max(0.0, 1 - y / 0.45) ** 2
            c = alb * np.array([0.10, 0.12, 0.2]) * (0.4 + 0.8 * moon) + np.array([0.14, 0.08, 0.2]) * glow * 0.5
    k = fog * (0.45 if night and mat in (MAT_STONE, MAT_BUILT, MAT_LAMP) else 1.0)
    fc = haze + (horizon - haze) * smooth01(0.05, 0.4, y)
    return c * (1 - k) + fc * k


def smooth01(e0, e1, x):
    t = min(1.0, max(0.0, (x - e0) / (e1 - e0)))
    return t * t * (3 - 2 * t)


# --- preview -----------------------------------------------------------------------------------

def render(meshes, Hpx, sky_top, sky_low, ridge_col, L, night, haze, horizon, ss=3, size=None):
    """meshes: [(mesh, x offset in H)]; a picture with the sky and a ridge, H pixels tall."""
    from PIL import Image
    W, Hh = size if size else (int(Hpx * 3.2), int(Hpx * 1.25))
    W *= ss
    Hh *= ss
    S = Hpx * ss
    img = np.zeros((Hh, W, 3))
    t = np.linspace(0, 1, Hh)[:, None]
    img[:] = (sky_top * (1 - t) + sky_low * t)[:, None, :]
    foot = Hh - int(0.12 * S)
    cx = W * (0.62 if size is None else 0.5)
    # a far ridge
    xs = np.arange(W)
    rh = (0.12 + 0.03 * np.sin(xs / S * 3.1) + 0.02 * np.sin(xs / S * 7.3)) * S
    for x in range(W):
        img[int(foot - rh[x]):, x] = ridge_col
    for mesh, off in meshes:
        col = light(mesh, L, night, haze, horizon)
        X = cx + (mesh["xy"][:, 0] + off) * S
        Y = foot - mesh["xy"][:, 1] * S
        for (i, j, k) in mesh["t"]:
            x0, x1 = int(max(0, math.floor(min(X[i], X[j], X[k])))), int(min(W - 1, math.ceil(max(X[i], X[j], X[k]))))
            y0, y1 = int(max(0, math.floor(min(Y[i], Y[j], Y[k])))), int(min(Hh - 1, math.ceil(max(Y[i], Y[j], Y[k]))))
            if x1 < x0 or y1 < y0:
                continue
            gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
            den = (Y[j] - Y[k]) * (X[i] - X[k]) + (X[k] - X[j]) * (Y[i] - Y[k])
            if abs(den) < 1e-12:
                continue
            l0 = ((Y[j] - Y[k]) * (gx - X[k]) + (X[k] - X[j]) * (gy - Y[k])) / den
            l1 = ((Y[k] - Y[i]) * (gx - X[k]) + (X[i] - X[k]) * (gy - Y[k])) / den
            l2 = 1 - l0 - l1
            ins = (l0 >= 0) & (l1 >= 0) & (l2 >= 0)
            if not ins.any():
                continue
            c = l0[..., None] * col[i] + l1[..., None] * col[j] + l2[..., None] * col[k]
            sub = img[y0:y1 + 1, x0:x1 + 1]
            sub[ins] = c[ins]
    # the near hills and the mist over the foot
    mist = foot - int(0.11 * S)
    for y in range(mist, Hh):
        a = (y - mist) / (Hh - mist) * 0.6
        img[y] = img[y] * (1 - a) + haze * a
    im = Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8))
    return im.resize((W // ss, Hh // ss), Image.LANCZOS)


# --- the landmarks -----------------------------------------------------------------------------

STATUE_M = 0.0092  # H per metre: the statue's 38 m are ~0.35 H (exaggerated ~7x against the peak, as on a postcard)
ELEV = math.radians(3.5)
YAW = math.radians(-18)  # its sunlit flank (the course's light comes from the right) towards the viewer


def build_cristo():
    terrain, top = corcovado()
    R = rot_y(YAW)
    feet = top + 11.0 * STATUE_M  # the lower deck set 0.6 m into the summit rock
    st = [p.transformed(R, STATUE_M, (0.0, feet, 0.0)) for p in statue_parts() + pedestal_parts()]
    for p in st:
        p.F = np.full(len(p.P), 0.09)
    return merge([terrain] + st)


def build_sugarloaf():
    part, top = sugarloaf()
    c = cable((-0.32, 0.178, 0.0), (-0.03, 0.366, 0.0), 0.016, 0.0032)
    return merge([part, c])


# --- lattices: members drawn as ribbons facing the viewer --------------------------------------

class Lines:
    """Thin members (lattice chords, braces, cables) in model units: each a segment with a
    width, a colour, a material and the normal of the face it lies on (for its light). Turned
    into ribbons that face the viewer once the model is placed (ribbons())."""

    def __init__(self):
        self.L = []

    def add(self, p0, p1, w, albedo, mat, n):
        self.L.append((np.asarray(p0, float), np.asarray(p1, float), w, np.asarray(albedo, float), mat, np.asarray(n, float)))

    def poly(self, pts, w, albedo, mat, n):
        for a, b in zip(pts[:-1], pts[1:]):
            self.add(a, b, w, albedo, mat, n)


def ribbons(lines, R=np.eye(3), s=1.0, t=(0, 0, 0), fog=0.2):
    """The members placed (rotated by R, scaled by s, moved by t) as flat quads facing the view
    (from +z, ELEV from below); members seen from the inside of their face take its normal
    turned to the viewer."""
    view = np.array([0.0, math.sin(ELEV), -math.cos(ELEV)])
    parts = []
    t = np.asarray(t, float)
    for (p0, p1, w, alb, mat, n) in lines.L:
        a, b = R @ p0 * s + t, R @ p1 * s + t
        d = b - a
        off = np.cross(d, view)
        ln = np.linalg.norm(off)
        if ln < 1e-12:
            continue
        off = off / ln * w * s / 2
        P = np.array([a + off, a - off, b - off, b + off])
        T = np.array([(0, 1, 2), (0, 2, 3)])
        if np.dot(np.cross(P[1] - P[0], P[2] - P[0]), -view) < 0:
            T = T[:, [0, 2, 1]]
        nn = R @ n
        if nn[2] < -0.05:  # (the inner side of a far face)
            nn = -nn
        parts.append(Part(P, T, alb, mat, np.tile(nn, (4, 1)), fog=fog))
    return parts


# --- the Eiffel Tower --------------------------------------------------------------------------

IRON = np.array([0.56, 0.43, 0.31])  # "Eiffel Tower brown", a bronze


def lattice_tower(lines, w_out, w_in, lev, w_s, slev, colour, chord=3.4, brace=1.5, schord=2.6, sbrace=1.3, edges=()):
    """A lattice tower's members: four piers (each four chords, X-bracing and ties on every
    face) from the foot up the levels lev, their inner and outer half-widths w_in(y), w_out(y)
    (meeting where w_in is 0), then a single braced shaft of half-width w_s(y) up slev.
    colour(y): a member's colour by its height (bands)."""
    def col(a, b):
        return colour(0.5 * (a[1] + b[1]))

    class Split:  # members cut where the colour bands change (edges), each piece its band's colour
        def add(self, p0, p1, w, c, mat, n):
            p0, p1 = np.asarray(p0, float), np.asarray(p1, float)
            ya, yb = sorted((p0[1], p1[1]))
            cuts = [e for e in edges if ya < e < yb]
            ts = [0.0] + sorted((e - p0[1]) / (p1[1] - p0[1]) for e in cuts) + [1.0]
            for t0, t1 in zip(ts[:-1], ts[1:]):
                a, b = p0 + (p1 - p0) * t0, p0 + (p1 - p0) * t1
                lines.add(a, b, w, col(a, b), mat, n)
    out = Split()
    for sx in (-1, 1):
        for sz in (-1, 1):
            def corner(y, ix, iz):
                ax = w_in(y) if ix == 0 else w_out(y)
                az = w_in(y) if iz == 0 else w_out(y)
                return np.array([sx * ax, y, sz * az])
            for ix in (0, 1):
                for iz in (0, 1):
                    pts = [corner(y, ix, iz) for y in lev]
                    for p0, p1 in zip(pts[:-1], pts[1:]):
                        out.add(p0, p1, chord, col(p0, p1), MAT_BUILT, (sx * (1 if ix else -1) * 0.7, 0.2, sz * (1 if iz else -1) * 0.7))
            for face in range(4):
                for y0, y1 in zip(lev[:-1], lev[1:]):
                    if face < 2:
                        ix = face
                        nrm = (sx * (1 if ix else -1), 0.15, 0)
                        a0, a1, b0, b1 = corner(y0, ix, 0), corner(y0, ix, 1), corner(y1, ix, 0), corner(y1, ix, 1)
                    else:
                        iz = face - 2
                        nrm = (0, 0.15, sz * (1 if iz else -1))
                        a0, a1, b0, b1 = corner(y0, 0, iz), corner(y0, 1, iz), corner(y1, 0, iz), corner(y1, 1, iz)
                    if np.linalg.norm(a1 - a0) < 0.5:
                        continue
                    out.add(a0, b1, brace, col(a0, b1), MAT_BUILT, nrm)
                    out.add(a1, b0, brace, col(a1, b0), MAT_BUILT, nrm)
                    out.add(b0, b1, brace * 0.85, col(b0, b1), MAT_BUILT, nrm)
    for sx in (-1, 1):
        for sz in (-1, 1):
            pts = [np.array([sx * w_s(y), y, sz * w_s(y)]) for y in slev]
            for p0, p1 in zip(pts[:-1], pts[1:]):
                out.add(p0, p1, schord, col(p0, p1), MAT_BUILT, (sx * 0.7, 0.1, sz * 0.7))
    for face in range(4):
        nrm = [(0, 0.1, 1), (0, 0.1, -1), (1, 0.1, 0), (-1, 0.1, 0)][face]
        for y0, y1 in zip(slev[:-1], slev[1:]):
            def pt(y, u):
                w = w_s(y)
                return [np.array([u * w, y, w]), np.array([u * w, y, -w]), np.array([w, y, u * w]), np.array([-w, y, u * w])][face]
            for p0, p1 in ((pt(y0, -1), pt(y1, 1)), (pt(y0, 1), pt(y1, -1)), (pt(y1, -1), pt(y1, 1))):
                out.add(p0, p1, sbrace, col(p0, p1), MAT_BUILT, nrm)


def face_arch(lines, w_face, y0, y1, half, colour, width, n=15):
    """An arch in each of the four faces of a tower: from -half(y) to +half(y), springing at y0,
    its crown at y1; the face at w_face(y) from the middle."""
    for f in range(4):
        pts = []
        for k in range(n):
            t = k / (n - 1)
            y = y0 + (y1 - y0) * math.sin(math.pi * t) ** 0.8
            x = -half(y) * math.cos(math.pi * t)
            zf = w_face(y)
            pts.append([np.array([x, y, zf]), np.array([x, y, -zf]), np.array([zf, y, x]), np.array([-zf, y, x])][f])
        lines.poly(pts, width, colour, MAT_BUILT, [(0, 0.2, 1), (0, 0.2, -1), (1, 0.2, 0), (-1, 0.2, 0)][f])


def eiffel_parts():
    """The Eiffel Tower in metres, its foot's middle at the origin: four curved lattice piers
    leaning together to the second platform, the great arches between them under the first,
    the single tapering shaft to the top, the three platforms, the campanile and the mast.
    300 m (324 with the mast), 125 m across the foot, the piers 26 m at the foot."""
    lines = Lines()

    def w_out(y):
        return 62.5 * math.exp(-y / 108.0)

    def w_in(y):
        return 36.5 * max(0.0, 1 - y / 115.0) ** 0.9

    def w_s(y):
        return 21.5 - 15.0 * ((y - 115) / 161.0) ** 0.75

    lattice_tower(lines, w_out, w_in, [0, 12, 26, 41, 57, 71, 86, 101, 115], w_s, [115, 132, 151, 172, 196, 222, 249, 276],
                  lambda y: IRON)
    face_arch(lines, lambda y: 0.5 * (w_in(y) + w_out(y)), 6, 50, w_in, IRON * 1.05, 2.6)
    solid = [box((0, 60.0, 0), (38.5, 3.6, 38.5), IRON * 1.08, MAT_BUILT, cell=8),
             box((0, 117.5, 0), (23.5, 2.6, 23.5), IRON * 1.08, MAT_BUILT, cell=8),
             box((0, 278.0, 0), (8.5, 2.2, 8.5), IRON * 1.1, MAT_BUILT, cell=6),
             box((0, 287.0, 0), (5.0, 7.0, 5.0), IRON * 1.0, MAT_BUILT, cell=6),
             box((0, 296.5, 0), (3.2, 2.5, 3.2), IRON * 1.1, MAT_BUILT, cell=6)]
    lines.add((0, 299, 0), (0, 324, 0), 1.6, IRON * 0.9, MAT_BUILT, (0, 0, 1))
    return solid, lines


EIFFEL_M = 1.0 / 324  # H per metre: the mast's tip at H, as the flat one


def build_eiffel():
    solid, lines = eiffel_parts()
    R = rot_y(math.radians(EIFFEL_YAW))
    parts = [p.transformed(R, EIFFEL_M) for p in solid]
    for p in parts:
        p.F = np.full(len(p.P), 0.2)
    return merge(parts + ribbons(lines, R, EIFFEL_M, fog=0.2))


EIFFEL_YAW = -20  # its sunlit face (Paris: the light from the right) towards the viewer


# --- Tokyo Tower ----------------------------------------------------------------------------

TT_ORANGE = np.array([0.86, 0.33, 0.13])  # international orange
TT_WHITE = np.array([0.90, 0.90, 0.88])


def tokyo_parts():
    """Tokyo Tower in metres (333 m): an Eiffel-like lattice with straighter legs, in the
    aviation bands of international orange and white; the two-storey main deck at 145-160 m
    where the legs have become one shaft, the special deck at ~250 m, the mast; the four-storey
    FootTown building between the legs at the foot."""
    lines = Lines()

    white = ((60, 70), (116, 125), (176, 186), (220, 229), (266, 275), (296, 305), (318, 326))

    def band(y):  # orange, with narrow white bands (as painted for aviation)
        return TT_WHITE if any(a <= y < b for a, b in white) else TT_ORANGE

    def w_out(y):
        return 40.0 - 26.0 * (y / 125.0) ** 0.8

    def w_in(y):
        return 24.0 * max(0.0, 1 - y / 100.0) ** 1.1

    def w_s(y):
        return 14.0 - 8.5 * ((y - 125) / 125.0) ** 0.9

    lattice_tower(lines, w_out, w_in, [0, 16, 33, 50, 68, 86, 105, 125], w_s, [125, 145, 160, 180, 202, 226, 250],
                  band, chord=3.2, brace=1.5, schord=2.6, sbrace=1.3, edges=[e for ab in white for e in ab])
    solid = [
        box((0, 9.0, 0), (19.0, 9.0, 19.0), np.array([0.78, 0.78, 0.76]), MAT_BUILT, cell=8),  # FootTown
        box((0, 149.5, 0), (17.5, 4.5, 17.5), TT_WHITE * 0.95, MAT_BUILT, cell=6),  # the main deck, two storeys
        box((0, 157.5, 0), (15.5, 3.5, 15.5), TT_WHITE * 0.92, MAT_BUILT, cell=6),
        box((0, 252.0, 0), (7.5, 4.0, 7.5), TT_WHITE * 0.95, MAT_BUILT, cell=5),  # the special deck
    ]
    # the mast, banded
    ys = [256, 270, 285, 296, 318, 333]
    for y0, y1 in zip(ys[:-1], ys[1:]):
        w = 4.2 - 3.0 * (y0 - 256) / 77.0
        solid.append(box((0, 0.5 * (y0 + y1), 0), (w * 0.5, 0.5 * (y1 - y0), w * 0.5), band(0.5 * (y0 + y1)), MAT_BUILT, cell=6))
    return solid, lines


TOKYO_M = 1.0 / 336
TOKYO_YAW = -20


def build_tokyo():
    solid, lines = tokyo_parts()
    R = rot_y(math.radians(TOKYO_YAW))
    parts = [p.transformed(R, TOKYO_M) for p in solid]
    for p in parts:
        p.F = np.full(len(p.P), 0.2)
    return merge(parts + ribbons(lines, R, TOKYO_M, fog=0.2))


# --- the Leaning Tower of Pisa ------------------------------------------------------------------

MARBLE = np.array([0.90, 0.88, 0.82])
MARBLE_SHADE = np.array([0.58, 0.56, 0.52])  # the walls behind the loggias, in their shade


def cyl(r_fn, y0, y1, albedo, mat, n=32, rows=2, front_only=True, scallop=None):
    """A cylinder wall (smooth normals), radius r_fn(y), from y0 to y1; n around (denser over
    the front half when front_only); scallop(theta) lowers the bottom edge (arches)."""
    if front_only:
        th = np.concatenate([np.linspace(-0.3, np.pi + 0.3, n), np.linspace(np.pi + 0.3, 2 * np.pi - 0.3, 7)[1:-1]])
    else:
        th = np.linspace(0, 2 * np.pi, n, endpoint=False)
    rings = []
    for k in range(rows):
        y = y0 + (y1 - y0) * k / (rows - 1)
        r = r_fn(y)
        yy = np.full_like(th, y)
        if k == 0 and scallop is not None:
            yy = yy + scallop(th)
        rings.append(np.stack([r * np.cos(th), yy, r * np.sin(th)], -1))
    P = np.concatenate(rings)
    part = Part(P, grid_tris(rows, len(th), wrap_u=True), albedo, mat)
    part = orient_out(part, lambda m: np.stack([np.zeros(len(m)), m[:, 1], np.zeros(len(m))], -1))
    # normals: radial (a cylinder), smooth
    N = np.stack([np.cos(np.arctan2(P[:, 2], P[:, 0])), np.zeros(len(P)), np.sin(np.arctan2(P[:, 2], P[:, 0]))], -1)
    part.N = N
    return part


def pisa_parts():
    """The Leaning Tower in metres (as in 1992: ~5.5 deg): the ground storey's wall with its
    blind arcade (15 half-columns), six storeys of open loggias (30 columns each, the arches
    over them, a cornice under each), the dark wall behind them, the narrower bell chamber with
    its arched openings; white marble. 15.5 m across, 56 m on its low side."""
    parts = []
    lines = Lines()
    R0 = 7.74
    parts.append(cyl(lambda y: R0, 0.0, 11.0, MARBLE, MAT_BUILT, rows=3))
    for k in range(15):  # the blind arcade: half-columns and their arches, a hair proud of the wall
        a = 2 * np.pi * (k + 0.5) / 15
        if np.sin(a) < -0.2:
            continue
        p0 = np.array([(R0 + 0.25) * math.cos(a), 0.4, (R0 + 0.25) * math.sin(a)])
        p1 = np.array([(R0 + 0.25) * math.cos(a), 8.6, (R0 + 0.25) * math.sin(a)])
        lines.add(p0, p1, 0.75, MARBLE * 1.02, MAT_BUILT, (math.cos(a), 0, math.sin(a)))
    floors = [11.0 + 6.83 * k for k in range(7)]
    parts.append(cyl(lambda y: 6.6, 11.0, floors[-1], MARBLE_SHADE, MAT_BUILT, rows=4))
    for f0, f1 in zip(floors[:-1], floors[1:]):
        parts.append(cyl(lambda y: R0 + 0.35, f0, f0 + 0.55, MARBLE * 0.97, MAT_BUILT))  # the cornice (the gallery's floor)
        # the arcade's band over the columns, its lower edge cut into 30 arches
        parts.append(cyl(lambda y: R0, f1 - 1.7, f1, MARBLE, MAT_BUILT, n=92,
                         scallop=lambda th: 1.0 * np.abs(np.sin(15 * th)) ** 0.5))
        for k in range(30):  # the columns
            a = 2 * np.pi * k / 30
            if np.sin(a) < -0.25:
                continue
            p0 = np.array([R0 * math.cos(a), f0 + 0.55, R0 * math.sin(a)])
            p1 = np.array([R0 * math.cos(a), f1 - 0.75, R0 * math.sin(a)])
            lines.add(p0, p1, 0.55, MARBLE * 1.02, MAT_BUILT, (math.cos(a), 0, math.sin(a)))
    top = floors[-1]
    parts.append(cyl(lambda y: R0 + 0.35, top, top + 0.6, MARBLE * 0.97, MAT_BUILT))  # the top terrace's parapet
    parts.append(cyl(lambda y: 4.9, top + 0.6, top + 5.4, MARBLE * 0.96, MAT_BUILT, rows=3))  # the bell chamber
    for k in range(12):  # its arched openings, dark (those facing the viewer)
        a = 2 * np.pi * (k + 0.5) / 12
        if np.sin(a) < 0.45:
            continue
        p0 = np.array([5.0 * math.cos(a), top + 1.5, 5.0 * math.sin(a)])
        p1 = np.array([5.0 * math.cos(a), top + 4.3, 5.0 * math.sin(a)])
        lines.add(p0, p1, 1.1 * math.sin(a), MARBLE_SHADE * 0.55, MAT_BUILT, (math.cos(a), 0, math.sin(a)))
    parts.append(cyl(lambda y: 5.25, top + 5.4, top + 6.0, MARBLE * 0.97, MAT_BUILT))
    return parts, lines


PISA_M = 1.0 / 60.0
PISA_LEAN = math.radians(5.5)


def build_pisa():
    parts, lines = pisa_parts()
    R = rot_z(-PISA_LEAN)  # leaning to the right
    out = [p.transformed(R, PISA_M, (-0.03, 0, 0)) for p in parts]
    for p in out:
        p.F = np.full(len(p.P), 0.18)
    return merge(out + ribbons(lines, R, PISA_M, (-0.03, 0, 0), fog=0.18))


def emit(name, mesh, f):
    xy = np.round(mesh["xy"] * 8192).astype(int)
    assert np.abs(xy).max() < 32767
    n = np.round(normalize(mesh["n"]) * 127).astype(int)
    a = np.clip(np.round(mesh["a"] * 255), 0, 255).astype(int)
    fog = np.clip(np.round(mesh["f"] * 255), 0, 255).astype(int)
    f.write(f"static const LmVert k{name}V[{len(xy)}] = {{\n")
    line = ""
    for i in range(len(xy)):
        rgb = a[i, 0] << 16 | a[i, 1] << 8 | a[i, 2]
        item = f"{{{xy[i, 0]},{xy[i, 1]},{n[i, 0]},{n[i, 1]},{n[i, 2]},{int(mesh['m'][i])},{fog[i]},0x{rgb:06X}}},"
        if len(line) + len(item) > 110:
            f.write("  " + line + "\n")
            line = ""
        line += item
    if line:
        f.write("  " + line + "\n")
    f.write("};\n")
    t = mesh["t"]
    assert t.max() < 65536
    f.write(f"static const uint16_t k{name}T[{len(t) * 3}] = {{\n")
    flat = t.reshape(-1)
    for i in range(0, len(flat), 24):
        f.write("  " + ",".join(str(int(q)) for q in flat[i:i + 24]) + ",\n")
    f.write("};\n")
    x0, x1, y1 = mesh["xy"][:, 0].min(), mesh["xy"][:, 0].max(), mesh["xy"][:, 1].max()
    f.write(f"static const LmMesh k{name} = {{k{name}V, {len(xy)}, k{name}T, {len(t)}, {x0:.4f}f, {x1:.4f}f, {y1:.4f}f}};\n\n")


# The landmarks baked: the C name, the builder, the light of the course it stands on in its view
# frame (TG_LM_INFO=1 prints it; previews only) and the yaw the model was turned by (notes).
LANDMARKS = [
    ("LmCristo", build_cristo, (0.538, 0.823, -0.183)),  # Rio (course 4)
    ("LmSugarloaf", build_sugarloaf, (0.538, 0.823, -0.183)),
    ("LmEiffel", build_eiffel, (0.408, 0.823, -0.395)),  # Paris (course 20)
    ("LmTokyoTower", build_tokyo, (0.292, 0.823, -0.487)),  # Tokyo (course 8)
    ("LmPisa", build_pisa, (0.452, 0.823, -0.344)),  # Pisa (course 24)
]

HEADER = """// Generated by tools/make_landmarks.py. Do not edit (run it again).
// Skyline landmarks modelled in 3D and baked for the one side they are seen from: per corner its
// place (x right, y up, in H / 8192 from the foot), normal (view frame: x right, y up, z to the
// viewer, x127), material (0 ground, 1 stone, 2 built, 3 cable, 4 lamp), distance haze (0-255)
// and colour (albedo); triangles in a painter's order (far first, hidden ones dropped).
typedef struct { int16_t x, y; int8_t nx, ny, nz; uint8_t mat, fog; uint32_t albedo; } LmVert;
typedef struct { const LmVert* v; int nv; const uint16_t* t; int nt; float x0, x1, y1; } LmMesh;  // bounds in H

"""

SKY_TOP, SKY_LOW = np.array([10, 145, 202]) / 255, np.array([53, 192, 247]) / 255
HAZE, HORIZON, RIDGE = np.array([53, 118, 100]) / 255, np.array([120, 200, 235]) / 255, np.array([62, 113, 100]) / 255
N_TOP, N_LOW, N_RIDGE = np.array([3, 6, 15]) / 255, np.array([58, 38, 80]) / 255, np.array([26, 24, 48]) / 255
N_HAZE, N_HORIZON = np.array([22, 28, 46]) / 255, np.array([40, 30, 70]) / 255


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out", nargs="?", default=str(OUT))
    ap.add_argument("--preview", default="", help="a folder for pictures of each landmark (day, night, close)")
    ap.add_argument("--only", default="", help="bake and preview just these (comma separated C names); no header")
    ap.add_argument("--res", type=int, default=2000)
    ap.add_argument("--debug", default="")
    a = ap.parse_args()
    global DEBUG_DIR
    DEBUG_DIR = Path(a.debug) if a.debug else None
    only = [q for q in a.only.split(",") if q]
    baked = []
    for name, build, L in LANDMARKS:
        if only and name not in only:
            continue
        m = bake(build(), ELEV, a.res)
        bym = np.bincount(m["m"][m["t"][:, 0]], minlength=5)
        print(f"{name}: {len(m['xy'])} corners, {len(m['t'])} triangles (ground {bym[0]}, stone {bym[1]}, built {bym[2]}, "
              f"cable {bym[3]}, lamp {bym[4]}; dropped {m['dropped']}, cycles {m['cycles']}, samples out of order "
              f"{m['wrong']} at {m['res']}/H)", file=sys.stderr)
        baked.append((name, m, L))
    if a.preview:
        d = Path(a.preview)
        d.mkdir(parents=True, exist_ok=True)
        for name, m, L in baked:
            if name == "LmCristo":
                sug = [q for q in baked if q[0] == "LmSugarloaf"]
                group = [(m, 0.0)] + ([(sug[0][1], -1.75)] if sug else [])
            else:
                group = [(m, 0.0)]
            render(group, 420, SKY_TOP, SKY_LOW, RIDGE, L, False, HAZE, HORIZON).save(d / f"{name}_day_1440.png")
            render(group, 315, SKY_TOP, SKY_LOW, RIDGE, L, False, HAZE, HORIZON).save(d / f"{name}_day_1080.png")
            render(group, 420, N_TOP, N_LOW, N_RIDGE, L, True, N_HAZE, N_HORIZON).save(d / f"{name}_night_1440.png")
            render([(m, 0.0)], 1200, SKY_TOP, SKY_LOW, RIDGE, L, False, HAZE, HORIZON, ss=2, size=(1800, 1500)).save(d / f"{name}_close.png")
    if only:
        return 0
    with open(a.out, "w") as f:
        f.write(HEADER)
        for name, m, L in baked:
            emit(name, m, f)
    return 0


if __name__ == "__main__":
    sys.exit(main())
