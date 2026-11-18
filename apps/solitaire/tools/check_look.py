#!/usr/bin/env python3
"""Veranda Solitaire in Veranda Look 2 (Veranda's DECISIONS V74, V77, V79):
the colours of a screen.

    check_look.py look DUMP [COVER]
    check_look.py tile CVT TILES NAME

DUMP: an x16emu dump (-dump V or RV). Checked: the colour map shown;
palette entries 0-15 Look 2's light ones (theme_palette, no settings
file); the menu bar's cards (rows 0-1) the chrome (2) and the Veranda
mark's two the cyan (11), none covered (the kernel's bar: theme_menus);
the status bar's (rows 58-59) the chrome. With COVER (host/soltool
render's OUT.cover: the cards a card picture reaches into, a byte each,
80 x 60): every card of the table's rows (2-57) the felt (14, HILL) but
those, which keep the normal paper (0): the cards stay black and white.
A card under a dialog box counts by its own colour (Veranda V33: kept in
its tile index).

tile: the CVT carries a tile (its info block's flags bit 1; the tile
block in its first block: the colour at 96, the 32 x 32 bitmap at 97-224,
Veranda's docs/APPFORMAT.md) equal to the tile NAME of Veranda's
shell/tiles.txt (TILES), the one the shell shows for the class.
GPL-3.0-or-later (see COPYING).
"""
import sys

VDUMP = 132878
VRAM, COMPOSER = 131072, 256
MAP = 0x1AC00
LIGHT = [0xFFF, 0x223, 0xDDE, 0xEEF, 0x9CF, 0xACE, 0xFD5, 0xF87, 0x8D9, 0xB9F, 0xFA5, 0x6CE, 0x36A, 0x8BE,
         0x5A7, 0xDAC]
CHROME, CYAN, FELT = 2, 11, 14
ROW0, ROW1, STATUS = 2, 58, 58


def fail(m):
    print('FAIL ', m)
    sys.exit(1)


COLOURS = ['paper', 'ink', 'chrome', 'sidebar', 'select', 'title', 'yellow', 'red', 'green', 'purple',
           'orange', 'cyan', 'sky', 'sky2', 'hill', 'dusk']


def tile(cvt, tiles, name):
    """the shell's tile NAME (its colour, its 128 bytes) against the CVT's"""
    rows, colour, cur = [], None, None
    for line in open(tiles, encoding='utf-8'):
        w = line.split(None, 1)
        if w and w[0] == 'tile':
            cur = w[1].strip()
            continue
        if cur != name:
            continue
        if w and w[0] == 'colour':
            colour = COLOURS.index(w[1].strip())
        elif len(line.rstrip()) == 32 and not set(line.rstrip()) - set('#.'):
            rows.append(line.rstrip())
    if len(rows) != 32 or colour is None:
        fail('no tile "%s" in %s' % (name, tiles))
    want = bytes(sum(0x80 >> i for i, ch in enumerate(r[x:x + 8]) if ch == '#') for r in rows for x in range(0, 32, 8))
    d = open(cvt, 'rb').read()
    if not d[254 + 141] & 2:
        fail('%s: no tile block (flags $%02X)' % (cvt, d[254 + 141]))
    if d[96] != colour or d[97:225] != want:
        fail('%s: its tile (colour %d) is not the shell\'s "%s" (colour %d)' % (cvt, d[96], name, colour))
    print('ok    the CVT\'s tile is the shell\'s "%s" (%s, %d ink pixels)' % (name, COLOURS[colour],
                                                                         sum(bin(b).count('1') for b in want)))


def main():
    if len(sys.argv) == 5 and sys.argv[1] == 'tile':
        return tile(*sys.argv[2:])
    if len(sys.argv) not in (3, 4) or sys.argv[1] != 'look':
        sys.exit(__doc__)
    d = open(sys.argv[2], 'rb').read()
    v = d[len(d) - VDUMP:]
    if not v[VRAM] & 0x20:
        fail('the colour map is not shown')
    p = v[VRAM + COMPOSER:]
    pal = [p[2 * i] | p[2 * i + 1] << 8 for i in range(16)]
    if pal != LIGHT:
        fail('palette entries 0-15 %s: want Look 2\'s %s' % (' '.join('$%03X' % c for c in pal),
                                                          ' '.join('$%03X' % c for c in LIGHT)))

    def card(x, y):
        tile, attr = v[MAP + 2 * (y * 128 + x):][:2]
        return attr >> 4, tile

    def own(x, y):
        c, t = card(x, y)
        return t >> 4 if t & 15 else c

    for y in range(2):
        for x in range(80):
            c, t = card(x, y)
            want = CYAN if x < 2 else CHROME
            if c != want or t:
                fail('menu bar card %d,%d: colour %d tile $%02X, want %d, uncovered' % (x, y, c, t, want))
    for y in range(STATUS, 60):
        for x in range(80):
            if own(x, y) != CHROME:
                fail('status bar card %d,%d: colour %d, want %d (the chrome)' % (x, y, own(x, y), CHROME))
    out = 'the bar chrome (the mark cyan), the status bar chrome, palette entries 0-15 Look 2\'s'
    if len(sys.argv) == 4:
        cover = open(sys.argv[3], 'rb').read()
        bad, felt, paper = [], 0, 0
        for y in range(ROW0, ROW1):
            for x in range(80):
                want = 0 if cover[y * 80 + x] else FELT
                if own(x, y) != want:
                    bad.append((x, y, own(x, y), want))
                elif want:
                    felt += 1
                else:
                    paper += 1
        if bad:
            for x, y, c, w in bad[:6]:
                print('      card %d,%d: colour %d, want %d' % (x, y, c, w))
            fail('%d cards of the table wrong' % len(bad))
        out += '; the table %d cards of felt (%d), the %d the cards reach into the normal paper' % (felt, FELT, paper)
    print('ok   ', out)


if __name__ == '__main__':
    main()
