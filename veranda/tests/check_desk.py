#!/usr/bin/env python3
"""gate desk: Paint, a desk accessory, Paint again (tests/desk_script.py,
tests/calc_script.py).

usage: check_desk.py <run dir: console.txt, PRE.RAW, POST.RAW, dump.bin>
       check_desk.py --calc <dump.bin>   (the Calculator open)

PRE.RAW and POST.RAW: both screens (foreground, then back screen; 480
rows of 80 bytes each) when Paint opened the accessory and when GetFile
returned to it.
"""
import os
import re
import sys

VDUMP = 132878
failures = 0


def check(cond, what):
    global failures
    print(('ok    ' if cond else 'FAIL  ') + what)
    if not cond:
        failures += 1


def calc(path):
    """the Calculator open over Paint's colours: the map hidden while it
    runs (DECISIONS V33; its VRAM put aside with the rest), Veranda's
    colours: the composed display slate ink on white (V74)"""
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools'))
    import vram2png
    v = vram2png.section(path)
    lay0 = v[131072 + 256 + 512:][:7]
    check(v[131072] & 0x30 == 0x10 and lay0[0] & 7 == 4, 'the Calculator open: the colour map hidden (layer 0 the '
          'bitmap, layer 1 off)')
    m = sum(1 for i in range(80 * 60) if v[0x1AC00 + 2 * ((i // 80) * 128 + i % 80) + 1] >> 4 == 2)
    check(m > 20, 'and kept in VRAM (%d cards in colour 2: put aside, then back with the rest)' % m)
    pic = vram2png.compose(v)
    n = sum(1 for row in pic for px in row if px not in ((0x22, 0x22, 0x33), (255, 255, 255)))
    check(n == 0 and vram2png.rgb(v, 0) == (255, 255, 255) and vram2png.rgb(v, 1) == (0x22, 0x22, 0x33),
          'the composed display: slate ink on white (V74), palette entries 0 and 1 Veranda\'s (%d pixels in another colour)' % n)
    sys.exit(1 if failures else 0)


def main():
    if sys.argv[1] == '--calc':
        calc(sys.argv[2])
    d = sys.argv[1]
    con = open(os.path.join(d, 'console.txt'), 'rb').read().decode('latin-1')
    con = re.sub(r'\\X[0-9A-F]{2}', '', con).replace('\r', '')
    want = ['PAINT RUNNING', 'CALC RUNNING', 'CALC = 42', 'CALC = 84', 'CALC CLOSED', 'PAINT DESK BACK']
    p, ok = 0, True
    for m in want:
        p = con.find(m, p)
        if p < 0:
            ok = False
            break
    check(ok, 'the console in order: %s' % ', '.join(want))
    t = dict((k, int(c, 16) / 8000) for k, c in re.findall(r'TIME (open|back) ([0-9A-F]{8})', con))
    if len(t) == 2:
        print('      GetFile to the accessory\'s main (Paint put aside, the accessory loaded): %.0f ms; RstrAppl '
              'to GetFile\'s return in Paint: %.0f ms' % (t['open'], t['back']))
    pre = open(os.path.join(d, 'PRE.RAW'), 'rb').read()
    post = open(os.path.join(d, 'POST.RAW'), 'rb').read()
    n = sum(1 for a, b in zip(pre, post) if a != b)
    check(len(pre) == len(post) == 76800 and n == 0,
          'the screens when GetFile returned equal those when the accessory was opened (both, 76,800 bytes; '
          '%d differ)' % n)
    pm = open(os.path.join(d, 'PRE.MAP'), 'rb').read()
    qm = open(os.path.join(d, 'POST.MAP'), 'rb').read()
    check(pm == qm and pm.count(2) > 20, 'the colour map when GetFile returned equals the one before (%d cards in '
          'colour 2; %d differ)' % (pm.count(2), sum(1 for a, b in zip(pm, qm) if a != b)))
    raw = open(os.path.join(d, 'dump.bin'), 'rb').read()
    v = raw[len(raw) - VDUMP:]
    ink = sum(bin(x).count('1') for x in pre[:38400])
    check(ink > 3000, 'the picture before: %d ink pixels' % ink)
    # after: the second rectangle (420,300)-(560,420) in pattern 7 ($AA);
    # everything else as before (both screens)
    l, t, r, b = 420, 300, 560, 420
    bad_in = bad_out = 0
    for s in range(2):
        base = s * 0x9600
        for y in range(480):
            for x in range(640):
                now = v[base + y * 80 + x // 8] >> (7 - x % 8) & 1
                if l <= x <= r and t <= y <= b:
                    bad_in += now != (0xAA >> (7 - x % 8) & 1)
                else:
                    bad_out += now != (pre[s * 38400 + y * 80 + x // 8] >> (7 - x % 8) & 1)
    check(bad_in == 0, 'drawing after the accessory: the filled rectangle with the tool and pattern 7 chosen before '
          '(%d pixels differ)' % bad_in)
    check(bad_out == 0, 'the rest of both screens as before the accessory (%d pixels differ)' % bad_out)
    # the colour map at the end: as before, the second rectangle's cards in colour 2
    now = bytes(v[0x1AC00 + 2 * (y * 128 + x) + 1] >> 4 for y in range(2, 60) for x in range(6, 80))
    exp = bytearray(pm)
    for cy in range(t // 8, b // 8 + 1):
        for cx in range(l // 8, r // 8 + 1):
            exp[(cy - 2) * 74 + cx - 6] = 2
    sw = all(v[0x1AC00 + 2 * ((48 + 2 * (i // 3)) * 128 + 2 * (i % 3)) + 1] >> 4 == i for i in range(16))
    check(v[131072 + 256 + 2:131072 + 256 + 4] == bytes([0x00, 0x08]),
          'the ink after the accessory: Paint\'s red again (palette entry 1; the accessory ran in black)')
    check(now == bytes(exp) and sw, 'the colour map at the end: as before, the new rectangle\'s cards in colour 2, '
          'the colour cells (%d cards differ)' % sum(1 for a, c in zip(now, exp) if a != c))
    sys.exit(1 if failures else 0)


if __name__ == '__main__':
    main()
