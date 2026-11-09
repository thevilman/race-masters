#!/usr/bin/env python3
"""Cuts the race sprites out of PPU dumps as transparent PNGs (3D mod, docs/3D-MOD.md).

Each dump (tg_headless --ppu-shots or --dump-ppu) holds VRAM, CGRAM, OAM and OBSEL as the
frame ends, and which object each sprite belongs to when high frame rate is on
(--interp-steps 2): a car, or the roadside object of a track segment. Every sprite is
decoded with its palette, an object's sprites are put together (a car is several hardware
sprites), and each picture is saved once, however many dumps show it. Sprites of no
object (the HUD, messages) are left out. The pictures come from your ROM, so they stay
local: never commit them.

  python3 tools/extract_sprites.py OUT_DIR DUMP.ppu|DUMP_DIR [...]

Writes OUT_DIR/car_WxH_HASH.png and OUT_DIR/roadside_WxH_HASH.png (1:1, transparent) and
OUT_DIR/sheet.png (all of them, 4x, biggest first). Needs Pillow.
"""

import hashlib
import os
import struct
import sys

from PIL import Image

# SNES sprite sizes by OBSEL size (small, large).
SIZES = [(8, 16), (8, 32), (8, 64), (16, 32), (16, 64), (32, 64), (16, 32), (16, 32)]


def load(path):
    data = open(path, "rb").read()
    vram = struct.unpack_from("<32768H", data, 0)
    cgram = struct.unpack_from("<256H", data, 0x10000)
    oam = struct.unpack_from("<256H", data, 0x10200)
    high = data[0x10400:0x10420]
    if len(data) < 0x10425:
        sys.exit(f"{path}: no OBSEL at the end; dump it with a current tg_headless")
    adr1, adr2, size = struct.unpack_from("<HHB", data, 0x10420)
    if len(data) < 0x10425 + 256:
        sys.exit(f"{path}: no object identities at the end; dump it with a current tg_headless")
    idents = struct.unpack_from("<128H", data, 0x10425)
    return vram, cgram, oam, high, adr1, adr2, size, idents


def rgb(c):
    r, g, b = c & 31, (c >> 5) & 31, (c >> 10) & 31
    return (r << 3 | r >> 2, g << 3 | g >> 2, b << 3 | b >> 2, 255)


def sprites(dump):
    """Yields (x, y, image, ident) for every sprite on screen, front sprite first."""
    vram, cgram, oam, high, adr1, adr2, osize, idents = dump
    for i in range(128):
        index = i * 2
        y = oam[index] >> 8
        x = (oam[index] & 0xFF) | ((high[index >> 3] >> (index & 7)) & 1) << 8
        if x >= 256:
            x -= 512
        size = SIZES[osize][(high[index >> 3] >> ((index & 7) + 1)) & 1]
        if 224 <= y <= 256 - size:  # parked below the screen
            continue
        if y >= 224:
            y -= 256
        attr = oam[index + 1]
        tile, pal = attr & 0xFF, (attr >> 9) & 7
        base = adr2 if attr & 0x100 else adr1
        hflip, vflip = attr & 0x4000, attr & 0x8000
        im = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        px = im.load()
        for row in range(size):
            for col in range(size):
                t = (((tile >> 4) + row // 8) << 4) | (((tile & 15) + col // 8) & 15)
                p1 = vram[(base + t * 16 + (row & 7)) & 0x7FFF]
                p2 = vram[(base + t * 16 + 8 + (row & 7)) & 0x7FFF]
                s = 7 - (col & 7)
                v = (p1 >> s) & 1 | ((p1 >> (8 + s)) & 1) << 1 | ((p2 >> s) & 1) << 2 | ((p2 >> (8 + s)) & 1) << 3
                if v:
                    dx = size - 1 - col if hflip else col
                    dy = size - 1 - row if vflip else row
                    px[dx, dy] = rgb(cgram[0x80 + 16 * pal + v])
        yield x, y, im, idents[i]  # with the view bit: each view sees a car on its own


def largest_cluster(parts):
    """The biggest set of touching sprites: an object's tag also covers the odd sprite drawn
    right after it elsewhere on screen (the player's marker on the map)."""
    clusters = []
    for p in parts:
        x, y, im = p
        hit = [c for c in clusters if any(x <= qx + qi.width and qx <= x + im.width and
                                          y <= qy + qi.height and qy <= y + im.height for qx, qy, qi in c)]
        merged = [p] + [q for c in hit for q in c]
        clusters = [c for c in clusters if c not in hit] + [merged]
    best = max(clusters, key=lambda c: sum(im.width * im.height for x, y, im in c))
    return [p for p in parts if p in best]  # in OAM order


def objects(dump):
    """Puts each object's sprites together; returns (kind, cropped RGBA image) pairs."""
    groups = {}
    for x, y, im, ident in sprites(dump):
        if ident:
            groups.setdefault(ident, []).append((x, y, im))
    out = []
    for ident, parts in groups.items():
        parts = largest_cluster(parts)
        x0 = min(x for x, y, im in parts)
        y0 = min(y for x, y, im in parts)
        x1 = max(x + im.width for x, y, im in parts)
        y1 = max(y + im.height for x, y, im in parts)
        canvas = Image.new("RGBA", (x1 - x0, y1 - y0), (0, 0, 0, 0))
        for x, y, im in reversed(parts):  # lower OAM index in front
            canvas.alpha_composite(im, (x - x0, y - y0))
        box = canvas.getbbox()
        if box and (box[2] - box[0]) * (box[3] - box[1]) >= 24:
            out.append(("car" if ident & 0x8000 else "roadside", canvas.crop(box)))
    return out


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    out_dir, dumps = sys.argv[1], []
    for arg in sys.argv[2:]:  # a folder stands for the dumps in it
        if os.path.isdir(arg):
            dumps += sorted(os.path.join(arg, f) for f in os.listdir(arg) if f.endswith(".ppu"))
        else:
            dumps.append(arg)
    os.makedirs(out_dir, exist_ok=True)
    seen = {}
    for path in dumps:
        for kind, im in objects(load(path)):
            h = hashlib.sha1(im.tobytes() + bytes(im.size)).hexdigest()[:10]
            if h not in seen:
                seen[h] = im
                im.save(os.path.join(out_dir, f"{kind}_{im.width}x{im.height}_{h}.png"))
    ims = sorted(seen.values(), key=lambda i: (-i.height * i.width))
    if not ims:
        print("no sprites found")
        return
    cell, cols = 4 * max(max(i.width, i.height) for i in ims) + 8, 10
    sheet = Image.new("RGBA", (cols * cell, ((len(ims) + cols - 1) // cols) * cell), (60, 60, 70, 255))
    for k, im in enumerate(ims):
        big = im.resize((im.width * 4, im.height * 4), Image.NEAREST)
        sheet.alpha_composite(big, ((k % cols) * cell + 4, (k // cols) * cell + 4))
    sheet.save(os.path.join(out_dir, "sheet.png"))
    print(f"{len(seen)} distinct objects from {len(dumps)} dumps -> {out_dir}")


if __name__ == "__main__":
    main()
