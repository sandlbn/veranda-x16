#!/bin/bash
# The Veranda package for an SD card, in the card's layout (DECISIONS V71).
#
#   tools/mkdist.sh <dest>
#
# <dest> gets what goes on the card's top level:
#   VERANDA/            VERANDA.PRG, AUTOBOOT.X16 (the same file: BOOT in
#                       this folder starts Veranda), VERANDA.SYS, RBOOT,
#                       SHELL.cvt, Veranda Setup.cvt, README.txt
#   VERANDA/APPS/       the applications and desk accessories
#   VERANDA/FONTS/      Gate Sans, Gate Mono (Veranda's own, tools/fontcvt.py)
#   VERANDA/WALLPAPERS/ Dusk.cvt, the desktop's default picture (tools/mkwall.py
#                       --dusk; DECISIONS V78), and Meadow.cvt
#                       (tools/wallpapers/mkmeadow.py; needs Pillow)
#   VERANDA/HELP/       VeraBASIC's help (VR_HELPDIR, DECISIONS V93), the
#                       manual in help form, the reference card's PDF (V99)
#   DOCUMENTS/          the user's files: VeraBASIC Examples/ (the example
#                       programs and the tutorial's steps), Garden.brd (Bird
#                       Watch's document; Bird Watch, a made application,
#                       is in VERANDA/APPS: V99)
# No .SYSTEM: the shell makes it, and Veranda Setup, which moves a card of
# the layout before into this one, runs once on it (its LAYOUT.LOG there).
#
# Built from this tree (the kernel, the SDK into <dest>/../ a work folder,
# the shell, Setup, the applications) and from the sibling repositories,
# each exported from a branch with git archive (their working trees are
# left as they are), built against this tree's SDK:
#   GEOWRITE  ~/Projects/veranda-geowrite  ref main      STAGE=8d
#   CALC      ~/Projects/commander-calc    ref layout (else veranda)
#   GEOCHESS  ~/Projects/veranda-geochess  ref veranda
#   TOPDESK   ~/Projects/veranda-topdesk   ref veranda   STAGE=td3 MODE=640x240
#   X16MOD    ~/Projects/x16-mod           ref layout (else veranda)
#   SOLITAIRE ~/Projects/veranda-solitaire ref layout (else main)
# (each: <NAME>=<tree>, <NAME>_REF=<ref>; a missing tree is left out with a
# warning). No test builds, scripts or fixtures go in.
#
# Inside the veranda-x16 repository (this tree is its veranda/, and ../apps
# holds the applications) the applications are built from ../apps/* instead
# (each copied into the work folder, without its build/), and the
# personal-use ports (geoWrite, geoChess, TopDesk) are not looked for:
#   apps/calc  apps/modplayer  apps/solitaire  apps/pinball
#   apps/harbour-walls  apps/sidplay
# MKDIST_SIBLINGS=1 forces the sibling repositories even there.
set -e
cd "$(dirname "$0")/.."
TOP=$PWD
[ -n "$1" ] || { echo "usage: tools/mkdist.sh <dest>" >&2; exit 2; }
mkdir -p "$1"
DEST=$(cd "$1" && pwd)
WORK=${MKDIST_WORK:-$TOP/build/dist}
export CC65_HOME=${CC65_HOME:-$HOME/.local/share/cc65}
export PATH=$CC65_HOME/bin:$PATH
PY="python3 -I -B"
P=$HOME/Projects
GEOWRITE=${GEOWRITE:-$P/veranda-geowrite}
CALC=${CALC:-$P/commander-calc}
GEOCHESS=${GEOCHESS:-$P/veranda-geochess}
TOPDESK=${TOPDESK:-$P/veranda-topdesk}
X16MOD=${X16MOD:-$P/x16-mod}
SOLITAIRE=${SOLITAIRE:-$P/veranda-solitaire}

say() { echo "mkdist: $*"; }
die() { echo "mkdist: $*" >&2; exit 1; }

rm -rf "$WORK"
mkdir -p "$WORK/b" "$WORK/src"
V=$DEST/VERANDA
[ ! -e "$V" ] || die "$V is there already (a new, empty <dest>)"
mkdir -p "$V/APPS" "$V/FONTS" "$V/WALLPAPERS" "$DEST/DOCUMENTS"

# ---------------------------------------------------------------- Veranda
$PY tools/apigen.py api/veranda.api build/gen >/dev/null || die "the API table"
$PY tools/kbuild.py >/dev/null || die "the kernel does not build"
tools/mksdk.sh "$WORK/sdk" >/dev/null || die "the SDK does not install"
SDK=$WORK/sdk
cp build/VERANDA.PRG build/VERANDA.SYS build/RBOOT "$V/"
cp build/VERANDA.PRG "$V/AUTOBOOT.X16"
cp -r shell "$WORK/b/shell"
rm -f "$WORK/b/shell"/*.cvt "$WORK/b/shell"/*.bin "$WORK/b/shell"/*.map
make -s -C "$WORK/b/shell" SDK="$SDK" >"$WORK/b/shell.log" 2>&1 || { tail "$WORK/b/shell.log"; die "the shell"; }
cp "$WORK/b/shell/SHELL.cvt" "$V/"
app() {                 # $1 apps/<dir>, $2 the folder in the package, then "built=on the card" ...
	local d=$1 to=$2 b=$WORK/b/$(basename "$1")
	shift 2
	mkdir -p "$b"
	make -s -C "$d" all SDK="$SDK" OUT="$b" >"$b/build.log" 2>&1 || { tail "$b/build.log"; die "$d"; }
	for f in "$@"; do
		cp "$b/${f%%=*}" "$to/${f#*=}"
	done
}
app apps/setup "$V" "Setup.cvt=Veranda Setup.cvt"
# (the names the cards had before V71, so Veranda Setup finds the old
# copies of the same programs: DECISIONS V71)
app apps/paint "$V/APPS" "Paint.cvt=Paint.cvt"
app apps/edit "$V/APPS" "Edit.cvt=Veranda Edit.cvt"
app apps/view "$V/APPS" "View.cvt=View.cvt"
app apps/archive "$V/APPS" "Archive.cvt=Archive.cvt"
app apps/term "$V/APPS" "Term.cvt=Term.cvt"
app apps/ftp "$V/APPS" "FTP.cvt=Veranda FTP.cvt"
app apps/fonted "$V/APPS" "FontEd.cvt=FontEd.cvt"
app apps/calc "$V/APPS" "Calculator.cvt=Calculator.cvt"
app apps/notepad "$V/APPS" "NotePad.cvt=Note Pad.cvt"
app apps/dex "$V/APPS" "Dex.cvt=Dex.cvt"
app apps/calendar "$V/APPS" "Calendar.cvt=Calendar.cvt"
app apps/prefs "$V/APPS" "Preferences.cvt=Preferences.cvt"
app apps/scrapman "$V/APPS" "PhotoMgr.cvt=PhotoMgr.cvt" "TextMgr.cvt=TextMgr.cvt"
# VeraBASIC (DECISIONS V93): the editor (it opens .bas files) and VeraBASIC
# Run (program documents)
b=$WORK/b/vbasic
mkdir -p "$b"
make -s -C apps/vbasic ide SDK="$SDK" OUT="$b" >"$b/build.log" 2>&1 || { tail "$b/build.log"; die "VeraBASIC"; }
cp "$b/ide/VeraBASIC.cvt" "$V/APPS/VeraBASIC.cvt"
make -s -C apps/vbasic all SDK="$SDK" OUT="$b" >>"$b/build.log" 2>&1 || { tail "$b/build.log"; die "VeraBASIC Run"; }
cp "$b/VBRun.cvt" "$V/APPS/VeraBASIC Run.cvt"
# its help (VR_HELPDIR) and the example programs
mkdir -p "$V/HELP" "$DEST/DOCUMENTS/VeraBASIC Examples"
$PY tools/vbgen.py >/dev/null || die "vbgen.py (the help files)"
cp vbasic/gen/HELP/KEYWORDS.HLP vbasic/gen/HELP/ERRORS.HLP vbasic/gen/HELP/TUTORIAL.HLP vbasic/gen/HELP/EDITOR.HLP \
	vbasic/gen/HELP/MANUAL.HLP "$V/HELP/"
# the manual's reference card as a PDF (DECISIONS V99: tools/vbmanual.py)
$PY tools/vbasic.py doc "$WORK/manual" >/dev/null || die "the manual (vbasic.py doc)"
cp "$WORK/manual/VeraBASIC-reference-card.pdf" "$V/HELP/Reference card.pdf"
X="$DEST/DOCUMENTS/VeraBASIC Examples"
cp docs/vbasic/examples/notes.bas "$X/Notes.bas"
cp docs/vbasic/examples/dice.bas "$X/Dice.bas"
cp docs/vbasic/examples/addressbook.bas "$X/Address book.bas"
cp docs/vbasic/examples/tiles.bas "$X/Tiles.bas"
for i in 1 2 3 4 5 6 7; do
	cp "tests/vbasic/ui/bird$i.bas" "$X/Bird Watch - tutorial step $i.bas"
done
# a made application (DECISIONS V99): Bird Watch, the tutorial's step 7 with
# its APP lines and its own tile, made by vbasic.py app as File > Make
# application... makes it; Garden.brd, its document, in /DOCUMENTS
cp docs/vbasic/examples/birdwatch.bas "$X/Bird Watch.bas"
$PY tools/vbasic.py app docs/vbasic/examples/birdwatch.bas --run "$V/APPS/VeraBASIC Run.cvt" \
	-o "$V/APPS/Bird Watch.cvt" || die "Bird Watch, the application (vbasic.py app)"
printf '42\n' > "$DEST/DOCUMENTS/Garden.brd"
$PY tools/fontcvt.py testfonts "$WORK/fonts" >/dev/null || die "the fonts"
cp "$WORK/fonts/Gate Sans.cvt" "$WORK/fonts/Gate Mono.cvt" "$V/FONTS/"
$PY tools/mkwall.py -o "$V/WALLPAPERS/Dusk.cvt" --dusk 2>/dev/null || die "the wallpaper"
$PY tools/wallpapers/mkmeadow.py -o "$V/WALLPAPERS/Meadow.cvt" || die "the Meadow wallpaper (tools/wallpapers/mkmeadow.py needs Pillow)"
say "Veranda: the system, the shell, Setup, $(ls "$V/APPS" | wc -l) programs, 2 fonts, 2 wallpapers"

# ---------------------------------------------------------------- the ports
# export <tree> <ref...>: the first ref that exists, into $WORK/src/<name>
export_tree() {
	local tree=$1 name ref r
	name=$(basename "$tree")
	shift
	for r in "$@"; do
		if git -C "$tree" rev-parse --verify -q "$r^{commit}" >/dev/null; then
			ref=$r
			break
		fi
	done
	[ -n "$ref" ] || return 1
	mkdir -p "$WORK/src/$name"
	git -C "$tree" archive "$ref" | tar -x -C "$WORK/src/$name"
	echo "$name at $ref ($(git -C "$tree" rev-parse --short "$ref"))" >> "$WORK/refs.txt"
	echo "$WORK/src/$name"
}
port() {                # $1 the name, $2 the tree, $3 the refs (space separated), $4 the build, $5 its output, $6 the name on the card
	local s
	if [ ! -d "$2/.git" ] && [ ! -f "$2/.git" ]; then
		say "WARNING: no $1 ($2): left out"
		return
	fi
	# shellcheck disable=SC2086
	s=$(export_tree "$2" $3) || { say "WARNING: $1: none of the refs $3: left out"; return; }
	(cd "$s" && eval "$4") >"$WORK/b/$1.log" 2>&1 || { tail "$WORK/b/$1.log"; die "$1 does not build"; }
	cp "$s/$5" "$V/APPS/$6"
	say "$1: $(tail -1 "$WORK/refs.txt")"
}
MONO=0
if [ -f "$TOP/../apps/calc/Makefile" ] && [ -f "$TOP/../apps/modplayer/Makefile" ] && [ "${MKDIST_SIBLINGS:-0}" = 0 ]; then
	MONO=1
fi
# local <name> <apps dir> <the build> <its output> <the name on the card>:
# an application of this repository's ../apps, copied into $WORK/src
local_app() {
	local s=$WORK/src/$(basename "$2") rev
	[ -d "$2" ] || die "$1: no $2"
	mkdir -p "$s"
	tar -C "$2" --exclude=./build -cf - . | tar -x -C "$s"
	(cd "$s" && eval "$3") >"$WORK/b/$1.log" 2>&1 || { tail "$WORK/b/$1.log"; die "$1 does not build"; }
	cp "$s/$4" "$V/APPS/$5"
	rev=$(git -C "$2" log -1 --format=%h -- . 2>/dev/null || true)
	echo "$1 from apps/$(basename "$2")${rev:+ ($rev)}" >> "$WORK/refs.txt"
	say "$1: apps/$(basename "$2")"
}
if [ $MONO = 1 ]; then
	APPS=$(cd "$TOP/../apps" && pwd)
	local_app "Veranda Calc" "$APPS/calc" "make veranda SDK='$SDK'" "build/veranda/Veranda Calc.cvt" "Veranda Calc.cvt"
	local_app "MOD Player" "$APPS/modplayer" "make veranda VSDK='$SDK'" "build/veranda/MOD Player.cvt" "MOD Player.cvt"
	local_app Solitaire "$APPS/solitaire" "make -C veranda SDK='$SDK'" "build/veranda/Veranda Solitaire.cvt" Solitaire.cvt
	local_app "Lighthouse Pinball" "$APPS/pinball" "make -C veranda SDK='$SDK'" "build/veranda/Lighthouse Pinball.cvt" "Lighthouse Pinball.cvt"
	local_app "Harbour Walls" "$APPS/harbour-walls" "make SDK='$SDK'" "build/app/Harbour Walls.cvt" "Harbour Walls.cvt"
	local_app "SID Player" "$APPS/sidplay" "make build/app/app.cvt VSDK='$SDK'" build/app/app.cvt "SID Player.cvt"
else
	port geoWrite "$GEOWRITE" "${GEOWRITE_REF:-main}" "make SDK='$SDK' STAGE=8d" build/geoWrite.cvt geoWrite.cvt
	port "Veranda Calc" "$CALC" "${CALC_REF:-layout veranda}" "make veranda SDK='$SDK'" "build/veranda/Veranda Calc.cvt" "Veranda Calc.cvt"
	port geoChess "$GEOCHESS" "${GEOCHESS_REF:-veranda}" "make -C veranda SDK='$SDK'" build/veranda/geoChess.cvt geoChess.cvt
	port TopDesk "$TOPDESK" "${TOPDESK_REF:-veranda}" "make SDK='$SDK' STAGE=td3 MODE=640x240" build/TopDesk.cvt TopDesk.cvt
	port "MOD Player" "$X16MOD" "${X16MOD_REF:-layout veranda}" "make veranda VSDK='$SDK'" "build/veranda/MOD Player.cvt" "MOD Player.cvt"
	port Solitaire "$SOLITAIRE" "${SOLITAIRE_REF:-layout main}" "make -C veranda SDK='$SDK'" "build/veranda/Veranda Solitaire.cvt" Solitaire.cvt
fi

# ---------------------------------------------------------------- README
{
	echo "VERANDA - built $(date +%Y-%m-%d) (Veranda $(git rev-parse --short HEAD))"
	cat <<'EOF'

THE FOLDERS (DECISIONS V71)
  /VERANDA            the system: VERANDA.PRG, VERANDA.SYS, AUTOBOOT.X16,
                      RBOOT, SHELL.cvt (the desktop), Veranda Setup
  /VERANDA/APPS       the applications and desk accessories
  /VERANDA/FONTS      fonts every program can load (Gate Sans, Gate Mono)
  /VERANDA/WALLPAPERS desktop pictures: Dusk (the default) and Meadow;
                      Preferences > Desktop chooses one, or a plain colour,
                      or a pattern.
                      Your own: tools/mkwall.py picture.jpg (in the SDK)
  /VERANDA/HELP       VeraBASIC's help files (F1 in the editor), the manual
                      in help form, Reference card.pdf
  /VERANDA/.SYSTEM    hidden: the settings, alarms, the clipboard, the Note
                      File, the albums, the shell's desk file, the trash
  /DOCUMENTS          your files: where the shell opens and programs save

START
  DOS"CD:VERANDA"
  BOOT            (or LOAD"VERANDA.PRG" and RUN)
The shell opens in /DOCUMENTS; disk > documents opens it again, the card
icon the card's top folder. Double-click a document to open it with its
application from /VERANDA/APPS (.X16S .XLSX .ODS .CSV Veranda Calc, .TXT
Veranda Edit, .MOD MOD Player, .SID SID Player, .D64 .D71 .D81 Archive,
.TEL Term, .BMP View, .BAS VeraBASIC). BASIC (keep Veranda): back with
SYS 35328, or LOAD"RBOOT" and RUN (in /VERANDA).

VERANDA FTP
  An FTP client through TexElec's Serial & ESP32 Network Card (its Wi-Fi
  modem): the card and a server side by side. Connection > Sites... saves
  servers in /VERANDA/.SYSTEM/FTP.SITES; the passwords there are stored
  as plain text.

VERABASIC
  VeraBASIC is a BASIC with Veranda's windows, menus, dialogs and files:
  the editor (VeraBASIC: write, Check with Ctrl+K, Run with Ctrl+R, F1 for
  help) and VeraBASIC Run for finished programs. Example programs and the
  tutorial's seven steps are in /DOCUMENTS/VeraBASIC Examples. File > Make
  application... turns a program into an application of its own: Bird
  Watch in /VERANDA/APPS is one (it opens Garden.brd in /DOCUMENTS).

A CARD FROM BEFORE (everything in /VERANDA)
  Copy this package's VERANDA folder over the card's (replace the files)
  and its DOCUMENTS folder to the card's top. At the next start the shell
  runs Veranda Setup once: the settings, the Note File, the scraps and the
  trash go to /VERANDA/.SYSTEM, the applications to /VERANDA/APPS (an old
  copy of one that APPS has now goes to /VERANDA/.SYSTEM/OLD), the fonts
  to FONTS, your documents (.cvt documents, .TXT, .BMP, .MOD ...) to
  /DOCUMENTS (a name taken there gets ~2). Programs (.PRG) and your own
  folders stay where they are. Nothing is deleted; what moved where is in
  /VERANDA/.SYSTEM/LAYOUT.LOG.

EOF
	if [ $MONO = 0 ]; then
		echo
		echo "geoWrite, TopDesk and geoChess are ports for personal use: do not share"
		echo "this folder."
	fi
	echo
	echo "BUILT FROM"
	echo "  Veranda $(git rev-parse --short HEAD)"
	sed 's/^/  /' "$WORK/refs.txt" 2>/dev/null || true
} > "$V/README.txt"

# nothing of a test may go in
bad=$(find "$DEST" \( -iname '*.TST' -o -iname '*.STP' -o -iname '*.PLY' -o -iname 'dump.bin' -o -iname 'console.txt' \
	-o -iname '*.map' -o -iname '*.bin' -o -iname 'TEST.PRG' -o -iname 'AUTOSTART.cvt' \) -print)
[ -z "$bad" ] || die "test files in the package: $bad"
say "the package in $DEST: $(find "$DEST" -type f | wc -l) files"
find "$DEST" -type f | sed "s|^$DEST/|  |" | sort
