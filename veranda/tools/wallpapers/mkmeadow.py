#!/usr/bin/env python3
"""Meadow.cvt, the Meadow desktop picture as a Veranda wallpaper.

usage: mkmeadow.py [-o out.cvt]        (default: Meadow.cvt; needs Pillow)

Paints the picture and snaps it to the cards (meadow.py), then converts
it as tools/mkwall.py does, with one difference: the wallpaper's own palette entries 12-15 start from
the picture's colours (deep sky, sunlit hill, hill shade, meadow; a darker
set for dark mode) instead of k-means' choice, so they keep their colours
exactly. Original art made for Veranda (MIT, like this tree).
"""
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))
import meadow  # noqa: E402
import mkwall  # noqa: E402

# the start colours of entries 12-15, light then dark
SEED = {'light': [(51, 119, 221), (136, 221, 51), (85, 170, 34), (170, 221, 68)],
        'dark': [(17, 51, 119), (51, 102, 17), (34, 68, 17), (85, 119, 34)]}


def main():
    a = sys.argv[1:]
    out = 'Meadow.cvt'
    if a[:1] == ['-o'] and len(a) == 2:
        out = a[1]
    elif a:
        sys.exit(__doc__)
    calls = []
    kmeans = mkwall.kmeans

    def seeded(points, n, rounds=10):
        calls.append(1)
        s = SEED['light' if len(calls) == 1 else 'dark']
        return s[:n] if n <= len(s) else kmeans(points, n, rounds)

    mkwall.kmeans = seeded
    with tempfile.TemporaryDirectory() as d:
        png = os.path.join(d, 'meadow.png')
        meadow.picture().save(png)
        px = mkwall.picture_pixels(png)
    try:
        cvt = mkwall.make(px, 'Meadow', 0.55, log=lambda s: None)
    except (ValueError, OSError, mkwall.vpack.PackError) as e:
        sys.exit('mkmeadow: %s' % e)
    open(out, 'wb').write(cvt)


if __name__ == '__main__':
    main()
