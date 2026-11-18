# Veranda

A GEOS-style graphical system for the Commander X16, written from scratch
for the machine: 640x480 on VERA, banked RAM, FAT32 files on the SD card,
and an SDK for writing applications in C or assembly.

Applications use the classic GEOS calls (menus, icons, dialog boxes,
GraphicsString, the r0-r15 registers), so GEOS programs that come with
source can be ported, plus a modern layer on top: 16-bit coordinates for
the full screen, file streams, banked memory, fast copy and scroll.

It runs on a stock X16. The card's folders (DECISIONS V71):

    /VERANDA            VERANDA.PRG, AUTOBOOT.X16 (the same), VERANDA.SYS,
                        RBOOT, SHELL.cvt, Veranda Setup
    /VERANDA/APPS       the applications and desk accessories
    /VERANDA/FONTS      the fonts
    /VERANDA/.SYSTEM    hidden: the settings, the alarms, the clipboard,
                        the Note File, the albums, SHELL.DSK, the trash
    /DOCUMENTS          the user's files: the shell opens there, programs
                        save and open there first

`tools/mkdist.sh <dest>` builds that package (in veranda-x16 with the
applications from `../apps`). Start it with `DOS"CD:VERANDA"` and `BOOT` (or `LOAD
"VERANDA.PRG"` and `RUN`). Quitting an application goes back to the
shell (`SHELL.cvt` in `/VERANDA`, or the root if there is no such folder),
or to `DESKTOP.cvt` there if the user put another desktop there
(DECISIONS V46). A card of the layout before V71 (everything in
`/VERANDA`) is moved into this one once by Veranda Setup, which the shell
starts. It does not run GEOS 64 binaries.

**BASIC and back** (DECISIONS V64, the RBOOT model). The shell's options
menu has *BASIC (keep Veranda)* and *Quit Veranda*. The first goes to
BASIC with Veranda kept in memory: BASIC gets `$0801-$87FF` (32,767
bytes; MEMTOP is `$8800`) and RAM banks 1-46 on a 512 KB machine (1-238
with 2 MB); the 17 banks above them hold the kernel, its copy and the
copy of the resident core. The current folder is the system folder. To
go back, type

    SYS 35328

or `^RBOOT` (`LOAD"RBOOT"` and `RUN`): the desktop comes back in the
folder the shell showed (the program that ran is not resumed; files were
closed). `SYS 35328` needs its 96 bytes at `$8A00` intact; `RBOOT`
also works when BASIC or a program overwrote the whole resident core:
it restores it and the kernel from their copies in RAM, without the
card. If the copy itself was overwritten, `SYS 35328` says so and
`RBOOT` loads `VERANDA.PRG` from the card instead. *Quit Veranda* frees
everything (MEMTOP as at power-on); then start it with `LOAD
"VERANDA.PRG"` again.

Status: phases 0-7 of [docs/PLAN.md](docs/PLAN.md) are done, each with
an automatic gate (`make gates`, `tools/gate_sdk.sh`, `gate_shell.sh`, `gate_paint.sh`, `gate_guide.sh`, `gate_export.sh`, `gate_fx.sh`, `gate_color.sh`, `gate_desk.sh`, `gate_print.sh`, `gate_term.sh`, `gate_crash.sh`, `gate_feedback.sh`, `gate_archive.sh`, `gate_clip.sh`, `gate_da.sh`, `gate_edit.sh`, `gate_notepad.sh`, `gate_mouse.sh`, `gate_view.sh`, `gate_font.sh`, `gate_basic.sh`, `gate_layout.sh`, `gate_review.sh` (the kernel review's findings, DECISIONS V68), `gate_sound.sh`):

| Phase | What | Gate |
|---|---|---|
| 0 | the API table and its generators | `spec` |
| 1 | boot, the bank gate, drawing at 640x480 | `boot`, `gfx1` |
| 2 | text, the classic calls, GraphicsString, bitmaps | `gfx`: equal to GEOS 64 byte for byte |
| 3 | interrupt, mouse, keyboard, processes, Sleep | `input` |
| 4 | menus, icons, dialog boxes, GetString | `ui`: equal to GEOS 64 at eleven moments |
| 5 | files (FAT32, CVT, record files), the loader, the SDK | `disk` (host folder and SD card), `sdk` |
| 6 | the shell (`shell/`, C) | `tools/gate_shell.sh` |
| 7 | Paint (`apps/paint/`, C) | `tools/gate_paint.sh` |
| 9 | the programming guide (`sdk/docs/GUIDE.md`) | `tools/gate_guide.sh`: every example builds against the installed SDK and runs |
| 9 | printing as a file: `ExportRect` (a 1 bpp BMP), Paint's file/export | `tools/gate_export.sh`, `gate_paint.sh` |
| 9 | printing to PDF: `PrintOpen`, `PrintBand`, `PrintPage`, `PrintClose`, `PrintRect` (pages at 150 or 300 dpi, A4 or Letter, CCITT Group 4; kernel bank P), Paint's file/print | `tools/gate_print.sh` |
| 9 | FX fills (VERA's 32-bit cache writes; a full-screen fill 65 to 10 ms) | `tools/gate_fx.sh` |
| 9 | the colour map: a paper colour per 8 x 8 card (`ColorRect`); colour in Paint (documents version 3: the cards' colours and one ink colour; 4 bpp BMP export); menus, dialog boxes and desk accessories in the normal colours | `tools/gate_color.sh`, `gate_paint.sh`, `gate_desk.sh` |
| 9 | desk accessories and the application swap (`apps/calc`, a calculator in C) | `tools/gate_desk.sh` |
| 9 | Veranda Term (`apps/term/`): ANSI, Telnet, XMODEM through a 16C550 and a Wi-Fi modem; `intFirstVector` (a device's interrupt), `SetClock` (the RTC) | `tools/gate_term.sh` (the serial line through an x16emu with `-uart`) |
| 9 | crash recovery: a BRK, `Panic` or a full stack shows the panic box (the program's name, the address); OK goes back to the desktop with the kernel banks restored from their copy in RAM | `tools/gate_crash.sh` |
| 9 | the clipboard: GEOS's Text Scrap and Photo Scrap in the system's data folder (`/VERANDA/.SYSTEM`), through the SDK's `scrap.h`; Paint's select, cut, copy and paste (a floating picture, its colours), the Photo Manager and the Text Manager (`apps/scrapman`, desk accessories: albums of scraps), Term's copy screen | `tools/gate_clip.sh` |
| 9 | feedback: pointer shapes (`SetPointer`: arrow, I-beam, crosshair, an animated busy pointer shown by itself during long operations), the system beep (`Beep`), screen blanking after `blankDelay` seconds | `tools/gate_feedback.sh` |
| 9 | native desk accessories (`apps/dex`, `apps/calendar`, `apps/prefs`, the toolkit `apps/daui`): Dex, an address book (search, an alphabet bar, CSV for Veranda Calc, copy, connect with Veranda Term); the Calendar (a month, its days' appointments, alarms that ring over any program: the kernel's alarm table and box); Preferences (blanking, paper and ink, beep, pointer speed, video mode, desktop, time zone, the clock), read by the kernel at start-up (`SysPrefs`) | `tools/gate_da.sh` (Term's dial through the UART emulator's fake BBS; on the host folder and an SD card image) |
| 9 | Veranda Edit (`apps/edit/`): plain text (Latin-1; LF, CR LF or CR kept), files of up to about 330 KB in RAM banks (a gap buffer), PutCards at 79 x 55, selection by keys and the pointer, undo, find and replace, go to line, the clipboard; the SDK's file chooser (`vrchoose.h`: long names, folders) and `vr_dlgbox` | `tools/gate_edit.sh` (a scripted session against a model; the host folder and an SD card image) |
| 9 | Note Pad (`apps/notepad/`), a desk accessory: pages of notes in the Note File (a record file in the system folder) | `tools/gate_notepad.sh` |
| 9 | View (`apps/view/`): text (wrapped, find), BMP pictures (1 and 4 bpp as ExportRect writes them, fitted or panned, colours through the colour map), Paint documents, Photo Scraps and albums, any GEOS file's info block; copy to the clipboard; it opens BMP files from the shell | `tools/gate_view.sh` (each screen against `tests/check_view.py`'s drawing; host folder and SD card image) |
| 9 | the shell's file utilities (`shell/files.c`): find (wildcards, from the folder shown down; open or reveal), copy and move whole folders (a progress box with cancel), the trash (`/VERANDA/.SYSTEM/TRASH` since V71, empty trash), get info (the description edited) | `tools/gate_shell.sh` |
| 9 | BASIC and back: `ToBASIC` with r1 = `TOBASIC_KEEP` keeps Veranda in memory, `SYS 35328` and `RBOOT` return to the desktop from the copies in RAM (or from the card if they are damaged); the shell's geos menu: BASIC (keep Veranda), Quit Veranda | `tools/gate_basic.sh` (BASIC's memory up to MEMTOP, the resident core overwritten, RBOOT with no kernel on the card, a damaged copy, the full exit; the host folder and an SD card image) |
| 9 | Archive (`apps/archive/`): C64 and C128 GEOS disk images (D64, D71, D81) listed with their icons, their files extracted as CVT files (plain files as they are), geoPaint pages, Photo Scraps and photo albums to Paint documents; `tools/vdisk.py`, the same on a PC | `tools/gate_archive.sh` (against `vdisk.py`, on the host folder and an SD card image) |
| 9 | fonts in RAM banks (kernel bank T: `LoadFont`, `UseFont`, `FontInfo`, `FontList`, `FontSizes`; GEOS font files in `/VERANDA/FONTS`, loaded once for every program), the font picker (`fontpick.h`), the built-in Veranda UI font for native programs' menus and dialog boxes (`uiFont`), Paint's text font, the Font Editor (`apps/fonted/`) | `tools/gate_font.sh` |
| 9 | a colour theme for Veranda's own programs (`theme.h`: an accent for title and menu bars, a desktop colour, a highlight; presets Veranda Blue, Classic, Graphite, Mint): the shell's bars and selection, the accessories' title bars, Veranda Edit's; Preferences' Theme page (DECISIONS V65) | `tools/gate_shell.sh`, `gate_da.sh` |
| 9 | system sounds (`PlaySound`, PC/GEOS style, on PSG voices 14-15 from the interrupt): a start-up chime, the exit to BASIC, an application's start, a desk accessory's open and close, a question's dialog box, the beep (`Beep` = `SND_ERROR`), the alarm, the trash's crunch (PSG noise), an optional menu tick; Preferences: sounds on or off, a volume, the menu ticks (`tools/mksounds.py`: the table) | `tools/gate_sound.sh` (the registers frame by frame against the table; WAVs of each sound in `build/gate/sound/wav`) |
| 9 | Shell 2, a GEOS deskTop (DECISIONS V70): one or two folder windows (striped title bars in the theme's accent, close boxes, moved by their title bars and resized by their corners, a status line with the files, the selection, Kbytes used and free), large icons, small icons or a list (name, size, date, type: a click on a header sorts), pages with the dog-ear, the list's scroll bar; on a patterned desktop the card (a small Commander X16), pinned applications, the printer (a BMP or Paint picture dropped on it prints to a PDF) and the trash; drags between windows copy, into a folder move; a menu bar with the date and time | `tools/gate_shell.sh`, `gate_mouse.sh` (a window drag and a double click through the real mouse path, R49) |
| 9 | the card's folders (DECISIONS V71): `/VERANDA` the system, `/VERANDA/APPS` the applications and accessories, `/VERANDA/.SYSTEM` the system's data (hidden), `/DOCUMENTS` the user's files; the paths in the API (`VR_SYSDIR` ...), the SDK's `vrpath.h` (`vr_cd`, `vr_home`); the shell opens `/DOCUMENTS` and starts applications working there; Veranda Setup (`apps/setup`) moves a card of the layout before into it once; `tools/mkdist.sh`, the package | `tools/gate_layout.sh` (an old card of the user's kind migrated under R49, a host-side inventory before and after; the settings, Note File and trash found in .SYSTEM; Save in Paint and Veranda Edit lands in /DOCUMENTS; open with from /DOCUMENTS; RBOOT; BOOT) |
| 9 | the switcher and the dock (DECISIONS V83): Alt + Tab (or `TaskCtl(TASK_ASIDE)`) puts the running program aside whole into RAM banks (its RAM, zero page, system variables, menus' state, record file and overlays, both screens and the colour map, packed: a small program 3 banks) and brings up the shell, whose dock at the bottom of the desktop shows the pinned programs and those aside (a dot); a click brings one back exactly where it was, a right click quits or unpins; windows minimise to the dock; GEOS ports are put aside the same way; a ninth kernel bank (W) | `tools/gate_switch.sh` (512 KB and 2 MB, host folder and an SD card image under R49) |

Phase 8, the geoWrite port, and the other ports of GEOS programs
(TopDesk, geoChess) contain or derive from Berkeley Softworks' and other
third parties' code, so they are kept in separate repositories for
personal use and are not part of this tree or of veranda-x16. Stages 8a
(as it is, 320x240) and 8b (its screen code as Veranda calls) were gated
against GEOS 64 (DECISIONS V25-V27: what it needed here), 8c (640 wide)
and 8d (all 480 lines) against reviewed golden pictures. Veranda Calc is
a build target of Commander Calc (`../apps/calc` in veranda-x16).

Phases 0-9 of the plan are done. Not done yet: a run on a real X16 of
this build (the first runs found the SetVideoMode and mouse-range bugs,
V-numbers in DECISIONS). Decisions and
measurements: [docs/DECISIONS.md](docs/DECISIONS.md); the file and
application format: [docs/APPFORMAT.md](docs/APPFORMAT.md); the SDK:
[sdk/docs/README.md](sdk/docs/README.md) and the programming guide
[sdk/docs/GUIDE.md](sdk/docs/GUIDE.md).

Building needs cc65 and Python 3; the gates need x16emu and the X16 ROM
(and, for the comparisons with GEOS 64, the old project's simulator and a
GEOS 64 KERNAL built from source in `GEOS64_REF`; they report SKIP
without it). Gate print needs Pillow with libtiff (to decode the pages);
it uses qpdf and pdftoppm when they are installed.

Licence: MIT (see LICENSE), for the kernel, the SDK and the tools.
View is Veranda's own: Bo Zimmerman's geoBrowser (Apache-2.0) was read
and not ported (DECISIONS V60), so no Apache-licensed code is in this
tree.
Ports of Berkeley Softworks programs, if any, are kept apart with their
own notices.
