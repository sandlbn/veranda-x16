# Veranda Edit, a Veranda application in C; its text, view, editing, undo,
# find, clipboard and files are the SDK's textkit (src/textkit, compiled
# here into the parts below; DECISIONS V93), buf.s the gap buffer's hot paths
#   make        "Veranda Edit.cvt" (as Edit.cvt), against the installed SDK (SDK, default ../../build/sdk)
#   make test   the test build: tests/edit_test.c and tests/edit_play.s linked in
#               (the session is the file EDIT.PLY: tests/edit_script.py)
#   make run    in the emulator (tools/vrun)
# OUT: where the results go (default here)
SDK      ?= ../../build/sdk
OUT      ?= .
TK       := $(SDK)/src/textkit
.DEFAULT_GOAL := all
CC65     := cl65 -t none --cpu 65C02 -O -I $(SDK)/include --asm-include-dir $(SDK)/include -I . -I $(TK)
SRC      := misc.c main.c menus.c font.c
# textkit: ops.c, undo.c, text.c, buf.s in main RAM (the text's code), view.c
# in overlay 1, file.c, find.c, scrap.c in overlay 2 (as tests/check_edit_banks.py
# reads them from here)
TK_MAIN  := ops undo text
TK_OVL1  := view
TK_OVL2  := file find scrap
TKO      := $(OUT)/view.o $(OUT)/ops.o $(OUT)/undo.o $(OUT)/file.o $(OUT)/find.o $(OUT)/scrap.o
TKO2     := $(OUT)/text.o $(OUT)/buf.o
$(foreach f,$(TK_OVL1),$(OUT)/$(f).o): SEGF := --code-name OVERLAY1
$(foreach f,$(TK_OVL2),$(OUT)/$(f).o): SEGF := --code-name OVERLAY2
$(OUT)/%.o: $(TK)/%.c $(TK)/textkit.h edit.h tkapp.h
	$(CC65) $(CFLAGS) $(SEGF) -c -o $@ $<
$(OUT)/buf.o: $(TK)/buf.s
	$(CC65) -c -o $@ $<
TEST_SRC ?= ../../tests/edit_play.s ../../tests/edit_test.c $(DBG)
# the clipboard: the SDK's scrap library compiled into overlay 2 (with
# scrap.c, its only caller), not linked from veranda.lib into the resident
# program, which had no room left for the folders (DECISIONS V71)
SCRAP    := $(OUT)/scrapio.o $(OUT)/scraptbeg.o
# the Open and Save dialog (vrchoose.h, DECISIONS V79): the SDK's source
# compiled into overlay 3, its BSS in main RAM (misc.c's file_dlg maps it)
VRF      := $(OUT)/vrchoose.o

all: $(OUT)/Edit.cvt

$(SCRAP): $(OUT)/%.o: $(SDK)/src/%.c $(SDK)/include/scrap.h
	$(CC65) --code-name OVERLAY2 --rodata-name OVL2RO --bss-name OVL2BSS --data-name OVL2DATA \
	    -DSCRAP_PATH=scrap_path -c -o $@ $<

$(VRF): $(SDK)/src/vrchoose.c $(SDK)/include/vrchoose.h
	$(CC65) --code-name OVERLAY3 --rodata-name OVL3RO -c -o $@ $<

$(OUT)/edit.bin: $(SRC) edit.h tkapp.h app.hdr $(SCRAP) $(VRF) $(TKO) $(TKO2)
	$(CC65) $(CFLAGS) -C $(SDK)/cfg/app-c-ovl.cfg -Wl -D,__STACKSIZE__=0x400 -m $(OUT)/edit.map -o $@ misc.c main.c menus.c $(TKO) font.c $(TKO2) $(EXTRA) $(SCRAP) $(VRF) $(SDK)/lib/veranda.lib

$(OUT)/Edit.cvt: $(OUT)/edit.bin icon.pbm tile.txt
	python3 -I -B $(SDK)/tools/vpack.py --load 0x0801 -o $@ app.hdr $(OUT)/edit.bin $(OUT)/edit.bin.1 $(OUT)/edit.bin.2 $(OUT)/edit.bin.3
	cp $@ "$(OUT)/Veranda Edit.cvt"

test:
	rm -f $(OUT)/edit.bin $(TKO)
	$(MAKE) OUT=$(OUT) CFLAGS="-DEDIT_TEST $(TESTFLAGS)" EXTRA="$(TEST_SRC)" all

run: $(OUT)/Edit.cvt
	$(SDK)/tools/vrun $(OUT)/Edit.cvt

clean:
	rm -f *.o edit.bin edit.bin.* edit.map Edit.cvt "Veranda Edit.cvt"

.PHONY: all test run clean
