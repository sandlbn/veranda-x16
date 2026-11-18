#!/usr/bin/env python3
"""Veranda Solitaire's card pictures, drawn here (1 bit, 56 x 80 pixels).

    mkcards.py <outdir>

Writes <outdir>/cards.1 ... cards.5 (the overlay records: 14 pictures of
560 bytes each, column-major: 7 columns of 80 bytes, bit 7 the left
pixel, 1 = ink), <outdir>/cards.h (the picture numbers and the corner
masks) and <outdir>/cards.png (every picture, for review).

Pictures 0-51: the faces, suit * 13 + rank (suits as PySolFC's: clubs,
spades, hearts, diamonds); 52 the back; 53 an empty place; 54-57 the
foundations' places (a suit each); 58 Spider's foundation (K); 59 the
talon's place when it can turn the waste over again.

Spades and clubs are solid, hearts and diamonds outlined, so the suits
stay apart in black and white. Nothing here comes from PySolFC's card
sets: the pips are drawn from simple geometry, the ranks are pixel fonts
made for this. GPL-3.0-or-later (see COPYING).
"""
import os
import sys

W, H, R = 56, 80, 4
PER_RECORD = 14

# --------------------------------------------------------------- shapes


def card_inside(x, y):
    """the card's rounded rectangle (pixel centres)"""
    px, py = x + 0.5, y + 0.5
    for cx, cy in ((R, R), (W - R, R), (R, H - R), (W - R, H - R)):
        if (px < R) == (cx == R) and (py < R) == (cy == R) and \
                (px < R or px > W - R) and (py < R or py > H - R):
            return (px - cx) ** 2 + (py - cy) ** 2 <= R * R
    return True


def heart(u, v):
    if (u + 0.48) ** 2 + (v + 0.38) ** 2 <= 0.52 ** 2 or (u - 0.48) ** 2 + (v + 0.38) ** 2 <= 0.52 ** 2:
        return True
    return -0.3 <= v <= 0.95 and abs(u) <= 0.98 * (0.95 - v) / 1.25


def diamond(u, v):
    return abs(u) / 0.78 + abs(v) / 0.98 <= 1


def stem(u, v):
    if abs(u) <= 0.11 and 0.2 <= v <= 0.95:
        return True
    return 0.62 <= v <= 0.97 and abs(u) <= (v - 0.62) * 1.35 + 0.05


def spade(u, v):
    if stem(u, v):
        return True
    w, z = u, -(v - 0.12) * 1.0        # a heart upside down, a little up
    if (w + 0.46) ** 2 + (z + 0.3) ** 2 <= 0.47 ** 2 or (w - 0.46) ** 2 + (z + 0.3) ** 2 <= 0.47 ** 2:
        return v <= 0.62
    return -0.25 <= z <= 1.05 and abs(w) <= 0.93 * (1.05 - z) / 1.32


def club(u, v):
    if stem(u, v):
        return True
    r = 0.36
    for cx, cy in ((0, -0.5), (-0.48, 0.08), (0.48, 0.08)):
        if (u - cx) ** 2 + (v - cy) ** 2 <= r * r:
            return True
    return u * u + (v + 0.05) ** 2 <= 0.22 ** 2


SHAPES = [club, spade, heart, diamond]
SOLID = [True, True, False, False]


def raster(shape, size, solid, thick=1):
    """a size x size picture of a suit: solid, or its outline"""
    ss = 5
    on = [[False] * size for _ in range(size)]
    for j in range(size):
        for i in range(size):
            n = 0
            for b in range(ss):
                for a in range(ss):
                    u = ((i + (a + 0.5) / ss) / size) * 2 - 1
                    v = ((j + (b + 0.5) / ss) / size) * 2 - 1
                    n += shape(u, v)
            on[j][i] = n * 2 >= ss * ss
    if solid:
        return on
    inner = on
    for _ in range(thick):
        inner = [[inner[j][i] and 0 < i < size - 1 and 0 < j < size - 1 and inner[j - 1][i]
                  and inner[j + 1][i] and inner[j][i - 1] and inner[j][i + 1]
                  for i in range(size)] for j in range(size)]
    return [[on[j][i] and not inner[j][i] for i in range(size)] for j in range(size)]


# --------------------------------------------------------------- ranks
GLYPHS = {
    'A': ["..###..", ".##.##.", "##...##", "##...##", "##...##", "#######", "#######", "##...##", "##...##", "##...##"],
    '2': [".#####.", "##...##", ".....##", ".....##", "....##.", "...##..", "..##...", ".##....", "##.....", "#######"],
    '3': ["######.", "....##.", "...##..", "..##...", ".####..", ".....##", ".....##", ".....##", "##...##", ".#####."],
    '4': ["....##.", "...###.", "..####.", ".##.##.", "##..##.", "#######", "#######", "....##.", "....##.", "....##."],
    '5': ["#######", "##.....", "##.....", "######.", ".....##", ".....##", ".....##", ".....##", "##...##", ".#####."],
    '6': ["..####.", ".##....", "##.....", "##.....", "######.", "##...##", "##...##", "##...##", "##...##", ".#####."],
    '7': ["#######", ".....##", ".....##", "....##.", "...##..", "...##..", "..##...", "..##...", "..##...", "..##..."],
    '8': [".#####.", "##...##", "##...##", "##...##", ".#####.", "##...##", "##...##", "##...##", "##...##", ".#####."],
    '9': [".#####.", "##...##", "##...##", "##...##", "##...##", ".######", ".....##", ".....##", "....##.", ".####.."],
    'T': [".##..####.", "###.##..##", ".##.##..##", ".##.##..##", ".##.##..##", ".##.##..##", ".##.##..##",
          ".##.##..##", ".##.##..##", "####.####."],
    'J': ["..#####", ".....##", ".....##", ".....##", ".....##", ".....##", ".....##", "##...##", "##...##", ".#####."],
    'Q': [".#####.", "##...##", "##...##", "##...##", "##...##", "##...##", "##.#.##", "##..###", ".#####.", ".....##"],
    'K': ["##...##", "##..##.", "##.##..", "####...", "###....", "####...", "##.##..", "##..##.", "##...##", "##...##"],
}
RANKS = 'A23456789TJQK'


class Pic:
    def __init__(self):
        self.p = [[0] * W for _ in range(H)]

    def set(self, x, y, v=1):
        if 0 <= x < W and 0 <= y < H:
            self.p[y][x] = v

    def blit(self, bm, x0, y0, flip=False, scale=1, dotted=False):
        h, w = len(bm), len(bm[0])
        for j in range(h):
            for i in range(w):
                on = bm[j][i] if not isinstance(bm[j], str) else bm[j][i] == '#'
                if not on:
                    continue
                for b in range(scale):
                    for a in range(scale):
                        x, y = i * scale + a, j * scale + b
                        if flip:
                            x, y = w * scale - 1 - x, h * scale - 1 - y
                        if dotted and (x0 + x + y0 + y) % 2:
                            continue
                        self.set(x0 + x, y0 + y)

    def outline(self, dotted=False):
        for y in range(H):
            for x in range(W):
                if not card_inside(x, y):
                    continue
                edge = x in (0, W - 1) or y in (0, H - 1) or not all(
                    card_inside(x + dx, y + dy) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))
                if edge and (not dotted or (x + y) % 2 == 0):
                    self.p[y][x] = 1

    def rotated_copy(self, x0, y0, x1, y1):
        """the area [x0, x1) x [y0, y1) again, turned half round"""
        for y in range(y0, y1):
            for x in range(x0, x1):
                if self.p[y][x]:
                    self.p[H - 1 - y][W - 1 - x] = 1

    def cols(self):
        out = bytearray()
        for c in range(7):
            for y in range(H):
                b = 0
                for k in range(8):
                    b = b << 1 | self.p[y][c * 8 + k]
                out.append(b)
        return out


# the small pips, drawn by hand (at 9 x 9 a computed club looks like a spade)
PIP = [
    ["...###...", "..#####..", "..#####..", ".##.#.##.", "#########", "#########", ".##.#.##.",
     "....#....", "..#####.."],
    ["....#....", "...###...", "..#####..", ".#######.", "#########", "#########", ".##.#.##.",
     "....#....", "..#####.."],
    [".##...##.", "#..#.#..#", "#...#...#", "#.......#", ".#.....#.", "..#...#..", "...#.#...",
     "....#....", "........."],
    ["....#....", "...#.#...", "..#...#..", ".#.....#.", "#.......#", ".#.....#.", "..#...#..",
     "...#.#...", "....#...."],
]
BIG = [raster(s, 27, SOLID[i], thick=2) for i, s in enumerate(SHAPES)]
MID = [raster(s, 21, SOLID[i]) for i, s in enumerate(SHAPES)]
MIDO = [raster(s, 21, False) for s in SHAPES]

# pip places (column, fraction down) of the number cards
LX = {'L': 15, 'C': 28, 'R': 41}
PIPS = {
    2: [('C', 0), ('C', 1)],
    3: [('C', 0), ('C', .5), ('C', 1)],
    4: [('L', 0), ('R', 0), ('L', 1), ('R', 1)],
    5: [('L', 0), ('R', 0), ('C', .5), ('L', 1), ('R', 1)],
    6: [('L', 0), ('R', 0), ('L', .5), ('R', .5), ('L', 1), ('R', 1)],
    7: [('L', 0), ('R', 0), ('C', .25), ('L', .5), ('R', .5), ('L', 1), ('R', 1)],
    8: [('L', 0), ('R', 0), ('C', .25), ('L', .5), ('R', .5), ('C', .75), ('L', 1), ('R', 1)],
    9: [(c, f) for c in 'LR' for f in (0, 1 / 3, 2 / 3, 1)] + [('C', .5)],
    10: [(c, f) for c in 'LR' for f in (0, 1 / 3, 2 / 3, 1)] + [('C', 1 / 6), ('C', 5 / 6)],
}
Y0, Y1 = 24, 56


def face(suit, rank):
    p = Pic()
    p.outline()
    g = GLYPHS[RANKS[rank]]
    p.blit(g, 4, 4)
    p.blit(PIP[suit], 15, 4)
    p.rotated_copy(0, 0, 25, 15)
    if rank == 0:
        p.blit(BIG[suit], 28 - 13, 40 - 13)
    elif rank < 10:
        for col, f in PIPS[rank + 1]:
            cy = round(Y0 + f * (Y1 - Y0))
            p.blit(PIP[suit], LX[col] - 4, cy - 4, flip=cy > 40)
    else:
        for x in range(11, 45):
            p.set(x, 17)
            p.set(x, 62)
        for y in range(17, 63):
            p.set(11, y)
            p.set(44, y)
        for x in range(13, 43, 2):
            p.set(x, 19)
            p.set(x + 1, 60)
        p.blit(g, 28 - 7, 40 - 10, scale=2)
        p.blit(PIP[suit], 28 - 4, 21)
        p.blit(PIP[suit], 28 - 4, 50, flip=True)
    return p


def back():
    p = Pic()
    p.outline()
    for y in range(H):
        for x in range(W):
            if not card_inside(x, y):
                continue
            inset = min(x, y, W - 1 - x, H - 1 - y)
            if inset == 3:
                p.set(x, y)
            elif inset >= 5:
                if (x + y) % 6 == 0 or (x - y) % 6 == 0:
                    p.set(x, y)
    # a small plain diamond in the middle
    for y in range(H):
        for x in range(W):
            d = abs(x - 27.5) / 9 + abs(y - 39.5) / 13
            if d <= 1:
                p.p[y][x] = 1 if d > 0.82 else 0
    return p


def place(kind, suit=0):
    p = Pic()
    p.outline(dotted=True)
    if kind == 'found':
        p.blit(MIDO[suit], 28 - 10, 40 - 10)
    elif kind == 'king':
        p.blit(GLYPHS['K'], 28 - 7, 40 - 10, scale=2, dotted=True)
    elif kind == 'redeal':
        for y in range(H):
            for x in range(W):
                d = ((x - 27.5) ** 2 + (y - 39.5) ** 2) ** 0.5
                if 9.5 <= d <= 12 and not (x > 28 and y < 36):
                    p.set(x, y)
        for i in range(6):          # the arrow's head
            for k in range(-i, i + 1):
                p.set(30 + i, 27 + k)
    return p


def masks():
    """the pixels outside the rounded corners: (column, row) -> bits"""
    m = {}
    for c in (0, 6):
        for y in list(range(R)) + list(range(H - R, H)):
            b = 0
            for k in range(8):
                b = b << 1 | (not card_inside(c * 8 + k, y))
            m[(c, y)] = b
    return m


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    pics = [face(s, r) for s in range(4) for r in range(13)]
    pics.append(back())
    pics.append(place('empty'))
    pics += [place('found', s) for s in range(4)]
    pics.append(place('king'))
    pics.append(place('redeal'))
    data = [p.cols() for p in pics]
    nrec = (len(data) + PER_RECORD - 1) // PER_RECORD
    for r in range(nrec):
        with open(os.path.join(out, 'cards.%d' % (r + 1)), 'wb') as f:
            for d in data[r * PER_RECORD:(r + 1) * PER_RECORD]:
                f.write(d)
    m = masks()
    with open(os.path.join(out, 'cards.h'), 'w') as f:
        f.write('/* made by tools/mkcards.py */\n')
        f.write('#define PIC_BACK 52\n#define PIC_EMPTY 53\n#define PIC_FOUND 54\n'
                '#define PIC_KING 58\n#define PIC_REDEAL 59\n#define PICS %d\n' % len(pics))
        f.write('#define PIC_RECORDS %d\n#define PIC_PER_RECORD %d\n#define PIC_BYTES %d\n'
                % (nrec, PER_RECORD, W * H // 8))
        f.write('#define CORNER_ROWS %d\n' % R)
        for c in (0, 6):
            f.write('#define MASK_TOP%d {%s}\n' % (c, ', '.join('0x%02X' % m[(c, y)] for y in range(R))))
            f.write('#define MASK_BOT%d {%s}\n' % (c, ', '.join('0x%02X' % m[(c, y)] for y in range(H - R, H))))
    try:
        from PIL import Image
        cols = 15
        rows = (len(pics) + cols - 1) // cols
        img = Image.new('L', (cols * (W + 4) + 4, rows * (H + 4) + 4), 160)
        for i, p in enumerate(pics):
            x0, y0 = 4 + (i % cols) * (W + 4), 4 + (i // cols) * (H + 4)
            for y in range(H):
                for x in range(W):
                    if card_inside(x, y):
                        img.putpixel((x0 + x, y0 + y), 0 if p.p[y][x] else 255)
        img = img.resize((img.width * 2, img.height * 2), Image.NEAREST)
        img.save(os.path.join(out, 'cards.png'))
    except ImportError:
        pass


if __name__ == '__main__':
    main()
