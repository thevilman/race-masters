# The cars' models, level 3 (see docs/CARS.md), Blender stage: geometry, UVs and bakes.
#
#   blender -b --factory-startup --python tools/cars/build_cars.py -- \
#       --vehicles ".../inspiration/6-specialist-remake/vehicles.py" --out WORK [--cars rosso,...]
#
# The design is the specialist's (vehicles.py: body station drawings, cabins, axles, each car's
# own lamps, intakes and vents), built as it builds them. On top of it, here:
# - the cabin is rebuilt as an open greenhouse: the same roof, screens and side glass, but the
#   glass is a pane set in a black frame in a real opening (vehicles.py lays the glass over a
#   closed painted skin), lined inside, with an interior (floor, dash, two bucket seats, a
#   steering wheel, a roll bar, a bulkhead) to be seen through it;
# - one lean wheel per car with its spoke design: a tyre with sidewall and tread profile, a
#   rim lip, barrel, spokes, hub and cap, a brake disc with its hat, and the caliper apart
#   (it steers with the wheel but doesn't spin);
# - a rear diffuser with strakes where the car has none.
# The game mesh (level 0) is that model without its fine detail (shut lines, seams, gaskets,
# louvres, slats, grille bars, bolts, plate markings, drilled holes): those are baked into the
# texture from the full model (high to low, Cycles "selected to active"), as game cars are made.
# Then one UV atlas per car (smart projection, islands scaled by what they are worth: wheel up,
# interior and underbody down, packed with margins), bakes into it: albedo and paint mask and
# material ids from the full model, ambient occlusion, position and normal on the game mesh;
# levels 1 and 2 by edge collapse (UVs kept). Written to WORK/<car>/ for paint_cars.py, which
# paints the atlas (liveries, decals, tyres, discs, lamps...) and writes the .tgcar.
import argparse
import importlib.util
import json
import math
import os
import re
import sys
import time

import bmesh
import bpy
import numpy as np
from mathutils import Vector

ARGS = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
ap = argparse.ArgumentParser()
ap.add_argument('--vehicles', required=True, help="the specialist's vehicles.py")
ap.add_argument('--out', required=True)
ap.add_argument('--cars', default='rosso,glacier,violet,azure,amber')
ap.add_argument('--tex', type=int, default=1024)
ap.add_argument('--ao-samples', type=int, default=96)
ap.add_argument('--threads', type=int, default=4)
ap.add_argument('--lod', default='0.5,0.22', help='levels 1 and 2 as a share of level 0 (body)')
ap.add_argument('--save-blend', action='store_true')
A = ap.parse_args(ARGS)

spec = importlib.util.spec_from_file_location('tg_vehicles', A.vehicles)
veh = importlib.util.module_from_spec(spec)
spec.loader.exec_module(veh)

KIND = {'rosso': 0, 'glacier': 1, 'violet': 2, 'azure': 3, 'amber': 2}
PAINT = {'amber': (0.85, 0.32, 0.03)}  # the rivals' fifth car: the violet body repainted (linear)
TAU = math.tau
lerp = veh._lerp

# Material roles: vehicles.py's palette keys and the new ones. Export class (tgcar v3):
# 0 trim (chrome or satin by colour), 1 paint, 2 glass, 3 tail lamp, 4 head lamp, 5 rubber,
# 7 interior (satin), 8 chrome.
ROLES = ['paint', 'rubber', 'black', 'trim', 'lower', 'glass', 'rim', 'rimdark', 'rotor', 'caliper', 'lamp',
         'red', 'amber', 'plate', 'interior', 'fabric', 'rollbar', 'carbon', 'chrome']
CLASS = {'paint': 1, 'glass': 2, 'red': 3, 'lamp': 4, 'rubber': 5, 'interior': 7, 'fabric': 7, 'rim': 8, 'chrome': 8}
EXTRA = {  # linear colour, metallic, roughness
    'interior': ((.022, .023, .026), 0, .7), 'fabric': ((.03, .03, .034), 0, .9),
    'rollbar': ((.20, .21, .23), .5, .4), 'carbon': ((.018, .019, .022), .2, .3), 'chrome': ((.6, .62, .64), 1, .15)}

# The full model's detail that goes into the texture only (the game mesh leaves it out).
BAKE_ONLY = re.compile(r'shut line|gasket|seam|wiper|tread channel|rolled lip|drilled|wheel bolt|stamped plate|'
                       r'grille slat|cooling louver|cooling grille|deck louver|grille upright|radiator grille|fine upright|'
                       r'intake divider|vent blade|centre badge|handle blade|handle recess|cooling louver', re.I)


# --- The greenhouse: vehicles.py's cabin with real openings, lined, with an interior ------------

def cabin_v3(b):
    kind = b.kind
    cowl, rf, rr, rear, bw, rw, h, base = veh.CABINS[kind]
    crown = (.014, .009, .026, .024)[kind]
    prof = ((cowl, bw, base), (rf, rw, h - .025), (rf + .10, rw * .99, h), (rr - .08, rw * .99, h),
            (rr, rw, h - .02), (rear, bw * .96, base + .015))

    def section(y):
        for a, d in zip(prof, prof[1:]):
            if y <= d[0]:
                t = max(0, min(1, (y - a[0]) / (d[0] - a[0])))
                return lerp(a[1], d[1], t), lerp(a[2], d[2], t)
        return prof[-1][1:]

    def topz(x, y):
        w, top = section(y)
        u = max(-1, min(1, x / w))
        return top + crown * (1 - u * u)

    FR, INSET = .032, .007
    # The screens as vehicles.py places them: trapezoids (y, half width) on the roof surface.
    y1, y2, w1, w2 = cowl + .05, rf - .018, bw - .055, rw - .045
    y3, y4, w3, w4 = rr + .035, rear - .06, rw - .05, bw * .96 - .075

    def g(y):  # the glass's half width along the roof (the frame's inner edge)
        if y <= y2:
            return lerp(w1, w2, (y - y1) / (y2 - y1))
        if y < y3:
            return lerp(w2, w3, (y - y2) / (y3 - y2))
        return lerp(w3, w4, (y - y3) / (y4 - y3))

    ys = sorted({round(y, 6) for y in [p[0] for p in prof] + [y1 - FR, y1, y2, y2 + FR, y3 - FR, y3, y4, y4 + FR] +
                 [lerp(y1, y2, i / 4) for i in range(1, 4)] + [lerp(y3, y4, i / 4) for i in range(1, 4)] +
                 [lerp(rf + .1, rr - .08, i / 4) for i in range(1, 4)]})
    verts, faces, mats, glassv = [], [], [], set()
    NC = 11
    for y in ys:
        W = section(y)[0]
        gg = min(g(y), W - FR - .006)
        cols = [-W, -(gg + FR), -gg, -gg * .66, -gg * .33, 0, gg * .33, gg * .66, gg, gg + FR, W]
        in_glass = (y1 - 1e-6 <= y <= y2 + 1e-6) or (y3 - 1e-6 <= y <= y4 + 1e-6)
        for i, x in enumerate(cols):
            z = topz(x, y)
            if in_glass and 2 <= i <= 8:
                z -= INSET
                glassv.add(len(verts))
            verts.append((x, y, z))
    for j in range(len(ys) - 1):
        yc = (ys[j] + ys[j + 1]) / 2
        ws = y1 <= yc <= y2 or y3 <= yc <= y4
        band = y1 - FR <= yc <= y2 + FR or y3 - FR <= yc <= y4 + FR
        for i in range(NC - 1):
            p = j * NC + i
            faces.append((p, p + 1, p + 1 + NC, p + NC))
            gcol, bcol = 2 <= i <= 7, 1 <= i <= 8
            mats.append('glass' if ws and gcol else 'black' if band and bcol else 'paint')
    faces = orient(verts, faces, (0, 0, 1))
    roof = mesh_multi(b, 'Greenhouse roof, screens and frames', verts, faces, mats)
    liner(b, 'Roof lining', verts, faces, mats, (0, 0, -.012))

    def bottom(y):
        body_w = veh._station(kind, y)[0]
        return min(base - .018, veh._deck_height(kind, y, bw / body_w) - .012)

    def skin_x(y, z):
        w, top = section(y)
        t = (top - z) / max(.001, top - bottom(y))
        return lerp(w, bw, t)

    # The side window: vehicles.py's glass quad, a ring of frame round it, then the painted
    # pillars and rails out to the cabin's outline - loops with the same count, quads between.
    P0, P1, P2, P3 = (cowl + .105, base + .035), (rf + .045, h - .065), (rr - .045, h - .06), (rear - .15, base + .047)
    NB, NS = 18, 6

    def seg(a, d, n):  # n points from a, not d
        return [(lerp(a[0], d[0], i / n), lerp(a[1], d[1], i / n)) for i in range(n)]

    def poly(points, n):  # n points along a polyline from its first point, not its last
        L = [math.dist(a, d) for a, d in zip(points, points[1:])]
        tot, out = sum(L), []
        for i in range(n):
            s, k = tot * i / n, 0
            while k < len(L) - 1 and s > L[k]:
                s -= L[k]
                k += 1
            t = s / L[k] if L[k] else 0
            out.append((lerp(points[k][0], points[k + 1][0], t), lerp(points[k][1], points[k + 1][1], t)))
        return out

    L0 = seg(P0, P3, NB) + seg(P3, P2, NS) + seg(P2, P1, NB) + seg(P1, P0, NS)
    # (the frame: the glass's outline pushed out by FR in the side's plane, mitred)
    quad = [P0, P3, P2, P1]

    def out_normal(a, d):
        ex, ez = d[0] - a[0], d[1] - a[1]
        n = math.hypot(ex, ez)
        return (ez / n, -ex / n)  # the quad runs P0 P3 P2 P1: clockwise in (y, z) - outward is to the right

    ens = [out_normal(quad[k], quad[(k + 1) % 4]) for k in range(4)]
    # (check the sense on the bottom edge: it must point down)
    if ens[0][1] > 0:
        ens = [(-a, -c) for a, c in ens]
    L1, counts = [], (NB, NS, NB, NS)
    for k in range(4):
        n0 = ens[k]
        for i in range(counts[k]):
            p = seg(quad[k], quad[(k + 1) % 4], counts[k])[i]
            if i == 0:
                m = ens[k - 1]
                sx, sz = n0[0] + m[0], n0[1] + m[1]
                d = 1 + n0[0] * m[0] + n0[1] * m[1]
                L1.append((p[0] + sx / d * FR, p[1] + sz / d * FR))
            else:
                L1.append((p[0] + n0[0] * FR, p[1] + n0[1] * FR))
    top = lambda y: section(y)[1]
    Lout = (poly([(cowl, bottom(cowl))] + [(lerp(cowl, rear, i / 24), bottom(lerp(cowl, rear, i / 24))) for i in range(1, 25)], NB) +
            poly([(rear, bottom(rear)), (rear, top(rear)), (rr, top(rr))], NS) +
            poly([(rr, top(rr)), (rr - .08, top(rr - .08)), (rf + .1, top(rf + .1)), (rf, top(rf))], NB) +
            poly([(rf, top(rf)), (cowl, top(cowl)), (cowl, bottom(cowl))], NS))
    loops = [L0, L1] + [[(lerp(a[0], d[0], k / 3), lerp(a[1], d[1], k / 3)) for a, d in zip(L1, Lout)] for k in (1, 2)] + [Lout]
    nu = NS
    for side in (-1, 1):
        verts, faces, mats = [], [], []
        for li, lp in enumerate(loops):
            for (y, z) in lp:
                x = skin_x(y, z) - (INSET if li == 0 else 0)
                verts.append((side * x, y, z))
        n = len(L0)
        for li in range(len(loops) - 1):
            for i in range(n):
                a, c = li * n + i, li * n + (i + 1) % n
                f = (a, c, c + n, a + n)
                faces.append(f if side == 1 else f[::-1])
                mats.append('black' if li == 0 else 'paint')
        # the glass: vehicles.py's bilinear patch, its edge the first loop
        base_i = len(verts)
        grid = {}
        for jv in range(NB + 1):
            v = jv / NB
            for iu in range(nu + 1):
                u = iu / nu
                a = (lerp(P0[0], P1[0], u), lerp(P0[1], P1[1], u))
                e = (lerp(P3[0], P2[0], u), lerp(P3[1], P2[1], u))
                y, z = lerp(a[0], e[0], v), lerp(a[1], e[1], v)
                # boundary points: the loop's own vertex (shared with the frame)
                idx = None
                if iu == 0:
                    idx = jv if jv < NB else NB
                elif jv == NB:
                    idx = NB + iu
                elif iu == nu:
                    idx = NB + NS + (NB - jv)
                elif jv == 0:
                    idx = (2 * NB + NS + (nu - iu)) % n
                if idx is not None:
                    grid[(iu, jv)] = idx
                else:
                    grid[(iu, jv)] = len(verts)
                    verts.append((side * (skin_x(y, z) - INSET), y, z))
        for jv in range(NB):
            for iu in range(nu):
                f = (grid[(iu, jv)], grid[(iu, jv + 1)], grid[(iu + 1, jv + 1)], grid[(iu + 1, jv)])
                faces.append(f if side == 1 else f[::-1])
                mats.append('glass')
        faces = orient(verts, faces, (side, 0, 0))
        mesh_multi(b, f'{side:+} greenhouse side, window and pillars', verts, faces, mats)
        liner(b, f'{side:+} side lining', verts, faces, mats, (-side * .012, 0, 0))
        # The B pillar: vehicles.py's quarterlight divider over the glass.
        divy = rr - (.14 if kind != 1 else .08)
        boty = divy + .07
        veh._patch(b, "Quarterlight separator",
                   [(side * (rw + .023), divy, h - .06), (side * (rw + .023), divy + .035, h - .06),
                    (side * (bw - .023), boty + .035, base + .044), (side * (bw - .023), boty, base + .044)], "trim", 2, 6,
                   project=lambda p, s=side: (s * (skin_x(p.y, p.z) + .004), p.y, p.z), reverse=side == 1)
    # Sealing strips between the roof surface and the decks (as vehicles.py).
    cross = (-1, -.88, -.5, 0, .5, .88, 1)
    for front, (y, w, tp) in ((True, prof[0]), (False, prof[-1])):
        vv, ff = [], []
        body_width = veh._station(kind, y)[0]
        for u in cross:
            x = u * w
            vv.extend(((x, y, tp + crown * (1 - u * u)), (x, y, veh._deck_height(kind, y, x / body_width) - .008)))
        for i in range(len(cross) - 1):
            p = 2 * i
            face = (p, p + 2, p + 3, p + 1)
            ff.append(face[::-1] if front else face)
        b.mesh("Sealed cowl-to-bonnet transition" if front else "Sealed rear sail foot", vv, ff, "paint")
    for s in (-1, 1):
        b.line("Wiper blade", [(s * .12, cowl + .09, base + .06), (s * .57, cowl + .18, base + .11)], "trim", .008)
        y = cowl + .15
        stalk = b.box("Mirror foot", (s * .84, y, base + .055), (.13, .08, .045), "carbon", .012)
        stalk.rotation_euler.z = s * .18
        b.box("Body-colour wing mirror", (s * .93, y - .025, base + .09), (.155, .17, .085), "paint", .032)
        b.box("Mirror dark inset", (s * .935, y + .064, base + .093), (.105, .008, .045), "glass", .012)
    interior(b, topz, skin_x)


def orient(verts, faces, out):
    """Each face turned so its normal has a positive part along out (x, y, z)."""
    res = []
    for f in faces:
        a, c, d = (Vector(verts[i]) for i in f[:3])
        n = (c - a).cross(d - a)
        if len(f) > 3 and n.length < 1e-12:
            n = (d - a).cross(Vector(verts[f[3]]) - a)
        res.append(f if n.dot(Vector(out)) >= 0 else f[::-1])
    return res


def mesh_multi(b, name, verts, faces, mats, smooth=True, out=None):
    """One object, a material per face (roles); faces turned towards out if given."""
    if out:
        faces = orient(verts, faces, out)
    data = bpy.data.meshes.new(name)
    data.from_pydata(verts, [], faces)
    data.update()
    roles = sorted(set(mats), key=mats.index)
    for r in roles:
        data.materials.append(b.m[r])
    for p, m in zip(data.polygons, mats):
        p.material_index = roles.index(m)
        p.use_smooth = smooth
    obj = bpy.data.objects.new(name, data)
    b.col.objects.link(obj)
    return obj


def liner(b, name, verts, faces, mats, off):
    """The inside of a shell (the glass left out), turned in and set off: what is seen of it
    through the glass (without it the far pillars, seen from behind, were missing)."""
    keep = [(f, m) for f, m in zip(faces, mats) if m != 'glass']
    vv = [(x + off[0], y + off[1], z + off[2]) for x, y, z in verts]
    mesh_multi(b, name, vv, [f[::-1] for f, _ in keep], ['interior'] * len(keep))


def interior(b, topz, skin_x):
    kind = b.kind
    cowl, rf, rr, rear, bw, rw, h, base = veh.CABINS[kind]
    # The floor: over the deck inside the cabin (dark).
    ny, nx = 16, 8
    vv, ff = [], []
    for j in range(ny + 1):
        y = lerp(cowl + .01, rear - .01, j / ny)
        W = veh._station(kind, y)[0]
        for i in range(nx + 1):
            x = lerp(-(bw - .02), bw - .02, i / nx)
            vv.append((x, y, veh._deck_height(kind, y, x / W) + .006))
    for j in range(ny):
        for i in range(nx):
            p = j * (nx + 1) + i
            ff.append((p, p + 1, p + nx + 2, p + nx + 1))
    b.mesh('Interior floor', vv, ff, 'interior', True)
    deck = lambda x, y: veh._deck_height(kind, y, x / veh._station(kind, y)[0])
    z0 = max(deck(0, (rf + rr) / 2), deck(.35, (rf + rr) / 2))
    yb = (rf + rr) / 2 + .12  # the seat backs
    ws = lambda y: topz(0, y)  # the windscreen's height at y (the roof surface)
    # Steering wheel: as far forward as the screen leaves it room.
    R = .145
    yw = yb - .62
    while yw < yb - .3 and ws(yw) < z0 + .13 + R + .035:
        yw += .01
    zw = z0 + .13
    # Dash: from the cowl to just ahead of the wheel, under the screen.
    yd1 = yw - .07
    dash = [(-(bw - .05), cowl + .02, z0), ((bw - .05), cowl + .02, z0),
            ((bw - .05), yd1, z0), (-(bw - .05), yd1, z0),
            (-(bw - .05), cowl + .02, min(ws(cowl + .02) - .012, z0 + .03)), ((bw - .05), cowl + .02, min(ws(cowl + .02) - .012, z0 + .03)),
            ((bw - .05), yd1, min(ws(yd1) - .03, z0 + .11)), (-(bw - .05), yd1, min(ws(yd1) - .03, z0 + .11))]
    mid = sum((Vector(q) for q in dash), Vector()) / 8
    dff = [orient(dash, [f], sum((Vector(dash[k]) for k in f), Vector()) / 4 - mid)[0]
           for f in [(0, 1, 2, 3), (4, 7, 6, 5), (0, 4, 5, 1), (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 4, 0)]]
    b.mesh('Dashboard', dash, dff, 'interior', False, .012)
    xd = .34  # the driver on the left (vehicles.py +x: the game's left)
    b.box('Instrument binnacle', (xd, yd1 - .06, min(ws(yd1 - .06) - .04, z0 + .135)), (.30, .12, .05), 'interior', .015)
    bpy.ops.mesh.primitive_torus_add(major_radius=R, minor_radius=.016, major_segments=20, minor_segments=6,
                                     location=(xd, yw, zw), rotation=(math.radians(90 - 24), 0, 0))
    wheel = bpy.context.object
    wheel.name = 'Steering wheel rim'
    b.add(wheel, 'interior')
    for p in wheel.data.polygons:
        p.use_smooth = True
    for a in (0, 2.4, 3.9):  # three spokes
        sp = b.box('Steering wheel spoke', (xd + math.cos(a) * R * .5, yw + .01, zw + math.sin(a) * R * .5 * .9),
                   (.025 if a else R, .015, R if a else .025), 'interior', .004)
        sp.rotation_euler.x = math.radians(-24)
    # Bucket seats: cushion, back with its side wings, a headrest; leaning back.
    for x in (xd, -xd):
        b.box('Seat cushion', (x, yb - .21, z0 + .035), (.40, .40, .07), 'fabric', .025)
        back = b.box('Seat back', (x, yb, z0 + .15), (.42, .08, .27), 'fabric', .03)
        back.rotation_euler.x = math.radians(-14)
        for s in (-1, 1):
            wing = b.box('Seat side wing', (x + s * .19, yb - .06, z0 + .13), (.05, .20, .21), 'fabric', .02)
            wing.rotation_euler.x = math.radians(-14)
        hr = b.box('Seat headrest', (x, yb + .045, z0 + .30), (.22, .07, .08), 'fabric', .025)
        hr.rotation_euler.x = math.radians(-14)
    # Roll bar behind the seats, then a bulkhead to the engine.
    yr = yb + .13
    zt = min(topz(0, yr) - .05, z0 + .34)
    # (its legs inside the cabin's sides at every height: the sides lean in towards the roof)
    hoop = [z0, z0 + .1, z0 + .18, zt - .07, zt - .03]
    leg = [skin_x(yr, z) - .065 for z in hoop]
    pts = [(-x, yr, z) for x, z in zip(leg, hoop)] + [(-leg[-1] * .5, yr, zt), (leg[-1] * .5, yr, zt)] + \
          [(x, yr, z) for x, z in reversed(list(zip(leg, hoop)))]
    bar = b.line('Roll bar', pts, 'rollbar', .022)
    bar.data.bevel_resolution = 1
    bar['lp'] = True
    zb = z0 + .10  # (low: the seats' backs show through the rear screen)
    b.mesh('Rear bulkhead', [(-(bw - .03), yr + .06, z0), ((bw - .03), yr + .06, z0), ((bw - .03), yr + .06, zb), (-(bw - .03), yr + .06, zb)],
           [(0, 1, 2, 3)], 'interior')
    b.mesh('Rear bulkhead back', [(-(bw - .03), yr + .061, z0), ((bw - .03), yr + .061, z0), ((bw - .03), yr + .061, zb), (-(bw - .03), yr + .061, zb)],
           [(3, 2, 1, 0)], 'interior')


# --- The wheel ------------------------------------------------------------------------------

N_WHEEL = 32
WHEELS = []  # (side, axle) as built


def ring_mesh(b, name, cx, side, axle, profile, mat, core, n=N_WHEEL, inward=False, smooth=True):
    """A surface of revolution round the axle (x): profile (outward offset, radius) points. Each
    face turned away from the part's core circle (outward offset, radius) - or towards it."""
    verts, faces = [], []
    for xo, r in profile:
        for i in range(n):
            t = TAU * i / n
            verts.append((cx + side * xo, axle + r * math.cos(t), .32 + r * math.sin(t)))
    for j in range(len(profile) - 1):
        for i in range(n):
            p, q = j * n + i, j * n + (i + 1) % n
            f = (p, q, q + n, p + n)
            c = sum((Vector(verts[k]) for k in f), Vector()) / 4
            t = math.atan2(c.z - .32, c.y - axle)
            o = Vector((cx + side * core[0], axle + core[1] * math.cos(t), .32 + core[1] * math.sin(t)))
            out = (o - c) if inward else (c - o)
            faces.append(orient(verts, [f], out)[0])
    return b.mesh(name, verts, faces, mat, smooth)


def disc_mesh(b, name, x, axle, r, side_out, mat, n=N_WHEEL):
    verts = [(x, axle, .32)] + [(x, axle + r * math.cos(TAU * i / n), .32 + r * math.sin(TAU * i / n)) for i in range(n)]
    faces = [(0, 1 + i, 1 + (i + 1) % n) for i in range(n)]
    if side_out < 0:
        faces = [f[::-1] for f in faces]
    return b.mesh(name, verts, faces, mat, False)


def wheel_v3(b, side, axle):
    WHEELS.append((side, axle))
    k = b.kind
    cx = side * .858
    tag = f'wheel {side:+d} {axle:+.2f}'
    parts = []
    # Tyre: bead, sidewall bulge, shoulder, tread (the tread's grooves and the lettering are texture).
    tyre = [(-.117, .217), (-.119, .262), (-.102, .305), (-.068, .32), (.068, .32), (.102, .305), (.119, .262), (.117, .217)]
    parts.append(ring_mesh(b, tag + ' tyre', cx, side, axle, tyre, 'rubber', (0, .268)))
    # Rim: the barrel (seen through the spokes), its back, the lip.
    parts.append(ring_mesh(b, tag + ' rim barrel', cx, side, axle, [(.098, .206), (-.105, .206)], 'rimdark', (0, .3)))
    parts.append(disc_mesh(b, tag + ' wheel back', cx + side * -.105, axle, .207, side, 'rimdark'))
    lip = [(.098, .206), (.112, .198), (.123, .208), (.119, .222), (.108, .225)]
    parts.append(ring_mesh(b, tag + ' rim lip', cx, side, axle, lip, 'rim', (.108, .212)))
    # Brake disc and its hat.
    rx = .074
    disc = [(rx - .007, .085), (rx - .007, .169), (rx + .007, .169), (rx + .007, .085)]
    parts.append(ring_mesh(b, tag + ' brake disc', cx, side, axle, disc, 'rotor', (rx, .127)))
    parts.append(ring_mesh(b, tag + ' disc hat', cx, side, axle, [(rx + .007, .085), (rx + .016, .072)], 'rotor', (rx, .05), n=18))
    parts.append(disc_mesh(b, tag + ' disc hat face', cx + side * (rx + .016), axle, .072, side, 'rotor', n=18))
    # Spokes (vehicles.py's design per car), a little thicker at the root.
    count, start, end, width, sweep = ((5, .055, .2, .040, .17), (8, .057, .2, .025, 0), (5, .047, .2, .030, -.10),
                                       (6, .055, .2, .023, .16))[k]
    face = .119
    for i in range(count):
        a = TAU * i / count + .2
        cs = []
        for radial, ang, half, depth in ((start, a, width * .65, -.026), (end, a + sweep, width * .5, -.018)):
            cy, cz = axle + radial * math.cos(ang), .32 + radial * math.sin(ang)
            cs.append(((cy - math.sin(ang) * half, cz + math.cos(ang) * half), (cy + math.sin(ang) * half, cz - math.cos(ang) * half), depth))
        (a0, a1, d0), (b0, b1, d1) = cs
        vv = []
        for (pt, d) in ((a0, d0), (a1, d0), (b1, d1), (b0, d1)):
            vv.append((cx + side * (face + d), pt[0], pt[1]))
        for (pt, d) in ((a0, d0), (a1, d0), (b1, d1), (b0, d1)):
            vv.append((cx + side * (face + .004 - (.004 if pt in (b0, b1) else 0)), pt[0], pt[1]))
        ff = [(0, 1, 2, 3), (4, 7, 6, 5), (0, 4, 5, 1), (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 4, 0)]
        mid = sum((Vector(q) for q in vv), Vector()) / 8  # (a convex block: faces away from its middle)
        ff = [orient(vv, [f], sum((Vector(vv[k]) for k in f), Vector()) / 4 - mid)[0] for f in ff]
        parts.append(b.mesh(tag + ' spoke', vv, ff, 'rim', False))
    parts.append(ring_mesh(b, tag + ' hub', cx, side, axle, [(.098, .058), (.121, .056), (.124, .045)], 'rim', (.1, .03), n=18))
    parts.append(disc_mesh(b, tag + ' centre cap', cx + side * .124, axle, .045, side, 'rimdark', n=18))
    # The caliper: fixed (steers, doesn't spin).
    cal = b.box(tag + ' caliper', (cx + side * (rx + .019), axle + .128, .345), (.04, .05, .105), 'caliper', .012)
    cal.modifiers[0].segments = 2
    for o in parts:
        o['wheel'] = tag
    cal['wheel'] = tag
    cal['fixed'] = True


def diffuser(b):
    kind = b.kind
    if kind == 3:
        return
    rear = veh.DRAWINGS[kind][-1][0]
    y0, y1 = rear - .5, rear - .03
    vv = [(-.66, y0, .205), (.66, y0, .205), (.66, y1, .30), (-.66, y1, .30)]
    b.mesh('Rear diffuser', vv, orient(vv, [(0, 1, 2, 3)], (0, 0, -1)), 'carbon', False)
    for x in (-.45, -.15, .15, .45):
        b.mesh('Rear diffuser strake', [(x, y0 + .08, .205), (x, y1, .30), (x, y1, .22), (x, y0 + .2, .19)], [(0, 1, 2, 3)], 'carbon', False)
        b.mesh('Rear diffuser strake', [(x + .001, y0 + .08, .205), (x + .001, y1, .30), (x + .001, y1, .22), (x + .001, y0 + .2, .19)],
               [(3, 2, 1, 0)], 'carbon', False)


# --- Building one car -----------------------------------------------------------------------

def build_car(name):
    kind = KIND[name]
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o, do_unlink=True)
    for c in list(bpy.data.collections):
        bpy.data.collections.remove(c)
    WHEELS.clear()
    veh._cabin = cabin_v3
    veh._wheel = wheel_v3
    b = veh._Builder(kind)
    pre = veh.NAMES[kind] + ' / '
    for r, (col, met, rough) in EXTRA.items():
        b.m[r] = veh._material(pre + r, col, met, rough)
    if name in PAINT:
        b.m['paint'].node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value = (*PAINT[name], 1)
        b.m['paint'].diffuse_color = (*PAINT[name], 1)
    for r, m in b.m.items():
        m['role'] = r
    veh._body(b)
    cabin_v3(b)
    (veh._red, veh._white, veh._purple, veh._blue)[kind](b)
    veh._finishing(b)
    diffuser(b)
    for o in b.col.all_objects:  # polished exhaust tips
        if o.name.startswith('Dark exhaust outlet'):
            o.data.materials[0] = b.m['chrome']
    return b


def role_of(mat):
    return mat.get('role', 'trim') if mat else 'trim'


def gather(objs, name, lp):
    """Objects evaluated (modifiers applied, curves meshed) and joined: one mesh, a material
    slot per role, a face attribute 'grp' (0 body, 1 interior, 2 wheel, 3 fixed wheel part)."""
    dg = bpy.context.evaluated_depsgraph_get()
    bm = bmesh.new()
    grp_layer = bm.faces.layers.int.new('grp')
    for o in objs:
        ev = o.evaluated_get(dg)
        try:
            me = bpy.data.meshes.new_from_object(ev)
        except RuntimeError:
            continue
        me.transform(o.matrix_world)
        slots = [role_of(m) for m in me.materials] or ['trim']
        tmp = bmesh.new()
        tmp.from_mesh(me)
        if lp:  # flat patches lose their grid (a projected vent was 200 quads), curves keep theirs
            bmesh.ops.dissolve_limit(tmp, angle_limit=math.radians(1.2), verts=tmp.verts, edges=tmp.edges,
                                     delimit={'MATERIAL', 'SHARP'})
        g = 2 if o.get('wheel') and not o.get('fixed') else 3 if o.get('fixed') else \
            1 if any(r in slots for r in ('interior', 'fabric', 'rollbar')) and 'paint' not in slots else 0
        tmp.to_mesh(me)
        tmp.free()
        base = len(bm.faces)
        bm.from_mesh(me)
        bm.faces.ensure_lookup_table()
        for f in bm.faces[base:]:
            f[grp_layer] = g
            f.material_index = ROLES.index(slots[min(f.material_index, len(slots) - 1)])
        bpy.data.meshes.remove(me)
    bmesh.ops.triangulate(bm, faces=bm.faces, quad_method='BEAUTY', ngon_method='BEAUTY')
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    obj = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(obj)
    for r in ROLES:
        me.materials.append(None)
    return obj


def objects_hp(col):
    return [o for o in col.all_objects if o.type in ('MESH', 'CURVE')]


def objects_lp(col, lf):
    out = []
    for o in col.all_objects:
        if o.type not in ('MESH', 'CURVE'):
            continue
        if o.type == 'CURVE' and o.name.startswith('Painted rolled arch edge'):
            # the arches' rolled lip: kept, four-sided, every other point (half a degree off the arc)
            sp = o.data.splines[0]
            pts = [tuple(p.co) for p in sp.points][::2]
            o.data.splines.remove(sp)
            sp = o.data.splines.new('POLY')
            sp.points.add(len(pts) - 1)
            for q, co in zip(sp.points, pts):
                q.co = co
            o.data.bevel_resolution = 0
            o['lp'] = True
        if o.type == 'CURVE' and not o.get('lp'):
            continue
        if BAKE_ONLY.search(o.name):
            continue
        if o.get('wheel') and o['wheel'] != lf:
            continue
        for m in o.modifiers:  # bevels: two segments on panels, one on small parts
            if m.type == 'BEVEL':
                m.segments = 2 if m.width >= .015 else 1
                m.harden_normals = False
        # small round parts (lamps, bezels, exhausts: 48-sided cylinders) lighter
        if o.type == 'MESH' and not o.get('wheel') and max(o.dimensions) < .2 and len(o.data.vertices) > 60:
            for m in list(o.modifiers):
                if m.type == 'BEVEL' and m.width < .006:
                    o.modifiers.remove(m)
            d = o.modifiers.new('lighter', 'DECIMATE')
            d.ratio = max(.25, 40 / len(o.data.vertices))
        out.append(o)
    return out


# --- UVs -------------------------------------------------------------------------------------

GROUP_SCALE = {0: 1.0, 1: .42, 2: 1.7, 3: 1.2}
ROLE_SCALE = {'plate': 3.0, 'lamp': 1.6, 'red': 1.6, 'amber': 1.3}  # (lettering, lamp patterns)


def unwrap(obj):
    bpy.context.view_layer.objects.active = obj
    for o in bpy.context.scene.objects:
        o.select_set(o == obj)
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(48), island_margin=0.0, area_weight=0.0, correct_aspect=True,
                             scale_to_bounds=False)
    bpy.ops.uv.average_islands_scale()
    bpy.ops.object.mode_set(mode='OBJECT')
    # Islands scaled by what they are worth (from below and inside little shows).
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    uvl = bm.loops.layers.uv.active
    grp = bm.faces.layers.int.get('grp')
    bm.faces.ensure_lookup_table()
    parent = list(range(len(bm.faces)))

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a
    for e in bm.edges:
        if len(e.link_faces) != 2:
            continue
        f1, f2 = e.link_faces
        ok = True
        for v in e.verts:
            u1 = [l[uvl].uv for l in f1.loops if l.vert == v][0]
            u2 = [l[uvl].uv for l in f2.loops if l.vert == v][0]
            ok &= (u1 - u2).length < 1e-6
        if ok:
            parent[find(f1.index)] = find(f2.index)
    islands = {}
    for f in bm.faces:
        islands.setdefault(find(f.index), []).append(f)
    for fs in islands.values():
        area = {}
        down = 0
        for f in fs:
            a = f.calc_area()
            area[f[grp]] = area.get(f[grp], 0) + a
            if f.normal.z < -.6 and f.calc_center_median().z < .4:
                down += a
        g = max(area, key=area.get)
        s = GROUP_SCALE[g]
        if g == 0 and down > .5 * sum(area.values()):
            s = .3  # the underside
        roles = {}
        for f in fs:
            r = ROLES[f.material_index]
            roles[r] = roles.get(r, 0) + f.calc_area()
        s *= ROLE_SCALE.get(max(roles, key=roles.get), 1)
        if s == 1:
            continue
        cu = sum((l[uvl].uv for f in fs for l in f.loops), Vector((0, 0))) / sum(len(f.loops) for f in fs)
        for f in fs:
            for l in f.loops:
                l[uvl].uv = cu + (l[uvl].uv - cu) * s
    bm.to_mesh(obj.data)
    bm.free()
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.select_all(action='SELECT')  # (else it packs nothing)
    t = time.time()
    bpy.ops.uv.pack_islands(udim_source='CLOSEST_UDIM', rotate=True, rotate_method='AXIS_ALIGNED', scale=True,
                            merge_overlap=False, margin_method='FRACTION', margin=0.007, shape_method='CONVEX')
    bpy.ops.object.mode_set(mode='OBJECT')
    print('  packed in %.1f s' % (time.time() - t))


# --- Bakes ----------------------------------------------------------------------------------

def setup_cycles(samples):
    sc = bpy.context.scene
    sc.render.engine = 'CYCLES'
    sc.cycles.device = 'CPU'
    sc.cycles.samples = samples
    sc.cycles.use_denoising = False
    sc.render.threads_mode = 'FIXED'
    sc.render.threads = A.threads
    if sc.world is None:
        sc.world = bpy.data.worlds.new('World')
    sc.world.use_nodes = False
    sc.world.color = (1, 1, 1)
    sc.world.light_settings.distance = .3


def bake_materials(lp, img):
    """The game mesh's materials: each with the bake image (the target) and an emission whose
    colour can be wired to the position or the normal."""
    mats = []
    for r in ROLES:
        m = bpy.data.materials.new('bake ' + r)
        m.use_nodes = True
        nt = m.node_tree
        nt.nodes.clear()
        out = nt.nodes.new('ShaderNodeOutputMaterial')
        em = nt.nodes.new('ShaderNodeEmission')
        geo = nt.nodes.new('ShaderNodeNewGeometry')
        vm = nt.nodes.new('ShaderNodeVectorMath')
        vm.operation = 'MULTIPLY_ADD'
        vm.name = 'map'
        nt.links.new(em.outputs[0], out.inputs[0])
        nt.links.new(vm.outputs[0], em.inputs[0])
        nt.links.new(geo.outputs['Position'], vm.inputs[0])
        tex = nt.nodes.new('ShaderNodeTexImage')
        tex.image = img
        nt.nodes.active = tex
        m['geo'] = geo.name
        mats.append(m)
    for i, m in enumerate(mats):
        lp.data.materials[i] = m
    return mats


def wire(mats, what):
    for m in mats:
        nt = m.node_tree
        geo, vm = nt.nodes[m['geo']], nt.nodes['map']
        for l in list(vm.inputs[0].links):
            nt.links.remove(l)
        if what == 'pos':  # metres, -4..4 into 0..1
            nt.links.new(geo.outputs['Position'], vm.inputs[0])
            vm.inputs[1].default_value = (.125, .125, .125)
            vm.inputs[2].default_value = (.5, .5, .5)
        else:
            nt.links.new(geo.outputs['Normal'], vm.inputs[0])
            vm.inputs[1].default_value = (.5, .5, .5)
            vm.inputs[2].default_value = (.5, .5, .5)


def emission_materials(colour_of):
    out = []
    for r in ROLES:
        m = bpy.data.materials.new('emit ' + r)
        m.use_nodes = True
        nt = m.node_tree
        nt.nodes.clear()
        o = nt.nodes.new('ShaderNodeOutputMaterial')
        e = nt.nodes.new('ShaderNodeEmission')
        e.inputs[0].default_value = (*colour_of(r), 1)
        nt.links.new(e.outputs[0], o.inputs[0])
        out.append(m)
    return out


def do_bake(kind, lp, hp, img, samples, selected, clear=True, **kw):
    bpy.context.scene.cycles.samples = samples
    for o in bpy.context.scene.objects:
        o.select_set(False)
    if selected:
        for o in hp:
            o.select_set(True)
    lp.select_set(True)
    bpy.context.view_layer.objects.active = lp
    bk = bpy.context.scene.render.bake
    bk.use_selected_to_active = selected
    bk.cage_extrusion = .012
    bk.max_ray_distance = .03
    bk.margin = 6
    bk.margin_type = 'EXTEND'
    bk.use_clear = clear
    t = time.time()
    bpy.ops.object.bake(type=kind, use_clear=clear, use_selected_to_active=selected, cage_extrusion=.012,
                        max_ray_distance=.03, margin=6, margin_type='EXTEND', target='IMAGE_TEXTURES', **kw)
    print('  bake %s %.1f s' % (kind, time.time() - t))


def pixels(img):
    a = np.empty(img.size[0] * img.size[1] * 4, np.float32)
    img.pixels.foreach_get(a)
    return a.reshape(img.size[1], img.size[0], 4)


def export_tris(obj, path, extra=None):
    """Triangles of a mesh: corners' positions, face normals, UVs, roles, groups (npz)."""
    me = obj.data
    me.calc_loop_triangles()
    n = len(me.loop_triangles)
    pos = np.zeros((n, 3, 3), np.float32)
    nrm = np.zeros((n, 3), np.float32)
    uv = np.zeros((n, 3, 2), np.float32)
    role = np.zeros(n, np.int32)
    grp = np.zeros(n, np.int32)
    uvd = me.uv_layers.active.data if me.uv_layers.active else None
    ga = me.attributes.get('grp')
    for i, t in enumerate(me.loop_triangles):
        for k in range(3):
            pos[i, k] = me.vertices[t.vertices[k]].co
            if uvd:
                uv[i, k] = uvd[t.loops[k]].uv
        nrm[i] = t.normal
        role[i] = t.material_index
        grp[i] = ga.data[t.polygon_index].value if ga else 0
    np.savez_compressed(path, pos=pos, nrm=nrm, uv=uv, role=role, grp=grp, **(extra or {}))
    return n


def main():
    os.makedirs(A.out, exist_ok=True)
    r1, r2 = (float(x) for x in A.lod.split(','))
    for name in A.cars.split(','):
        t0 = time.time()
        print('==', name)
        b = build_car(name)
        kind = b.kind
        col = b.col
        side, axle = 1, veh.AXLES[kind][0]  # the left front wheel (vehicles.py +x is the game's left)
        lf = f'wheel {side:+d} {axle:+.2f}'
        bpy.context.view_layer.update()
        hp = gather(objects_hp(col), 'HP', False)
        lp = gather(objects_lp(col, lf), 'LP', True)
        for o in list(col.all_objects):
            o.hide_render = True
        print('  hp %d tris, lp %d tris' % (len(hp.data.polygons), len(lp.data.polygons)))
        unwrap(lp)
        # (four wheels for the body's occlusion: the left front one is in lp, three copies)
        W = A.tex
        img = bpy.data.images.new('atlas', W, W, alpha=True, float_buffer=True)
        img.colorspace_settings.name = 'Non-Color'
        setup_cycles(1)
        mats = bake_materials(lp, img)
        work = os.path.join(A.out, name)
        os.makedirs(work, exist_ok=True)
        hp_slots = list(hp.data.materials)
        # Position and normal on the game mesh (emission, one sample).
        hp.hide_render = True
        for what in ('pos', 'nrm'):
            wire(mats, what)
            do_bake('EMIT', lp, [], img, 1, False)
            np.save(os.path.join(work, 'bake_%s.npy' % what), pixels(img).astype(np.float16))
        hp.hide_render = False
        # Albedo from the full model.
        realm = [b.m[r] for r in ROLES]
        for i, m in enumerate(realm):
            hp.data.materials[i] = m
            bsdf = m.node_tree.nodes['Principled BSDF']  # (the diffuse colour pass is base x (1 - metallic))
            bsdf.inputs['Metallic'].default_value = 0
        do_bake('DIFFUSE', lp, [hp], img, 16, True, pass_filter={'COLOR'})
        np.save(os.path.join(work, 'bake_albedo.npy'), pixels(img).astype(np.float16))
        # Paint mask and material ids from the full model.
        for what, samples, colour in (('mask', 16, lambda r: (1, 1, 1) if r == 'paint' else (0, 0, 0)),
                                      ('id', 1, lambda r: ((ROLES.index(r) + 1) / 32, 0, 0))):
            em = emission_materials(colour)
            for i, m in enumerate(em):
                hp.data.materials[i] = m
            do_bake('EMIT', lp, [hp], img, samples, True)
            np.save(os.path.join(work, 'bake_%s.npy' % what), pixels(img).astype(np.float16))
        # Ambient occlusion on the game mesh: the body with all four wheels, then the wheel alone
        # (it turns: nothing of the body on it).
        hp.hide_render = True
        bpy.ops.object.select_all(action='DESELECT')
        lp.select_set(True)
        bpy.context.view_layer.objects.active = lp
        bpy.ops.object.mode_set(mode='EDIT')
        bpy.ops.mesh.select_all(action='DESELECT')
        bpy.ops.object.mode_set(mode='OBJECT')
        ga = lp.data.attributes['grp']
        for p in lp.data.polygons:
            p.select = ga.data[p.index].value in (2, 3)
        bpy.ops.object.mode_set(mode='EDIT')
        bpy.ops.mesh.separate(type='SELECTED')
        bpy.ops.object.mode_set(mode='OBJECT')
        wheel = [o for o in bpy.context.selected_objects if o != lp][0]
        wheel.name = 'LP wheel'
        copies = []
        for (s2, ax2) in WHEELS:
            if (s2, ax2) == (side, axle):
                continue
            c = wheel.copy()
            bpy.context.scene.collection.objects.link(c)
            if s2 != side:
                c.scale.x = -1
            c.location.y = ax2 - axle
            copies.append(c)
        do_bake('AO', lp, [], img, A.ao_samples, False, clear=True)
        for o in copies + [lp]:
            o.hide_render = True
        do_bake('AO', wheel, [], img, A.ao_samples, False, clear=False)
        np.save(os.path.join(work, 'bake_ao.npy'), pixels(img).astype(np.float16))
        for o in copies:
            bpy.data.objects.remove(o, do_unlink=True)
        lp.hide_render = False
        # Levels: 0 the game mesh; 1 by edge collapse; 2 the same without the interior.
        cowl, rf, rr, rear, bw, rw, h, base = veh.CABINS[kind]
        meta = {'name': name, 'kind': kind, 'roles': ROLES, 'class': {r: CLASS.get(r, 0) for r in ROLES},
                'colour': {r: list(b.m[r].node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value[:3]) for r in ROLES},
                'hubs': [[s2 * .858, ax2] for s2, ax2 in WHEELS], 'lf': [side * .858, axle], 'tex': W,
                'number': list((((.02, .50), (-.06, .52), (0.0, .47), (-.05, .58))[kind])),
                'cabin': [cowl, rf, rr, rear, bw, rw, h, base], 'drawing': veh.DRAWINGS[kind], 'axles': list(veh.AXLES[kind])}
        meta['number_x'] = veh._side_x(kind, meta['number'][0], meta['number'][1])
        n0 = export_tris(lp, os.path.join(work, 'lod0.npz'))
        dg = bpy.context.evaluated_depsgraph_get()
        for lvl, ratio, keep_inside in ((1, r1, True), (2, r2, False)):
            c = lp.copy()
            c.data = lp.data.copy()
            bpy.context.scene.collection.objects.link(c)
            if not keep_inside:
                bm = bmesh.new()
                bm.from_mesh(c.data)
                gl = bm.faces.layers.int.get('grp')
                bmesh.ops.delete(bm, geom=[f for f in bm.faces if f[gl] == 1], context='FACES')
                bm.to_mesh(c.data)
                bm.free()
            mod = c.modifiers.new('dec', 'DECIMATE')
            mod.ratio = ratio * n0 / max(1, len(c.data.polygons))
            mod.use_collapse_triangulate = True
            bpy.context.view_layer.update()
            ev = c.evaluated_get(bpy.context.evaluated_depsgraph_get())
            me = bpy.data.meshes.new_from_object(ev)
            c2 = bpy.data.objects.new('LP%d' % lvl, me)
            bpy.context.scene.collection.objects.link(c2)
            n = export_tris(c2, os.path.join(work, 'lod%d.npz' % lvl))
            print('  level %d: %d tris' % (lvl, n))
            meta['tris%d' % lvl] = n
        meta['tris0'] = n0
        # The wheel: around its hub; the caliper apart.
        ga = wheel.data.attributes['grp']
        export_tris(wheel, os.path.join(work, 'wheel.npz'))
        meta['wheel_tris'] = len(wheel.data.polygons)
        json.dump(meta, open(os.path.join(work, 'meta.json'), 'w'), indent=1)
        if A.save_blend:
            bpy.ops.wm.save_as_mainfile(filepath=os.path.join(work, name + '.blend'))
        print('  %s done in %.0f s' % (name, time.time() - t0))


main()
