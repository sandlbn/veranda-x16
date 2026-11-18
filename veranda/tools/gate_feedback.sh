#!/bin/bash
# Gate feedback (DECISIONS V51): the pointer's shapes, the system beep,
# blanking the screen.
#
#   tools/gate_feedback.sh
#
# tests/feedback.s (TEST.PRG) sets each pointer shape, makes a long call
# (the busy pointer by itself), beeps, lets the screen blank after 2 s of
# no input and wakes it with a key and with a press (neither passed on),
# then clicks; a monitor in the interrupt logs sprite 0's picture,
# DC_VIDEO, palette entry 0 and PSG voice 15 whenever they change.
# tests/check_feedback.py checks the log and the dump (sprite 0's slot
# and attributes) and writes the shapes as PNGs (ptr-*.png).
set -e
cd "$(dirname "$0")/.."
OUT=build/gate/feedback
PY="python3 -I -B"
EMU=${EMU:-$HOME/Projects/x16-emulator/build/x16emu}
ROM=${ROM:-$HOME/Projects/x16-rom/build/x16/rom.bin}
export CC65_HOME=${CC65_HOME:-$HOME/.local/share/cc65}

ok() { echo "ok    $*"; }
fail() { echo "FAIL  $*"; exit 1; }

rm -rf "$OUT"
mkdir -p "$OUT/run"
$PY tools/kbuild.py >/dev/null || fail "the kernel does not build"
ca65 --cpu 65C02 -I build/gen -I inc tests/feedback.s -o "$OUT/feedback.o" || fail "tests/feedback.s"
ca65 tests/prghdr.s -o "$OUT/prghdr.o"
ld65 -C cfg/testprg.cfg "$OUT/prghdr.o" "$OUT/feedback.o" -o "$OUT/run/TEST.PRG" || fail "tests/feedback.s does not link"
cp build/VERANDA.PRG build/VERANDA.SYS "$OUT/run/"
(cd "$OUT/run" && SDL_VIDEODRIVER=dummy timeout -s INT 120 "$EMU" -rom "$ROM" -prg VERANDA.PRG -run \
	-warp -sound none -echo -dump V > console.txt 2>&1) || true
[ -f "$OUT/run/dump.bin" ] || { tail -20 "$OUT/run/console.txt"; fail "the session did not end (no dump)"; }
$PY tests/check_feedback.py "$OUT/run" "$OUT" || fail "feedback"
$PY tools/check_vera.py "$OUT/run/dump.bin" || fail "the display after the session"
echo "pointer shapes: $OUT/ptr-arrow.png, ptr-ibeam.png, ptr-cross.png, ptr-busy.png"
echo "GATE feedback: PASS"
