#!/usr/bin/env python3
"""A Veranda wallpaper (the desktop picture, Veranda Look 2 step C,
docs/DECISIONS.md V78) from a picture, as a GEOS document.

usage: mkwall.py [-o out.cvt] [--name NAME] [--dark-level F] [--stretch] <picture | --dusk>
       mkwall.py --png out.png [--dark] <wallpaper.cvt>
       mkwall.py --check <wallpaper.cvt>

<picture>: any picture Pillow reads (PNG, JPEG ...), cropped to 4:3 round
its middle (--stretch: not cropped) and resized to 640 x 480; --dusk: the
built-in dusk scene (the proposal's), Veranda's default wallpaper.

The X16 shows a picture the way Veranda draws everything: a 1 bpp bitmap
in one ink (palette entry 1) over a paper colour per 8 x 8 card (the
colour map, DECISIONS V30). So a wallpaper is, per card, the palette entry
that, darkened toward the ink (lightened, in dark mode), best matches the
picture there, and the ink as a 4 x 4 Bayer dither of how far toward the
ink each pixel is. Palette entries 2-11 are Look 2's fixed colours
(sdk/lib/theme.h); 12-15 are the wallpaper's own, chosen for each picture
(k-means, then refined against the dither). Dark mode's ink is light, so
the same bitmap would look pale: a second variant is made against the dark
palette and its light ink, from the picture darkened (--dark-level, 0.55).

The file (docs/APPFORMAT.md, "Wallpapers"): a sequential GEOS document
(APPL_DATA, class "Wallpaper"), its data
    0   "VWAL"    4 version (1)   5 variants (2: light, dark)
    6   80 (cards across)         7 60 (card rows)       8-15 0
    16 + v * 43208, variant v:
        8       palette entries 12-15 ($0RGB, little-endian words)
        4800    a palette entry a card, row by row (80 a row)
        38400   the bitmap, 80 bytes a row, bit 7 the left pixel, 1 ink

--png renders a wallpaper file as VERA shows it (light, or --dark);
--check checks a file's structure. Inputs are only read.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vpack  # noqa: E402

W, H = 640, 480
CW, CH = 80, 60
MAGIC = b'VWAL'
VERSION = 1
HEAD = 16
VARIANT = 8 + CW * CH + W * H // 8          # 43208
FREE = (12, 13, 14, 15)                      # the wallpaper's own entries
FIXED = tuple(range(2, 12))                  # Look 2's fixed colours
# Look 2's palette (sdk/lib/theme.c), light and dark
LIGHT = [0x0FFF, 0x0223, 0x0DDE, 0x0EEF, 0x09CF, 0x0ACE, 0x0FD5, 0x0F87,
         0x08D9, 0x0B9F, 0x0FA5, 0x06CE, 0x036A, 0x08BE, 0x05A7, 0x0DAC]
DARK = [0x0334, 0x0EEF, 0x0223, 0x0223, 0x0357, 0x0346, 0x0A82, 0x0A44,
        0x0385, 0x0649, 0x0A62, 0x0278, 0x0124, 0x0247, 0x0253, 0x0535]
BAYER = [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5]


def rgb(c):
    """$0RGB -> 8-bit channels, as VERA's 4-bit ones show"""
    return ((c >> 8 & 15) * 17, (c >> 4 & 15) * 17, (c & 15) * 17)


def word(p):
    """8-bit channels -> the nearest $0RGB"""
    q = [min(15, max(0, int(round(v / 17.0)))) for v in p]
    return q[0] << 8 | q[1] << 4 | q[2]


# ---------------------------------------------------------------- pictures
def dusk(x, y):
    """the built-in scene: a dusk sky over two lines of hills (the
    proposal's mockup, look/mock.py)"""
    def mix(a, b, t):
        return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t)
    top, mid, low = (40, 70, 140), (120, 170, 225), (225, 170, 200)
    t = y / 330
    sky = mix(top, mid, min(1, t * 1.6)) if t < .62 else mix(mid, low, min(1, (t - .62) / .38))
    h1 = 300 + 28 * math.sin(x / 70) + 14 * math.sin(x / 23 + 1)
    h2 = 350 + 20 * math.sin(x / 55 + 2) + 9 * math.sin(x / 17)
    if y > h2:
        return mix((70, 140, 100), (30, 60, 50), min(1, (y - h2) / 110))
    if y > h1:
        return mix((120, 110, 160), (90, 80, 130), min(1, (y - h1) / 60))
    return sky


def dusk_pixels():
    return [dusk(x, y) for y in range(H) for x in range(W)]


def picture_pixels(path, stretch=False):
    from PIL import Image
    img = Image.open(path).convert('RGB')
    if not stretch:
        w, h = img.size
        if w * 3 > h * 4:                   # wider than 4:3
            nw = h * 4 // 3
            img = img.crop(((w - nw) // 2, 0, (w - nw) // 2 + nw, h))
        elif w * 3 < h * 4:
            nh = w * 3 // 4
            img = img.crop((0, (h - nh) // 2, w, (h - nh) // 2 + nh))
    img = img.resize((W, H), Image.LANCZOS)
    b = img.tobytes()
    return [(float(b[i]), float(b[i + 1]), float(b[i + 2])) for i in range(0, len(b), 3)]


# ---------------------------------------------------------------- the fit
def samples(px):
    """per card, 16 samples: the means of its 2 x 2 blocks"""
    out = []
    for cy in range(CH):
        for cx in range(CW):
            s = []
            for by in range(4):
                for bx in range(4):
                    x, y = cx * 8 + bx * 2, cy * 8 + by * 2
                    a, b, c, d = px[y * W + x], px[y * W + x + 1], px[(y + 1) * W + x], px[(y + 1) * W + x + 1]
                    s.append(((a[0] + b[0] + c[0] + d[0]) / 4, (a[1] + b[1] + c[1] + d[1]) / 4,
                              (a[2] + b[2] + c[2] + d[2]) / 4))
            out.append(s)
    return out


def card_err(ts, p, k):
    """the error of paper p darkened toward ink k (a in [0, 1] a sample)"""
    p0, p1, p2 = p
    d0, d1, d2 = k[0] - p0, k[1] - p1, k[2] - p2
    dd = d0 * d0 + d1 * d1 + d2 * d2 or 1.0
    e = 0.0
    for t in ts:
        u0, u1, u2 = t[0] - p0, t[1] - p1, t[2] - p2
        s = u0 * d0 + u1 * d1 + u2 * d2
        if s <= 0:
            e += u0 * u0 + u1 * u1 + u2 * u2
        elif s >= dd:
            v0, v1, v2 = t[0] - k[0], t[1] - k[1], t[2] - k[2]
            e += v0 * v0 + v1 * v1 + v2 * v2
        else:
            e += u0 * u0 + u1 * u1 + u2 * u2 - s * s / dd
    return e


def kmeans(points, n, rounds=10):
    """n centres of points (k-means, spread start)"""
    pts = points
    cs = [pts[len(pts) // 2]]
    while len(cs) < n:
        best, far = None, -1
        for p in pts[::7]:
            d = min((p[0] - c[0]) ** 2 + (p[1] - c[1]) ** 2 + (p[2] - c[2]) ** 2 for c in cs)
            if d > far:
                far, best = d, p
        cs.append(best)
    for _ in range(rounds):
        acc = [[0.0, 0.0, 0.0, 0] for _ in cs]
        for p in pts:
            j = min(range(n), key=lambda i: (p[0] - cs[i][0]) ** 2 + (p[1] - cs[i][1]) ** 2 + (p[2] - cs[i][2]) ** 2)
            a = acc[j]
            a[0] += p[0]
            a[1] += p[1]
            a[2] += p[2]
            a[3] += 1
        cs = [(a[0] / a[3], a[1] / a[3], a[2] / a[3]) if a[3] else cs[i] for i, a in enumerate(acc)]
    return cs


def fit(px, base, rounds=5, log=None):
    """palette entries 12-15 and a colour a card for pixels px against
    palette base (its ink entry 1) -> (pal16, colours)"""
    k = rgb(base[1])
    smp = samples(px)
    # the fixed entries' errors, once
    fixed = {c: [card_err(ts, rgb(base[c]), k) for ts in smp] for c in FIXED}
    # a card's paper: its sample farthest from the ink
    paper = [max(ts, key=lambda t: (t[0] - k[0]) ** 2 + (t[1] - k[1]) ** 2 + (t[2] - k[2]) ** 2) for ts in smp]
    seeds = [[rgb(word(c)) for c in kmeans(paper, len(FREE))], [rgb(base[f]) for f in FREE]]
    best = None
    for r in range(2 * rounds):
        if r % rounds == 0:
            free = seeds[r // rounds]
        ferr = [[card_err(ts, p, k) for ts in smp] for p in free]
        choice, total = [], 0.0
        for i in range(CW * CH):
            c, e = min(((c, fixed[c][i]) for c in FIXED), key=lambda x: x[1])
            for j, f in enumerate(FREE):
                if ferr[j][i] < e:
                    c, e = f, ferr[j][i]
            choice.append(c)
            total += e
        if log:
            log('  %s round %d: error %.0f a card' % ('k-means' if r < rounds else 'Look 2', r % rounds,
                                                     total / (CW * CH)))
        if best is None or total < best[0]:
            best = (total, list(free), choice)
        # the free entries refitted to their cards (a held as the dither has it)
        nf = []
        for j, f in enumerate(FREE):
            p = free[j]
            d = (k[0] - p[0], k[1] - p[1], k[2] - p[2])
            dd = d[0] ** 2 + d[1] ** 2 + d[2] ** 2 or 1.0
            n0 = n1 = n2 = den = 0.0
            for i, c in enumerate(choice):
                if c != f:
                    continue
                for t in smp[i]:
                    a = ((t[0] - p[0]) * d[0] + (t[1] - p[1]) * d[1] + (t[2] - p[2]) * d[2]) / dd
                    a = min(0.95, max(0.0, a))
                    w = 1 - a
                    n0 += w * (t[0] - a * k[0])
                    n1 += w * (t[1] - a * k[1])
                    n2 += w * (t[2] - a * k[2])
                    den += w * w
            nf.append(rgb(word((n0 / den, n1 / den, n2 / den))) if den else p)
        free = nf
    _, free, choice = best
    pal = list(base)
    for j, f in enumerate(FREE):
        pal[f] = word(free[j])
    return pal, choice


def bitmap(px, pal, choice):
    """the ink: a 4 x 4 Bayer dither of how far toward the ink a pixel is"""
    k = rgb(pal[1])
    out = bytearray(W * H // 8)
    for cy in range(CH):
        for cx in range(CW):
            p = rgb(pal[choice[cy * CW + cx]])
            d = (k[0] - p[0], k[1] - p[1], k[2] - p[2])
            dd = d[0] ** 2 + d[1] ** 2 + d[2] ** 2 or 1.0
            for j in range(8):
                y = cy * 8 + j
                b = 0
                for i in range(8):
                    x = cx * 8 + i
                    t = px[y * W + x]
                    a = ((t[0] - p[0]) * d[0] + (t[1] - p[1]) * d[1] + (t[2] - p[2]) * d[2]) / dd
                    if BAYER[(y & 3) * 4 + (x & 3)] < a * 16:
                        b |= 0x80 >> i
                out[y * (W // 8) + cx] = b
    return bytes(out)


def variant(px, base, log=None):
    pal, choice = fit(px, base, log=log)
    bm = bitmap(px, pal, choice)
    head = b''.join(pal[c].to_bytes(2, 'little') for c in FREE)
    return head + bytes(choice) + bm


def icon(px):
    """a 24 x 21 icon: the picture's light, dithered"""
    out = bytearray()
    for y in range(21):
        for b in range(3):
            v = 0
            for i in range(8):
                x = b * 8 + i
                if x == 0 or x == 23 or y == 0 or y == 20:
                    v |= 0x80 >> i
                    continue
                sx, sy = x * W // 24, y * H // 21
                t = px[sy * W + sx]
                lum = (t[0] * 0.3 + t[1] * 0.59 + t[2] * 0.11) / 255
                if BAYER[(y & 3) * 4 + (x & 3)] >= lum * 16:
                    v |= 0x80 >> i
            out.append(v)
    return bytes(out)


def make(px, name, dark_level=0.55, log=None):
    if log:
        log('mkwall: light')
    light = variant(px, LIGHT, log)
    dpx = [(t[0] * dark_level, t[1] * dark_level, t[2] * dark_level) for t in px]
    if log:
        log('mkwall: dark')
    dark = variant(dpx, DARK, log)
    data = MAGIC + bytes([VERSION, 2, CW, CH]) + bytes(8) + light + dark
    return vpack.pack_doc(name, 'Wallpaper', data, icon=icon(px), author='Veranda',
                          description='A desktop picture for the shell (Preferences, Desktop).')


# ---------------------------------------------------------------- reading
def read(path):
    """a wallpaper file -> its data (raises ValueError)"""
    d = open(path, 'rb').read()
    try:
        info = vpack.parse(d)
    except vpack.PackError as e:
        raise ValueError(str(e))
    if info['structure'] != 'sequential' or not info['class'].startswith('Wallpaper'):
        raise ValueError('%s: not a wallpaper (class %s)' % (path, info['class']))
    data = d[508:]
    if data[:4] != MAGIC or data[4] != VERSION or data[6] != CW or data[7] != CH:
        raise ValueError('%s: not a wallpaper of version 1' % path)
    if len(data) != HEAD + data[5] * VARIANT:
        raise ValueError('%s: %d bytes of data, not %d' % (path, len(data), HEAD + data[5] * VARIANT))
    return data


def parts(data, v):
    """variant v -> (16 palette entries, colours, bitmap)"""
    o = HEAD + v * VARIANT
    pal = list(DARK if v else LIGHT)
    for j, f in enumerate(FREE):
        pal[f] = data[o + 2 * j] | data[o + 2 * j + 1] << 8
    return pal, data[o + 8:o + 8 + CW * CH], data[o + 8 + CW * CH:o + VARIANT]


def render(data, v):
    """as VERA shows it: a list of rows of RGB"""
    pal, cols, bm = parts(data, v)
    ink = rgb(pal[1])
    rows = []
    for y in range(H):
        r = []
        for x in range(W):
            if bm[y * (W // 8) + x // 8] & (0x80 >> (x & 7)):
                r.append(ink)
            else:
                r.append(rgb(pal[cols[(y // 8) * CW + x // 8] & 15]))
        rows.append(r)
    return rows


def main():
    a = sys.argv[1:]
    out, name, level, stretch, dark, png, check = None, None, 0.55, False, False, None, False
    try:
        while a and a[0].startswith('-') and a[0] != '--dusk':
            o = a.pop(0)
            if o == '-o' and a:
                out = a.pop(0)
            elif o == '--name' and a:
                name = a.pop(0)
            elif o == '--dark-level' and a:
                level = float(a.pop(0))
            elif o == '--stretch':
                stretch = True
            elif o == '--dark':
                dark = True
            elif o == '--png' and a:
                png = a.pop(0)
            elif o == '--check':
                check = True
            else:
                sys.exit(__doc__)
        if len(a) != 1:
            sys.exit(__doc__)
        if png or check:
            data = read(a[0])
            if check:
                for v in range(data[5]):
                    pal, cols, bm = parts(data, v)
                    if any(c > 15 or c < 2 for c in cols):
                        raise ValueError('variant %d: a card colour outside 2-15' % v)
                    print('variant %d: entries 12-15 %s, %d cards in them, %d ink pixels' % (
                        v, ' '.join('$%03X' % pal[f] for f in FREE), sum(c >= 12 for c in cols),
                        sum(bin(b).count('1') for b in bm)))
            if png:
                from PIL import Image
                rows = render(data, 1 if dark else 0)
                img = Image.new('RGB', (W, H))
                img.putdata([p for r in rows for p in r])
                img.save(png)
            return
        if a[0] == '--dusk':
            px, name = dusk_pixels(), name or 'Dusk'
        else:
            px = picture_pixels(a[0], stretch)
            name = name or os.path.splitext(os.path.basename(a[0]))[0][:16]
        cvt = make(px, name, level, log=lambda s: print(s, file=sys.stderr))
        out = out or name + '.cvt'
        open(out, 'wb').write(cvt)
    except (ValueError, OSError, vpack.PackError) as e:
        sys.exit('mkwall: %s' % e)


if __name__ == '__main__':
    main()
