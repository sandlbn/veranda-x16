# veranda-x16: Veranda and its applications in one tree (README.md)
#
#   make            everything: Veranda, the SDK, every application, and the
#                   SD card package in dist/ (dist/VERANDA, dist/DOCUMENTS)
#   make build      Veranda (the kernel, VERANDA.SYS, RBOOT), the SDK
#                   (veranda/build/sdk), the shell and every application
#                   (build/apps/<name>/; VeraBASIC's editor in
#                   build/apps/vbasic/ide/)
#   make dist       the SD card package only (veranda/tools/mkdist.sh: it
#                   builds everything again in its own work folder)
#   make gates      Veranda's core gates (make -C veranda gates)
#   make clean      removes build/, dist/ and every build/ below
#
# Needs cc65 2.19 (CC65_HOME, default ~/.local/share/cc65) and Python 3
# (with Pillow for make dist: the Meadow wallpaper).
# The gates also need x16emu and an X16 ROM: README.md, "Building".

export CC65_HOME ?= $(HOME)/.local/share/cc65
export PATH := $(CC65_HOME)/bin:$(PATH)

TOP   := $(CURDIR)
SDK   := $(TOP)/veranda/build/sdk
OUT   := $(TOP)/build/apps
DIST  := $(TOP)/dist

# Veranda's own applications and desk accessories (veranda/apps)
VAPPS := archive calc calendar dex edit fonted notepad paint prefs scrapman setup term vbasic view

.PHONY: all build veranda sdk shell vapps apps calc modplayer solitaire pinball harbour-walls sidplay dist gates clean

all: build dist

build: veranda sdk shell vapps apps

veranda:
	$(MAKE) -C veranda all

sdk: veranda
	$(MAKE) -C veranda sdk SDKDIR=$(SDK)

shell: sdk
	$(MAKE) -C veranda/shell SDK=$(SDK)

vapps: sdk
	@for a in $(VAPPS); do \
	  echo "== veranda/apps/$$a"; \
	  mkdir -p $(OUT)/$$a; \
	  $(MAKE) -s -C veranda/apps/$$a all SDK=$(SDK) OUT=$(OUT)/$$a || exit 1; \
	done
	@echo "== veranda/apps/vbasic (ide: VeraBASIC, the editor)"
	$(MAKE) -s -C veranda/apps/vbasic ide SDK=$(SDK) OUT=$(OUT)/vbasic

apps: calc modplayer solitaire pinball harbour-walls sidplay

calc: sdk
	$(MAKE) -C apps/calc veranda SDK=$(SDK)
modplayer: sdk
	$(MAKE) -C apps/modplayer veranda VSDK=$(SDK)
solitaire: sdk
	$(MAKE) -C apps/solitaire/veranda SDK=$(SDK)
pinball: sdk
	$(MAKE) -C apps/pinball/veranda SDK=$(SDK)
harbour-walls: sdk
	$(MAKE) -C apps/harbour-walls SDK=$(SDK)
sidplay: sdk
	$(MAKE) -C apps/sidplay build/app/app.cvt VSDK=$(SDK)

# the package: a new, empty dist/ each time (mkdist.sh wants one)
dist:
	rm -rf $(DIST)
	veranda/tools/mkdist.sh $(DIST)

gates: veranda
	$(MAKE) -C veranda gates

clean:
	rm -rf build dist
	$(MAKE) -C veranda clean
	rm -rf apps/*/build
