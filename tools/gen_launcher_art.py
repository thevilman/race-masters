#!/usr/bin/env python3
"""Draws the launcher's art and writes it to src/launcher_art.h: the logo (an emblem, the
project's name in chrome and a line under it) and the Start button's word; and its icon,
assets/launcher-icon.ico (the emblem on a navy tile, 16 to 256 pixels).

Our own design, drawn here from the two OFL fonts in the repository (Oxanium for the name,
Barlow Condensed for the line under it) and plain geometry; nothing comes from any game. The
emblem is a globe (two meridians, the equator) with a lap round it: a tilted orange orbit, its
front half over the globe, a bright dot running on it. The name is upright Oxanium ExtraBold
with a chrome face: white above a horizon line, a dark steel reflection below it warming to
the launcher's orange at the foot, a fine light bevel on the upper edges and a soft shadow.
Under it a short line in the launcher's gold and a rule running from gold to orange.

The name is TG_APP_NAME in CMakeLists.txt (read from there; --name previews another one
without writing anything). launcher_art.h records it (LAUNCHER_ART_NAME): a build with
another name draws its name as plain text instead of this logo until the art is redrawn.

Each picture is drawn four times finer than it is stored, then averaged down, and stored at
twice the launcher's 1280x720 layout: one texel a pixel in a 2560x1440 window, an exact 2:1
average in a 1280x720 one. Premultiplied RGBA (drawn with SDL_BLENDMODE_BLEND_PREMULTIPLIED),
run-length packed: a byte c < 128 is the next pixel (4 bytes) c + 1 times, c >= 128 is c - 127
pixels as they are.

Usage (from anywhere; Pillow, numpy, fontTools):
  tools/gen_launcher_art.py [--preview DIR]          writes src/launcher_art.h and the icon
  tools/gen_launcher_art.py --name "X" --preview DIR  previews another name only
"""
import argparse
import io
import os
import re
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer

SCALE = 2           # stored texels per launcher pixel
SS = 4              # drawn this much finer again
K = SCALE * SS      # drawing pixels per launcher pixel

# The launcher's palette (src/launcher.c).
ACCENT = (255, 92, 56)
ACCENT2 = (255, 196, 64)

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OXANIUM = os.path.join(ROOT, "third_party", "oxanium", "Oxanium[wght].ttf")
BARLOW = os.path.join(ROOT, "third_party", "barlow", "BarlowCondensed-Bold.ttf")
SUBLINE = "3D RACING MOD"  # the line under the name

STEEL = (196, 206, 226)
STEEL_DIM = (104, 116, 148)
WHITE = (255, 255, 255)


def app_name():
    """TG_APP_NAME from CMakeLists.txt: the one place the name is set."""
    with open(os.path.join(ROOT, "CMakeLists.txt")) as f:
        m = re.search(r'set\(TG_APP_NAME "([^"]+)"', f.read())
    if not m:
        raise SystemExit("TG_APP_NAME not found in CMakeLists.txt")
    return m.group(1)
OXANIUM_CAP = 0.69  # cap height / size (OS/2 sCapHeight 690 of 1000)
BARLOW_CAP = 0.70


def oxanium(weight, px):
    f = instancer.instantiateVariableFont(TTFont(OXANIUM), {"wght": weight})
    data = io.BytesIO()
    f.save(data)
    data.seek(0)
    return ImageFont.truetype(data, int(round(px)), layout_engine=ImageFont.Layout.BASIC)


def barlow(px):
    return ImageFont.truetype(BARLOW, int(round(px)), layout_engine=ImageFont.Layout.BASIC)


def text_mask(size, s, font, x, baseline, track=0.0, kern=None):
    """s from x on the baseline, letter by letter with the tracking and our own kerning (the
    font's kerning is in GPOS, which the basic layout doesn't read). Returns the mask and the
    right edge of the last letter's advance."""
    m = Image.new("L", size, 0)
    d = ImageDraw.Draw(m)
    for i, ch in enumerate(s):
        d.text((x, baseline), ch, font=font, fill=255, anchor="ls")
        x += font.getlength(ch) + track
        if kern and i + 1 < len(s):
            x += kern.get(s[i:i + 2], 0) * font.size
    return m, x - track


def ramp(h, stops):
    """A vertical gradient h rows tall: stops [(row, (r, g, b)), ...], rows increasing."""
    rows = np.arange(h, dtype=np.float32)
    ys = [s[0] for s in stops]
    return np.stack([np.interp(rows, ys, [s[1][i] for s in stops]) for i in range(3)], axis=1).astype(np.float32)


def layer(mask, colour, opacity=1.0):
    """A mask (L image or 0-1 array) in a colour (tuple, or rows x 3, or h x w x 3): premultiplied RGBA."""
    a = (np.asarray(mask, np.float32) / 255.0 if isinstance(mask, Image.Image) else mask) * opacity
    c = np.empty(a.shape + (3,), np.float32)
    c[:] = colour[:, None, :] if isinstance(colour, np.ndarray) and colour.ndim == 2 else colour
    return np.dstack([c * a[..., None], a])


def over(dst, src):
    return src + dst * (1 - src[..., 3:4])


def shifted(mask, dx, dy):
    """The mask moved by (dx, dy) drawing pixels."""
    return mask.transform(mask.size, Image.AFFINE, (1, 0, -dx, 0, 1, -dy), resample=Image.BILINEAR)


def shadow(mask, blur, dx, dy, opacity):
    return layer(shifted(mask.filter(ImageFilter.GaussianBlur(blur)), dx, dy), (4, 4, 10), opacity)


def chrome(acc, mask, top, base, u=K, bevel=True):
    """The chrome face over cap height top..base (drawing rows), its bevel and its shadow; u is
    drawing pixels a launcher pixel."""
    acc = over(acc, shadow(mask, 2.2 * u, 0.8 * u, 2.4 * u, 0.75))
    hz = top + (base - top) * 0.52
    face = ramp(acc.shape[0], [(top, (255, 255, 255)), (hz, (206, 216, 232)), (hz + 1, (64, 78, 112)),
                               (hz + (base - hz) * 0.55, (150, 160, 188)), (base, (255, 168, 120))])
    acc = over(acc, layer(mask, face))
    if not bevel:
        return acc
    # A light bevel along the upper edges: where the letter is and the row above it is not.
    m = np.asarray(mask, np.float32) / 255
    above = np.asarray(shifted(mask, 0, 0.9 * u), np.float32) / 255
    return over(acc, layer(np.clip(m - above, 0, 1), (255, 255, 255), 0.9))


def ellipse_points(cx, cy, rx, ry, tilt, t0, t1, n=240):
    """Points of an ellipse (radii rx, ry, turned tilt radians) from angle t0 to t1."""
    t = np.linspace(t0, t1, n)
    x, y = rx * np.cos(t), ry * np.sin(t)
    c, s = np.cos(tilt), np.sin(tilt)
    return [(cx + x[i] * c - y[i] * s, cy + x[i] * s + y[i] * c) for i in range(n)]


def band(cx, cy, rx, ry, tilt, t0, t1, w):
    """The polygon of an elliptical band w wide (centred on the ellipse) from angle t0 to t1."""
    outer = ellipse_points(cx, cy, rx + w / 2, ry + w / 2, tilt, t0, t1)
    inner = ellipse_points(cx, cy, rx - w / 2, ry - w / 2, tilt, t0, t1)
    return outer + inner[::-1]


def emblem(acc, cx, cy, r, u, detail=True):
    """The globe with a lap round it, centred on (cx, cy), the globe's radius r; u is drawing
    pixels a launcher (or icon) pixel."""
    H, W = acc.shape[:2]

    def mask():
        return Image.new("L", (W, H), 0)

    tilt = np.radians(-18.0)
    orx, ory = r * 1.42, r * 0.46  # the orbit
    ow = (2.6 if detail else 4.0) * u
    # The orbit's far half, behind the globe.
    back = mask()
    ImageDraw.Draw(back).polygon(band(cx, cy, orx, ory, tilt, np.pi, 2 * np.pi, ow), fill=255)
    acc = over(acc, layer(back, (150, 60, 40), 0.9))
    # The globe: a disc shaded from the top left, its lines.
    disc = mask()
    ImageDraw.Draw(disc).ellipse((cx - r, cy - r, cx + r, cy + r), fill=255)
    acc = over(acc, shadow(disc, 2.0 * u, 0.6 * u, 1.6 * u, 0.6))
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    lit = np.clip(1 - np.hypot(xx - (cx - r * 0.45), yy - (cy - r * 0.5)) / (r * 2.1), 0, 1)[..., None]
    face = np.array((30, 38, 66), np.float32) * (1 - lit) + np.array((92, 112, 168), np.float32) * lit
    acc = over(acc, layer(disc, face))
    if detail:
        g = mask()
        d = ImageDraw.Draw(g)
        lw = max(1, int(round(1.3 * u)))
        for k in (0.36, 0.78):
            d.ellipse((cx - r * k, cy - r, cx + r * k, cy + r), outline=255, width=lw)
        d.line((cx - r, cy, cx + r, cy), fill=255, width=lw)
        for t in (-0.55, 0.55):
            half = r * np.sqrt(1 - t * t)
            d.line((cx - half, cy + r * t, cx + half, cy + r * t), fill=255, width=lw)
        clip = np.asarray(disc, np.float32) / 255
        acc = over(acc, layer(np.asarray(g, np.float32) / 255 * clip, STEEL, 0.55))
    rim = mask()
    ImageDraw.Draw(rim).ellipse((cx - r, cy - r, cx + r, cy + r), outline=255, width=max(1, int(round(1.4 * u))))
    acc = over(acc, layer(rim, STEEL, 0.9))
    # The orbit's near half, in front, warming from gold to orange; the dot running on it.
    front = mask()
    pts = ellipse_points(cx, cy, orx, ory, tilt, 0, np.pi)
    ImageDraw.Draw(front).polygon(band(cx, cy, orx, ory, tilt, 0, np.pi, ow * 1.15), fill=255)
    v = np.clip((xx - (cx - orx)) / (2 * orx), 0, 1)[..., None]
    acc = over(acc, shadow(front, 0.8 * u, 0, 0.8 * u, 0.5))
    acc = over(acc, layer(front, np.array(ACCENT, np.float32) * v + np.array(ACCENT2, np.float32) * (1 - v)))
    dx, dy = pts[int(len(pts) * 0.30)]
    dr = (3.2 if detail else 4.6) * u
    dot = mask()
    ImageDraw.Draw(dot).ellipse((dx - dr, dy - dr, dx + dr, dy + dr), fill=255)
    acc = over(acc, layer(dot.filter(ImageFilter.GaussianBlur(dr * 0.9)), (255, 230, 200), 0.8))
    acc = over(acc, layer(dot, WHITE))
    return acc


def store(acc):
    """Averages the drawing down by SS and crops it to what is drawn; premultiplied RGBA8 and
    the crop's corner (launcher pixels)."""
    h, w = acc.shape[:2]
    acc = acc[: h // SS * SS, : w // SS * SS].reshape(h // SS, SS, w // SS, SS, 4).mean(axis=(1, 3))
    px = np.dstack([np.clip(acc[..., :3], 0, 255), np.clip(acc[..., 3:] * 255, 0, 255)]).round().astype(np.uint8)
    px[px[..., 3] == 0] = 0
    ys, xs = np.nonzero(px[..., 3])
    y0, x0 = ys.min(), xs.min()
    # Whole launcher pixels: keep the corner on even texels.
    y0, x0 = y0 // SCALE * SCALE, x0 // SCALE * SCALE
    y1, x1 = -(-(ys.max() + 1) // SCALE) * SCALE, -(-(xs.max() + 1) // SCALE) * SCALE
    return px[y0:y1, x0:x1], (x0 // SCALE, y0 // SCALE)


def logo(name):
    """The emblem, the name in chrome and the line under it."""
    W, H = 560 * K, 96 * K
    acc = np.zeros((H, W, 4), np.float32)
    r = 21 * K
    cx, cy = 8 * K + r * 1.42, 12 * K + 27 * K
    acc = emblem(acc, cx, cy, r, K)
    left, top, cap = cx + r * 1.42 + 12 * K, 12 * K, 34 * K
    base = top + cap
    word = oxanium(800, cap / OXANIUM_CAP)
    m, right = text_mask((W, H), name.upper(), word, left, base, track=1.6 * K)
    ink = np.nonzero(np.asarray(m).max(axis=0))[0]
    ink_left, ink_right = ink.min(), ink.max() + 1
    acc = chrome(acc, m, top, base)
    # The line under it, then a rule to the name's right edge.
    sub_cap = 11 * K
    line = base + 9 * K + sub_cap
    mid = line - sub_cap / 2
    sub, sub_right = text_mask((W, H), SUBLINE, barlow(sub_cap / BARLOW_CAP), ink_left + 0.5 * K, line, track=3.2 * K)
    acc = over(acc, layer(sub, ACCENT2))
    rule = Image.new("L", (W, H), 0)
    r0, r1 = sub_right + 8 * K, ink_right
    if r1 - r0 > 12 * K:
        ImageDraw.Draw(rule).rectangle((r0, mid - 1.2 * K, r1, mid + 1.2 * K), fill=255)
        u = np.clip((np.arange(W) - r0) / (r1 - r0), 0, 1)[None, :, None]
        acc = over(acc, layer(rule, np.array(ACCENT2, np.float32) * (1 - u) + np.array(ACCENT, np.float32) * u))
    px, _ = store(acc)
    return px


def start_word():
    """START in the logo's chrome, for the Start button."""
    W, H = 260 * K, 60 * K
    acc = np.zeros((H, W, 4), np.float32)
    top, cap = 10 * K, 28 * K
    m, _ = text_mask((W, H), "START", oxanium(800, cap / OXANIUM_CAP), 10 * K, top + cap, track=3.0 * K, kern={"TA": -0.02})
    acc = chrome(acc, m, top, top + cap)
    px, _ = store(acc)
    return px


def icon(size):
    """The launcher's icon, size x size, straight RGBA: the emblem on a rounded navy tile (the
    header's colours). Each size is drawn on its own, eight times finer; the small ones without
    the globe's lines."""
    ss = 8
    S = size * ss
    u = S / 64.0  # drawing pixels a pixel of the 64-pixel icon
    acc = np.zeros((S, S, 4), np.float32)
    tile = Image.new("L", (S, S), 0)
    ImageDraw.Draw(tile).rounded_rectangle((0, 0, S - 1, S - 1), radius=S * 0.19, fill=255)
    acc = over(acc, layer(tile, ramp(S, [(0, (46, 42, 76)), (S, (16, 18, 30))])))
    detail = size >= 48
    acc = emblem(acc, S / 2, S / 2, S * (0.27 if detail else 0.29), u * (1.0 if detail else 1.5), detail=detail)
    acc = acc.reshape(size, ss, size, ss, 4).mean(axis=(1, 3))
    a = acc[..., 3:]
    rgb = np.where(a > 1e-4, acc[..., :3] / np.maximum(a, 1e-4), 0)
    return Image.fromarray(np.dstack([rgb.clip(0, 255), (a * 255).clip(0, 255)]).round().astype(np.uint8), "RGBA")


def pack(px):
    """Run-length packing (see the top of this file)."""
    p = px.reshape(-1, 4)
    out = bytearray()
    lit = []

    def flush():
        while lit:
            n = min(128, len(lit))
            out.append(127 + n)
            for q in lit[:n]:
                out.extend(q)
            del lit[:n]

    i = 0
    while i < len(p):
        run = 1
        while i + run < len(p) and run < 128 and (p[i + run] == p[i]).all():
            run += 1
        if run >= 2:
            flush()
            out.append(run - 1)
            out += bytes(p[i])
        else:
            lit.append(bytes(p[i]))
        i += run
    flush()
    return bytes(out)


def unpack(data, n):
    out = bytearray()
    i = 0
    while i < len(data):
        c = data[i]
        i += 1
        if c < 128:
            out += data[i:i + 4] * (c + 1)
            i += 4
        else:
            out += data[i:i + 4 * (c - 127)]
            i += 4 * (c - 127)
    assert len(out) == n * 4
    return bytes(out)


def c_array(name, data):
    lines = ["static const unsigned char %s[%d] = {" % (name, len(data))]
    for i in range(0, len(data), 40):
        lines.append("  " + ",".join(str(b) for b in data[i:i + 40]) + ",")
    lines.append("};")
    return lines


def straight(px):
    """Premultiplied texels as a straight-alpha image."""
    a = px[..., 3:].astype(np.float32)
    rgb = np.where(a > 0, px[..., :3] * 255.0 / np.maximum(a, 1), 0)
    return Image.fromarray(np.dstack([rgb.round().clip(0, 255), a]).astype(np.uint8), "RGBA")


def header_preview(art, scale):
    """The logo over the launcher's header as src/launcher.c places it (the header art left out)."""
    w, h = 1000, 92
    bg = Image.new("RGBA", (w * scale, h * scale), (14, 16, 24, 255))
    d = ImageDraw.Draw(bg)
    for y in range(86):
        d.rectangle((0, y * scale, w * scale, (y + 1) * scale - 1), fill=(20 + y // 4, 22 + y // 6, 36 + y // 3))
    d.rectangle((0, 86 * scale, w * scale, 88 * scale - 1), fill=ACCENT)
    im = straight(art)
    if scale != SCALE:
        im = im.resize((im.width * scale // SCALE, im.height * scale // SCALE), Image.LANCZOS)
    bg.alpha_composite(im, ((40 - 6) * scale, (86 * scale - im.height) // 2))
    return bg


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--preview", metavar="DIR", help="also write PNG previews to DIR")
    ap.add_argument("--name", help="preview another name (writes no source files)")
    args = ap.parse_args()
    name = args.name or app_name()
    logo_px = logo(name)
    start_px = start_word()
    if not args.name:
        lines = ["// Generated by tools/gen_launcher_art.py: the launcher's logo and the Start button's word,",
                 "// drawn from Oxanium and Barlow Condensed (SIL Open Font License 1.1, third_party/oxanium and",
                 "// third_party/barlow) and geometry. Do not edit. Premultiplied RGBA at LAUNCHER_ART_SCALE",
                 "// texels a launcher pixel, run-length packed: a byte c < 128 is the next pixel (4 bytes)",
                 "// c + 1 times, c >= 128 is c - 127 pixels as they are.",
                 "#define LAUNCHER_ART_NAME \"%s\"  // the name the logo shows (TG_APP_NAME when drawn)" % name,
                 "#define LAUNCHER_ART_SCALE %d" % SCALE,
                 "#define LOGO_ART_W %d" % logo_px.shape[1], "#define LOGO_ART_H %d" % logo_px.shape[0],
                 "#define START_ART_W %d" % start_px.shape[1], "#define START_ART_H %d" % start_px.shape[0]]
        for cname, px in (("kLogoArt", logo_px), ("kStartArt", start_px)):
            data = pack(px)
            assert unpack(data, px.shape[0] * px.shape[1]) == px.tobytes()
            lines += c_array(cname, data)
            print("%s %dx%d: %d bytes packed (%d raw)" % (cname, px.shape[1], px.shape[0], len(data), px.size))
        with open(os.path.join(ROOT, "src", "launcher_art.h"), "w", newline="\n") as f:
            f.write("\n".join(lines) + "\n")
        sizes = [16, 24, 32, 48, 64, 128, 256]
        icons = [icon(n) for n in sizes]
        icons[-1].save(os.path.join(ROOT, "assets", "launcher-icon.ico"), format="ICO", sizes=[(n, n) for n in sizes],
                       append_images=icons[:-1])
    if args.preview:
        out = args.preview
        os.makedirs(out, exist_ok=True)
        tag = "-" + "-".join(args.name.lower().split()) if args.name else ""
        for sc in (1, 2):
            header_preview(logo_px, sc).save(os.path.join(out, "logo%s_header_%dx.png" % (tag, sc)))
        straight(logo_px).save(os.path.join(out, "logo%s.png" % tag))
        if not args.name:
            straight(start_px).save(os.path.join(out, "start.png"))
            sizes = [16, 24, 32, 48, 64, 128, 256]
            sheet = Image.new("RGBA", (sum(sizes) + 20 * len(sizes) + 20, 296), (40, 40, 48, 255))
            x = 20
            for n in sizes:
                sheet.alpha_composite(icon(n), (x, 276 - n))
                x += n + 20
            sheet.save(os.path.join(out, "icon_sizes.png"))


if __name__ == "__main__":
    main()
