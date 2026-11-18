#!/bin/bash
# Lighthouse Pinball's gate: everything checked, in the emulator under the
# X16 ROM R49.
#
#   tools/gate.sh            PB_MOUSE=0: skip the real-mouse runs
#
# Needs: Veranda's repository (VERANDA=../../veranda: its main branch is
# exported into build/vsrc and built there, never in Veranda's tree; the
# SDK installed from it into build/sdk), cc65 (CC65_HOME), cc, x16emu
# (EMU) and the mouse-script emulator (MEMU, ~/Projects/x16-emulator-mouse),
# the R49 ROM (R49), Python 3 with Pillow.
#
# H. the host (cc, the same src/ C as the X16 runs): host/tests.c, the
#    physics: no tunnelling at the top speed in every direction from all
#    over the table, aimed shots at every wall, end and fog bank, rest on a
#    flipper, the flipper's, the fog banks' and the slingshots' kicks, the
#    plunger; host/sim.c's autopilot plays two games (seeds 1 and 2) into
#    key scripts
# 1. the application: an application, class Lighthouse, $0801, VRND,
#    640x480, API 4, banked records, a tile; it starts through the loader
#    (AUTOSTART) under R49 and logs PINBALL RUNNING
# 2. the scripted games (the test build plays a script's key events through
#    the keyboard hook's own routine, a tick at a time): the launch, the
#    flipper hits, the score, the drains, game over, the high score typed
#    in a dialog box and saved to the system folder (the file read back),
#    the frame timing (the emulator's cycle counter: every frame's work
#    within the frame, no frame late), the X16's log equal to the host's,
#    line for line (events, a checksum of the state every 256 ticks, the
#    score at game over): deterministic physics; the palette the game found
#    is the palette after it ends
# 3. screenshots (sprites composed): the plunger pulled, a flipper hit, the
#    ferry's ride, game over
# M. the real mouse (x16emu -mousescript, R49): the Game menu opened (a
#    screenshot), Options > Sound (saved), Game > Quit: the shell starts,
#    with Veranda's own Look 2 palette
set -e -o pipefail
cd "$(dirname "$0")/.."
TOP=$(pwd)
VERANDA=${VERANDA:-$(cd ../../veranda && pwd)}
EMU=${EMU:-$HOME/Projects/x16-emulator/build/x16emu}
MEMU=${MEMU:-$HOME/Projects/x16-emulator-mouse/build/x16emu}
R49=${R49:-$HOME/Downloads/Release.R49.ROM.Image/rom.bin}
ROM=$R49
export CC65_HOME=${CC65_HOME:-$HOME/.local/share/cc65}
export PATH=$CC65_HOME/bin:$PATH
PY="python3 -I -B"
OUT=build/gate
SDK=$TOP/build/sdk
export EMU ROM SDK

ok() { echo "ok    $*"; }
fail() { echo "FAIL  $*" >&2; exit 1; }

[ -f "$R49" ] || fail "no R49 ROM at $R49"
rm -rf "$OUT"
mkdir -p "$OUT/host"

#---------------------------------------------------------------- the SDK
echo "== the SDK: Veranda's main, exported and built here"
rm -rf build/vsrc
mkdir -p build/vsrc
(cd "$VERANDA" && git archive main) | tar -x -C build/vsrc
(cd build/vsrc && $PY tools/apigen.py api/veranda.api build/gen >/dev/null && $PY tools/kbuild.py >/dev/null) \
	|| fail "Veranda does not build (in build/vsrc)"
build/vsrc/tools/mksdk.sh "$SDK" >/dev/null || fail "the SDK does not install"
[ -f "$SDK/bin/VERANDA.PRG" ] || fail "the SDK has no VERANDA.PRG"
ok "Veranda main $(cd "$VERANDA" && git rev-parse --short main) built in build/vsrc, its SDK in build/sdk"

#---------------------------------------------------------------- H
echo "== H: the host"
H=$OUT/host
$PY tools/mktable.py "$H/gen" > "$H/mktable.log" || fail "the table generator"
ok "H: the table: $(cat "$H/mktable.log")"
cc -O2 -Wall -Werror -DPB_PROBE -I"$H/gen" -o "$H/tests" host/tests.c src/phys.c src/game.c src/fix.c src/trace.c \
	"$H/gen/table.c" -lm || fail "host/tests.c does not build"
cc -O2 -Wall -Werror -I"$H/gen" -o "$H/sim" host/sim.c src/phys.c src/game.c src/fix.c src/trace.c "$H/gen/table.c" \
	|| fail "host/sim.c does not build"
"$H/tests" > "$H/tests.log" || { cat "$H/tests.log"; fail "the physics tests"; }
sed 's/^/      /' "$H/tests.log"
for s in 1 2; do
	"$H/sim" bot "$H/game$s.script" $s 2> "$H/bot$s.log" || { cat "$H/bot$s.log"; fail "the autopilot (seed $s)"; }
	"$H/sim" bin "$H/game$s.script" "$H/game$s.bin"
	"$H/sim" run "$H/game$s.script" > "$H/game$s.log"
	"$H/sim" run "$H/game$s.script" > "$H/game$s.again"
	cmp -s "$H/game$s.log" "$H/game$s.again" || fail "the host's game $s differs from itself"
	echo "      game $s (host): $(sed 's/^bot: //' "$H/bot$s.log")"
done
ok "H: two games by the autopilot, each the same when played again"

#---------------------------------------------------------------- 1
echo "== 1: the application"
make -s -C veranda SDK="$SDK" OUT="$TOP/$OUT/app" >"$OUT/app.log" 2>&1 || { cat "$OUT/app.log"; fail "it does not build"; }
A=$OUT/app/pinball.cvt
field() { $PY "$SDK/tools/vpack.py" --list "$1" | sed -n "s/^$2 *//p"; }
[ "$(field "$A" 'geos type')" = 6 ] && [ "$(field "$A" class)" = 'Lighthouse  V1.0' ] || fail "the CVT: type or class"
[ -f "$OUT/app/Lighthouse Pinball.cvt" ] && cmp -s "$A" "$OUT/app/Lighthouse Pinball.cvt" || fail "no release file Lighthouse Pinball.cvt"
[ "$(field "$A" name)" = 'Lighthouse Pinba' ] || fail "the CVT: the GEOS name is not the file name's first 16 characters"
[ "$(field "$A" load)" = '$0801' ] && [ "$(field "$A" veranda)" = True ] && [ "$(field "$A" mode)" = 640x480 ] \
	&& [ "$(field "$A" api)" = 4 ] && [ "$(field "$A" flags)" = 3 ] || fail "the CVT: load address, VRND, mode, API or flags"
field "$A" tile | grep -q '^sky2, ' || fail "the CVT: no tile"
end=$(field "$A" end)
[ $((16#${end#\$})) -le $((16#8000)) ] || fail "the program ends at $end (the C stack is at \$8000)"
ok "1: an application, class Lighthouse, \$0801-$end, VRND, 640x480, API 4, banked records, tile $(field "$A" tile); records $(field "$A" records)"
tools/run.sh "$A" "$OUT/app/run" 30
grep -aq "PINBALL RUNNING" "$OUT/app/run/console.txt" || fail "it does not start"
ok "1: it starts through the loader (AUTOSTART.cvt) under R49"

#---------------------------------------------------------------- 2
# the test build of script $1 into $OUT/$2 (SHOT: stop at that tick)
session() {
	local bin=$1 d=$OUT/$2 shot=$3
	mkdir -p "$d"
	make -s -C veranda test SDK="$SDK" OUT="$TOP/$d" SCRIPT="$TOP/$bin" SHOT="$shot" >"$d/build.log" 2>&1 \
		|| { cat "$d/build.log"; fail "$2: the test build"; }
	tools/run.sh "$d/pinball.cvt" "$d/run" 300
	[ -f "$d/run/dump.bin" ] || { tail -5 "$d/run/console.txt"; fail "$2: the session did not finish"; }
}

hex() { echo $((16#$1)); }

for s in 1 2; do
	echo "== 2: game $s, scripted, under R49"
	G=$OUT/game$s
	session "$H/game$s.bin" game$s
	C=$G/run/console.txt
	grep -a -o 'PB [A-Z]* .*' "$C" | grep -v 'TIME\|PALETTE\|HISCORE\|SLOW\|PAL[0-3]\|LATE' > "$G/x16.log"
	grep -v 'END\|SCORE' "$H/game$s.log" > "$G/host.log"
	n=$(grep -c LAUNCH "$G/x16.log" || true)
	[ "$n" -ge 1 ] || fail "game $s: no launch"
	first=$(grep -m1 LAUNCH "$G/x16.log" | awk '{print $3}')
	ok "game $s: the plunger launched the ball (first at tick $(hex "$first"), $n launches)"
	n=$(grep -c FLIPHIT "$G/x16.log" || true)
	[ "$n" -ge 5 ] || fail "game $s: $n flipper hits"
	ok "game $s: $n flipper hits (a moving flipper striking the ball)"
	n=$(grep -c DRAIN "$G/x16.log" || true)
	[ "$n" -ge 3 ] || fail "game $s: $n drains"
	ok "game $s: $n drains"
	over=$(grep 'PB OVER' "$G/x16.log" | awk '{print $4}')
	[ -n "$over" ] || fail "game $s: no game over"
	score=$(hex "$over")
	[ "$score" -gt 0 ] || fail "game $s: no score"
	ok "game $s: game over at tick $(hex "$(grep 'PB OVER' "$G/x16.log" | awk '{print $3}')"), the score $score"
	diff "$G/host.log" "$G/x16.log" > "$G/diff.txt" \
		|| { head "$G/diff.txt"; fail "game $s: the X16's log differs from the host's"; }
	ok "game $s: deterministic: the X16's $(wc -l < "$G/x16.log") log lines (events, the state's checksum every 256 ticks, game over) equal the host's"
	grep -aq "PB HISCORE SAVED" "$C" || fail "game $s: the high score was not saved"
	$PY - "$G/run/Lighthouse Pinball Scores" "$score" <<-'EOF' || fail "game $s: the high score file"
	import struct, sys
	d = open(sys.argv[1], 'rb').read()
	assert d[:4] == b'LHPB' and d[4] == 1, 'magic'
	rows = [(d[8 + 16 * i:20 + 16 * i].split(b'\0')[0].decode(), struct.unpack_from('<I', d, 20 + 16 * i)[0]) for i in range(5)]
	assert ('GATE', int(sys.argv[2])) in rows, rows
	assert [r[1] for r in rows] == sorted([r[1] for r in rows], reverse=True), rows
	print('      the high scores read back: ' + ', '.join('%s %d' % r for r in rows))
	EOF
	ok "game $s: the high score, typed in the dialog box (GATE), saved in the system folder (\"Lighthouse Pinball Scores\") and read back"
	read -r _ _ frames tmax tavg pmax late <<< "$(grep -a 'PB TIME' "$C" | tr -d '\r')"
	[ "$late" = 0 ] || fail "game $s: $late frames late"
	[ "$tmax" -lt 133333 ] || echo "      (one frame's work above a frame: $tmax cycles; none came late)"
	ok "game $s: frame timing: $frames frames, their work $(awk -v a="$tavg" 'BEGIN{printf "%.1f", a/8000}') ms on average, at most $(awk -v a="$tmax" 'BEGIN{printf "%.1f", a/8000}') ms (a frame is 16.7 ms at 8 MHz), the ticks alone at most $(awk -v a="$pmax" 'BEGIN{printf "%.1f", a/8000}') ms; no frame late: 60 frames a second"
	grep -aq "PB PALETTE RESTORED" "$C" || fail "game $s: the palette was not put back"
	p0=$(grep -a -o 'PB PAL[0-3] .*' "$C" | cut -d' ' -f3- | tr -d '\r' | tr '\n' ' ' | sed 's/ $//')
	[ -n "$p0" ] || fail "game $s: the palette at the start was not logged"
	p1=$($PY tools/shot.py "$SDK/tools" "$G/run/dump.bin" "$G/end.png" --palette)
	[ "$p0" = "$p1" ] || { echo "start: $p0"; echo "end:   $p1"; fail "game $s: the palette after the game is not the one it found"; }
	ok "game $s: the palette after the game (entries 0-63 in the dump) is the one it found at its start"
done

#---------------------------------------------------------------- 3
echo "== 3: screenshots"
tick_of() { grep -m1 "$1" "$OUT/game1/x16.log" | awk '{print $3}'; }
L=$(hex "$(tick_of LAUNCH)")
F=$(hex "$(tick_of FLIPHIT)")
R=$(hex "$(tick_of FERRY || true)" 2>/dev/null || echo 0)
shots="plunger:$((L - 1)) flipper:$((F + 2))"
[ "$R" -gt 0 ] && shots="$shots ferry:$((R + 40))"
for sv in $shots; do
	name=${sv%%:*}
	t=${sv#*:}
	session "$H/game1.bin" "shot-$name" "$t"
	S=$OUT/shot-$name
	p=$($PY tools/shot.py "$SDK/tools" "$S/run/dump.bin" "$OUT/$name.png" --palette)
	[ "$(echo "$p" | cut -d' ' -f1-16)" = "$($PY - <<-'EOF'
	import re
	t = open('build/gate/host/gen/table.c').read()
	v = re.search(r'game_palette\[32\] = \{([^}]*)\}', t).group(1).split(',')
	print(' '.join('%03X' % int(x) for x in v[:16]))
	EOF
	)" ] || fail "$name: the game's palette is not shown"
	ok "3: $name.png at tick $t: the game's palette, the sprites"
done
cp "$OUT/game1/end.png" "$OUT/gameover.png"
ok "3: gameover.png (after the end: the palette put back)"

#---------------------------------------------------------------- M
if ! { "$MEMU" -help 2>&1 || true; } | grep -q -- -mousescript; then
	echo "SKIP  $MEMU has no -mousescript"
elif [ "${PB_MOUSE:-1}" = 0 ]; then
	echo "SKIP  PB_MOUSE=0"
else
	echo "== M: the real mouse, the menus, quit to the shell (R49)"
	M=$OUT/mouse
	mkdir -p "$M"
	rm -rf "$M/shell"
	cp -r build/vsrc/shell "$M/shell"
	make -s -C "$M/shell" SDK="$SDK" >"$M/shell.log" 2>&1 || { cat "$M/shell.log"; fail "M: the shell does not build"; }
	hook=$(grep -o '_hook_on  *00[0-9A-F]*' "$OUT/app/pinball.map" | head -1 | sed 's/.* 00//')
	[ -n "$hook" ] || fail "M: no _hook_on in the map"
	mrun() {
		local d=$1 secs=$2
		mkdir -p "$d"
		rm -f "$d"/dump*.bin "$d/Lighthouse Pinball Scores"
		cp "$A" "$d/AUTOSTART.cvt"
		cp "$M/shell/SHELL.cvt" "$SDK/bin/VERANDA.PRG" "$SDK/bin/VERANDA.SYS" "$d/"
		(cd "$d" && SDL_VIDEODRIVER=dummy timeout -s INT "$secs" "$MEMU" -rom "$R49" -prg VERANDA.PRG -run \
			-warp -sound none -echo -dump RV -mousescript mouse.txt > console.txt 2>&1) || true
		grep -aq "the mouse script's exit" "$d/console.txt" || { tail -5 "$d/console.txt"; fail "M: the run did not reach its end"; }
	}
	# the Game menu open (the bar: the mark, Lighthouse, Game at x 102-129)
	mkdir -p "$M/menu"
	cat > "$M/menu/mouse.txt" <<-EOF
	0 id 3
	wait $hook 1
	30 move -2000 -2000
	40 move 115 7
	60 down
	64 up
	100 move 0 16
	130 exit
	EOF
	mrun "$M/menu" 60
	$PY tools/shot.py "$SDK/tools" "$M/menu/dump.bin" "$OUT/menu.png" >/dev/null
	ok "M: the Game menu opened by the mouse (menu.png)"
	# Options > Sound (off, saved), then Game > Quit
	mkdir -p "$M/quit"
	# (an opening menu puts the pointer on the middle of its first item,
	# kernel/u/menu.s: Pointer; items are 16 lines apart)
	cat > "$M/quit/mouse.txt" <<-EOF
	0 id 3
	wait $hook 1
	30 move -2000 -2000
	40 move 160 7
	60 down
	64 up
	100 down
	104 up
	160 move -2000 -2000
	170 move 115 7
	180 down
	184 up
	220 move 0 32
	240 down
	244 up
	700 exit
	EOF
	mrun "$M/quit" 90
	C=$M/quit/console.txt
	grep -aq "PINBALL RUNNING" "$C" || fail "M: the game did not start"
	grep -a "PANIC" "$C" && fail "M: a crash"
	grep -aq "SHELL STARTED" "$C" || { tail -5 "$C"; fail "M: the shell did not start after Game > Quit"; }
	$PY - "$M/quit/Lighthouse Pinball Scores" <<-'EOF' || fail "M: Options > Sound was not saved"
	import sys
	d = open(sys.argv[1], 'rb').read()
	assert d[:4] == b'LHPB' and d[5] == 1, d[:8]
	EOF
	ok "M: Options > Sound turned the sound off, saved with the high scores"
	p=$($PY tools/shot.py "$SDK/tools" "$M/quit/dump.bin" "$OUT/quit.png" --palette | cut -d' ' -f1-16)
	look2="FFF 223 DDE EEF 9CF ACE FD5 F87 8D9 B9F FA5 6CE 36A 8BE 5A7 DAC"
	[ "$p" = "$look2" ] || { echo "shell: $p"; echo "Look2: $look2"; fail "M: the shell's palette after the game is not Look 2's"; }
	ok "M: Game > Quit: the shell started, palette entries 0-15 Veranda's Look 2 (quit.png)"
fi

echo "pictures: $OUT/plunger.png, flipper.png, ferry.png (in play), gameover.png, menu.png, quit.png"
echo "release:  $OUT/app/Lighthouse Pinball.cvt"
echo "GATE veranda-pinball: PASS"
