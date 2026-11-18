#!/usr/bin/env python3
"""A synthetic "Veranda Prefs.cvt" as V65's Preferences saved it (DECISIONS V72).

usage: mkprefs.py <out.cvt>

The fixture of gates sound (part 3c) and basic (the user's card): a settings
file from before V72, so byte 15 holds no PREFS_VOLSET (full volume) and the
flags are 0 (sounds on, no menu ticks). The block (kernel/s/prefs.s, from
byte 508): paper $0FFF, ink $0000, no screen blank, flags 0, no video mode
($FF), time zone 0, the colour theme 14 0 14 $80 (Look 2's entries), look
0, byte 15 0. The rest is the CVT layout of docs/APPFORMAT.md: the
directory entry (a sequential file, GEOS type SYSTEM, dated 2026-01-01),
the signature, the info block (class "Veranda Prefs V1.0", author
"Veranda"), with a plain icon.
Written by the test suite's maintainers; it is not anyone's own settings file.
"""
import struct
import sys

SIG = b'PRG formatted GEOS file V1.0'
BLOCK = bytes([0xFF, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF,
               0x00, 0x00, 0x0E, 0x00, 0x0E, 0x80, 0x00, 0x00])


def make():
    name = b'Veranda Prefs'.ljust(16, b'\xA0')
    # the directory entry (30 bytes): CBM type $83 (SEQ), no track/sector,
    # the name, no info block t/s, structure 0 (sequential), GEOS type 4
    # (SYSTEM), the date YY MM DD HH MM, 2 blocks
    d = bytes([0x83, 0, 0]) + name + bytes([0, 0, 0, 4]) + bytes([26, 1, 1, 0, 0]) + struct.pack('<H', 2)
    assert len(d) == 30
    first = (d + SIG).ljust(254, b'\0')
    # the info block (254 bytes: the GEOS header without its link): the
    # icon (3 x 21, a framed box), the file's types, the load addresses,
    # the class, the author, the description
    icon = bytearray()
    for y in range(21):
        row = 0xFFFFFF if y in (0, 20) else 0x800001
        icon += row.to_bytes(3, 'big')
    info = bytes([3, 21 | 0x80]) + bytes(icon)
    info += bytes([0x83, 4, 0, 0, 0, 0x10, 0, 0])    # CBM type, GEOS type, structure, load $0000, end $1000, start
    info += b'Veranda Prefs V1.0'.ljust(20, b'\0')
    info += b'Veranda'.ljust(20, b'\0')
    info = info.ljust(0xA0 - 2, b'\0') + b"Veranda's settings (Preferences)."
    info = info.ljust(254, b'\0')
    data = first + info
    return data.ljust(508, b'\0') + BLOCK


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    open(sys.argv[1], 'wb').write(make())


if __name__ == '__main__':
    main()
