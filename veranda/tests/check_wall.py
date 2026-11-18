#!/usr/bin/env python3
"""Gate wall (DECISIONS V78): the desktop's picture in an x16emu dump's
VRAM (-dump V or RV).

usage: check_wall.py picture <dump> <wallpaper.cvt> <variant> [--tall 0] [--skip x0,y0,x1,y1 ...]
       check_wall.py plain <dump> <colour> [--skip ...]
       check_wall.py diff <wallpaper.cvt> <picture.png>

picture: every card of the desktop (card rows 2 and down, the cards left
of the desktop's icons, x < 69: the labels reach card 69) not in a --skip rectangle (cards,
inclusive) shows the wallpaper's variant (0 light, 1 dark): its 8 bytes
in the foreground and in the back screen are the file's bitmap there, its
colour map entry the file's colour (attribute high nibble, tile 0: not
covered); palette entries 12-15 the file's, 2-11 Look 2's (light or
dark). --tall 0 (640 x 240): card row y shows the file's card row 2y,
pixel row r the file's row 2r.
plain: those cards are the colour given, their bitmap 0 (pattern 0).
diff: the wallpaper's light variant as VERA shows it against the picture
it was made from (both 640 x 480): the mean error of 8 x 8 card averages
(a channel, 0-255) under 24, the dark variant different from the light.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools'))
import mkwall  # noqa: E402

VDUMP = 132878
VRAM, COMPOSER = 131072, 256
MAP = 0x1AC00
BACK = 0x09600


def fail(m):
    print('FAIL ', m)
    sys.exit(1)


def section(path):
    d = open(path, 'rb').read()
    return d[len(d) - VDUMP:]


def pal(v, i):
    p = v[VRAM + COMPOSER:]
    return p[2 * i] | p[2 * i + 1] << 8


def cards(skip, rows=60):
    for cy in range(2, rows):
        for cx in range(69):
            if any(x0 <= cx <= x1 and y0 <= cy <= y1 for x0, y0, x1, y1 in skip):
                continue
            yield cx, cy


def picture(v, path, variant, tall, skip):
    data = mkwall.read(path)
    p16, cols, bm = mkwall.parts(data, variant)
    for i in range(2, 16):
        if pal(v, i) != p16[i]:
            fail('palette entry %d is $%03X, not $%03X' % (i, pal(v, i), p16[i]))
    n = 0
    rows = 60 if tall else 30
    for cx, cy in cards(skip, rows):
        sy = cy if tall else cy * 2
        a = MAP + 2 * (cy * 128 + cx)
        if v[a] != 0 or v[a + 1] >> 4 != cols[sy * 80 + cx]:
            fail('card %d, %d: tile %d, colour %d, not the picture\'s %d' % (cx, cy, v[a], v[a + 1] >> 4,
                                                                             cols[sy * 80 + cx]))
        for j in range(8):
            r = cy * 8 + j
            fr = r if tall else r * 2
            want = bm[fr * 80 + cx]
            if v[r * 80 + cx] != want:
                fail('card %d, %d, row %d: the foreground has $%02X, the picture $%02X' % (cx, cy, j, v[r * 80 + cx],
                                                                                         want))
            if v[BACK + r * 80 + cx] != want:
                fail('card %d, %d, row %d: the back screen has $%02X, the picture $%02X' % (
                    cx, cy, j, v[BACK + r * 80 + cx], want))
        n += 1
    print('ok    %d cards of the desktop show the picture (variant %d%s): bitmap on both screens, colours; '
          'palette entries 12-15 %s' % (n, variant, '' if tall else ', 640 x 240',
                                        ' '.join('$%03X' % p16[i] for i in range(12, 16))))


def plain(v, colour, skip):
    n = 0
    for cx, cy in cards(skip):
        a = MAP + 2 * (cy * 128 + cx)
        if v[a + 1] >> 4 != colour or v[a]:
            fail('card %d, %d: colour %d (tile %d), not %d' % (cx, cy, v[a + 1] >> 4, v[a], colour))
        for j in range(8):
            if v[(cy * 8 + j) * 80 + cx]:
                fail('card %d, %d: ink in a plain desktop' % (cx, cy))
        n += 1
    print('ok    %d cards of the desktop in colour %d, no ink' % (n, colour))


def diff(path, png):
    from PIL import Image
    data = mkwall.read(path)
    src = Image.open(png).convert('RGB')
    if src.size != (640, 480):
        src = src.resize((640, 480))
    b = src.tobytes()
    sp = [tuple(b[i:i + 3]) for i in range(0, len(b), 3)]
    rows = mkwall.render(data, 0)
    err, n = 0.0, 0
    for cy in range(60):
        for cx in range(80):
            a = [0, 0, 0]
            b = [0, 0, 0]
            for j in range(8):
                for i in range(8):
                    x, y = cx * 8 + i, cy * 8 + j
                    for k in range(3):
                        a[k] += rows[y][x][k]
                        b[k] += sp[y * 640 + x][k]
            err += sum(abs(a[k] - b[k]) / 64 for k in range(3)) / 3
            n += 1
    err /= n
    if err >= 24:
        fail('the wallpaper is %.1f a channel from the picture (card averages)' % err)
    if mkwall.parts(data, 0)[2] == mkwall.parts(data, 1)[2]:
        fail('the dark variant\'s bitmap is the light one\'s')
    print('ok    the light variant is %.1f a channel from the picture (8 x 8 averages); the dark one differs' % err)


def main():
    a = sys.argv[1:]
    skip, tall = [], 1
    while '--skip' in a:
        i = a.index('--skip')
        skip.append(tuple(int(x) for x in a[i + 1].split(',')))
        del a[i:i + 2]
    if '--tall' in a:
        i = a.index('--tall')
        tall = int(a[i + 1])
        del a[i:i + 2]
    if len(a) == 4 and a[0] == 'picture':
        picture(section(a[1]), a[2], int(a[3]), tall, skip)
    elif len(a) == 3 and a[0] == 'plain':
        plain(section(a[1]), int(a[2]), skip)
    elif len(a) == 3 and a[0] == 'diff':
        diff(a[1], a[2])
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main()
