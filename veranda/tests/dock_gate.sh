#!/bin/bash
# Gate switch, part 2 (DECISIONS V83): the shell's dock (tests/dock_script.py),
# run by tools/gate_switch.sh with its folder ($1: the SDK, the test
# programs there). On the host folder and an SD card image under R49; the
# dock's picture.
set -e
OUT=$1
PY="python3 -I -B"
EMU=${EMU:-$HOME/Projects/x16-emulator/build/x16emu}
ROM=${ROM:-$HOME/Projects/x16-rom/build/x16/rom.bin}
R49=${R49:-$HOME/Downloads/Release.R49.ROM.Image/rom.bin}
export CC65_HOME=${CC65_HOME:-$HOME/.local/share/cc65}
ok() { echo "ok    $*"; }
fail() { echo "FAIL  $*"; exit 1; }
SDK=$(cd "$OUT/sdk" && pwd)
A=$OUT/apps
rm -rf "$OUT/shell"
cp -r shell "$OUT/shell"
rm -f "$OUT/shell"/*.cvt "$OUT/shell"/shell.bin* "$OUT/shell"/*.map
make -s -C "$OUT/shell" SDK="$SDK" >"$OUT/shell.log" 2>&1 || { cat "$OUT/shell.log"; fail "the shell does not build"; }
ok "the shell (the dock: in overlay 10) built against the installed SDK"

files() {
	rm -rf "$1"
	mkdir -p "$1"
	cp "$OUT/shell/SHELL.cvt" "$A/SwOne.cvt" "$A/SwTwo.cvt" build/VERANDA.SYS "$1/"
}
order() {
	got=$(tr -d '\r' < "$1" | grep -a -o -E 'SHELL STARTED|SHELL DONE|SHELL DOCK (START|MIN|RESTORE|RESUME|MENU|QUIT|UNPIN) .*|TASK (ASIDE|BACK) Sw[A-Za-z]*|SW [12] (START|SAME|DIFF|QUIT)' | sed 's/ *$//')
	want=$($PY tests/dock_script.py --want)
	[ "$got" = "$want" ] || { echo "$got" | diff <(echo "$want") - | head -20; fail "$2: the console's lines"; }
	ok "$2: SwOne pinned (the dock), SwTwo started and put aside: in the dock with a dot; SwOne started from its pin, aside"
	ok "$2: window 0 minimised to the dock and opened again from it; SwTwo brought back from the dock (the same CRC), aside again"
	ok "$2: right clicks: SwTwo's menu, Quit (asked: TASK_END); SwOne's (Quit, Unpin), Quit"
	# (V84) the shell gives each program it starts its path as the
	# switcher's note; the dock's tile of a program aside comes from it
	tr -d '\r' < "$1" | grep -a -q "SHELL DOCK TILE SwTwo OWN" \
		|| { tr -d '\r' < "$1" | grep -a -o "SHELL DOCK TILE [A-Za-z]* [A-Z]*"; fail "$2: SwTwo's own tile in the dock"; }
	ok "$2: SwTwo aside: its own tile in the dock (Calc's), found by the path the shell gave it as its note"
}
run() {         # run <folder> <rom> [sd]
	rm -f "$1/dump.bin"
	if [ "$3" = sd ]; then
		$PY tools/vcard.py make --mb 64 "$1/sd.img" "$1/card" >/dev/null || fail "vcard make"
		cp build/VERANDA.PRG "$1/"
		(cd "$1" && SDL_VIDEODRIVER=dummy timeout -s INT 400 "$EMU" -rom "$2" -ram 512 -sdcard sd.img -prg VERANDA.PRG \
			-run -warp -sound none -echo -dump V > console.txt 2>&1) || true
	else
		cp build/VERANDA.PRG "$1/"
		(cd "$1" && SDL_VIDEODRIVER=dummy timeout -s INT 300 "$EMU" -rom "$2" -ram 512 -prg VERANDA.PRG -run \
			-warp -sound none -echo -dump V > console.txt 2>&1) || true
	fi
	[ -f "$1/dump.bin" ] || { tail -20 "$1/console.txt"; fail "$1: the session did not end"; }
}

H=$OUT/dock-host
files "$H"
cp build/VERANDA.PRG "$H/"
$PY tests/dock_script.py "$H" console.txt || fail "the script"
run "$H" "$ROM"
order "$H/console.txt" "dock, host folder, 512 KB"
$PY tools/vram2png.py --color "$H/dump.bin" "$OUT/dock-end.png" >/dev/null 2>&1 || true

D=$OUT/dock-shot
files "$D"
cp build/VERANDA.PRG "$D/"
$PY tests/dock_script.py "$D" shot console.txt || fail "the script"
R=$ROM
[ -f "$R49" ] && R=$R49
run "$D" "$R"
grep -a -q "SHELL DOCK SwOne \*, SwTwo \*, /" "$D/console.txt" || { grep -a "SHELL DOCK" "$D/console.txt"; fail "the dock's picture: its items"; }
$PY tools/vram2png.py --color "$D/dump.bin" "$OUT/dock.png" >/dev/null || fail "vram2png"
# (V84) SwTwo's tile (the dock's second item, cards 38-41 of rows 53-56)
# in its own colour, Calc's purple (9), not a document's (the sidebar's, 3)
$PY - "$D/dump.bin" <<-'EOF' || fail "the dock's picture: SwTwo's tile is not its own"
	import sys
	d = open(sys.argv[1], 'rb').read()
	c = {d[0x1AC00 + r * 256 + x * 2 + 1] >> 4 for r in range(53, 57) for x in range(38, 42)}
	assert c == {9}, c
	EOF
ok "the dock: SwOne (pinned: its icon) and SwTwo put aside (its own tile, purple), dots, window 0 minimised: $OUT/dock.png"

if [ -f "$R49" ]; then
	S=$OUT/dock-sd49
	files "$S/card"
	$PY tests/dock_script.py "$S/card" || fail "the script"
	run "$S" "$R49" sd
	order "$S/console.txt" "dock, SD card image, R49, 512 KB"
fi
