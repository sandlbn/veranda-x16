# Veranda files and applications

## GEOS files on FAT32

A GEOS file is one FAT32 file in **CVT format** (GEOS Convert 2.x), named
`<GEOS name>.cvt` (the GEOS name: up to 16 characters, no `/`, `:`, `*`,
`?`, `=`, `"`; case kept). The kernel's classic calls (`FindFile`,
`GetFile`, `OpenRecordFile` ...) take the GEOS name and add `.cvt`.

**Long names** (API 5, docs/DECISIONS.md V82). With `longNames` set (a
native program's choice; 0 at every program's start) the name may have up
to 63 characters (`LONG_MAX`) before `.cvt`; the name in the file's own
directory entry (offset 3) keeps its first 16. A document a program is
started with (`GetFile`, `ST_LD_DATA`) is kept whole in `dataLongName`
(64 bytes at `$07C0`; `dbFileNames` lists 26 names now, not 30) and in
`dataFileName` as GEOS's 16 characters: the name itself up to 16, else
its **short form**: the first 11 characters less the extension's, `~`,
the CRC-16 of the whole name (CCITT from `$FFFF`, the ROM's `memory_crc`)
in four upper-case hexadecimal digits, and the extension (a dot and 1-4
characters at the name's end; none for a GEOS document's name), so
`Holiday photos from the lake 2026.txt` is `Holiday~1D38.txt` and `A
picture with a long name` is `A picture w~0FF9`. Every GEOS file call and
`FOpen` given that short form (as a GEOS program passes `dataFileName`)
use `dataLongName` instead: the short form never names a file on the card.
A record file is written back through `Name.tmp`, or `VRTEMP.tmp` for a
name longer than 16.

| Offset | Size | Contents |
|---|---|---|
| 0 | 30 | directory entry: 0 CBM type (`$83` USR, `$82` PRG), 1-2 0, 3-18 the name padded with `$A0`, 19-20 0 (header block), 21 structure (0 sequential, 1 VLIR), 22 GEOS type, 23-27 date (year - 1900, month, day, hour, minute), 28-29 size in 254-byte blocks |
| 30 | 224 | `PRG formatted GEOS file V1.0`, zero padded; a Veranda application's **tile block** (optional, info block byte 143 bit 1, below) in the padding: 96 the tile's colour, 97-224 its bitmap |
| 254 | 254 | the info block, bytes 2-255 of GEOS's 256-byte header (below) |
| 508 | 254 | VLIR only: the record table, per record (blocks, bytes in the last block + 1); 0/0 ends it, 0/$FF an empty record |
| 508 or 762 | - | sequential: the data. VLIR: each record padded to whole 254-byte blocks |

The info block (offsets in GEOS's 256-byte header, as `fileHeader` holds
it after `GetFHdrInfo`):

| Offset | Size | Contents |
|---|---|---|
| 0-1 | 2 | 0, $FF |
| 2-3 | 2 | icon width 3 (cards), height 21 |
| 4-67 | 64 | the icon: $BF, then 63 bytes (24x21) |
| 68 | 1 | CBM type |
| 69 | 1 | GEOS type (`APPLICATION` 6, `APPL_DATA` 7, `FONT` 8, `SYSTEM` 4 ...) |
| 70 | 1 | structure |
| 71-72 | 2 | load address |
| 73-74 | 2 | end address |
| 75-76 | 2 | start address |
| 77-96 | 20 | class: 12 characters, `V` and the version (`x.y`), 0 |
| 97-116 | 20 | author |
| 117-136 | 20 | the parent application's class (documents) |
| 137-159 | 23 | application data. **Veranda applications:** 137-140 `VRND`, 141 API level, 142 video mode (`MODE_*`, $FF: keep), 143 flags (bit 0: banked overlays, API 2; bit 1: a tile block; bit 2: `APPF_STAY`, never put aside by the switcher, which asks to quit it instead, API 6, DECISIONS V83; else 0), 144-159 the plain files it opens: four slots of 4 bytes, an extension each (upper case, space padded; a slot starting with 0 is empty), which the shell opens with it (`vpack.py`'s `documents`; DECISIONS V48) |
| 160-255 | 96 | description |

## Applications

An application is a GEOS file of type `APPLICATION` with `VRND` in its
info block (others are refused: `NOT_VERANDA`).

- **Sequential:** its data loads at the load address (at least `$0801`,
  ending below `$8800`); the kernel jumps to the start address.
- **VLIR:** record 0 is the main program (as above); records 1-126 are
  overlays, read into RAM banks when the application starts.
  `LoadOverlay` (a = record, r7 = address) copies one into memory; the
  overlay's first two bytes are not special (it loads where the caller
  says).
- **Banked overlays** (flags bit 0, API 2; `overlays = banked` in
  `app.hdr`): record n is read to `$A000` of RAM bank first + n (first:
  the application's first overlay bank; one more bank holds the last
  record's padding), so each overlay can run in place; a record may hold
  at most 8191 bytes (else `STRUCT_MISMAT`). `LoadOverlay` with r7 = 0
  copies nothing and gives the bank in a and r1L, the address in r0.

Before the jump the loader: sets the video mode (unless $FF), clears the
screens, resets GEOS's state (as at start-up: vectors 0, no menus or icons,
processes, the system font), `appBank` to the first RAM bank above the
kernel's work banks, and the stack. The application returns to `MainLoop`
(`rts` from the start address is the same as `jmp MainLoop`).

The API level: the loader refuses a level above `API_LEVEL`
(`TOO_NEW`).

### The tile (Veranda Look 2, docs/DECISIONS.md V77)

The shell shows an application as a **tile**: a 32 x 32 picture on a
block of 4 x 4 cards of one paper colour, the ink a glyph in a rounded
frame (radius 4) that the shell draws. An application carries its own
in the CVT file's first block, in the signature's zero padding, which
GEOS and Veranda's loader do not read (so the file stays a valid CVT
file and older shells ignore it):

| CVT offset | Size | Contents |
|---|---|---|
| 96 | 1 | the colour: a palette entry of Look 2 (`sdk/lib/theme.h`: `PAPER` 0 ... `DUSK` 15; low nibble) |
| 97-224 | 128 | the glyph: 32 rows of 4 bytes, bit 7 the left pixel, 1 ink (the frame is not in it) |

and sets bit 1 of the info block's flags (byte 143). `vpack.py` writes
it from `tile = <file>` in `app.hdr`: a text file with `colour <name>`
(paper, ink, chrome, sidebar, select, title, yellow, red, green, purple,
orange, cyan, sky, sky2, hill, dusk) and 32 rows of 32 characters, `#`
ink, `.` none (`apps/*/tile.txt`); `vpack.py --list` shows it. The
shell reads the 129 bytes from the file when the flag is set. Programs
that cannot carry one (the ports of GEOS programs) are known by their
class (`shell/tiles.txt`, `tools/mktiles.py` -> `shell/tiles.h`); any
other GEOS file's icon (24 x 21) is shown centred on a chrome tile.

## Desk accessories

A desk accessory is a GEOS file of type `DESK_ACC` (5) with `VRND` in
its info block, **sequential** (`STRUCT_MISMAT` otherwise), and video mode
`$FF` (it runs in the application's mode). `vpack.py` makes one from
`type = deskacc` in `app.hdr`. Its memory is an application's: it loads
at its load address (`$0801` with the SDK's configurations) and may use
`$0801-$87FF`, the application zero page and RAM banks.

An application (or the shell) opens one with `GetFile` (the name; the
other arguments are ignored). `GetFile` returns only on an error
(`INCOMPATIBLE`: an accessory is running already; `NO_BANKS`, `NOT_VERANDA`,
`TOO_NEW`, a file error), else when the accessory ends: it calls
`RstrAppl`, and `GetFile` returns with x = 0 to an application exactly as
it was. In between (kernel/s/desk.s, docs/DECISIONS.md V31):

1. **Put aside**, into 22 RAM banks (176 KB): `$0801-$87FF`; kernel bank U
   whole (menus, icons, dialog boxes, `GetString`); VRAM `$00000-$1EFFF`
   (both screens, the save-under stack, the sprite pictures, the colour
   map); and in the kernel: zero page `$22-$3E`, `$50-$7F`, `$A9-$FF`, the
   stack page and pointer, `$0400-$07FF`, the processes, the sleeping
   routines, the bank table, the sprites' attributes, palette entries
   0-15, the layers.
2. **The accessory starts** as an application does (GEOS's start-up
   state, an empty stack returning to `MainLoop`, a RAM bank of its own as
   `appBank`), but over the screen as it is: no video mode change, nothing
   cleared.
3. **`RstrAppl`** puts it all back (the accessory's banks are freed with
   the bank table), except what is live: the pointer's position and
   buttons, the key queue, the clock, `random`. r0-r15 are not restored
   (as after any call).

Not put aside: bank F's state. An open record file stays the
application's, so an accessory must not open a record file while the
application has one open. Opening an accessory takes about 0.6 s, closing
it 0.56 s (the emulator, the host folder).

## System files: the settings and the alarms

Two sequential GEOS files of type `SYSTEM` in the system folder
(`/VERANDA`, else the root), read by the kernel when Veranda starts (and
after a crash's panic box), written whole by the programs that own them
(docs/DECISIONS.md V56; `SysPrefs` hands the same bytes to the running
system):

| File | Class | Data (from byte 508) | Written by |
|---|---|---|---|
| `Veranda Prefs.cvt` | `Veranda Prefs V1.0` | the settings block, 16 bytes: 0-1 the paper, 2-3 the ink (`$0RGB`, palette entries 0 and 1 at every program's start), 4-5 `blankDelay`, 6 the flags (bit 7: no beep; bits 0-1: the pointer's speed, x1, x2, x4), 7 the video mode for programs that keep theirs (`$FF`: none), 8 the time zone (quarter hours, signed), 9 the desktop chosen (0 the shell, 1 TopDesk), 10-15 0 | Preferences (`apps/prefs`) |
| `Veranda Alarms.cvt` | `Veranda Alarms V1.0` | the alarm table, 8 entries of 32 bytes: the year (0-99: 2000-2099), month, day, hour, minute, the state (0 none, 1 armed, 2 done), the text (up to 24 characters, 0-terminated) | the Calendar (`apps/calendar`) |

The native desk accessories' own record files, also in the system
folder, `APPL_DATA`: `Dex Cards.cvt` (class `Dex Cards V1.0`: a record a
card, its five fields as 0-terminated strings: name, phone or
host:port, e-mail, address, note) and `Appointments.cvt` (class
`Appointments V1.0`: record 0, an appointment of 32 bytes each, in the
alarm entry's form, byte 5 the flags, bit 0 the alarm). A `.TEL` file
(Veranda Term's document, plain) holds one address book line,
`name|host:port`.

## Scraps (the clipboard)

Veranda's clipboard is GEOS's two scrap files (docs/DECISIONS.md V54;
the SDK's `scrap.h`), in the system folder: `/VERANDA`, or the root when
there is no `/VERANDA`. Both are sequential GEOS files of type `SYSTEM`
(4), in CVT format as every GEOS file (above): the entry, the signature,
the info block (icon, `$83`, `SYSTEM`, sequential; the class; bytes
158-159 a serial that Veranda's library counts up at every write), the
data from byte 508.

| File | Class | Data |
|---|---|---|
| `Text  Scrap.cvt` (two spaces: GEOS 2's name) | `Text  Scrap V2.0` | a word, the count of the bytes that follow; GEOS text: characters 32-126, TAB 9, CR 13 (a paragraph's end), PAGE_BREAK 12, NEWCARDSET 23 + the font id word + the style byte, ESC_RULER 17 + 26 bytes (geoWrite's ruler), ESC_GRAPHICS 16 + 4 bytes |
| `Photo Scrap.cvt` | `Photo Scrap V2.0` | the width in cards (1-80), the height in rows (a word), the bitmap: width x height bytes in GEOS's compacted format (BitmapUp's: a count below `$80` repeats the next byte; `$80`-`$DB` that many - `$80` bytes follow; `$DC` and up a pattern); then, with colour, a byte a card (width x (height + 7) / 8, row by row, compacted the same way): the ink in the high nibble, the paper (a palette entry; 0 the normal paper) in the low one |

geoWrite 2.1 reads and writes the Text Scrap and reads the Photo Scrap
(it stops after the bitmap; it takes classes up to version 2.1 and
pictures below 145 rows). Albums: the Photo Manager's `Photo Album.cvt`
and the Text Manager's `Text Album.cvt`, record files of type
`APPL_DATA` (classes `Photo Album V1.0`, `Text Album V1.0`) in the
current folder, a record a page holding a scrap's data as it is.

## Wallpapers (the desktop's picture)

The shell's desktop shows a picture, the theme's plain desktop colour or
a GEOS pattern on it (Veranda Look 2, step C: docs/DECISIONS.md V78).
A picture is a **wallpaper file**: a sequential GEOS document, type
`APPL_DATA` (7), class `Wallpaper   V1.0`, in CVT format as every GEOS
file (its icon a small dithered view of it), made by `tools/mkwall.py`
(in the SDK's tools: `mkwall.py picture.jpg`, or `--dusk` for the
built-in scene) from any picture Pillow reads, cropped to 4:3 and resized
to 640 x 480. It is drawn the way Veranda draws everything: a 1 bpp
bitmap in the one ink over a paper colour a card (V30). Its data, from
byte 508:

| Offset | Size | Contents |
|---|---|---|
| 0 | 4 | `VWAL` |
| 4 | 1 | the version: 1 |
| 5 | 1 | the variants: 2 (0 light, 1 dark) |
| 6 | 1 | 80: cards across |
| 7 | 1 | 60: card rows |
| 8 | 8 | 0 |
| 16 + v x 43,208 | 8 | variant v: palette entries 12-15 (`$0RGB`, little-endian words), chosen for the picture |
| + 8 | 4,800 | a palette entry a card (2-15: Look 2's fixed 2-11 or the picture's 12-15), row by row, 80 a row |
| + 4,808 | 38,400 | the bitmap: 480 rows of 80 bytes, bit 7 the left pixel, 1 ink: a 4 x 4 Bayer dither of how far toward the ink each pixel is from its card's paper |

The light variant is made for Look 2's light palette and its slate ink
(`$223`), the dark one for the dark palette and its light ink (`$EEF`),
from the picture darkened (dark mode's light ink would make the light
variant's dither pale). The shell takes the variant of the mode.

The choice is a plain file, `WALLPAPER.CFG` in the system's data folder
(`/VERANDA/.SYSTEM`; the SDK's `wallcfg.h`), 64 bytes, written by
Preferences (Desktop) and by the shell (options > desktop pattern):
0-1 `WP`, 2 the version (1), 3 the kind (0 the picture, 1 the plain
colour, 2 the pattern), 4 the pattern (0-7), 5-7 0, 8-31 the picture's
GEOS name (0-terminated: the file `NAME.cvt`), 32-63 its folder
(0-terminated). Without the file: the picture `Dusk` in
`/VERANDA/WALLPAPERS` (`VR_WALLDIR`), where the package puts it.

## The Mods folder (`MODS.CFG`)

The shell's sidebar place "Mods" opens a folder the user chooses with
File > Set as Mods folder (the front window's folder; docs/DECISIONS.md
V84). The choice is kept in `MODS.CFG`, a plain (not GEOS) file in the
system's data folder, `/VERANDA/.SYSTEM` (`VR_DATADIR`; else `/VERANDA`,
else the root, as `vr_cd(VR_DATA)` finds it):

- one line: the folder's whole path from the card's top folder, starting
  with `/` (`/MODS`, `/Music/Amiga mods`), at most 61 characters, in the
  FAT name's case; the shell writes it with one line feed (`$0A`) after
  it;
- a reader takes the bytes up to the first line feed, carriage return
  or 0 (or the end of the file), and uses the default when the file is
  missing, does not start with `/` or is longer than 61 characters;
- the default, with no file: `/MODS`.

Any program may read it (the MOD Player: the folder to start in). Only
the shell writes it; a program that wants to change the Mods folder
writes the same form.

## The trash

The shell's delete moves a file or a folder into `TRASH` in the system
folder (`/VERANDA/TRASH`, or `/TRASH` without a `/VERANDA`), as it is: a
name already there gets `~2` ... `~9` before its extension. Its file/empty
trash deletes what is in it (docs/DECISIONS.md V61).

## PDF files (printing)

`PrintOpen` ... `PrintClose` and `PrintRect` (docs/DECISIONS.md V38)
write a PDF 1.4 file under the name given (any name; Paint adds `.pdf`),
in one pass:

| Object | Contents |
|---|---|
| 1 | the catalogue: `<</Type/Catalog/Pages 2 0 R>>` |
| 2 | the page tree: `/Count` and `/Kids`, every page (written last) |
| per page, n | the page: `/MediaBox [0 0 595.2 841.92]` (A4) or `[0 0 612 792]` (US Letter), `/Resources` (its image `/I`, and `/K` with colour), `/Contents` |
| n + 1 | the content stream: `q s 0 0 s 0 0 cm` (s = 72 / dpi: dots to points), with colour the clip and the colours' image, the ink's `r g b rg`, then `W 0 0 H 0 0 cm /I Do Q` |
| n + 2 | the page's image, the whole page: `/ImageMask true`, `/Width` and `/Height` the page in dots, `/Filter /CCITTFaxDecode`, `/DecodeParms <</K -1 /Columns W /Rows H>>` (Group 4; ink coded as black), `/Length` the next object |
| n + 3 | that length |
| n + 4 | with colour: the paper colours, a byte a card (`/ColorSpace [/Indexed /DeviceRGB 15 <VERA's 16 palette entries>]`, 8 bits), placed over the area's cards |

Then the cross reference table (an entry per object, its offset counted
as the bytes were written) and the trailer (`/Size`, `/Root 1 0 R`). The
pages: A4 1240 x 1754 dots at 150 dpi, 2480 x 3508 at 300; Letter 1275 x
1650 and 2550 x 3300; each page's image is exactly the media box. A
document holds up to 250 pages; the offsets are kept in a RAM bank of
its own (`BankAlloc`) while it is open.

## Memory for applications

| Area | Use |
|---|---|
| `$0801-$87FF` | code and data |
| zero page `$50-$7F`, `$A9-$FF` | free (`a0-a9` at `$50-$63`); C programs: cc65's zero page at `$A9` |
| RAM banks | `BankAlloc` (a = count) gives consecutive banks; overlays use them too; the kernel has the top seven (and an open PDF document one more) |
