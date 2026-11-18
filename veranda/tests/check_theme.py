#!/usr/bin/env python3
"""The shell's colours from the theme (DECISIONS V65; Veranda Look 2, V74),
read from an x16emu dump's VRAM (-dump V): the colour map at $1AC00 (128 x
64 entries of 2 bytes: the tile index, the attribute; a card's paper is
the attribute's high nibble; a card under a menu or a dialog box has
attribute 0 and its own colour in the tile index's high nibble, the
covers in the low one: V33), the palette, the pointer's sprite.

usage: check_theme.py bars <dump> <accent> <desk> [<hilite> [<dark>]]
           the map shown (Shell 2, V70, in Look 2: V74, V77); card rows 0-1
           (the menu bar, V74): the mark's cards 0-1 cyan (11), the rest
           the chrome (2), nothing covered; window 0 in its default place
           (cards 2-61, rows 4-49): the title band the accent, the toolbar
           and the status bar the chrome, the icons toggle the selection
           colour (the highlight, else 4), the search field the paper, the
           sidebar the sidebar colour (3) but for at most one place (12 x
           2) in the selection colour, the contents the paper but for the
           icons' tiles (4 x 4 cards of one colour each) and the selected
           name's cards; the desktop the desktop colour but for its icons'
           tiles (the card chrome, the printer and the trash the sidebar
           colour) and labels; palette entries 0-15 Look 2's (light, 0 and
           1 the default paper and ink $FFF $223; dark 1: all 16 the dark
           palette)
       check_theme.py tiles <dump> <i:colour,...> [<i:tile.txt> ...]
           window 0's icon i's tile: its 4 x 4 cards that colour; its
           pixels the tile file's (tools/vpack.py) in the rounded frame
       check_theme.py menu <dump> <hilite>
           the main menu's item 0 open (its sub menu under it): the bar's
           cards from 2 to the item's end in the highlight colour, the
           mark's cyan, the rest the chrome, none covered; cards under the
           sub menu (from row 2) covered
       check_theme.py prefs <out> <accent> <desk> <hilite> <preset> [<look>]
           a settings file as Preferences writes it ("Veranda Prefs.cvt":
           a CVT head of 508 bytes, here zeros, which the kernel skips;
           the block: Veranda's defaults, the theme, byte 14 the look:
           default 128, Look 2's colours; 0: a file saved before V74, the
           old UI palette's; + 1 dark)
       check_theme.py none <dump> [<dark>]
           no colour map (the Classic theme): VERA's layer 1 off, palette
           entries 0 and 1 the paper and ink (light $FFF $223)
       check_theme.py pointer <dump>
           sprite 0's slot: tools/mkpointers.py's shapes (Look 2's arrow,
           white with a dark outline, and the busy pointer, V74); palette
           entries 17-20 their colours; sprite 0 shows the arrow (16 x 16,
           palette offset 1)
"""
import os
import sys

VDUMP = 132878
VRAM, COMPOSER, PALETTE = 131072, 256, 512
MAP = 0x1AC00
SPRITES, SPRATTR = 0x16C00, 0x1FC00
# Veranda Look 2's palette (sdk/lib/theme.c; the proposal's LIGHT, DARK)
LIGHT = [0xFFF, 0x223, 0xDDE, 0xEEF, 0x9CF, 0xACE, 0xFD5, 0xF87, 0x8D9, 0xB9F, 0xFA5, 0x6CE, 0x36A, 0x8BE,
         0x5A7, 0xDAC]
DARK = [0x334, 0xEEF, 0x223, 0x223, 0x357, 0x346, 0xA82, 0xA44, 0x385, 0x649, 0xA62, 0x278, 0x124, 0x247,
        0x253, 0x535]
CHROME, CYAN = 2, 11


def section(path):
    d = open(path, 'rb').read()
    return d[len(d) - VDUMP:]


def pal(v, i):
    p = v[VRAM + COMPOSER:]
    return p[2 * i] | p[2 * i + 1] << 8


def card(v, x, y):
    """(attribute colour, tile) of card x, y"""
    a = MAP + 2 * (y * 128 + x)
    return v[a + 1] >> 4, v[a]


def fail(m):
    print('FAIL ', m)
    sys.exit(1)


def map_on(v):
    return bool(v[VRAM] & 0x20)


def colours(v, dark=0, upto=2):
    """palette entries 0 .. upto - 1 Look 2's"""
    want = DARK if dark else LIGHT
    got = [pal(v, i) for i in range(16)]
    if got[:upto] != want[:upto]:
        fail('palette entries 0-%d %s: want Look 2\'s %s %s' % (upto - 1, ' '.join('$%03X' % c for c in got[:upto]),
             'dark' if dark else 'light', ' '.join('$%03X' % c for c in want[:upto])))


def bar(v, hilite=None, upto=None):
    """the menu bar's cards: the mark's cyan, item 0's (2 .. upto) the
    highlight colour, the rest the chrome; none covered"""
    for y in range(2):
        for x in range(80):
            c, t = card(v, x, y)
            want = CYAN if x < 2 else hilite if upto is not None and x <= upto else CHROME
            if c != want or t:
                fail('menu bar card %d,%d: colour %d tile $%02X, want %d, uncovered' % (x, y, c, t, want))


# Shell 2 in Veranda Look 2 (DECISIONS V70, V77): window 0 in its default
# place, cards 2-61 by rows 4-49 (x 16-495, y 32-399): the title band
# (rows 4-5), the toolbar (6-8: its icons and list toggle cards 40-45, the
# search field cards 47-59 rows 6-7), the sidebar (cards 2-13, rows
# 9-47), the contents (cards 14-61), the status bar (rows 48-49); the
# icon cells 9 x 9 cards from card 15, row 10 (the tile 2 cards in, the
# name in the cell's rows 4-5). The desktop's icons: tiles at cards 72-75
# from row 4, 7 rows apart (the card, the printer, the trash), their
# labels in the 2 rows under them
WX0, WX1, WY0, WY1 = 2, 61, 4, 49
SIDEBAR, SELECT = 3, 4
DESKT = [(4, CHROME), (11, SIDEBAR), (18, SIDEBAR)]


def tile_at(x, y):
    """(the cell's column, row) if card x, y is in an icon cell's tile"""
    if x >= 17 and y >= 10 and (x - 17) % 9 < 4 and (y - 10) % 9 < 4:
        return (x - 17) // 9, (y - 10) // 9
    return None


def bars(v, accent, desk, hilite=None, dark=0):
    colours(v, dark, 16)
    if not map_on(v):
        fail('the colour map is hidden')
    bar(v)
    sel = hilite or SELECT
    tiles, label, place = {}, set(), set()
    for y in range(2, 60):
        for x in range(80):
            c, t = card(v, x, y)
            if t:
                fail('card %d,%d covered (tile $%02X)' % (x, y, t))
            where = None
            if not (WX0 <= x <= WX1 and WY0 <= y <= WY1):
                want = desk
                if 52 <= y <= 57:                                       # the dock (V83): any
                    want, where = c, 'the dock'
                if y >= 25 and (y - 4) % 7 < 6 and 68 <= x <= 79 and y < 52:   # the pins: any tile
                    if 72 <= x <= 75 and (y - 4) % 7 < 4:
                        want, where = c, 'a pin\'s tile'
                        tiles.setdefault(('pin', (y - 4) // 7), set()).add(c)
                    elif (y - 4) % 7 >= 4 and c in (0, sel):
                        want, where = c, 'a pin\'s label'
                for ty, tc in DESKT:
                    if 72 <= x <= 75 and ty <= y < ty + 4:
                        want, where = tc, 'the desktop\'s tile'
                    elif 68 <= x <= 79 and ty + 4 <= y < ty + 6 and c in (0, sel):
                        want, where = c, 'the desktop\'s label'
                where = where or 'the desktop'
            elif y < WY0 + 2:
                want, where = accent, 'the title band'
            elif y < WY0 + 5:
                want, where = CHROME, 'the toolbar'
                if 40 <= x <= 42:
                    want, where = sel, 'the icons toggle (shown)'
                elif 47 <= x <= 59 and y < WY0 + 4:
                    want, where = 0, 'the search field'
            elif y >= WY1 - 1:
                want, where = CHROME, 'the status bar'
            elif x <= 13:
                want, where = SIDEBAR, 'the sidebar'
                if c == sel:
                    place.add((x, y))
                    want = sel
            else:
                want, where = 0, 'the contents'
                if tile_at(x, y):
                    tiles.setdefault(tile_at(x, y), set()).add(c)
                    continue
                if c == sel and (y - 10) % 9 in (4, 5):
                    label.add((x, y))
                    want = sel
            if c != want:
                fail('%s: card %d,%d colour %d, want %d' % (where, x, y, c, want))
    tiles = {k: c for k, c in tiles.items() if c != {0}}
    for k, cs in tiles.items():
        if len(cs) != 1:
            fail('the tile of cell %d,%d: its cards have colours %s, not one' % (k[0], k[1], sorted(cs)))
    if place and (len(place) != 24 or len({y for _, y in place}) != 2):
        fail('the sidebar: %d cards in the selection colour, not a place (12 x 2)' % len(place))
    if label and len({y for _, y in label}) != 2:
        fail('the contents: the selection\'s name on rows %s' % sorted({y for _, y in label}))
    print('ok    the colour map (Look 2, V77): the menu bar the chrome, its mark cyan; window 0\'s title band %d, '
          'its toolbar and status bar the chrome, the icons toggle %d, the search field the paper, the sidebar %d%s; '
          '%d tiles each of one colour (%s)%s; the desktop %d with its icons\' tiles; palette entries 0-15 Look 2\'s %s'
          % (accent, sel, SIDEBAR, ', a place in %d' % sel if place else '', len(tiles),
             ' '.join(str(min(c)) for _, c in sorted(tiles.items(), key=str)),
             ', the selected name\'s %d cards %d' % (len(label), sel) if label else '', desk,
             'dark palette' if dark else 'light palette'))


def tiles(v, want, *files):
    """the tiles of window 0's icons: want 'i:colour,...' (icon i's tile's
    cards all that colour); files 'i:tile.txt': icon i's pixels that tile
    in its rounded frame (tools/vpack.py's source form)"""
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools'))
    import vpack
    frame = [0x1FFFFFF8, 0x60000006, 0x40000002] + [0x80000001] * 26 + [0x40000002, 0x60000006, 0x1FFFFFF8]
    for item in want.split(','):
        i, col = (int(x) for x in item.split(':'))
        cx, cy = 17 + (i % 5) * 9, 10 + (i // 5) * 9
        got = {card(v, x, y)[0] for x in range(cx, cx + 4) for y in range(cy, cy + 4)}
        if got != {col}:
            fail('icon %d: its tile\'s cards %s, want %d' % (i, sorted(got), col))
    for f in files:
        i, path = f.split(':', 1)
        i = int(i)
        cx, cy = 17 + (i % 5) * 9, 10 + (i // 5) * 9
        bm = vpack.tile_bitmap(vpack.read_tiles(path)[0][2])
        for r in range(32):
            row = int.from_bytes(bm[4 * r:4 * r + 4], 'big') | frame[r]
            got = int.from_bytes(v[(cy * 8 + r) * 80 + cx:(cy * 8 + r) * 80 + cx + 4], 'big')
            if got != row:
                fail('icon %d: tile row %d %08X, want %08X (%s in its frame)' % (i, r, got, row, path))
    print('ok    window 0\'s tiles: %s (icon:colour)%s' % (want, '; the pixels of ' + ', '.join(files) if files else ''))


def menu(v, hilite):
    colours(v, 0, 16)
    if not map_on(v):
        fail('the colour map is hidden')
    upto = 2                            # item 0's end: its last card in the highlight colour
    while upto < 79 and card(v, upto + 1, 0)[0] == hilite:
        upto += 1
    if upto < 3:
        fail('the bar\'s item 0 is not shown open (its cards in colour %d)' % hilite)
    bar(v, hilite, upto)
    cov = sum(1 for y in range(2, 8) for x in range(80) if card(v, x, y)[1] & 15 and card(v, x, y)[0] == 0)
    if not cov:
        fail('no card under the sub menu is covered: is it open?')
    print('ok    the main menu\'s item 0 open: its bar cards 2-%d in the highlight colour %d, the mark cyan, the '
          'rest the chrome (the bar not covered); %d cards under the sub menu covered' % (upto, hilite, cov))


def none(v, dark=0):
    colours(v, dark)
    if map_on(v):
        fail('the colour map is shown (DC_VIDEO $%02X)' % v[VRAM])
    print('ok    no colour map (DC_VIDEO $%02X), Look 2\'s %s paper and ink: the classic look'
          % (v[VRAM], 'dark' if dark else 'light'))


def pointer(v):
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools'))
    import mkpointers
    base = SPRITES + mkpointers.SLOT_OFFSET
    for i, (name, rows, _, _) in enumerate(mkpointers.shapes()):
        if v[base + 128 * i:base + 128 * (i + 1)] != mkpointers.packed(rows):
            fail('sprite 0\'s slot: %s is not tools/mkpointers.py\'s' % name)
    p = [pal(v, i) for i in range(17, 21)]
    if p != mkpointers.PAL:
        fail('palette entries 17-20 %s: want %s' % (p, mkpointers.PAL))
    out = {(x, y) for y, r in enumerate(mkpointers.ARROW) for x, c in enumerate(r) if c == '1'}
    fill = {(x, y) for y, r in enumerate(mkpointers.ARROW) for x, c in enumerate(r) if c == '2'}
    lum = [((c >> 8) + (c >> 4 & 15) + (c & 15)) for c in mkpointers.PAL]
    if not fill or lum[1] != 45 or lum[0] > 9 or any((x + dx, y + dy) not in out | fill for x, y in fill
                                                      for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
        fail('the arrow is not white with a dark outline all round')
    a = v[SPRATTR:SPRATTR + 8]
    if (a[0] | a[1] << 8) & 0xFFF != base >> 5 or a[7] != 0x51 or a[6] & 0x0C == 0:
        fail('sprite 0 does not show the arrow (attributes %s)' % a.hex())
    print('ok    the pointer: sprite 0 shows the arrow (16 x 16, 4 bpp, palette offset 1), white $%03X with a dark '
          'outline $%03X all round; its slot holds the %d shapes (the busy pointer\'s %d pictures)'
          % (mkpointers.PAL[1], mkpointers.PAL[0], len(mkpointers.shapes()), mkpointers.BUSY_FRAMES))


def prefs(out, accent, desk, hilite, preset, look=0x80):
    block = bytes([0xFF, 0x0F, 0x23, 0x02, 600 & 255, 600 >> 8, 0, 0xFF, 0, 0, accent, desk, hilite, 0x80 | preset,
                   look, 0])
    open(out, 'wb').write(bytes(508) + block)


def main():
    a = sys.argv[1:]
    if len(a) < 2:
        sys.exit(__doc__)
    if a[0] == 'prefs':
        prefs(a[1], *[int(x) for x in a[2:]])
        return
    v = section(a[1])
    if a[0] == 'tiles':
        tiles(v, *a[2:])
        return
    n = [int(x) for x in a[2:]]
    {'bars': bars, 'menu': menu, 'none': none, 'pointer': pointer}[a[0]](v, *n)


if __name__ == '__main__':
    main()
