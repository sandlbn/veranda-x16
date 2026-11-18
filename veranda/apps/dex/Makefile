# Dex, an address book, a Veranda desk accessory in C (and the shared
# toolkit ../daui/daui.c)
#   make        Dex.cvt, against the installed SDK (SDK, default ../../build/sdk)
#   make test   the test build: a scripted session linked in (TEST_SRC;
#               TESTFLAGS=-DDA_STOP=n ends it at frame n with a dump)
# OUT: where the results go (default here)
SDK      ?= ../../build/sdk
OUT      ?= .
CC65     := cl65 -t none --cpu 65C02 -O -I $(SDK)/include -I ../daui --asm-include-dir $(SDK)/include
TEST_SRC ?= ../../tests/da_play.c ../../tests/sdk_play.s

all: $(OUT)/Dex.cvt

$(OUT)/dex.bin: dex.c ../daui/daui.c ../daui/daui.h app.hdr
	$(CC65) $(CFLAGS) -C $(SDK)/cfg/app-c.cfg -m $(OUT)/dex.map -o $@ dex.c ../daui/daui.c $(EXTRA) $(SDK)/lib/veranda.lib

$(OUT)/Dex.cvt: $(OUT)/dex.bin icon.pbm tile.txt
	python3 -I -B $(SDK)/tools/vpack.py --load 0x0801 -o $@ app.hdr $(OUT)/dex.bin

test:
	rm -f $(OUT)/dex.bin
	$(MAKE) OUT=$(OUT) CFLAGS="-DDEX_TEST $(TESTFLAGS)" EXTRA="$(TEST_SRC)" all

clean:
	rm -f *.o dex.bin dex.map Dex.cvt

.PHONY: all test clean
