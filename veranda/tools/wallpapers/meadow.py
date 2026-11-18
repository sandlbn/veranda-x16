#!/usr/bin/env python3
"""Meadow, a desktop picture for Veranda: a green hill under a blue sky,
painted in solid colours of the X16's 12-bit palette only (no ink
shading, which would turn saturated colours grey: tools/mkwall.py,
DECISIONS V78), so VERA shows every one of them exactly and the steps
between them are the shading. The wallpaper's own entries 12-15: deep sky
$37D, sunlit hill $8D3, hill shade $5A2, meadow $AD4; Look 2's fixed
entries: cyan $6CE, select $9CF, title $ACE, sidebar $EEF, paper $FFF,
green $8D9. The clouds come from value noise with a fixed seed, so every
run paints the same picture. Then the picture is snapped to the 8 x 8 card
grid Veranda colours by: each card filled with its majority colour, so the
wallpaper has no dither speckles (a card is one paper colour on the X16).

usage: meadow.py [--unsnapped] <out.png>   the picture, 640 x 480 (needs Pillow)

tools/wallpapers/mkmeadow.py turns it into VERANDA/WALLPAPERS/Meadow.cvt.
Original art made for Veranda (MIT, like the rest of this tree).
"""
import math
import random
import sys

W, H = 640, 480


def c12(h):
    return tuple(int(ch, 16) * 17 for ch in h)


DEEP, CYAN, LIGHT, HAZE = c12('37D'), c12('6CE'), c12('9CF'), c12('ACE')
WHITE, CLOUDSH = c12('FFF'), c12('EEF')
SUN, SHADE, MEADOW, FARHILL = c12('8D3'), c12('5A2'), c12('AD4'), c12('8D9')
G = 64
grid = []


def vnoise(x, y):
    xi, yi = int(x) % G, int(y) % G
    xf, yf = x - int(x), y - int(y)
    sx, sy = xf * xf * (3 - 2 * xf), yf * yf * (3 - 2 * yf)
    a, b = grid[yi][xi], grid[yi][xi + 1]
    c, d = grid[yi + 1][xi], grid[yi + 1][xi + 1]
    top = a + (b - a) * sx
    return top + ((c + (d - c) * sx) - top) * sy


def fbm(x, y, oct=4):
    s, amp, f = 0.0, 0.5, 1.0
    for _ in range(oct):
        s += amp * vnoise(x * f, y * f)
        amp *= 0.5
        f *= 2.0
    return s


def hill(x):
    u = x / W
    return 300 - 150 * math.exp(-((u - 0.30) / 0.30) ** 2) + 22 * u


def far(x):
    return 322 - 8 * math.sin(x / 70.0 + 2) - 5 * math.sin(x / 23.0)


def paint():
    """the painted picture, before snapping (RGB, 640 x 480)"""
    from PIL import Image
    global grid
    random.seed(5)
    grid = [[random.random() for _ in range(G + 1)] for _ in range(G + 1)]
    img = Image.new('RGB', (W, H))
    px = img.load()
    for y in range(H):
        for x in range(W):
            hy, fy = hill(x), far(x)
            if y < hy and y < fy:
                wave = 12 * math.sin(x / 150.0)
                if y < 120 + wave:
                    c = DEEP
                elif y < 200 + wave:
                    c = CYAN
                elif y < 270 + wave * 0.5:
                    c = LIGHT
                else:
                    c = HAZE
                w = fbm(x / 120.0, y / 20.0 + x / 600.0)
                if w > 0.6 and y < 200:
                    c = WHITE if w > 0.66 else CLOUDSH
                p = fbm(x / 34.0 + 5, y / 24.0 + 11)
                if p > 0.6 and 235 < y < 312 and x > W * 0.6:
                    c = WHITE if (p > 0.64 and y < 292) else CLOUDSH
            elif y < hy:
                c = FARHILL
            else:
                front = 372 + 18 * math.sin(x / 110.0 + 0.4)
                if y < front:
                    # the sunlit side faces up and left; the shade starts on a
                    # curve from the crest down to the right
                    band = 26 + 34 * (x / W) + 6 * math.sin(x / 37.0)
                    c = SHADE if (front - y) < band else SUN
                else:
                    c = MEADOW
            px[x, y] = c
    return img


def snap(img):
    """each 8 x 8 card filled with its majority colour (a tie: the colour
    met first, row by row)"""
    from collections import Counter
    from PIL import Image
    src = img.load()
    out = Image.new('RGB', (W, H))
    px = out.load()
    for cy in range(0, H, 8):
        for cx in range(0, W, 8):
            c = Counter(src[cx + x, cy + y] for y in range(8) for x in range(8)).most_common(1)[0][0]
            for y in range(8):
                for x in range(8):
                    px[cx + x, cy + y] = c
    return out


def picture():
    """the wallpaper's picture: painted, then snapped to the cards"""
    return snap(paint())


def main():
    a = sys.argv[1:]
    if a and a[0] == '--unsnapped':
        if len(a) != 2:
            sys.exit(__doc__)
        paint().save(a[1])
        return
    if len(a) != 1:
        sys.exit(__doc__)
    picture().save(a[0])


if __name__ == '__main__':
    main()
