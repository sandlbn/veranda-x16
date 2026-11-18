#!/usr/bin/env python3
"""gate archive: checks Archive's run against tools/vdisk.py (the oracle).

usage: check_archive.py log <console.txt> <image>...
           the console's listings (ARCHIVE IMAGE ... and a line a file) are
           vdisk.py's for each image, in order; the files extracted (ARCHIVE
           GOT) are the copyable ones; the Paint documents made (ARCHIVE
           PAINT) are vdisk.py's conversions
       check_archive.py listing <console.txt> <image>
           the console's first listing only
       check_archive.py files <folder> <image> [--paint]
           every file extracted from the image is in the folder (the
           image's own) and equal byte for byte to vdisk.py's; with --paint
           every Paint document converted equals vdisk.py's conversion as a
           picture (the bitmap, the card colours, the ink)
       check_archive.py screen <dump.bin> <rom.bin> <image> <name> <first> [<selected>...]
           the list on the screen (a VRAM dump): the header line, each row's
           text (read back through the ROM's 8 x 8 font), the selected rows'
           names reversed, each row's icon (the info block's, or Archive's
           own for a plain file), the page shown
       check_archive.py paint <folder> <document.cvt>
           Paint opened the document: OPENED.RAW (the drawing area's rows)
           and OPENED.MAP (its cards' colours) are the document's picture
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'tools'))
import vdisk  # noqa: E402

TOP, ROWH, ROWS, STATUSY = 32, 24, 18, 472           # apps/archive/archive.c
VRAM = 131072
TAIL = 132878
DOCICON = bytes([
    0x7F, 0xFF, 0xC0, 0x40, 0x00, 0x60, 0x40, 0x00, 0x50, 0x40, 0x00, 0x78, 0x40, 0x00, 0x08,
    0x40, 0x00, 0x08, 0x40, 0x00, 0x08, 0x40, 0x00, 0x08, 0x40, 0x00, 0x08, 0x40, 0x00, 0x08,
    0x40, 0x00, 0x08, 0x40, 0x00, 0x08, 0x40, 0x00, 0x08, 0x40, 0x00, 0x08, 0x40, 0x00, 0x08,
    0x40, 0x00, 0x08, 0x40, 0x00, 0x08, 0x40, 0x00, 0x08, 0x40, 0x00, 0x08, 0x40, 0x00, 0x08,
    0x7F, 0xFF, 0xF8])


def fail(msg):
    print('check_archive: ' + msg)
    sys.exit(1)


def conversions(im):
    """[(file entry, [(document name, records)])] as Archive converts"""
    out = []
    for e in im.entries():
        if not e.geos:
            continue
        content = im.records(e) if e.vlir else im.data(e)
        if vdisk.classify(e.cls, content, e.vlir) is None:
            continue
        out.append((e, vdisk.to_paint(e.name, e.cls, e.vlir, content)))
    return out


def cmd_log(console, images, only=False):
    lines = [ln.split('ARCHIVE ', 1)[1].rstrip('\r') for ln in
             open(console, 'rb').read().decode('latin-1').replace('\r', '\n').split('\n') if 'ARCHIVE ' in ln]
    pos = 0
    for path in images:
        im = vdisk.Image.load(path)
        want, es = im.listing()
        while pos < len(lines) and not lines[pos].startswith('IMAGE|'):
            pos += 1
        got = lines[pos:pos + len(want)]
        if got != want:
            for a, b in zip(want + [''] * 99, got + [''] * 99):
                if a != b:
                    fail('%s: the listing has %r, vdisk.py %r' % (os.path.basename(path), b, a))
        pos += len(want)
        if only:
            print('check_archive: %s: the listing (%d files) equals vdisk.py\'s' % (os.path.basename(path), len(es)))
            continue
        copied = [e.file for e in es if e.copyable]
        got = [ln[4:] for ln in lines[pos:pos + len(copied)] if ln.startswith('GOT ')]
        if got != copied:
            fail('%s: extracted %s, want %s' % (os.path.basename(path), got, copied))
        pos += len(copied)
        if lines[pos] != 'EXTRACTED %d' % len(copied):
            fail('%s: %r after the files' % (os.path.basename(path), lines[pos]))
        pos += 1
        conv = pos < len(lines) and lines[pos].startswith('PAINT ')
        if conv:
            docs = [n for _, parts in conversions(im) for n, _ in parts]
            got = [ln[6:] for ln in lines[pos:pos + len(docs)]]
            if got != docs:
                fail('%s: Paint documents %s, want %s' % (os.path.basename(path), got, docs))
            pos += len(docs)
            if lines[pos] != 'CONVERTED %d' % len(conversions(im)):
                fail('%s: %r after the conversions' % (os.path.basename(path), lines[pos]))
            pos += 1
        print('check_archive: %s: the listing (%d files) equals vdisk.py\'s; %d files extracted%s' % (
            os.path.basename(path), len(es), len(copied),
            ', its pictures converted (as vdisk.py names them)' if conv else ''))


def cmd_files(folder, path, paint):
    im = vdisk.Image.load(path)
    n = 0
    for e in im.entries():
        if not e.copyable:
            continue
        got = os.path.join(folder, e.file)
        if not os.path.isfile(got):
            fail('%s: no %s' % (os.path.basename(path), e.file))
        if open(got, 'rb').read() != im.extract(e):
            fail('%s: %s differs from vdisk.py\'s' % (os.path.basename(path), e.file))
        n += 1
    docs = 0
    for e, parts in conversions(im) if paint else []:
        for name, recs in parts:
            got = os.path.join(folder, name + '.cvt')
            if not os.path.isfile(got):
                fail('%s: no Paint document %s' % (os.path.basename(path), name))
            a = vdisk.paint_decode(open(got, 'rb').read())
            b = vdisk.paint_decode(vdisk.paint_cvt(name, recs))
            what = ['the bitmap', 'the card colours', 'the ink']
            for i in range(3):
                if a[i] != b[i]:
                    fail('%s: %s: %s differs from vdisk.py\'s' % (os.path.basename(path), name, what[i]))
            e2, info, _ = vdisk.parse_cvt(open(got, 'rb').read())
            if e2[22] != 7 or vdisk.cstr(info[77:97]) != b'Paint Image V1.0' or vdisk.cstr(info[117:137]) != b'Paint       V1.0':
                fail('%s: %s is not a Paint document (type, class, parent)' % (os.path.basename(path), name))
            docs += 1
    print('check_archive: %s: %d files equal vdisk.py\'s byte for byte%s' % (
        os.path.basename(path), n, ', %d Paint documents equal its conversions' % docs if docs else ''))


def rom_font(rom):
    d = open(rom, 'rb').read()[6 * 16384:7 * 16384]
    smiley = bytes([0x7E, 0x81, 0xA5, 0x81, 0xBD, 0x99, 0x81, 0x7E])
    for a in range(0, 16384, 2048):
        if d[a + 8:a + 16] == smiley:
            return d[a:a + 2048]
    fail('no CP437 set in the ROM')


class Screen:
    def __init__(self, dump, rom):
        d = open(dump, 'rb').read()
        if len(d) < TAIL:
            fail('no VRAM in ' + dump)
        self.v = d[len(d) - TAIL:][:VRAM]
        font = rom_font(rom)
        self.glyphs = {}
        for c in list(range(32, 127)) + list(range(256)):
            self.glyphs.setdefault(font[c * 8:c * 8 + 8], c)

    def text(self, x, w, y, rev=False):
        s = ''
        for cx in range(x, x + w):
            cell = bytes(self.v[(y + i) * 80 + cx] ^ (0xFF if rev else 0) for i in range(8))
            c = self.glyphs.get(cell)
            s += '\x7f' if c is None else chr(c)
        return s

    def icon(self, y):
        return bytes(self.v[(y + r) * 80 + 1 + b] for r in range(21) for b in range(3))


def fld(s, w):
    return (s + ' ' * w)[:w]


def cmd_screen(dump, rom, path, name, first, sel):
    im = vdisk.Image.load(path)
    lines, es = im.listing()
    sc = Screen(dump, rom)
    head = '%s  %s  "%s"  %d files  %d blocks free' % (name, im.kind, im.disk_name(), len(es), im.free())
    if sc.text(0, 80, 16) != fld(head, 80):
        fail('the header line reads %r, want %r' % (sc.text(0, 80, 16), head))
    for r in range(ROWS):
        i = first + r
        y0 = TOP + r * ROWH
        y = y0 + 8
        if i >= len(es):
            if any(sc.v[(y0 + k) * 80:(y0 + k + 1) * 80].count(0) != 80 for k in range(ROWH)):
                fail('row %d is not empty' % r)
            continue
        e = es[i]
        f = e.file or ''
        if len(f) > 17:
            f = f[:16] + '>'
        want = [(5, 16, e.name, i in sel), (22, 11, e.kind, False),
                (34, 1, 'V' if e.vlir else 'S' if e.geos else '-', False),
                (36, 5, ('%5d' % e.blocks)[-5:], False), (42, 20, e.cls, False), (63, 17, f, False)]
        for x, w, s, rev in want:
            got = sc.text(x, w, y, rev)
            if got != fld(s, w):
                fail('row %d (%s), cells %d-%d: %r, want %r%s' % (r, e.name, x, x + w - 1, got, fld(s, w),
                                                                    ' (reversed)' if rev else ''))
        icon = e.info[5:68] if e.geos else DOCICON
        if sc.icon(y0 + 1) != icon:
            fail('row %d (%s): the icon is not %s' % (r, e.name, 'its info block\'s' if e.geos else 'Archive\'s own'))
    n = len(es)
    page = 'page %d/%d' % (first // ROWS + 1, max(1, (n + ROWS - 1) // ROWS))
    if sc.text(64, 9, STATUSY) != fld(page, 9):
        fail('the page reads %r, want %r' % (sc.text(64, 9, STATUSY), page))
    print('check_archive: the screen lists %s from file %d as vdisk.py does: %d rows (text, icons%s), %s' % (
        name, first + 1, min(ROWS, n - first), ', %d selected' % len(sel) if sel else '', page))


def cmd_paint(folder, doc):
    bits, cmap, ink = vdisk.paint_decode(open(doc, 'rb').read())
    raw = open(os.path.join(folder, 'OPENED.RAW'), 'rb').read()
    for y in range(vdisk.P_H):
        row = raw[y * 74:(y + 1) * 74]
        got = [row[x // 8] >> (7 - x % 8) & 1 for x in range(vdisk.P_W)]
        if got != bits[y]:
            fail('Paint\'s drawing area, row %d, differs from %s' % (y, os.path.basename(doc)))
    m = open(os.path.join(folder, 'OPENED.MAP'), 'rb').read()
    for cy in range(vdisk.P_MH):
        if list(m[cy * 74:(cy + 1) * 74]) != cmap[cy]:
            fail('Paint\'s colours, card row %d, differ from %s' % (cy, os.path.basename(doc)))
    print('check_archive: Paint opened %s: its drawing area and its cards\' colours are the picture (ink %d)' % (
        os.path.basename(doc), ink))


def main():
    a = sys.argv[1:]
    if len(a) >= 3 and a[0] == 'log':
        cmd_log(a[1], a[2:])
    elif len(a) == 3 and a[0] == 'listing':
        cmd_log(a[1], a[2:], True)
    elif len(a) in (3, 4) and a[0] == 'files':
        cmd_files(a[1], a[2], a[3:] == ['--paint'])
    elif len(a) >= 6 and a[0] == 'screen':
        cmd_screen(a[1], a[2], a[3], a[4], int(a[5]), [int(x) for x in a[6:]])
    elif len(a) == 3 and a[0] == 'paint':
        cmd_paint(a[1], a[2])
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main()
