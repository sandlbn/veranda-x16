#!/bin/bash
# Gate switch (DECISIONS V83): the switcher, in the kernel.
#
#   tools/gate_switch.sh        (SWITCH_SHELL=0: skip the shell's dock part)
#
# 1. the kernel and the SDK; test programs from tests/switch_app.s: SwOne,
#    SwTwo (native, API 6, 640 x 480), SwPort (as a GEOS port: API 1,
#    640 x 240, no long names, a record file kept open), SwBig (it holds
#    every free RAM bank but the switcher's reserve); a test desktop
#    (tests/switch_desk.s as SHELL.cvt) that drives TaskCtl one step a
#    start (its comment)
# 2. a session (tests/check_switch.py): three programs started and put
#    aside, each brought back with its zero page, RAM, bank 1, both screens,
#    the colour map, the palette and the video mode as they were (the CRC
#    each logs before and after), put aside again, brought back and quit;
#    one dropped from the dock (TASK_END); SwBig cannot be put aside: the
#    kernel's box asks, Return, it quits; SwPort's record (written while it
#    was aside twice) read back and written to the card when it quits;
#    with a program aside, BASIC (keep Veranda) and SYS 35328: Veranda
#    back, nothing aside, every bank free again
# 3. at 512 KB and 2 MB on the host folder, and at 512 KB on an SD card
#    image under R49 (~/Downloads/Release.R49.ROM.Image/rom.bin); the
#    banks each program held and the time of each switch
# 4. the shell's dock (tests/dock_script.py; gate part 2)
set -e
cd "$(dirname "$0")/.."
OUT=${OUT:-build/gate/switch}
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
A=$OUT/apps
mkdir -p "$A"
app() {         # app <name> <n> <api> <mode> [defines]
	nm=$1 n=$2 api=$3 mode=$4
	shift 4
	stay=no
	[ "$1" = stay ] && { stay=yes; shift; }
	ca65 --cpu 65C02 -I "$SDK/include" -D APPN=$n "$@" -o "$A/$nm.o" tests/switch_app.s || fail "tests/switch_app.s ($nm)"
	ld65 -C "$SDK/cfg/app.cfg" -o "$A/$nm.bin" "$A/$nm.o" || fail "$nm does not link"
	printf 'name = %s\nclass = %s\nmode = %s\napi = %s\nstay = %s\nicon = %s\n' $nm $nm $mode $api $stay \
		"$SDK/template/c/icon.pbm" > "$A/$nm.hdr"
	# (V84) SwTwo has a tile of its own (Calc's): the dock shows it when
	# SwTwo is aside, from the path the shell gave it as its note
	[ "$nm" = SwTwo ] && echo "tile = $(pwd)/apps/calc/tile.txt" >> "$A/$nm.hdr"
	$PY "$SDK/tools/vpack.py" --load 0x0801 -o "$A/$nm.cvt" "$A/$nm.hdr" "$A/$nm.bin" >/dev/null || fail "vpack $nm"
}
app SwOne 1 6 640x480
app SwTwo 2 6 640x480
app SwPort 3 1 640x240 -D PORT
app SwBig 4 6 640x480 -D BIG
ca65 --cpu 65C02 -I "$SDK/include" -o "$A/desk.o" tests/switch_desk.s || fail "tests/switch_desk.s"
ld65 -C "$SDK/cfg/app.cfg" -o "$A/desk.bin" "$A/desk.o" || fail "the test desktop does not link"
printf 'name = SHELL\nclass = SwDesk\nmode = 640x480\napi = 6\nicon = %s\n' "$SDK/template/c/icon.pbm" > "$A/desk.hdr"
$PY "$SDK/tools/vpack.py" --load 0x0801 -o "$A/SHELL.cvt" "$A/desk.hdr" "$A/desk.bin" >/dev/null || fail "vpack the desktop"
ok "SwOne, SwTwo (native), SwPort (a GEOS port's ways), SwBig and a test desktop (tests/switch_desk.s) built"

files() {
	mkdir -p "$1"
	cp "$A"/Sw*.cvt "$A/SHELL.cvt" build/VERANDA.SYS "$1/"
}

check() {       # check <console> <what> <ram>
	$PY tests/check_switch.py "$1" "$3" || { tail -40 "$1"; fail "$2"; }
}

for ram in 512 2048; do
	H=$OUT/host-$ram
	files "$H"
	cp build/VERANDA.PRG "$H/"
	(cd "$H" && SDL_VIDEODRIVER=dummy timeout -s INT 300 "$EMU" -rom "$ROM" -ram $ram -prg VERANDA.PRG -run \
		-warp -sound none -echo -dump V > console.txt 2>&1) || true
	echo "== host folder, $ram KB"
	check "$H/console.txt" "host folder, $ram KB" $ram
	grep -q "Written back after" "$H/SwData.cvt" || fail "host folder, $ram KB: SwPort's record not written back to SwData.cvt"
	ok "host folder, $ram KB: SwData.cvt holds the record SwPort changed after it came back"
done

if [ -f "$R49" ]; then
	S=$OUT/sd49
	files "$S/card"
	$PY tools/vcard.py make --mb 64 "$S/sd.img" "$S/card" >/dev/null || fail "vcard make"
	cp build/VERANDA.PRG "$S/"
	(cd "$S" && SDL_VIDEODRIVER=dummy timeout -s INT 400 "$EMU" -rom "$R49" -ram 512 -sdcard sd.img -prg VERANDA.PRG \
		-run -warp -sound none -echo -dump V > console.txt 2>&1) || true
	echo "== SD card image, R49, 512 KB"
	check "$S/console.txt" "SD card, R49" 512
	$PY tools/vcard.py get "$S/sd.img" SwData.cvt "$S/SwData.cvt" >/dev/null || fail "SD card: no SwData.cvt"
	grep -q "Written back after" "$S/SwData.cvt" || fail "SD card: SwPort's record not written back"
	ok "SD card, R49: SwData.cvt holds the record SwPort changed after it came back"
fi

# a program that is never put aside (stay = yes: APPF_STAY): the kernel
# asks; Return (OK): it quits (SwOne built so, the session's first step)
app SwOne 1 6 640x480 stay -D KEYS
Y=$OUT/stay
files "$Y"
cp build/VERANDA.PRG "$Y/"
(cd "$Y" && SDL_VIDEODRIVER=dummy timeout -s INT 60 "$EMU" -rom "$ROM" -ram 512 -prg VERANDA.PRG -run \
	-warp -sound none -echo > console.txt 2>&1) || true
grep -a -q "TASK ASK BUSY SwOne" "$Y/console.txt" && grep -a -q "TASK QUIT SwOne" "$Y/console.txt" \
	&& grep -a -q "SWD 1 TASKS 0" "$Y/console.txt" || { tail -20 "$Y/console.txt"; fail "APPF_STAY"; }
ok "SwOne with stay = yes (APPF_STAY): the kernel's box asked (TASK ASK BUSY), OK: it quit, nothing put aside"
app SwOne 1 6 640x480

# a picture of a program brought back (SwPort: 640 x 240, its colours, the
# colour map), the session's step 5 ending there
app SwPort 3 1 640x240 -D PORT -D SHOT
P=$OUT/back
files "$P"
cp build/VERANDA.PRG "$P/"
(cd "$P" && SDL_VIDEODRIVER=dummy timeout -s INT 300 "$EMU" -rom "$ROM" -ram 512 -prg VERANDA.PRG -run \
	-warp -sound none -echo -dump V > console.txt 2>&1) || true
grep -a -q "SW 3 SAME" "$P/console.txt" && [ -f "$P/dump.bin" ] || fail "the picture of SwPort brought back"
$PY tools/vram2png.py --color "$P/dump.bin" "$OUT/back.png" >/dev/null || fail "vram2png"
ok "SwPort brought back (640 x 240, its colours and colour map): $OUT/back.png"

if [ "${SWITCH_SHELL:-1}" != 0 ] && [ -f tests/dock_gate.sh ]; then
	bash tests/dock_gate.sh "$OUT" || fail "the dock"
fi
echo "GATE switch: PASS"
