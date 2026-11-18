# Decision log

Each entry: what was decided, the measurement behind it, and the date.
Measurements are cycle-exact, taken from inside the guest via the emulator's
counter at `$9FB8-$9FBB` (see docs/environment.md).

---

## 2026-09-06 -- Phase 0: harness

Test code is poked in with `STM` rather than loaded with `-prg`, because
`-prg` is a BASIC paste and never loads under `-testbench`. Measured poke
throughput ~105 KB/s, so an 8 KB blob costs <0.1 s. Non-issue once known.

Pure-assembly targets link with `cfg/x16mod.cfg` (derived from cc65's
`cx16-asm.cfg`) which adds page-aligned `ALIGNBSS`/`ALIGNDATA` segments. The
first build put the mixer's source page at `$09C8`; unaligned buffers make
`lda buf,y` cross a page and cost an extra cycle on most iterations, which
both slows the loop and corrupts any measurement over it.

`cl65 -I` is a *C* include path and is not forwarded to ca65. Use
`--asm-include-dir`.

## 2026-09-06 -- Phase 1: the audio path (PASSED)

**FIFO write cost: 4 cycles/byte, 8 cycles per stereo frame.** Measured
`sta AUDIO_DATA` + `inx` + `bne` at exactly 9.00 cycles/byte, matching hand
calculation and confirming the VERA range carries no I/O penalty (only
`$9F40-$9F60` and `$9FA0+` add 3 cycles -- memory.c:311-325).

**The full theoretical budget is really available.** A load sweep (tests/t_rt.s)
raised an artificial per-frame load until the FIFO starved, over 130,560-frame
runs:

| rate | theoretical cycles/frame | highest clean | first underrun |
|---:|---:|---:|---:|
| 8,011 Hz | 998.6 | 1011.9 | (above sweep) |
| 12,207 Hz | 655.4 | 663.4 | 991 |
| 16,022 Hz | 499.3 | 489.1 | 510 |
| 18,311 Hz | 436.9 | 437.8 | 458 |
| 21,362 Hz | 374.5 | 366.1 | 387 |
| 24,414 Hz | 327.7 | 314.8 | 335 |
| 32,043 Hz | 249.7 | 243.0 | 264 |

Every rate's threshold brackets theory. No hidden overhead.

**Run length matters when measuring this.** A first attempt over 12,288 frames
reported thresholds 2-17% *above* theory, because the 4 KB FIFO holds 2,048
stereo frames and absorbs a small deficit for the whole run. Runs must be long
enough that the deficit exceeds FIFO depth.

## 2026-09-06 -- Phase 2: the 4-voice mixer (PASSED)

**169.04 cycles per stereo frame**, 256-frame block, four voices, hard-panned
stereo, 8.16 phase, page-aligned 256-byte caches. Matches the 169-cycle
estimate in PLAN.md §3.

| rate | budget | mixer | headroom |
|---:|---:|---:|---:|
| 16,022 Hz | 499.3 | 33.9% | **66.1%** |
| 18,311 Hz | 436.9 | 38.7% | 61.3% |
| 21,362 Hz | 374.5 | 45.1% | 54.9% |
| 24,414 Hz | 327.7 | 51.6% | 48.4% |
| 32,043 Hz | 249.7 | 67.7% | 32.3% |

Gate was >=16 kHz with >=40% headroom: **66.1% at 16 kHz, and 24 kHz also
clears it at 48.4%.** Bit-exact against `host/host_mixer.c` on 8 random
vectors including deliberately out-of-range cache values, so the 8-bit
wraparound in the mix is covered.

**Decision: the cache stores `(sample * volume) / 128`, not `/64`.** Each voice
then lands in -64..63, two voices sum to -128..126, which is exactly int8. The
hot loop needs no clamp, no overflow test and no headroom shift.

**Decision: X, not a 16-bit counter, bounds the block.** A 16-bit
decrement-and-test costs ~20 cycles/frame -- 4% of the entire 16 kHz budget
spent on bookkeeping. Blocks are capped at 256 frames, which the 256-byte
cache page forces anyway.

## 2026-09-06 -- Phase 3: banked sample cache

**Volume moved OUT of the cache fill and INTO the mixer.** PLAN.md §5.4 argued
for scaling during the refill to keep a 16 KB table out of the hot loop; it
also marked that decision falsifiable. It was wrong. Refill scales *source*
bytes, so its cost rises with pitch, and it needs a table rebuild whenever a
volume slide moves. The mixer instead does `tay / lda voltab,y` with the page
high byte self-modified: **+24 cycles/frame flat (169.04 -> 193.04)**, a
volume change costs one store, and there is no rebuild at all. The price is a
resident 65 x 256 = 16,640-byte table, and base RAM is not the binding
constraint.

**Refill: 14.19 cycles/byte** (256-byte page, single run). Two rounds:

| version | cycles/byte |
|---|---:|
| `lda (zsrc),y` / `sta abs,y`, byte-at-a-time loop | 18.35 |
| same, unrolled 8x | **14.19** |

The 5-cycle loop tail (`iny`/`dex`/`bne`) cost as much as the copy itself.

Total cost with cache refill amortised over output frames, four voices:

| content | cycles/frame | headroom @16 kHz | @24 kHz |
|---|---:|---:|---:|
| low note (step 0.26) | 207.7 | 58.4% | 36.6% |
| middle C (step 0.52) | 222.4 | 55.5% | 32.1% |
| step 1.00 | 249.8 | 50.0% | 23.8% |
| top note (step 1.96) | 304.2 | 39.1% | 7.2% |

**Honest reading of the gate.** Typical content clears 40% headroom at 16 kHz
comfortably (50-58%). The pathological case -- all four voices simultaneously
at period 113, the highest note ProTracker allows -- lands at 39.1%, a hair
under. It is still playable; it just leaves less for the UI. 24 kHz is only
viable for low-pitched content and should not be the default.

PLAN.md's "<=10% additional CPU" gate for this phase was unrealistic: caching
inherently costs `bytes_per_frame * cycles_per_byte`, which is 15-58 cycles
per frame depending on pitch, i.e. 8-30% on top of the mixer. The gate that
matters is total headroom, and that is what the table above reports.

**Two bugs worth remembering.** `run_length` used `tay`/`cpy` as scratch and
silently destroyed Y, which is the destination index. And the first unrolled
copy used a `.repeat` block, which emitted eight *unpatched* `sta $FF00,y`
literals -- only the labelled remainder loop was being patched, so every
unrolled byte went to the wrong page. Both were caught by the six-case
correctness test, not by inspection.

## 2026-09-06 -- Phase 4: the replay/mixer boundary

`src/mixer_api.s` is the whole surface the sequencer will ever touch:
`mixer_set_period`, `mixer_set_volume_ch`, `mixer_set_sample`,
`mixer_trigger`, `mixer_stop`. Nothing above this line sees VERA, the bank
register, or a phase accumulator.

Arguments pass in globals rather than on cc65's parameter stack -- pushing
arguments costs more than these routines do.

`mixer_set_period` is a table lookup, verified against the exact PAL formula
for 28 cases across all four voices, including out-of-range periods (100 and
1000 clamp to 113 and 907, matching Paula).

## 2026-09-06 -- Phase 5: the MOD loader (PASSED)

Parser: all 7 synthetic modules agree with libopenmpt on title, pattern count
and order count, and header + patterns + samples accounts for each file size
*exactly* -- the check that catches a wrong pattern count, which is the
classic MOD misparse. All 6 malformed inputs (truncated, bad signature, song
length 0 and 200, empty) are rejected with a specific error rather than
crashing.

On hardware: `MODPLAY.PRG` loads a module into banked RAM and reports it.
Patterns land at bank 1 `$A000`; for a 4-pattern module the first sample lands
at bank 1 `$B000`, i.e. `$A000 + 4*1024`, as it should.

### Three encoding traps, all the same root cause

cc65 translates ASCII string literals to PETSCII for CBM targets. That bit
three separate times and cost more time than any algorithm here:

1. **Filenames.** `"MUSIC.MOD"` became PETSCII `$CD $D5 $D3...` -- *shifted*
   M-U-S -- which CMDR-DOS never matches. `OPEN` still reported success and
   the first read returned `ST=$42` (EOF immediately). Write filenames in
   **lowercase**: `'m'` (0x6D) maps to `$4D`, the unshifted M that BASIC sends.
2. **Format magic.** `memcmp(sig, "M.K.", 4)` compared the file's ASCII bytes
   against a PETSCII-translated literal and always failed. File-format
   constants must be raw byte values; a format is defined in ASCII, not in
   whatever charmap the compiler applies. `sig_is()` now takes four bytes.
3. **Display.** The X16 boots in uppercase/graphics mode where `$41-$5A` render
   as capitals, so C literals belong in lowercase and ASCII data read from a
   file should be passed through untouched. An "ASCII to PETSCII" case swap is
   exactly wrong here.

The lesson for the "portable C parser" idea in PLAN.md §7: the *logic* ports
cleanly, but string literals do not. Anything comparing bytes from a file must
use numeric constants.

### cc65's cbm_read does not work on this setup

`cbm_open` succeeds and `cbm_read` returns 0, while BASIC's `OPEN 2,8,2` plus
`GET#` reads the same file correctly. `src/kread.s` therefore does what BASIC
does -- SETNAM/SETLFS/OPEN, CHKIN, BASIN per byte, CLRCHN.

Per-byte BASIN is slow (~100-200 cycles/byte, so a 500 KB module takes several
seconds). MACPTR (`$FF44`) reads in bulk and is the obvious optimisation, but
correctness came first and this path is proven against BASIC's behaviour.

## 2026-09-07 -- Phase 6: the sequencer (PASSED)

Order/row progression, speed, tempo, note triggering, sample loops, and
effects Bxx, Cxx, Dxx, Fxx. Written from the format's semantics rather than
ported: micromod is the oracle, not the source.

### The three-way comparison works exactly as PLAN.md §7 hoped

| comparison | what a difference would mean | result |
|---|---|---|
| X16 build vs native build of the *same source* | a porting bug (cc65 codegen, integer width, banked pattern reads) | identical, 200 ticks |
| our sequencer vs micromod (independent implementation) | a ProTracker-semantics bug | identical, 20,000 ticks |

20,000 ticks is roughly 6.5 minutes of playback. All 11 modules match: the 7
synthetic ones plus micromod's own Origin, PlanetSized, Strange and
TestModFive, which between them use effects 8, 9, C, D and F.

### The oracle caught a bug on its first run

`volume.mod` diverged at tick 54: our channel volume snapped back to 64 while
micromod held 56. Cause: `trigger()` tested the channel's *persistent*
instrument assignment instead of the instrument number written on that row, so
every empty row reloaded the sample's default volume and silently undid the
preceding `Cxx`.

This is exactly the class of bug the trace oracle exists for. By ear it would
read as "the volume envelope is a bit off" on modules that use volume slides,
which is to say it would not have been found at all until much later.

### ProTracker details worth recording

- **`Dxx` reads its parameter as DECIMAL**, not hex: row `(p>>4)*10 + (p&0xF)`.
- **Finetune** is 1/8 semitone per step, so the period multiplier is
  `2^(-ft/96)`. Derived from that formula, scaled by 4096 and indexed with
  `(n&7)-(n&8)+8`, the table reproduces micromod's byte for byte -- a useful
  confirmation that the semantics were understood rather than copied.
- **Effect encoding.** An `Exy` becomes internal effect `0x10|x`, which frees
  slot `0x0E` for arpeggio (file effect 0 with a non-zero parameter). Every
  effect is then a single switch case.
- **`set_position` seeds the pending jump** rather than assigning the position,
  sets `tick = 1`, and runs one tick, so initialisation lands on row 0 through
  the normal path. Starting at position 0 therefore reports song-end on the
  first row; that quirk is ProTracker's and the caller ignores it.

### A stale-artifact trap

A failed `cl65` link left a partial `TRACE.PRG` behind, and the next `make`
saw a file newer than its prerequisites and did nothing. The 10 KB result was
missing the 16 KB volume table entirely and crashed into ROM. When a link
fails, delete the target before rebuilding.

## 2026-09-07 -- Phase 7: full effect compatibility (PASSED)

Every ProTracker effect micromod implements is now implemented here, and all
25 modules match its trace exactly over 20,000 ticks:

`0xy` arpeggio, `1xx`/`2xx` portamento, `3xx` tone portamento, `4xy` vibrato,
`5xy`, `6xy`, `7xy` tremolo, `9xx` offset, `Axy` volume slide, `Bxx`, `Cxx`,
`Dxx`, `Fxx`, and `E1x E2x E4x E5x E6x E7x E9x EAx EBx ECx EDx EEx`.

`E0` (Amiga LED filter), `E3` (glissando), `E8` and `EF` (funk repeat) are
implemented by neither player, so they agree trivially. `8xx` panning is a
no-op: Amiga panning is fixed at L=1+4, R=2+3.

### The trace had to be extended before it could catch anything

Vibrato, tremolo and arpeggio leave the base period and volume untouched --
they contribute an *offset* that is applied when the frequency is computed. A
trace of period and volume alone reports a perfect match while all three are
completely broken. The trace now carries `vibrato_add`, `tremolo_add` and
`arpeggio_add` per channel, and `mod_chan_period()` / `mod_chan_volume()`
expose the effective values the mixer is actually given.

### One bug, found three times over

`fx_tremolo`, `fx_vibrato` and `fx_waveform` all failed, at different ticks and
on different effects. Single cause: **a note trigger must reset the vibrato and
tremolo phase**, and only for waveform types 0-3 -- types 4-7 are the
"continue" variants that deliberately carry the phase across a note. Without
the reset the waveform resumed from wherever the previous note left it.

That three separate modules pointed at one line is the argument for
one-behaviour-per-module test files: the failure set localised the bug before
any debugging started.

### Effect tables are derived, not transcribed

Each was generated from its defining formula and then checked against the
oracle's table, which confirms the semantics were understood rather than
copied:

| table | formula | matches oracle |
|---|---|---|
| finetune | `2^(-ft/96) * 4096` | yes, all 16 |
| arpeggio | `2^(-n/12) * 4096` | yes, all 16 |
| vibrato sine | `floor(255 * sin(pi*i/32))` | yes, all 32 |

### Also aligned: the "no loop" threshold

The parser treated a loop length of `<= 2` bytes as "no loop"; the oracle uses
`< 4`. Aligned to `< 4`. This never showed up in a trace -- loop state is not
traced -- but it changes playback on modules using the one-word idiom.

### X16 build verified on the hard cases

The X16 trace matches the native build of the same source on `fx_vibrato`,
`fx_waveform` (the 32-bit random-seed waveform, where cc65's long arithmetic
could plausibly have differed) and `fx_patternloop` (control flow rather than
arithmetic).

## 2026-09-07 -- Real-module corpus, and making it actually play

### 484 of 488 real modules trace-identical

The corpus (488 modules) found one bug the synthetic suite structurally could
not: **an instrument number with no note swaps the sounding sample**, but only
when the incoming sample loops and the channel is already playing. Twelve
modules diverged, all on the `instrument` field with period and volume already
correct -- which named the rule immediately. The remaining 4 are 15-instrument
modules that neither player supports; both reject them.

Signature acceptance was widened to match the oracle, which keys off the last
two bytes only: `M.K.`, `M!K!`, `M&K!`, `N.T.`, `FLT4`, `4CHN`. `xCHN`/`xxCH`
announce a channel count this mixer cannot serve and stay rejected.

### The stereo desync -- the bug that made playback sound wrong

Symptom: on a module playing only channel 1 (hard-panned LEFT), the tone came
out of the RIGHT channel and left was silent -- then swapped back seconds
later, over and over.

Cause: the mixer writes left and right as two separate stores, and VERA
discards writes to a full FIFO. If the FIFO fills *between* those two stores,
the right byte is lost, the byte count goes odd, and every later frame is
swapped. It re-syncs only when an underrun flushes the odd byte
(`vera_pcm.c`: on a partial pair it does `fifo_cnt = 0; rdidx = wridx`), which
is exactly why the fault came and went.

Two emulator details made this a boundary case rather than a rarity:

- `pcm_write_fifo` accepts a byte only while `fifo_cnt < sizeof(fifo) - 1`, so
  usable capacity is **4095 bytes -- an odd number**. Filling to the brim
  splits a pair by construction.
- `aflow` is `fifo_cnt < 1024`.

The first fix used AFLOW's guarantee to write 1536 frames (3072 bytes), which
from a worst-case 1023 reaches exactly 4095: correct on paper, zero margin,
and still desyncing. **1024 frames (2048 bytes) leaves a full kilobyte spare.**

Result on a single-channel module, 95 s of capture: right-channel activity
windows went from 229 to **0**, pitch measured 128 vs 129.5 Hz expected, and
left RMS held steady at ~18,930 with no dropouts.

Against micromod's own renderer on a real 41-pattern module, envelope
correlation rose from **0.42 to 0.72** (aligned at an 11 s lag -- that lag is
the module load, since `kread` uses BASIN one byte at a time).

### A harness trap worth more than the bug it hid

Every audio test for an hour played the *wrong module*. `tests/run_x16_trace.sh`
wrote `MUSIC.MOD` while the player harness wrote `music.mod`; hostfs matches
case-insensitively, so a stale uppercase file silently shadowed the intended
one. Both now use `music.mod` and sweep the fsroot first. The measurement
looked plausible the whole time -- it was comparing one module's audio against
another module's reference.

### RAM is now the binding constraint, not CPU

Adding a `%lu` to a diagnostic overflowed the image: cc65's long-printf support
was the last straw against the resident 16,640-byte volume table. The C stack
was cut from 2 KB to 1 KB to fit. That table buys a flat +24 cycles/frame and
free volume changes, but it costs 43% of fixed RAM, and a UI will want that
space back. Revisiting it (banked pages copied on volume change, or 33 levels
instead of 65) is the first thing to weigh in Phase 8 proper.

### Cache refill no longer advances the position

`cache_refill` used to advance the read position by 256 as it filled. The
player also calls `cache_consume` with what the mixer actually used, so the two
double-counted and every refill skipped 256 bytes of sample. A refill now only
makes data visible; consuming it is the caller's business.

## 2026-09-07 -- Phase 8 (RAM) and Phase 10 (initial UI)

### RAM, not CPU, is what limits this player

The memory map settled the argument. Of 38.6 KB of fixed RAM:

| segment | bytes | what |
|---|---:|---|
| ALIGNDATA | 19,025 | volume table (16,640) + period->step tables (2,385) |
| CODE | 13,051 | player, mixer, cache, sequencer, cc65 runtime |
| BSS | 2,762 | mod_info, header buffer, state |
| ALIGNBSS | 1,024 | four voice cache pages |

Plus a 1 KB stack: about 900 bytes free. Not enough for a UI.

**The volume table now stores 33 levels, not 65** -- every second volume, with
the mixer picking its page by `(v + 1) >> 1` (rounding to nearest, so 64 still
reaches full scale). That frees **8.2 KB** for no CPU cost at all: volume
changes remain a single store. Two units out of 64 is roughly 0.27 dB, which
is inaudible on MOD content. PLAY.PRG went from 33,266 to 25,011 bytes.

(It was halved again to 17 levels later, as the UI grew -- see the correction
at the end of this file. The mixer now selects with `(v + 2) >> 2`.)

The bit-exact mixer test caught this properly once the C model was taught the
new mapping -- and then caught a stale test binary, which is the third time a
flag or data change has failed to dirty a target. `rm -f` before rebuilding.

### The initial UI

`examples/modplay/play.c`, verified running against a real 41-pattern module:
title, format, pattern count, position, pattern, row, speed, BPM, elapsed
time, an underrun counter, and four per-channel level bars. Space pauses, Q
quits.

Two things make it safe to draw while audio plays:

1. **conio writes straight to VERA**, not through CHROUT, so it is far faster
   than printf -- and invisible to the emulator's `-echo`, which is why the UI
   had to be checked by capturing a GIF and pulling a frame out of it.
2. **Every screen write is interleaved with `mod_player_update()`.** Starving
   the FIFO is not a dropped frame here: a partial stereo pair gets flushed and
   the channels swap. Measured result with the UI running: zero silent windows
   across 176 s, and the underrun counter reads 0.

The underrun counter is now zeroed after priming -- the FIFO is empty until
primed, so the first pass always counted one and the display was misleading.

## 2026-09-07 -- Mouse GUI, playlists, and the release build

### MACPTR made playlists possible

Loading a 347 KB module went from **11 s to 0.85 s** by replacing the
byte-at-a-time BASIN loop with MACPTR ($FF44), which moves up to 255 bytes per
call and wraps banks itself. This was not an optimisation -- an 11 second gap
between tracks makes a playlist unusable, so nothing else in this phase could
have shipped without it. BASIN remains the fallback: MACPTR returns carry set
on a device that does not support it.

### Halving the volume table paid for the UI

65 volume pages cost 16,640 bytes and left ~900 bytes free. Storing every
second level instead (33 pages, mixer picks its page with `(v + 1) >> 1`) freed
**8.2 KB** at no CPU cost -- volume changes are still one store. Two units out
of 64 is about 0.27 dB.

### The PETSCII literal trap, for the fourth time

The directory scanner compared filenames against `'M'`, which cc65 translates
to $CD (shifted M) while the directory returns $4D. Filenames, format magic,
the `"$"` device name -- **every byte that comes off the wire must be matched
numerically**, never against a character literal.

### Drawing while audio plays

Two rules keep the UI from breaking playback, and neither is optional:
starving the FIFO flushes a partial stereo pair and the output swaps channels.

1. `mod_player_update()` is called between screen writes -- including inside
   the per-channel loop of a pattern row. It returns immediately unless AFLOW
   is set, so calling it often is nearly free. Adding the inner call took the
   underrun count from 7 to 1 over the same run.
2. conio writes straight to VERA rather than through CHROUT, which is what
   makes a 19-row pattern redraw affordable at all. It also means the UI is
   invisible to the emulator's `-echo`, so it has to be checked by capturing a
   GIF and pulling a frame out of it.

### A paged pattern view was tried and reverted

Showing 32 rows a page and moving only the highlight would cut a redraw from 19
rows to 2. It showed stale row numbers -- the page-change redraw did not fire
reliably -- and the cause was not identifiable from screenshots. Reverted to
the centred scrolling window, which was already verified correct. Worth
revisiting behind a real test rather than by eye.

### Release layout

`make release` produces `release/` holding `AUTOBOOT.X16`, `MODPLAY.PRG` and a
README. AUTOBOOT.X16 is a straight copy of the player: the ROM's BOOT loads and
runs a PRG of that name from device 8 at startup, and cl65's cx16 target
already prefixes the binary with a BASIC RUN stub, so no launcher is needed.
Verified by booting the emulator with no `-prg` at all.

## 2026-09-07 -- Directory browsing

An empty directory used to end the program with "no .mod files here". That is
the wrong behaviour for a player: it leaves the user with nothing to act on.
The player now starts regardless, and the playlist lists folders alongside
modules so the user can browse to wherever the music actually is.

- The listing's type column ("PRG", "SEQ", "DIR") is parsed, so directories can
  be told from files. `CD:` is issued by opening logical file 15 with secondary
  address 15 -- on CBM devices the filename given to OPEN *is* the command.
- ".." is prepended by the scanner rather than relied upon from the device, so
  there is always a way up; the device's own "." and ".." are then skipped to
  avoid listing it twice.
- `step_track` skips directories and unloadable files, and does nothing at all
  when the list holds no modules -- it previously took `% playlist_count()`,
  which would have divided by zero.
- Keyboard navigation (up/down/RETURN) was added alongside the mouse. Mouse
  clicks cannot be injected into a headless emulator, so the directory path was
  verified with a `-DVERIFY_CHDIR` test hook that enters the first folder
  automatically. Confirmed: it entered the subdirectory, rescanned, and played
  track 4/6 with no underruns.

## 2026-09-07 -- Four bugs from real-world use

Reported against a real directory: no modules listed, long names displayed as
"prg", and the interface frozen with q not responding. Four distinct causes.

### 1. Long filenames were silently rejected

A dump of the raw "$" listing settled it -- the device returns names in full:

```
   "tunes1"           DIR
 "animals - gabba_mix - by DeeJay Jones.mod" PRG
```

A playlist entry held 32 bytes, so a name was truncated at 30 characters, which
cut off ".mod" -- the extension test then failed and the file was never listed.
A truncated name could not have been opened later either.

Entries are now 64 bytes across four banks (128 per bank, 512 total), and the
extension is tested against a rolling four-character tail so it sees the true
end of the name regardless of where the buffer ran out. Modules load from bank
5 upward to stay clear of the entries.

### 2. Names overflowed the panel and corrupted the screen

`printf("%-17s")` pads a short string but does **not** truncate a long one, so a
27-character filename ran past the 22-column playlist panel, wrapped, and
overwrote the pattern view down the left of the screen. That was the "long
names shown as prg" symptom. A `fitname()` helper now pads *or cuts* to an
exact width.

### 3. The song-end flag was never consumed -- the "freeze"

`mod_seq.song_end` is cleared by the sequencer only on its next tick, and the
UI loop spins far faster than that. So a single song end made *every*
subsequent pass call `step_track()`, loading module after module: a
track-change storm that looked like a hang. The flag is now cleared where it
is acted on.

### 4. Input sat behind a `continue` -- q did nothing

The song-end branch ended in `continue`, skipping the keyboard and mouse
handling below it. While that branch was live no key was ever read. Input is
now serviced first and unconditionally, before any playback state is examined.

**The lesson across all four:** every one needed a real directory of real files
to surface. The synthetic tests used short generated names in a flat directory
and could not have found any of them.

## 2026-09-07 -- Choppy playback: a wrong diagnosis, and the real one

One module played badly on hardware -- speed 4, high notes,
and several samples looping on 16 bytes. It sits at the intersection of every
expensive case at once, which is why it was the only one that showed.

### Two corrections to earlier work

**The UI was NOT starving the FIFO.** That was measured wrong. The longest gap
between audio top-ups during play is 2.8 ms and never exceeds 16 ms; drawing
never came close to starving anything. The earlier "UI off gives 1 underrun,
UI on gives 11" comparison was real but the mechanism was misread.

**The short-loop replication introduced an audible bug.** Filling a page by
replicating one loop iteration is only valid when the read position is already
INSIDE the loop. A fresh note starts at the sample start, before `loop_start`,
where the content is not yet periodic -- so the page became the first bytes of
the attack repeated, a ~1 kHz buzz, on 619 of that module's 863 note-ons.

All six existing cache cases happened to start exactly at `loop_start`, so
every one passed. Five cases that start before it now exist, and were checked
by disabling the guard and confirming they fail: a test that passes with and
without the fix proves nothing.

### The real cause: CPU overload

**~515 cycles per frame against a 499 budget -- 103%.** Measured breakdown:

| | cycles/frame |
|---|---:|
| mixer | 193 |
| sequencer tick + hooks | 110 |
| cache consume + refill | 104-128 |
| C chunk bookkeeping | 74 |
| tick_len recomputation | 15 |
| KERNAL VBLANK IRQ | 18 |

The cost model used until now counted only the mixer and the per-byte refill
cost, and so missed about 60% of the load -- most of it cc65's 32-bit
arithmetic in the sequencer, plus `cache_consume`, per-chunk bookkeeping and
the IRQ.

**The underrun counter understates by ~100x.** It samples the FIFO-empty flag
once per `mod_player_update()` call, so it misses starvation that happens
mid-burst. It reported 11 where a WAV capture showed 219 silent gaps.

### Fixes applied: four identities

Each was verified equivalent rather than assumed:

| change | verification |
|---|---|
| `mod_chan_period` skips its 32-bit multiply when `arpeggio_add == 0` -- `arp_tuning[0]` is 4096, so the multiply and both shifts are the identity | 19.4M combinations, 0 mismatches |
| `mod_seq_tick_len` cached; it was a 32-bit multiply AND divide per tick for a value that changes only on Fxx | trace gate |
| `mod_read_note` is pure 16-bit: 1024-byte patterns divide 8192 exactly, so `bank = base + p/8`, `addr = $A000 + (p%8)*1024 + row*16 + ch*4` -- no 32-bit offset, no `mod_advance_pos` | X16 trace, on a 17-pattern module so patterns 8-16 land in the second bank |
| neutral finetune skips its multiply (`finetune_mul[8]` is 4096) | trace gate |

**A near miss worth recording:** the `mod_read_note` change was first verified
against a 4-pattern module, where `pattern >> 3` is always zero and the new
path is never exercised. It passed and proved nothing. The corpus has to
contain the case, not merely the feature.

## Items 4, 5, 7: the rest of that module's work, and how it was verified

### Proving a render-loop change is safe

The obvious test -- capture a WAV before and after and compare -- does not
work here, and it took a while to accept that. The emulator's audio is
cycle-deterministic (a block from one run is found verbatim in another, just
shifted), but its WAV *recording* starts on host time, and how often AFLOW
fires depends on when the host's audio device drains the FIFO. Two captures of
one unmodified binary differ in **1354 of 1.59M frames**. Any real regression
smaller than that noise floor would be invisible.

So `tests/t_chunk.c` compiles the FIFO gate out (`-DCHUNK_TRACE`), which makes
`mod_player_update` a pure function of the module, and hashes every chunk
decision: the frame count, all four voice phases, all four read positions.
Page CONTENTS need no hashing -- they are a pure function of the position they
were filled from. Two builds with the same hash sequence render bit-identical
audio.

**The trap inside the harness itself:** `mod_player_start` primes the FIFO with
`while (audio_aflow()) mod_player_update();`. That loop is host-timing
dependent, and priming advances the sequencer, so every later chunk shifts.
This produced three confident FAIL reports that were really two runs of the
SAME binary disagreeing. Trace builds now skip priming, and the control -- run
the identical binary twice -- is checked BEFORE any FAIL is believed.

### Item 4: the chunk loop in assembly

Two four-iteration C loops ran per chunk, ~250 chunks a second, and cc65 spent
most of that recomputing `mix_phase[v * 3 + 2]` with a runtime multiply
(`mulax3`/`incax2` per voice). `_chunk_prepare` in `src/mixer_api.s` replaces
both with one pass over a four-entry offset table.

Merging the two loops is safe for a non-obvious reason: the second loop
re-reads `c_static[v]` after the refills, so a voice that just BECAME static is
excluded from the minimum. The assembly instead contributes 255 for it, which
is identical because the minimum starts at 255.

**28.8 cycles/frame** on that module (365.6 -> 336.8, against a 499 budget), and
256 bytes smaller. The estimate had been ~60; half of it was real.

### Item 5: cache_consume by arithmetic

A loop confined to one 256-byte page is advanced with
`(pos - loop_start + n) mod len` instead of being walked. The general path
costs four subroutine calls per wrap, and a 20-byte loop wraps eleven times to
advance the 224 bytes a page hands back.

| `cache_consume`, 224 bytes | before | after |
|---|---:|---:|
| 3-byte loop | 18741 | 2807 |
| 20-byte loop | 3054 | 602 |
| 50-byte loop | 1311 | 357 |
| 100-byte loop | 813 | 287 |
| long sample (no loop) | 300 | 307 |
| attack before the loop | 2556 | 2602 |

Power-of-two loops never reach this path -- those pages are static and are
never consumed at all -- so what is left is the odd remainder, the 20s and 50s.
On that module it is worth **-0.7** cycles/frame (it has none); on another,
**+3.5**. The case for it is the worst case: an 18,741-cycle consume is 84
cycles/frame for a single voice, 17% of the entire budget.

**The guard that matters:** being in the loop's PAGE is not being in the loop.
A short sample whose loop sits at its end has its attack in that same page, so
a fresh note-on would be reduced modulo the loop length and land somewhere
arbitrary inside it -- the same trap as the replication guard in
`cache_refill`. Removing the guard fails two of the new cases.

`cache_consume` had **no test coverage at all** before this. It now has 13
cases against a byte-at-a-time model, covering both sides of the branch.

### Item 7: an honest underrun counter

The counter sampled FIFO-empty once per `mod_player_update` call -- once per
~64 ms -- so a dropout that opened and closed inside one AFLOW batch was
invisible, and a burst counted once. It read 11 where a WAV showed 219 gaps.
It is now sampled once per chunk (~2.4 ms), for ~0.8 cycles/frame.

The number now tracks how LONG the FIFO was empty rather than counting
distinct dropouts: one long gap increments it once per chunk it spans. Zero
still means zero, which is what it is read for.

### Item 6: declined

"Render one chunk per `update()` call" would cut input latency from ~43 ms to
~3 ms by returning early. But the full 1024-frame batch is what fills the FIFO
to ~3 KB, and that depth is the cushion that absorbs a slow UI redraw --
returning after one chunk tops it to barely over the 1 KB AFLOW threshold. It
buys latency by spending the underrun margin the other three items exist to
create. If click latency needs fixing, the way to do it is a hook called
between chunks that polls the mouse without shrinking the fill depth.

## The level meters were not level meters

`draw_bar` plotted `ch_vol` -- the sequencer's channel volume. Most modules
leave that at 64 for most of the music, so all four bars sat at full scale and
the meter carried no information. The colour made it worse: it was chosen from
the bar's LENGTH, so a loud channel turned entirely red rather than reddening
at its top. The result was four red blocks that rarely moved.

Three separate fixes, because there were three separate faults:

**A real level.** `src/vu.s` reads the peak of each voice's 256-byte cache
page -- the sample data the mixer is about to play -- every 4th byte, so 64
samples spanning roughly the 16 ms that page covers. That is a sensible VU
window at a quarter of the cost of reading the page. ~2,600 cycles for all
four voices; the same loop in C would have been about 3% of the machine at a
25 Hz refresh, for a decoration. The audible level is that peak times the
channel volume, since the mixer applies volume through its own table.

**A logarithmic scale.** 8 cells per doubling, so the 40-cell bar spans five
doublings, about 30 dB. A linear bar spends most of its travel in the top few
dB, which is the other half of why the old one looked pinned.

**Colour by position, not by length.** Cells 0-23 green, 24-33 amber, 34-39
red, so a bar runs through the zones as it grows. Plus a peak-hold marker that
sits for ~0.8 s and then falls a cell per refresh -- ~1.6 s from full scale,
the usual VU fallback. The first attempt fell a cell per 8 refreshes and left
markers stranded above channels that had gone quiet seconds earlier.

**Cadence.** Meters update on the clock (25 Hz), not on row changes: at speed
6 a row is 120 ms, and a meter refreshed eight times a second reads as a
stutter rather than a level. That is affordable only because the bars repaint
incrementally -- just the cells that changed, which between two refreshes is
normally a handful rather than 40.

## Two smaller things

**Track numbering.** The header printed `cur_track + 1`, but `cur_track` is a
LIST index and sorting puts ".." and every subdirectory first -- so the first
module in a folder was announced as track 2, and the denominator counted
folders as tracks. The sort guarantees directories form a leading block, so
`playlist.c` counts them once per scan and `playlist_track_no()` /
`playlist_file_count()` report position among the modules.

**Loading feedback.** A large module takes about a second to come off the card
and the screen simply stopped, with no way to tell a slow read from a hang --
a bad property for a program that has actually hung on real hardware. The
loader now publishes `mod_load_total` / `mod_load_done` and calls an optional
`mod_load_tick` hook every 4 KB (4 KB, not every chunk: per-chunk would
repaint hundreds of times for no more information). It stays ignorant of the
display -- it counts bytes and calls a hook, and whoever set the hook decides
what that looks like. A failed load now prints the reason instead of silently
skipping to the next track.

## Correction: the volume table is 17 pages, not 33

`tools/gen_tables.py` emits `VOLTAB_PAGES = 17` at 4-unit granularity and
`src/mixer.s` selects the page with `(v + 2) >> 2`. The 65 -> 33 step is the
one this log described; the 33 -> 17 step that followed it was made to fit the
UI and never written down, so the docstring, the README and this file all
still claimed 33. 17 pages is 4,352 bytes.

Found while auditing what a second replay engine could reuse -- the audit had
to read the generator to discover the real number, which is exactly the cost
of an out-of-date comment next to a generated table.

## The Veranda player: next, previous and a song's end did nothing

Reported from a real X16 (R49): in the Veranda MOD Player next and previous
did nothing, and a song never gave way to the next one; the stand-alone
`PLAY.PRG` on the same machine and modules was fine. Everything those three
do goes through one routine, `step_track` (`examples/veranda/vmp_ui.c`): the
next and previous icons, play > next and previous, the Right and Left keys,
and the end of a song (`update_body`, on `mod_status.song_ends`). The engine
and the end-of-song signal were not at fault: the engine's first song end
equals micromod's song length for all 1,580 modules of the corpus that both
read, and under the emulator the player saw every end. `step_track` itself
did nothing in three states, each reproduced with real PS/2 packets and keys
(the emulator's `-mousescript`, R49, booted from an SD card as on the
machine):

- **Shuffle.** The random pick counted its misses in the same byte that then
  meant "found none": a first pick that fitted left it 0, and `step_track`
  returned. With a folder of n modules that is most picks (with ".." and
  four modules, three in five), so next and a song's end were mostly dead,
  and the song played again and again. Previous, with nothing in the
  history, still stepped back.
- **The playlist showing another folder.** The list in RAM is the folder
  shown; the music may come from another (a double click on "..", Backspace,
  a folder entered while it plays). `step_track` worked on the list shown:
  with no modules there it returned, so nothing moved again until the music's
  folder was opened by hand.
- **A module that does not load.** `play_entry` stops the music before it
  loads; on a failure the position was lost (`playing_ent` 0xFF), so the
  next step began at the top of the list and previous at its bottom, and a
  song's end on to such a module left silence.

Now, as `play.c`'s `step_track`: the position (`q_pos`) is the module last
played *or tried*, and survives a failure and stop; a module that does not
load is passed over (its error shown) and the next one tried, up to the
number of modules; the ends wrap (next at the last module plays the first,
previous at the first the last, and a song's end at the end of the list goes
on from the top -- the folder plays round, as `play.c` does); with one module
next and previous play it again. Next, previous and a song's end first make
the music's folder the playlist again (`music_dir`, main RAM: the folder of
the module last played or tried), so they always act on the music. Shuffle
picks a module other than the one before; when its picks miss, the next in
order. Repeat (off at the start) keeps the song: its end is not a step.
Opened from the shell, the playlist is the module's folder from that module
on (unchanged; now gated).

Gate `veranda-mouse-gate` (`tools/gate_vmp_mouse.sh`): the player from its
icon in the shell, every way of stepping, over a module that does not load,
paused, stopped, at both ends, from another folder, after Backspace, a song's
end with repeat off and on, shuffle; opened from the shell with a module; and
the order a folder plays in, as songs end, against `PLAY.PRG` built with the
test hooks `AUTOPLAY` and the new `VERIFY_ORDER` (each module tried on the
emulator's stdout): the same modules in the same order. The player before
this change fails its first step (next over the module that does not load
leaves silence). Overlay 3 grows by 88 bytes (7,497 of 8,191); the stress
build has 8 bytes to spare.

## Veranda main (V77-V84): shuffle never repeats, the Mods folder from MODS.CFG

Against Veranda's main (032e0c5, V84: Look 2's shell, long names, the dock,
the Mods folder).

- **Shuffle repeated the module playing.** A pick had to differ from
  `q_pos`, the module last played *or tried*. When the first pick was a
  module that does not load (b.mod), `q_pos` became that one, and the second
  pick could take the module that had been playing: a.mod, next, a.mod
  (`gate_vmp_mouse.sh` failed so). Now `step_track` keeps the module playing
  when next was asked for (`was`: `playing_ent`, else `q_pos`) and, with two
  or more modules, no pick is it nor the one just tried; when the picks find
  none, the next in order passes over it too (with nothing else that loads,
  nothing plays rather than the same module again). Its locals are static
  (smaller code): overlay 3 is 7,505 bytes and the stress build exactly
  8,191.
- **The Mods folder** (Veranda's docs/APPFORMAT.md, "The Mods folder",
  V84). At its start without a module, the player reads `MODS.CFG` in the
  data folder (`vr_cd(VR_DATA)`: `/VERANDA/.SYSTEM`, else `/VERANDA`, else
  the root): the bytes up to the first LF, CR or 0. A missing file, a
  path not starting with `/` or longer than 61 characters give the
  default, `/MODS`. If `ChDir` takes the path, it is `curDirName` and the
  playlist (logged `VMP MODS path`); if not (no such folder), the start is
  as it was (`/DOCUMENTS` from the shell). Nothing is trimmed: the shell
  alone writes the file. Started with a module, the module's folder is
  the playlist, as before. `mods_dir` in overlay 4 (+185 bytes). The FAT
  names' case is ignored, so a folder `Mods` is the default's `/MODS`.
- **The gates on Look 2 and V84.** `gate_vmp_mouse.sh` starts the player
  through the shell's new window (the sidebar's place Apps at 56,112, the
  second tile at 224,96) and opens o1.mod from `/DOCUMENTS` at the second
  tile. Its music is now in `/Tunes` (sessions 1-3: no `MODS.CFG`, no
  `/MODS`, so the player starts in `/DOCUMENTS` as before); session 4 puts
  `MODS.CFG` (`/Tunes` LF) in `.SYSTEM`: the player starts in `/Tunes`,
  shuffle on, eight nexts, never the same module twice in a row; session 5
  a file not valid (`Tunes` LF) and a folder `/MODS`: it starts in `/MODS`
  and plays from there. `gate_vmodplay.sh`'s host folders and SD cards get
  `MODS.CFG` = `/` (its scripted sessions start in the top folder and open
  `Mods` themselves; the default would find `Mods`). Its golden pictures
  were made again: Veranda's ink is now `$223` (Look 2's slate, V74), not
  black, and that is the only change in them outside the timing's fields.

## Veranda Look 2 (V74, V77, V79, V82): the bar, the window's chrome, the Open dialog, long names

Against Veranda's main (7dada0f).

- **The menu bar** is the kernel's (`theme_menus()` before `DoMenu`): the
  Veranda mark, then **MOD Player** (About, the desk accessories), File
  (Open..., Play all, Quit), Play, View, capitalised, in the UI font; the
  clock and the speaker at the right. The lists' widths are measured in
  the UI font and the bar's end follows the kernel's rule (`bar_end` in
  overlay 4: the SDK's `vr_bar_end` would cost 448 bytes of main RAM).
  The kernel puts a list under its bar item, 16 lines an item; the test's
  script (`barx`) and `gate_vmp_mouse.sh` (Play at x 156, items 16 apart)
  click there.
- **The window** is the SDK's chrome (`vrwin.h`, the SDK's `vrwin.c`
  compiled into overlay 2 beside `frame()`; `win_title`, `win_status`,
  `win_hit` in main RAM map it for the UI): card columns 1-78, rows 3-58
  as before; the title band on the theme's accent with the module's title
  and the close glyph at the right (was a box at the left), the status bar
  (rows 57-58: the state at the left, the modes at the right), a frame and
  the dither shadow, over the desk's colour (the theme's; GEOS's grey
  without one). Inside, the layout is as it was.
- **Look 2's palette** (`theme_palette`, light or dark) replaces the X16's
  with the player's own softer entries: panel heads and the progress
  bar's track chrome, the selection the theme's highlight (`hilite`),
  the scroll bar's thumb, the progress and the pattern's row the
  selection blue, the meters green, yellow and red on chrome.
- **File > Open...** is Veranda's Open dialog (`vr_file`, the SDK's
  `vrchoose.c` compiled into overlay 5, its BSS in main RAM; `choose()`
  maps it; `is_mod` its filter): modules and folders by their long names,
  the folder pop-up, the keys. The player's own box (Veranda Calc's
  chooser's design) is gone; its "Play all" is now File > Play all (the
  playlist's folder from its first module).
- **Long names:** started with a module, its whole name comes from
  `dataLongName` (api 5); `dataFileName` holds only the short form of a
  name longer than 16 (`Holiday~1D38.mod`), which the playlist does not
  have. `gate_vmp_mouse.sh`'s document is now "o1 a module with a long
  name from 2026.mod" (42 characters).
- **Its own tile** (`tile = tile.txt`: the picture the shell showed for
  the class VModPlay, purple); version 1.2.
- **Main RAM.** The test build had 169 bytes left; the SDK's parts in main
  RAM (the theme, the trampolines, the dialog's 686 bytes of BSS) did not
  fit. Moved: `vmp_draw.c` into overlay 3 (only the UI calls it), the VU
  meters and `text()` into overlay 2, the menus' layout and the test
  builds' logging (`log_track`, `vmp_meter_log`) into overlay 4 (its
  read-only data in `OVL4RO`), the test script's array to 64 steps (59
  used). Now: the release ends at $7C7E (was $7950), the test build at
  $84EA (278 bytes left), the stress build at $826C; overlays 2,385,
  6,142, 7,231, 1,307 and 8,059 bytes (the stress build's overlay 3
  7,963). `log_track`'s line (37 characters and 20 of the name) had a
  buffer of 48: with a long name it ran over into `main`'s frame; 64 now.
- **The cost** (`gate_vmodplay.sh`, 8 MHz cycles without the audio
  batches, before / after): a main loop pass 61,412 / 59,349 on average;
  the stress run's VU meters 2.0 / 1.8 ms (max 7.0 / 6.9), fields 5.7 /
  5.7 ms, pattern rows 12.7 / 12.6 ms, the list panel 221 / 217 ms, a
  list scroll 89.8 / 81.2 ms; 0 underruns, the audio 63 % of the CPU as
  before. The status bar's state is drawn alone when only it changes (the
  UI font costs about 4,000 cycles a character): `ui_loading` writes
  "Loading n %" there every 4 KB.
- **The gates.** `gate_vmodplay.sh`: the info block's class V1.2, api 5,
  flags 3 (the tile), the tile; the session through the new dialog (its
  `VRF` lines: Backspace to `/`, M and Return into Mods, C and Return:
  the module starting with C) and File > Play all; the eleven golden pictures made
  again (the Look 2 bar, the chrome, the palette, the dialog in `open` and
  `openroot`, the about box in the UI font; the bar's clock masked: it
  follows the emulated minutes); new `check_vmodplay.py --look` on
  `playing`: Look 2's palette, the bar (the mark, the name, its chrome
  cards and line), the title band, the close glyph, the status bar, the
  frame, the shadow, the desk, the panels' heads.
