#!/usr/bin/env python3
"""A Veranda screen from an x16emu dump (-dump V or BV) as a PNG.

usage: vram2png.py <dump.bin> <out.png> [width height]   (default 640 480)
       vram2png.py --color <dump.bin> <out.png> [width height]

The first form draws the foreground's bits (ink black, paper white). With
--color the picture is composed as VERA shows it, from the dump's layer
registers and palette: the 1 bpp bitmap on whichever layer holds it (its
ink: palette entry 1 + 16 * the layer's palette offset), and under it,
when the colour map is on (docs/DECISIONS.md V30), layer 0's 1 bpp tile
map: each 8 x 8 card's background colour (0: transparent, palette entry
0 shows). compose() is also used by the gates.
"""
import sys

VDUMP = 132878                  # x16emu's VRAM section: 128 KB and 1,806 bytes
VRAM, COMPOSER, PALETTE = 131072, 256, 512


def section(path):
    d = open(path, 'rb').read()
    return d[len(d) - VDUMP:]


def rgb(v, i):
    p = v[VRAM + COMPOSER:]
    gb, r = p[2 * i], p[2 * i + 1]
    return ((r & 15) * 17, (gb >> 4) * 17, (gb & 15) * 17)


def compose(v, w=640, h=480):
    """rows of (r, g, b) as VERA shows the screen (no sprites)"""
    layers = [v[VRAM + COMPOSER + PALETTE + 7 * i:][:7] for i in range(2)]
    video = v[VRAM]
    on = [bool(video & 0x10), bool(video & 0x20)]
    bm = [i for i in range(2) if on[i] and layers[i][0] & 0x04]
    if not bm:
        raise ValueError('no bitmap layer is shown')
    b = bm[-1]
    base = (layers[b][2] >> 2) << 11
    ink = rgb(v, 1 + 16 * (layers[b][4] & 15))
    paper = rgb(v, 0)
    stride = w // 8
    cmap = None
    if b == 1 and on[0] and not layers[0][0] & 0x04:
        cfg = layers[0]
        mapw = 32 << ((cfg[0] >> 4) & 3)
        mbase = cfg[1] << 9
        cmap = (mapw, mbase)
    out = []
    for y in range(h):
        row = []
        for x in range(w):
            if v[base + y * stride + x // 8] & (0x80 >> (x % 8)):
                row.append(ink)
                continue
            c = paper
            if cmap:
                mapw, mbase = cmap
                attr = v[mbase + 2 * ((y // 8) * mapw + x // 8) + 1] >> 4
                if attr:
                    c = rgb(v, attr)
            row.append(c)
        out.append(row)
    return out


def main():
    a = sys.argv[1:]
    color = bool(a) and a[0] == '--color'
    if color:
        a = a[1:]
    if len(a) < 2:
        sys.exit(__doc__)
    from PIL import Image
    w, h = (int(a[2]), int(a[3])) if len(a) > 3 else (640, 480)
    v = section(a[0])
    if color:
        img = Image.new('RGB', (w, h))
        px = img.load()
        for y, row in enumerate(compose(v, w, h)):
            for x, c in enumerate(row):
                px[x, y] = c
        img.save(a[1])
        return
    img = Image.new('1', (w, h))
    px = img.load()
    stride = w // 8
    for y in range(h):
        for x in range(w):
            px[x, y] = 0 if v[y * stride + x // 8] & (0x80 >> (x % 8)) else 1
    img.save(a[1])


if __name__ == '__main__':
    main()
