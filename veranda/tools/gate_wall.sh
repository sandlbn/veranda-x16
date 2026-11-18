#!/bin/bash
# Gate wall (Veranda Look 2, step C: the desktop's picture; DECISIONS V78)
#
#   tools/gate_wall.sh
#
# 1. the conversion (tools/mkwall.py): the built-in dusk scene and a
#    picture (a PNG made here) into wallpaper files: GEOS documents
#    (APPL_DATA, class Wallpaper, sequential), two variants of entries
#    12-15, 4,800 card colours (2-15) and a 38,400-byte bitmap; the light
#    variant as VERA shows it near the picture (8 x 8 averages), the dark
#    one different
# 2. the drawing (tests/check_wall.py reads the dump): the shell in the
#    card's layout (DECISIONS V71) with /VERANDA/WALLPAPERS/Dusk.cvt and
#    no WALLPAPER.CFG: every desktop card outside window 0 and the icons
#    shows the light variant (bitmap on both screens, colours, entries
#    12-15); on the host folder and an SD card image
# 3. window 0 dragged 80 right and down: what it uncovered is the picture
#    again (the time the move took, under the R49 ROM too); then closed,
#    the Veranda menu opened and closed, Veranda > About and OK: the whole
#    desktop is the picture
# 4. dark mode (the settings' byte 14): the dark variant
# 5. options > 640 x 240: every other row and card row
# 6. Preferences over the shell (its Desktop page): a plain colour (the
#    desktop the theme's colour 13, no ink; WALLPAPER.CFG written), then
#    Choose: the second picture of /VERANDA/WALLPAPERS: the shell shows it
# 7. without the picture file: the pattern, as before (the other gates)
# PNGs: build/gate/wall/desktop.png, desktop-dark.png, moved.png ...
set -e
cd "$(dirname "$0")/.."
OUT=build/gate/wall
PY="python3 -I -B"
EMU=${EMU:-$HOME/Projects/x16-emulator/build/x16emu}
ROM=${ROM:-$HOME/Projects/x16-rom/build/x16/rom.bin}
R49=${R49:-$HOME/Downloads/Release.R49.ROM.Image/rom.bin}
export CC65_HOME=${CC65_HOME:-$HOME/.local/share/cc65}

ok() { echo "ok    $*"; }
fail() { echo "FAIL  $*"; exit 1; }

rm -rf "$OUT"
mkdir -p "$OUT"
$PY tools/apigen.py api/veranda.api build/gen >/dev/null || fail "the API table"
$PY tools/kbuild.py >/dev/null || fail "the kernel does not build"
tools/mksdk.sh "$OUT/sdk" >/dev/null || fail "the SDK does not install"
SDK=$(cd "$OUT/sdk" && pwd)
TOP=$(pwd)
[ -f "$SDK/tools/mkwall.py" ] && [ -f "$SDK/include/wallcfg.h" ] || fail "the SDK has no mkwall.py or wallcfg.h"

# ---------------------------------------------------------------- 1. the conversion
W=$OUT/wall
mkdir -p "$W"
$PY "$SDK/tools/mkwall.py" -o "$W/Dusk.cvt" --dusk 2>/dev/null || fail "mkwall --dusk"
field() { $PY "$SDK/tools/vpack.py" --list "$1" | sed -n "s/^$2 *//p"; }
[ "$(field "$W/Dusk.cvt" 'geos type')" = 7 ] && [ "$(field "$W/Dusk.cvt" class)" = "Wallpaper   V1.0" ] \
	&& [ "$(field "$W/Dusk.cvt" structure)" = sequential ] && [ "$(field "$W/Dusk.cvt" size)" = 86432 ] \
	|| fail "Dusk.cvt: not a sequential APPL_DATA document of class Wallpaper with 86,432 bytes of data"
$PY "$SDK/tools/mkwall.py" --check "$W/Dusk.cvt" > "$W/check.txt" || fail "mkwall --check Dusk.cvt"
ok "mkwall --dusk: Dusk.cvt, a GEOS document (APPL_DATA, class Wallpaper V1.0), two variants: $(tr '\n' ';' < "$W/check.txt")"
# a picture: a gradient with a sun and a band of green, 800 x 600 (cropped to 4:3, resized)
$PY - "$W/scene.png" "$W/scene640.png" <<'EOF'
import sys, math
from PIL import Image
img = Image.new('RGB', (800, 600))
px = img.load()
for y in range(600):
    for x in range(800):
        r, g, b = 255 - y * 200 // 600, 120 + y * 100 // 600, 200 - y // 6
        if (x - 560) ** 2 + (y - 180) ** 2 < 60 ** 2:
            r, g, b = 250, 220, 90
        if y > 430 + 30 * math.sin(x / 60):
            r, g, b = 60, 150 - (y - 430) // 3, 70
        px[x, y] = (r, g, b)
img.save(sys.argv[1])
img.resize((640, 480), Image.LANCZOS).save(sys.argv[2])
EOF
$PY "$SDK/tools/mkwall.py" -o "$W/Test.cvt" --name Test "$W/scene.png" 2>/dev/null || fail "mkwall scene.png"
$PY "$SDK/tools/mkwall.py" --check "$W/Test.cvt" > "$W/check2.txt" || fail "mkwall --check Test.cvt"
$PY tests/check_wall.py diff "$W/Test.cvt" "$W/scene640.png" || fail "Test.cvt is not near its picture"
$PY tests/check_wall.py diff "$W/Dusk.cvt" "$W/scene640.png" >/dev/null 2>&1 && fail "the check passes Dusk.cvt for another picture"
$PY "$SDK/tools/mkwall.py" --png "$OUT/test-light.png" "$W/Test.cvt"
$PY "$SDK/tools/mkwall.py" --png "$OUT/dusk-light.png" "$W/Dusk.cvt"
$PY "$SDK/tools/mkwall.py" --png "$OUT/dusk-dark.png" --dark "$W/Dusk.cvt"
ok "mkwall scene.png (800 x 600): Test.cvt; $(tr '\n' ';' < "$W/check2.txt") (rendered: $OUT/test-light.png, dusk-light.png, dusk-dark.png)"

# an application whose file name is not its GEOS name (V77's bug): Solitaire
printf 'name = Solitaire\nclass = Solitaire\nmode = 640x480\napi = 1\n' > "$W/sol.hdr"
printf '\x60' > "$W/sol.bin"
$PY "$SDK/tools/vpack.py" --load 0x0801 -o "$W/Veranda Solitaire.cvt" "$W/sol.hdr" "$W/sol.bin" >/dev/null || fail "vpack Solitaire"

# ---------------------------------------------------------------- the shell, Preferences
cp -r shell "$OUT/src"
rm -f "$OUT/src"/*.cvt "$OUT/src"/*.bin "$OUT/src"/*.map
make -s -C "$OUT/src" SDK="$SDK" >"$OUT/build.log" 2>&1 || { cat "$OUT/build.log"; fail "the shell does not build"; }
$PY tools/check_ovl.py shell || fail "a call between overlays not through XCALL (tools/check_ovl.py)"
H=$TOP/$OUT/h
$PY tests/wall_script.py prefs "$H" || fail "tests/wall_script.py prefs"
for s in wall1 wall2; do
	mkdir -p "$OUT/$s"
	make -s -C apps/prefs SDK="$SDK" OUT="$TOP/$OUT/$s" test TESTFLAGS="-I$H/$s" >"$OUT/$s/build.log" 2>&1 \
		|| { cat "$OUT/$s/build.log"; fail "Preferences ($s)"; }
done
ok "the shell (eleven overlays: wall.c the picture) and Preferences' test builds"

# a run folder in the card's layout: $1 the folder, $2 the script, $3 the
# wallpapers (names in $W), $4 a settings file's arguments (check_theme.py
# prefs) or "", $5 a Preferences build or ""
folder() {
	rm -rf "$1"
	mkdir -p "$1/VERANDA/WALLPAPERS" "$1/VERANDA/.SYSTEM" "$1/DOCUMENTS"
	cp "$OUT/src/SHELL.cvt" "$1/VERANDA/"
	cp build/VERANDA.SYS "$1/"
	local f
	for f in $3; do cp "$W/$f" "$1/VERANDA/WALLPAPERS/"; done
	[ -z "$4" ] || $PY tests/check_theme.py prefs "$1/VERANDA/.SYSTEM/Veranda Prefs.cvt" $4
	[ -z "$5" ] || cp "$5/Preferences.cvt" "$1/DOCUMENTS/"
	echo "a note" > "$1/DOCUMENTS/Note.txt"
	[ "$2" != probe ] || cp "$W/Veranda Solitaire.cvt" "$1/DOCUMENTS/"
	$PY tests/wall_script.py shell "$1" "$2" || fail "tests/wall_script.py shell $2"
}
# $1 the folder, $2 the ROM, the rest the emulator's; on the host folder
run() {
	local d=$1 rom=$2
	shift 2
	cp build/VERANDA.PRG "$d/"
	(cd "$d" && SDL_VIDEODRIVER=dummy timeout -s INT 120 "$EMU" -rom "$rom" -prg VERANDA.PRG -run \
		-warp -sound none -echo -dump V "$@" > console.txt 2>&1) || true
	set -- "$d"
	[ -f "$1/dump.bin" ] || { tail -20 "$1/console.txt"; fail "$1: the session did not end (no dump)"; }
	grep -a -q "SHELL DONE" "$1/console.txt" || fail "$1: the shell's script did not end"
}
# the same on an SD card image of folder $1 (without the host run's
# files; the dump and the console in $1.sd)
runsd() {
	local d=$1.sd
	rm -rf "$d"
	mkdir -p "$d/card"
	cp -r "$1"/. "$d/card/"
	rm -f "$d/card/SHELL.STP" "$d/card/dump.bin" "$d/card/console.txt" "$d/card/VERANDA.PRG"
	$PY tools/vcard.py make --mb 64 "$d/sd.img" "$d/card" >/dev/null || fail "vcard make"
	cp build/VERANDA.PRG "$d/"
	(cd "$d" && SDL_VIDEODRIVER=dummy timeout -s INT 120 "$EMU" -rom "$2" -sdcard sd.img -prg VERANDA.PRG \
		-run -warp -sound none -echo -dump V > console.txt 2>&1) || true
	[ -f "$d/dump.bin" ] || { tail -20 "$d/console.txt"; fail "$1 (SD card): the session did not end"; }
}
log() { tr -d '\r' < "$1" | grep -a -o -E 'SHELL (WALLPAPER|MOVED|WINDOW 0 AT)[A-Za-z0-9 .,]*' | sed 's/ *$//' | tr '\n' ',' ; }
png() { $PY tools/vram2png.py --color "$1" "$2" 2>/dev/null || true; }
CHK="$PY tests/check_wall.py"
WIN0=2,4,62,50                  # window 0 in its default place, with its shadow (cards)

# ---------------------------------------------------------------- 2. the drawing
R=$OUT/run/probe
folder "$R" probe "Dusk.cvt" "" ""
run "$R" "$ROM"
got=$(log "$R/console.txt")
case "$got" in "SHELL WALLPAPER Dusk.cvt LIGHT, READ IN "*",SHELL WALLPAPER SHOWN,") ;; *) fail "the console has $got" ;; esac
$CHK picture "$R/dump.bin" "$W/Dusk.cvt" 0 --skip $WIN0 || fail "the desktop is not the picture"
png "$R/dump.bin" "$OUT/desktop.png"
ok "the shell with /VERANDA/WALLPAPERS/Dusk.cvt (no WALLPAPER.CFG: the default): $(echo "$got" | cut -d, -f1-2) cycles: $OUT/desktop.png"
$PY tests/check_theme.py tiles "$R/dump.bin" 1:3,2:8 || fail "the tiles: Veranda Solitaire.cvt (GEOS name Solitaire) is not known by its class"
ok "an application whose file name is longer than its GEOS name (Veranda Solitaire.cvt, Solitaire: V77's bug) shows its class's tile (green), not a document's"
for v in light:: dark:"5 13 4 0 129":1; do
	n=${v%%:*}; p=${v#*:}; p=${p%:*}; k=${v##*:}
	R=$OUT/run/r49-$n
	folder "$R" probe "Dusk.cvt" "$p" ""
	run "$R" "$R49"
	$CHK picture "$R/dump.bin" "$W/Dusk.cvt" ${k:-0} --skip $WIN0 >/dev/null || fail "R49, $n: the desktop is not the picture"
	png "$R/dump.bin" "$OUT/desktop-r49-$n.png"
done
ok "under the R49 ROM: the desktop light and dark: $OUT/desktop-r49-light.png, desktop-r49-dark.png"
R=$OUT/run/probe
runsd "$R" "$ROM"
$CHK picture "$R.sd/dump.bin" "$W/Dusk.cvt" 0 --skip $WIN0 || fail "SD card: the desktop is not the picture"
ok "on an SD card image: $(log "$R.sd/console.txt" | cut -d, -f1-2) cycles"

# ---------------------------------------------------------------- 3. a window moved, closed; a menu, a box
R=$OUT/run/move
folder "$R" move "Dusk.cvt" "" ""
run "$R" "$ROM"
got=$(log "$R/console.txt")
echo "$got" | grep -q "SHELL WINDOW 0 AT 96 112" || fail "the drag: $got"
$CHK picture "$R/dump.bin" "$W/Dusk.cvt" 0 --skip 12,14,72,59 || fail "after the move"
png "$R/dump.bin" "$OUT/moved.png"
mv=$(echo "$got" | grep -o 'MOVED IN [0-9]*' | grep -o '[0-9]*')
ok "window 0 dragged to 96, 112: what it uncovered is the picture again; the move (CopyRect, the uncovered parts drawn, the shadow, the colours) took $mv cycles ($((mv / 8000)) ms at 8 MHz): $OUT/moved.png"
cp -r "$R" "$OUT/run/move49"
rm -f "$OUT/run/move49/dump.bin" "$OUT/run/move49/SHELL.STP"
run "$OUT/run/move49" "$R49"
$CHK picture "$OUT/run/move49/dump.bin" "$W/Dusk.cvt" 0 --skip 12,14,72,59 || fail "after the move (R49)"
mv49=$(log "$OUT/run/move49/console.txt" | grep -o 'MOVED IN [0-9]*' | grep -o '[0-9]*')
png "$OUT/run/move49/dump.bin" "$OUT/moved-r49.png"
ok "under the R49 ROM: the same, the move $mv49 cycles ($((mv49 / 8000)) ms): $OUT/moved-r49.png"
R=$OUT/run/closed
folder "$R" closed "Dusk.cvt" "" ""
run "$R" "$ROM"
$CHK picture "$R/dump.bin" "$W/Dusk.cvt" 0 || fail "after the window closed, the menu and the box"
png "$R/dump.bin" "$OUT/closed.png"
ok "window 0 closed, the Veranda menu opened and closed, About and its OK: the whole desktop is the picture: $OUT/closed.png"

# ---------------------------------------------------------------- 4. dark mode
R=$OUT/run/dark
folder "$R" probe "Dusk.cvt" "5 13 4 0 129" ""
run "$R" "$ROM"
got=$(log "$R/console.txt")
case "$got" in "SHELL WALLPAPER Dusk.cvt DARK, READ IN "*) ;; *) fail "dark mode: the console has $got" ;; esac
$CHK picture "$R/dump.bin" "$W/Dusk.cvt" 1 --skip $WIN0 || fail "dark mode: the desktop is not the dark variant"
png "$R/dump.bin" "$OUT/desktop-dark.png"
ok "dark mode (the settings' byte 14): the dark variant, its entries 12-15 over the dark palette: $OUT/desktop-dark.png"

# ---------------------------------------------------------------- 5. 640 x 240
R=$OUT/run/mode
folder "$R" mode "Dusk.cvt" "" ""
run "$R" "$ROM"
$CHK picture "$R/dump.bin" "$W/Dusk.cvt" 0 --tall 0 --skip 2,3,62,28 || fail "640 x 240"
png "$R/dump.bin" "$OUT/mode240.png" 640 240
ok "options > 640 x 240: every other row and card row of the picture: $OUT/mode240.png"

# ---------------------------------------------------------------- 6. Preferences
# (at 512 KB the shell's 17 banks, the picture's 5 and the 22 an accessory
# puts the shell aside in do not fit with the fonts: the picture's are lent
# to it and read again after it, nothing drawn again; at 2 MB they stay)
plog() { tr -d '\r' < "$1" | grep -a -o -E 'PREFS (DESKTOP|PICTURE) [A-Za-z]+( colour)? ?[A-Za-z]* ?[0-9]*|SHELL WALLPAPER ([A-Z]{3,}|[A-Za-z]+\.cvt)' | sed 's/ *$//' | tr '\n' ',' ; }
R=$OUT/run/prefs
folder "$R" desk "Dusk.cvt Test.cvt" "" "$OUT/wall1"
run "$R" "$ROM"
got=$(plog "$R/console.txt")
[ "$got" = "SHELL WALLPAPER Dusk.cvt,SHELL WALLPAPER SHOWN,PREFS DESKTOP PAGE,PREFS DESKTOP Plain colour Dusk 0,SHELL WALLPAPER PLAIN," ] \
	|| fail "Preferences, plain: $got"
[ -f "$R/VERANDA/.SYSTEM/WALLPAPER.CFG" ] || fail "no WALLPAPER.CFG"
[ "$(od -An -tu1 -N5 "$R/VERANDA/.SYSTEM/WALLPAPER.CFG" | tr -s ' ')" = " 87 80 1 1 0" ] || fail "WALLPAPER.CFG: $(od -An -tu1 -N5 "$R/VERANDA/.SYSTEM/WALLPAPER.CFG")"
$CHK plain "$R/dump.bin" 13 --skip $WIN0 || fail "the plain desktop"
png "$R/dump.bin" "$OUT/plain.png"
ok "Preferences' Desktop page over the shell (512 KB: since V83 the accessory's put-aside is packed: the picture keeps its banks): Plain colour, Save: WALLPAPER.CFG (kind 1); the shell at once: the theme's desktop colour, no ink: $OUT/plain.png"
folder "$R" desk "Dusk.cvt Test.cvt" "" "$OUT/wall1"
run "$R" "$ROM" -ram 2048
got=$(plog "$R/console.txt")
[ "$got" = "SHELL WALLPAPER Dusk.cvt,SHELL WALLPAPER SHOWN,PREFS DESKTOP PAGE,PREFS DESKTOP Plain colour Dusk 0,SHELL WALLPAPER PLAIN," ] \
	|| fail "Preferences, plain, 2 MB: $got"
$CHK plain "$R/dump.bin" 13 --skip $WIN0 || fail "the plain desktop (2 MB)"
ok "the same at 2 MB: the picture keeps its banks"
folder "$R" desk "Dusk.cvt Test.cvt" "" "$OUT/wall2"
run "$R" "$ROM"
got=$(plog "$R/console.txt")
[ "$got" = "SHELL WALLPAPER Dusk.cvt,SHELL WALLPAPER SHOWN,PREFS DESKTOP PAGE,PREFS PICTURE Test,PREFS DESKTOP Picture Test 0,SHELL WALLPAPER Test.cvt,SHELL WALLPAPER SHOWN," ] \
	|| fail "Preferences, Choose: $got"
$CHK picture "$R/dump.bin" "$W/Test.cvt" 0 --skip $WIN0 || fail "the chosen picture"
png "$R/dump.bin" "$OUT/chosen.png"
ok "Preferences' Desktop page: Choose lists /VERANDA/WALLPAPERS (Dusk, Test), Test chosen, Save; the shell shows Test.cvt: $OUT/chosen.png"

# ---------------------------------------------------------------- 7. no picture
R=$OUT/run/none
folder "$R" probe "" "" ""
run "$R" "$ROM"
got=$(log "$R/console.txt")
[ "$got" = "SHELL WALLPAPER MISSING Dusk.cvt," ] || fail "no picture: the console has $got"
$PY tests/check_theme.py bars "$R/dump.bin" 5 13 4 || fail "no picture: not the desktop of before"
ok "no picture file: the pattern on the theme's desktop colour, as before V78"
echo "gate wall: ok"
