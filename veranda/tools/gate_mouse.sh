#!/bin/bash
# Gate mouse: the menus through the X16's real mouse path (DECISIONS V59).
# The other gates move the pointer through mouseSource (a script routine:
# absolute positions); here it comes from the ROM's PS/2 mouse driver as
# on a real X16: the emulator's -mousescript (branch mousescript of
# ~/Projects/x16-emulator, built in ~/Projects/x16-emulator-mouse) puts
# PS/2 packets into the SMC's mouse queue, the KERNAL's mouse_scan reads
# them, the kernel follows mouse_get relatively (re-centring the device
# near its edges).
#
#   tools/gate_mouse.sh      (EMU: an x16emu with -mousescript; ROM:
#                             default R49's release image if it is there)
#
# A click as a person makes it: a press, 9 frames (150 ms) held, a
# release, the mouse not moving. The pointer is first pushed to 0, 0 (a
# move of -2000, -2000), then moved relatively onto a menu in the bar.
# 1. the shell (device ID 3): geos opens and stays open, the pointer on
#    its first item; 4 rows of items down, "320 x 240" clicked the same
#    way: the shell comes back at 320x240 (screenWidth)
# 2. Paint (device ID 4: a wheel step and button 4 while the button is
#    held): options opens and stays open; its first item, about, clicked
#    the same way: the box is drawn
# 3. Paint (device ID 0, 3-byte packets; 150 rows down and back, which
#    re-centres the device twice, before the click): edit stays open
# Each: menuNumber, mouseOn, the pointer in the sub menu and mouseYPos the
# byte view of mouseYPosW, from the dump's RAM. Pictures in
# build/gate/mouse/<run>.png.
set -e -o pipefail
cd "$(dirname "$0")/.."
OUT=build/gate/mouse
PY="python3 -I -B"
EMU=${EMU:-$HOME/Projects/x16-emulator-mouse/build/x16emu}
R49=$HOME/Downloads/Release.R49.ROM.Image/rom.bin
[ -f "$R49" ] || R49=$HOME/Projects/x16-rom/build/x16/rom.bin
ROM=${ROM:-$R49}
export CC65_HOME=${CC65_HOME:-$HOME/.local/share/cc65}
export PATH=$CC65_HOME/bin:$PATH

ok() { echo "ok    $*"; }
fail() { echo "FAIL  $*"; exit 1; }

if ! { "$EMU" -help 2>&1 || true; } | grep -c -- -mousescript >/dev/null; then
	echo "SKIP  $EMU has no -mousescript (the emulator's branch mousescript)"
	echo "GATE mouse: SKIP"
	exit 0
fi
rm -rf "$OUT"
mkdir -p "$OUT"
TOP=$(pwd)
echo "      ROM: $ROM"
$PY tools/apigen.py api/veranda.api build/gen >/dev/null || fail "the API table"
$PY tools/kbuild.py >/dev/null || fail "the kernel does not build"
tools/mksdk.sh "$OUT/sdk" >/dev/null || fail "the SDK does not install"
SDK=$(cd "$OUT/sdk" && pwd)
cp -r shell "$OUT/shell"
rm -f "$OUT/shell"/*.cvt "$OUT/shell"/*.bin "$OUT/shell"/*.map
make -s -C "$OUT/shell" SDK="$SDK" >"$OUT/shell.log" 2>&1 || { cat "$OUT/shell.log"; fail "the shell does not build"; }
mkdir -p "$OUT/paint"
make -s -C apps/paint SDK="$SDK" OUT="$TOP/$OUT/paint" >"$OUT/paint.log" 2>&1 \
	|| { cat "$OUT/paint.log"; fail "Paint does not build"; }
ok "the kernel, the shell and Paint build"

# run $1 ($OUT/$1.txt, the mouse script) with CVT $2 as AUTOSTART.cvt
run() {
	local d=$OUT/$1
	mkdir -p "$d"
	cp "$2" "$d/AUTOSTART.cvt"
	cp build/VERANDA.PRG build/VERANDA.SYS "$d/"
	cp "$OUT/$1.txt" "$d/mouse.txt"
	(cd "$d" && SDL_VIDEODRIVER=dummy timeout -s INT 60 "$EMU" -rom "$ROM" -prg VERANDA.PRG -run \
		-warp -sound none -echo -dump RV -mousescript mouse.txt > console.txt 2>&1) || true
	grep -a -q "the mouse script's exit" "$d/console.txt" && [ -f "$d/dump.bin" ] \
		|| { tail -5 "$d/console.txt"; fail "$1: the run did not reach its end"; }
	$PY tools/vram2png.py --color "$d/dump.bin" "$OUT/$1.png" $3 >/dev/null
}

# the dump of run $1: menu level $2; with $3 the screen's width
state() {
	$PY - "$OUT/$1/dump.bin" "$1" "$2" "$3" <<-'EOF'
	import sys
	d = open(sys.argv[1], 'rb').read()
	name, level, width = sys.argv[2], int(sys.argv[3]), sys.argv[4]
	w = lambda a: d[a] | d[a + 1] << 8
	x, y, yb, menu, on, sw = w(0x3A), w(0x560), d[0x3C], d[0x53F], d[0x30], w(0x56C)
	print('      %s: pointer %d, %d (mouseYPos %d), menuNumber %d, mouseOn $%02X, screen %d wide'
	      % (name, x, y, yb, menu, on, sw))
	bad = []
	if menu != level:
	    bad.append('menuNumber %d, not %d' % (menu, level))
	if level and not on & 0x40:
	    bad.append('the menus are off')
	if yb != (y if y < 256 else 255):
	    bad.append('mouseYPos %d is not the byte view of mouseYPosW %d' % (yb, y))
	if level and not (12 < y < 100):
	    bad.append('the pointer (y %d) is not in the sub menu' % y)
	if width and sw != int(width):
	    bad.append('the screen is %d wide, not %s' % (sw, width))
	print(('FAIL  ' if bad else 'ok    ') + name + ': ' + ('; '.join(bad) if bad else
	      ('menu level %d open, the pointer in it' % level if level else 'no menu open') + ', mouseYPos current'))
	sys.exit(1 if bad else 0)
	EOF
}

start() {           # device ID $1; the pointer to 0, 0 and then to x $2 in the bar
	echo "0 id $1"
	echo "400 move -2000 -2000"
	echo "440 move $2 7"
}

# 1. the shell: geos, then options > 320 x 240 (Shell 2's menus, V70)
{ start 3 15; echo "460 down"; echo "469 up"; echo "530 exit"; } > "$OUT/shell-geos.txt"
run shell-geos "$OUT/shell/SHELL.cvt"
state shell-geos 1 || fail "the shell's geos menu did not stay open"
{ start 3 300; echo "460 down"; echo "469 up"; echo "500 move 0 32"; echo "520 down"; echo "529 up"; echo "800 exit"; } \
	> "$OUT/shell-mode.txt"
run shell-mode "$OUT/shell/SHELL.cvt" "320 240"
state shell-mode 0 320 || fail "options: 320 x 240 chosen by a still click"
ok "the shell: geos stays open after a click held 150 ms, the mouse still; options > 320 x 240 chosen the same way works"
# a window dragged by its title bar (moves of up to 100 a packet, as a
# hand makes them), then a double click on the card's desktop icon
{ echo "0 id 3"; echo "400 move -2000 -2000"; echo "440 move 100 10"; echo "445 move 100 26"; echo "460 down"
  echo "470 move 40 40"; echo "475 move 40 40"; echo "485 up"
  for f in 540 542 544 546; do echo "$f move 80 -18"; done
  echo "560 down"; echo "564 up"; echo "568 down"; echo "572 up"; echo "660 exit"; } > "$OUT/shell-drag.txt"
run shell-drag "$OUT/shell/SHELL.cvt"
log=$(tr -d '\r' < "$OUT/shell-drag/console.txt" | grep -a -o "SHELL WINDOW [0-9] [A-Z]* [/0-9 ]*" | tr '\n' ',')
[ "$log" = "SHELL WINDOW 0 AT 96 112,SHELL WINDOW 1 OPEN /," ] || fail "the shell's window drag and double click: $log"
state shell-drag 0 || fail "the shell after the drag"
ok "the shell: window 0 dragged by its title bar (to 96, 112: 80 across, 80 down), the card's icon double-clicked (window 1 on the top folder): $OUT/shell-drag.png"

# 2. Paint, ID 4: the Paint menu (V79: its about), then about
P=$OUT/paint/Paint.cvt
opts() {
	start 4 32
	echo "460 down"
	echo "462 wheel 1"
	echo "463 down 16"
	echo "465 up 16"
	echo "466 wheel -1"
	echo "469 up"
}
{ opts; echo "530 exit"; } > "$OUT/paint-options.txt"
run paint-options "$P"
state paint-options 1 || fail "Paint's options menu did not stay open"
{ opts; echo "520 down"; echo "529 up"; echo "600 exit"; } > "$OUT/paint-about.txt"
run paint-about "$P"
state paint-about 0 || fail "options: about"
$PY - "$OUT/paint-options/dump.bin" "$OUT/paint-about/dump.bin" <<-'EOF' || fail "options: about: no box"
	import sys
	VDUMP = 132878
	a, b = (open(p, 'rb').read() for p in sys.argv[1:])
	a, b = a[len(a) - VDUMP:], b[len(b) - VDUMP:]
	# the box: rows 192-287, x 224-415 (bytes 28-51 of 80 a row)
	n = sum(a[r * 80 + c] != b[r * 80 + c] for r in range(192, 288) for c in range(28, 52))
	print(('ok    ' if n > 100 else 'FAIL  ') + 'options: about: the box drawn (%d bytes changed there)' % n)
	sys.exit(0 if n > 100 else 1)
	EOF
ok "Paint (device ID 4, the wheel and button 4 while held): options stays open; about chosen the same way works"

# 3. Paint, ID 0, the device re-centred before the click
{ start 0 35; echo "450 move 0 150"; echo "455 move 0 -150"; echo "460 down"; echo "469 up"; echo "530 exit"; } \
	> "$OUT/paint-edit.txt"
run paint-edit "$P"
state paint-edit 1 || fail "Paint's edit menu did not stay open"
ok "Paint (device ID 0, 3-byte packets, the device re-centred): edit stays open"
echo "GATE mouse: PASS"
