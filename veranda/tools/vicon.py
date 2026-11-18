#!/usr/bin/env python3
"""A GEOS file icon (24x21, 1 bpp, 63 bytes) from a PBM or PNG picture.

usage: vicon.py <in.pbm|in.png> <out.bin>
       vicon.py --default <out.bin>     Veranda's default application icon

Black (or dark) pixels are ink. The picture must be 24x21. PBM (P1 and P4)
is read here; PNG needs Pillow.
"""
import sys

W, H = 24, 21


def default():
    rows = []
    for y in range(H):
        r = ''
        for x in range(W):
            r += '#' if y in (0, H - 1) or x in (0, W - 1) else '.'
        rows.append(r)
    # a "V" inside the frame
    for y in range(4, 17):
        d = (y - 4) * 5 // 12
        for x in (5 + d, 6 + d, 17 - d, 18 - d):
            rows[y] = rows[y][:x] + '#' + rows[y][x + 1:]
    return rows


def read_pbm(data):
    toks, i = [], 0
    magic = data[:2]
    # header: magic, width, height (comments allowed)
    pos = 2
    while len(toks) < 2:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b'#':
            while data[pos:pos + 1] not in (b'\n', b''):
                pos += 1
            continue
        start = pos
        while not data[pos:pos + 1].isspace():
            pos += 1
        toks.append(int(data[start:pos]))
    w, h = toks
    if magic == b'P4':
        pos += 1
        stride = (w + 7) // 8
        return w, h, [[(data[pos + y * stride + x // 8] >> (7 - x % 8)) & 1 for x in range(w)] for y in range(h)]
    if magic == b'P1':
        bits = [int(c) for c in data[pos:].decode('ascii') if c in '01']
        return w, h, [bits[y * w:(y + 1) * w] for y in range(h)]
    raise ValueError('not a PBM (P1 or P4)')


def read_png(path):
    from PIL import Image
    img = Image.open(path).convert('L')
    return img.width, img.height, [[1 if img.getpixel((x, y)) < 128 else 0 for x in range(img.width)]
                                   for y in range(img.height)]


def icon_bytes(path):
    """63 bytes"""
    if path is None:
        px = [[1 if c == '#' else 0 for c in r] for r in default()]
    elif path.lower().endswith('.png'):
        w, h, px = read_png(path)
    else:
        w, h, px = read_pbm(open(path, 'rb').read())
    if len(px) != H or any(len(r) != W for r in px):
        raise ValueError('%s: an icon is 24x21' % path)
    return bytes(sum(px[y][8 * b + i] << (7 - i) for i in range(8)) for y in range(H) for b in range(3))


def main():
    a = sys.argv[1:]
    if len(a) == 2 and a[0] == '--default':
        open(a[1], 'wb').write(icon_bytes(None))
    elif len(a) == 2:
        open(a[1], 'wb').write(icon_bytes(a[0]))
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main()
