# Veranda shell (docs/PLAN.md phase 6), built with the installed SDK
#   make SDK=<the SDK's folder>     SHELL.cvt
# The resident part and eleven banked overlays (shell.h): app-c-ovl.cfg,
# overlays = banked in app.hdr.
SDK   ?= ../build/sdk
CC65  := cl65 -t none --cpu 65C02 -O -I $(SDK)/include --asm-include-dir $(SDK)/include
SRC   := main.c win.c mouse.c wops.c view.c dir.c files.c print.c chrome.c tiles.c bars.c look.c wall.c dock.c ovl.s play.s wallblit.s clip.s shade.s cmove.s
OVLS  := shell.bin.1 shell.bin.2 shell.bin.3 shell.bin.4 shell.bin.5 shell.bin.6 shell.bin.7 shell.bin.8 shell.bin.9 shell.bin.10 shell.bin.11

all: SHELL.cvt

# the SDK's tile drawing (vr_tile, DECISIONS V90) in the tiles' overlay
vrtile.o: $(SDK)/src/vrtile.c $(SDK)/include/vr.h
	$(CC65) --code-name OVERLAY10 --rodata-name OVL10RO --bss-name OVL10BSS -c -o $@ $<

shell.bin: $(SRC) shell.h winp.h icons.h tiles.h vrtile.o
	$(CC65) -C $(SDK)/cfg/app-c-ovl.cfg -m shell.map -o shell.bin $(SRC) vrtile.o $(SDK)/lib/veranda.lib

SHELL.cvt: shell.bin app.hdr icon.pbm
	python3 -I -B $(SDK)/tools/vpack.py --load 0x0801 -o SHELL.cvt app.hdr shell.bin $(OVLS)

clean:
	rm -f *.o shell.bin shell.bin.* shell.map SHELL.cvt

.PHONY: all clean
