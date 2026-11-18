# x16-mod — an Amiga ProTracker MOD player for the Commander X16

**Status:** Phases 0-5 implemented and passing (2026-09-06). Phases 6-10
(sequencer, effects, optimisation, library API, UI) remain.
Measured results are in [docs/decisions.md](docs/decisions.md); run `make test`.
**Source brief:** [docs/original-brief.md](docs/original-brief.md)
**Verified toolchain facts:** [docs/environment.md](docs/environment.md)

---

## 1. What this is

A ProTracker `.MOD` replay **library** for the stock Commander X16 (8 MHz
65C02, VERA PCM, 2 MB banked RAM), plus a standalone player application built
on top of it. Games and demos should be able to link the library; the player is
just its first consumer.

The brief's core insight is correct and is preserved: **the project lives or
dies on one question** — can an 8 MHz 65C02 mix four sampled voices at usable
quality while paying for banked-RAM access? Everything downstream is ordinary
software engineering. So the plan front-loads that question and puts a hard,
measurable gate on it before any MOD parsing exists.

## 2. What changed after surveying the machine

The brief was written without access to the box. Six findings change the plan;
all are reproduced in [docs/environment.md](docs/environment.md).

1. **The emulator has a guest-readable 32-bit cycle counter at `$9FB8-$9FBB`.**
   The brief proposed measuring headroom indirectly ("how much CPU remains").
   We can instead measure the mixer's cost *exactly*, in cycles, from inside
   the guest. Phase 1 and 2 become quantitative rather than judgemental. This
   is the single most valuable thing found and it reshapes the whole test plan.

2. **A bank switch is 3 cycles (`sta $00`), not a subroutine.** The brief's
   fear that per-sample bank switching "will probably kill performance" is
   overstated — 4 switches per frame is ~24 cycles. The sample cache is still
   the right design, but for a *different* reason: it removes the 8 KB window
   boundary from the hot loop and lets the inner loop use fixed absolute
   addressing. Justify it on those grounds, not on bank-switch cost.

3. **`AFLOW` can only be cleared by refilling the FIFO to >= 1/4 (1024 bytes).**
   This is a hard floor on block size that the brief did not account for. It
   happens to be convenient: it forces block-based rendering, which is what we
   wanted anyway.

4. **`-prg` does not work in `-testbench` mode** (it is implemented as a BASIC
   paste). Test code must be poked in with `STM` — measured at ~105 KB/s, so
   this is a non-issue once known, but it silently wastes an afternoon if not.

5. **`openmpt123` is installed and `--filter 1` selects nearest-neighbour
   interpolation** — which is exactly the resampling the brief specifies. It is
   a free second oracle alongside Micromod, agreeing with our own algorithm.

6. **The volume table can leave the hot loop entirely.** The brief worries about
   a 65 x 256 = 16,640-entry pre-scaled table. Because volume is constant across
   a tick and we are already copying sample data into a cache, volume scaling
   can be applied *during the cache fill* — on the source bytes, of which there
   are fewer than output samples for any note below its base pitch. This
   collapses 16 KB of tables to a single 256-byte table and saves ~6 cycles per
   voice per output sample. See §5.4.

## 3. Cycle budget — is this actually going to work?

Worth doing on paper before committing, because it determines the target rate.

Per stereo output frame the mixer must advance and sample four voices, sum two
pairs, and write two bytes to the FIFO. Estimated inner-loop cost per voice,
using 8.16 fixed-point phase, a page-aligned per-voice cache, and volume
pre-applied during cache fill:

| Step | Cycles |
|---|---:|
| phase fraction low  (`lda`/`adc`/`sta` zp) | 9 |
| phase fraction high (`lda`/`adc`/`sta` zp) | 9 |
| carry into 8-bit integer index (`bcc`/`inc zp`) | ~5.5 |
| `ldy phase_int` | 3 |
| `lda cache_page,y` (self-modified page, no page-cross) | 4 |
| accumulate into the running L or R sum | ~5 |
| **per voice** | **~35.5** |

Four voices ~142, plus two `sta $9F3D` FIFO writes (8) and loop overhead (~8),
unrolled: **~160 cycles per stereo frame**, before cache refills and the
tracker tick.

Against the measured frame budgets:

| Output rate | Cycles/frame | Mixer @160 | Headroom |
|---:|---:|---:|---:|
| 12,207 Hz (R=32) | 655 | 24% | 76% |
| 16,022 Hz (R=42) | 499 | 32% | 68% |
| 18,311 Hz (R=48) | 437 | 37% | 63% |
| 21,362 Hz (R=56) | 374 | 43% | 57% |
| 24,414 Hz (R=64) | 328 | 49% | 51% |
| 32,044 Hz (R=84) | 250 | 64% | 36% |

Reserve ~10% for cache refills and ~5% for the tracker tick and effects.

**Conclusion: 16 kHz should be comfortable and 24 kHz looks genuinely
reachable.** That is a better outlook than the brief assumed. Treat 16,022 Hz
(R=42) as the baseline target and 24,414 Hz (R=64) as the stretch goal. These
are estimates from instruction timings — Phase 2 replaces every number in this
table with a measurement.

## 4. Architecture

```
        MODPLAY.PRG / game / demo
                  |
        ---------------------------  public API (include/x16mod.h)
                  |
            MOD sequencer            C     ticks at ~50 Hz
         (order, rows, effects)            knows nothing about VERA
                  |
        ---------------------------  channel command interface (§5.5)
                  |
          4 x mod_voice state        ZP    the boundary that lets the
                  |                        mixer be replaced wholesale
        ---------------------------
                  |
        sample cache  <----  banked RAM   asm
                  |
            65C02 mixer                    asm, the hot path
                  |
             VERA PCM FIFO
```

Two boundaries matter:

- **Sequencer / voice-state.** The sequencer only ever writes
  sample/period/volume/trigger into four voice structs. It must never touch
  VERA. This is what makes a future 65C816 mixer, or a PSG fallback, a
  drop-in replacement.
- **Cache / mixer.** The mixer only reads from page-aligned cache buffers in
  fixed RAM and never touches the bank register. Banking is entirely the cache
  layer's problem.

### Language split

C (`cc65`) for loading, parsing, sequencing, effects, UI. Assembly (`ca65`)
for the mixer, the FIFO/IRQ path and the cache refill. This follows the brief.
Note that the sequencer being C is not just convenience — it is what makes the
host oracle in §7 possible, because the *same source* compiles natively.

## 5. Design decisions taken up front

These are decided now so implementation is not blocked on them; each names the
measurement that would overturn it.

### 5.1 Output format: 8-bit stereo

8-bit signed stereo, `<left><right>`. Amiga hard panning means L = ch1+ch4 and
R = ch2+ch3, so each side sums only two voices — no headroom scaling needed
beyond a shift. 16-bit output would double FIFO bandwidth and mixing width for
inaudible gain given 8-bit source samples.

Stereo costs exactly one extra `sta $9F3D` per frame over mono. Take it.

### 5.2 Fixed point: 8.16 phase, 8.16 step

The brief's `uint32_t phase/step` is right in spirit; the exact split matters.

An 8-bit fraction is **not enough**: a step of 0.517 (middle C at 16 kHz) has
1/256 resolution, ~0.75% of the step, which is ~13 cents of pitch error —
plainly audible. A 16-bit fraction gives ~0.0005 cents. Use **8 integer bits
(index into a 256-byte cache page) + 16 fraction bits**.

### 5.3 Period -> step by table lookup, no division

Step for Amiga period `P` at output rate `F` is:

```
step = 7093789.2 / (2 * P * F)        (PAL)
```

ProTracker periods span 113..856 normally and 113..1712 with the extended
range, and effects (portamento, vibrato, arpeggio) produce arbitrary values in
between — so this cannot be reduced to a 60-entry note table.

Instead build a **direct-indexed table over the period itself**: 3 bytes of
8.16 step per period. 113..907 is 795 entries = 2,385 bytes; the full extended
range is ~4.8 KB. Generated by `tools/gen_tables.py` at build time for the
chosen output rate. This removes division from the runtime entirely and turns
pitch changes into a 3-byte copy.

### 5.4 Volume applied during cache fill, not in the mixer

Volume is constant for the duration of a tick, and we are copying sample bytes
into the cache anyway. Scale them there, through a single 256-byte table
selected by the channel's current volume.

- Hot loop: saves the `tay` + table `lda` per voice per output sample (~6
  cycles/voice/frame, ~24 cycles/frame, ~15% of the mixer).
- Storage: one 256-byte table instead of 16,640 bytes.
- Cost: the scaling happens on *source* bytes. For any note at or below its
  base pitch (step <= 1) that is fewer operations than doing it per output
  sample. For the top octave (step up to ~1.96) it is up to 2x more — an
  acceptable trade, and measurable in Phase 3.
- The 256-byte table for volume `v` is rebuilt only when a channel's volume
  changes. Worst case 4 channels x 50 Hz; if that proves costly, precompute all
  65 tables into banked RAM once at init and copy the page in on change.

**Falsifiable:** if Phase 3 shows cache-fill cost dominating for high notes,
fall back to the brief's pre-scaled tables quantised to 32 volume levels
(8 KB resident).

### 5.5 Mixer command interface

Per the brief, with the addition that everything is `uint8_t`-friendly and no
struct is passed by value (cc65 generates poor code for that):

```c
void mixer_set_sample (uint8_t ch, uint8_t sample_num);
void mixer_set_period (uint8_t ch, uint16_t period);
void mixer_set_volume (uint8_t ch, uint8_t volume);   /* 0..64 */
void mixer_set_offset (uint8_t ch, uint16_t offset);  /* effect 9xx */
void mixer_trigger    (uint8_t ch);
void mixer_stop       (uint8_t ch);
```

### 5.6 Sample cache: 256-byte page per voice, chunked rendering

Each voice has one **page-aligned 256-byte** buffer in fixed RAM. The mixer's
8-bit integer phase indexes it directly, and the page's high byte is patched
into the `lda abs,y` by self-modifying code at voice-setup time.

Rendering is therefore three nested levels:

```
tick        (~320 frames at 16 kHz / 125 BPM; sequencer runs between ticks)
  chunk     (bounded by the first voice whose cache page runs out)
    frame   (the unrolled 4-voice inner loop)
```

Chunk length = `min(frames remaining in tick, per-voice frames until refill)`.
"Frames until refill" is `(256 - phase_int) / step`, a division — but it is
computed at most a few times per tick per voice (~800/s), so ~2% CPU. If that
measures badly, it is replaceable by a reciprocal table indexed by period,
exactly like §5.3.

Samples spanning the 8 KB window are entirely the cache layer's problem:
a 24-bit sample position maps to `bank = base + (pos >> 13)`,
`addr = $A000 + (pos & $1FFF)`, and a refill that crosses `$BFFF` simply bumps
the bank and continues. Loop wrap is handled in the same routine.

### 5.7 Tick length

Standard MOD timing, accumulated in fixed point so the error does not drift:

```
frames_per_tick = (output_rate * 5) / (bpm * 2)
```

At 16,022 Hz and 125 BPM that is 320.44 — keep the fraction.

### 5.8 Memory map

Fixed RAM (`$0800-$9EFF`, ~38.4 KB):

| Contents | Size |
|---|---|
| Player + mixer + sequencer code | ~10-14 KB |
| 4 x voice cache page (page-aligned) | 1 KB |
| period -> step table | ~2.4 KB |
| volume scaling table(s) | 256 B (or 8 KB if §5.4 is overturned) |
| current pattern (64 rows x 4 ch x 4 B) | 1 KB |
| sample descriptors, voice/sequencer state | < 1 KB |
| UI / screen buffers | ~2 KB |

Comfortable. Zero page: ~180 bytes available (§ environment doc); the four
voice phase/step words are the priority residents.

Banked RAM: patterns and all 31 samples. A worst-case MOD is ~2 MB of sample
data against 2,040 KB available; typical modules are 100-600 KB. Samples are
stored contiguously with 24-bit positions.

## 6. Phases

Phase ordering follows the brief's own revised sequence (VERA PCM -> 1 voice ->
4 voices -> memory -> parser -> sequencer -> effects -> UI), with a new Phase 0
for the harness, and testing folded in continuously rather than deferred.

Every phase has an **exit gate that is a measurement, not an opinion.**

---

### Phase 0 — Scaffolding and the measurement harness

Build the instruments before the thing being measured. Small phase, high
leverage: every later gate depends on it.

- Repo layout (§8), `Makefile` with `CC65_HOME` and `-C cx16-asm.cfg` wired in.
- `tools/tbrun.py` — testbench driver: poke a linked blob in via `STM`, import
  `-Ln` labels, `RUN` a symbol, read results back. Wraps the `-prg` trap.
- `tools/cycles.inc` — `CYC_START` / `CYC_STOP` macros around `$9FB8-$9FBB`,
  plus the host side that reports cycles and cycles-per-frame.
- `tools/wavfix.py` — repair headers on emulator WAVs killed by `timeout`.
- `tools/gen_tables.py` — period->step and volume tables for a given rate.
- `tools/mkmod.py` — synthetic MOD generator (needed from Phase 6, built now
  because it is pure host code and unblocks test authoring).
- Fetch and build Micromod (BSD-3-Clause) into `vendor/`.

**Gate:** `make test` runs a trivial 65C02 routine under testbench and reports
its cycle count, matching a hand-computed value. (Already prototyped: the
`sta $9FB8` / `lda $9FB8` pair measured 7 cycles.)

---

### Phase 1 — Prove the audio path and establish the real budget

No MOD parsing, no mixing. Just: can we keep the FIFO fed, and what does that
alone cost?

- `vera_audio.s`: `audio_init(rate)`, `audio_start()`, `audio_stop()`.
- AFLOW interrupt handler, respecting the >= 1/4 refill rule (§ environment).
  Also implement a polled variant — compare the two; polling may well win for a
  dedicated player, and the IRQ path matters for the library use case.
- Play a fixed waveform from fixed RAM at each rate.
- Measure, with the cycle counter: interrupt entry/exit overhead, cost per FIFO
  byte, and total cycles consumed per second by an *empty* mixer.

**Gate:** continuous playback with zero underruns at 8, 12, 16, 20, 24 and
32 kHz, and a table of measured overhead per rate. This replaces §3's estimates
with facts and picks the baseline rate for Phase 2.

---

### Phase 2 — The 4-voice mixer

The project's real risk. Build it in two steps.

**2a — one voice.** Nearest-neighbour, 8.16 phase, sample in fixed RAM, no
looping. Establish the inner-loop shape and measure cycles/frame.

**2b — four voices, stereo.** Unrolled, hard-panned L=1+4 / R=2+3, sample loops
supported, inactive-channel handling.

Optimise in the brief's order — zero-page state, fixed-point stepping, no
multiply, volume tables, unrolling, inactive channels, stereo — but **only
against measurements**, and record each step's cost in `docs/decisions.md`.

Correctness is checked bit-exactly, not by ear: build the mixer with a
compile-time switch that renders into a RAM buffer instead of `AUDIO_DATA`,
run it under testbench, read the buffer back, and compare against
`host/host_mixer.c` — a C model of the same algorithm. Any divergence is a bug
in one of the two.

**Gate (the project's validation gate):** 4-voice stereo at >= 16,022 Hz with
**>= 40% CPU headroom**, output bit-identical to the C model, sustained
indefinitely without underruns. Also record the highest rate that holds.

If this gate fails, stop and reconsider (drop to 2 voices? mono? PSG hybrid?)
before writing a line of MOD parsing.

---

### Phase 3 — Banked sample memory and the cache

Now make the samples live where real samples live.

- 24-bit sample positions; bank/offset mapping; window-crossing refills.
- Per-voice 256-byte cache page; volume applied during fill (§5.4).
- Chunked rendering (§5.6); loop-point handling inside the refill.

**Gate:** Phase 2's rate sustained with all sample data in banked RAM, costing
**<= 10% additional CPU**, output still bit-identical to the host model
(extended to model the cache). Include a stress case: four voices at maximum
pitch, each in a different bank, each crossing a window boundary and a loop
point within the same tick.

---

### Phase 4 — Freeze the replay/mixer boundary

Small but deliberate. Implement §5.5 exactly, make the voice state the only
shared surface, and add a build that swaps in a null mixer to prove the
sequencer has no VERA dependency. This is what protects a future 65C816 mixer.

**Gate:** sequencer object files link and run against the null mixer with zero
VERA symbols referenced.

---

### Phase 5 — MOD loader

- Parse `M.K.`, `M!K!`, `4CHN`, `FLT4`; 31 instruments, 4 channels only.
- Header, 31 sample descriptors, length, order table, patterns, sample data.
- Load patterns and samples into banked RAM (KERNAL `LOAD`/`MACPTR` to `$A000`,
  8 KB per bank).
- Stays in C; no optimisation.

**Gate:** `MODPLAY.PRG MUSIC.MOD` prints title, pattern count, sample count and
song length matching what Micromod reports for the same file, for a corpus of
test modules — including deliberately malformed ones, which must be rejected
rather than crash.

---

### Phase 6 — Minimal sequencer

Order table, row progression, speed, tempo, sample trigger, period, volume,
sample loops. Effects `Cxx`, `Fxx`, `Bxx`, `Dxx` only.

Runs on ticks at ~50 Hz, so its cost is structurally small — but measure it
anyway and record it.

**Gate:** recognisable music from real modules, and the per-tick channel trace
(§7) matches Micromod exactly for every test module.

---

### Phase 7 — Effect compatibility

Add the remaining effects **one at a time, each with its own synthetic test
module and trace comparison** before starting the next:

`0xy` arpeggio, `1xx`/`2xx` portamento, `3xx` tone portamento, `4xy` vibrato,
`5xy`, `6xy`, `7xy` tremolo, `9xx` sample offset, `Axy` volume slide,
`E1x`/`E2x` fine portamento, `E5x` finetune, `E6x` pattern loop, `E9x`
retrigger, `EAx`/`EBx` fine volume, `ECx` note cut, `EDx` note delay,
`EEx` pattern delay.

Budget real time here: this is where ProTracker's genuine quirks live
(`E6x` loop counting, `9xx` offset memory, arpeggio's tick phase,
vibrato waveform tables and retrigger behaviour). The trace oracle is what
makes it tractable.

**Gate:** all effects trace-identical to Micromod across the synthetic suite
and a corpus of real modules.

---

### Phase 8 — Profile, then optimise

Only now, and only where measurements point. Candidates from the brief:

- Specialised mixers for 1/2/3/4 active voices, selected on voice-state change
  rather than branched per sample.
- Deeper unrolling; further zero-page allocation.
- Revisiting §5.4 with measured cache-fill costs.
- Raising the output rate if headroom allows.

**Gate:** a measured improvement, with the before/after cycle counts recorded
in `docs/decisions.md`. No unmeasured optimisation is merged.

---

### Phase 9 — Library API

Separate the engine from the application (`lib/x16mod.lib`):

```c
void     mod_init(void);
uint8_t  mod_load(const char *filename);
void     mod_play(void);   void mod_pause(void);  void mod_stop(void);
void     mod_update(void);
uint16_t mod_get_position(void);  void mod_set_position(uint16_t pos);
void     mod_set_master_volume(uint8_t vol);
const char *mod_title(void);      const char *mod_sample_name(uint8_t n);
uint8_t  mod_pattern(void);       uint8_t mod_row(void);
```

**Gate:** `examples/minimal/` — a program under 30 lines that loads and plays a
module — builds and runs against the library alone.

---

### Phase 10 — Player UI

Only once playback is solid. Title/artist, pattern and row position, per-channel
level bars, a tracker-style pattern view, elapsed/total time. Sample names and
scopes if the headroom measured in Phase 8 permits.

**Gate:** the UI costs measurably less than the remaining headroom, and
disabling it changes no audio output byte.

## 7. Test strategy

The brief's Micromod-as-oracle idea is right, and the machine survey makes a
stronger three-way version possible.

**Layer 1 — trace equivalence (the workhorse).** Every player dumps, per tick
per channel: sample number, period, volume, sample position, loop state, plus
song position/row/speed. Three producers of that trace:

1. `vendor/micromod` — an independent BSD-3 implementation. Validates that our
   *algorithm* is right.
2. `host/` — our own sequencer C compiled natively with gcc. Validates that the
   *X16 build* is right, because it is literally the same source.
3. The X16 build under testbench. The real thing.

A disagreement between 2 and 3 is a compiler/arithmetic/porting bug. A
disagreement between 1 and 2 is a ProTracker semantics bug. Different causes,
different fixes — which is precisely why running all three is worth it.

**Layer 2 — bit-exact PCM.** For the mixer (assembly, no host twin), render to
a RAM buffer under testbench, read it back via `RQM`, and compare byte-for-byte
against `host/host_mixer.c`. Deterministic, no audio timing involved.

**Layer 3 — cycle regression.** Every gate's cycle measurement is recorded and
asserted in CI. A change that silently costs 15% shows up as a failed test, not
as underruns three phases later.

**Layer 4 — listening.** Warp-mode WAV capture (~9x real time) plus
`openmpt123 --filter 1 --samplerate <rate> --channels 2 --render` for an
independent rendering to compare against. For humans, not for CI.

**Test corpus.** Synthetic modules from `tools/mkmod.py`, one per behaviour —
`arpeggio`, `vibrato`, `sample-loop`, `pattern-break`, `note-delay`, `tempo`,
`offset`, `pattern-loop`, `finetune`, `retrigger` — each minimal enough that a
trace divergence points at one thing. Plus a handful of real modules for
integration; those are fetched at test time rather than committed, since their
licensing is not ours to assume.

## 8. Repository layout

```
x16-mod/
  Makefile                 CC65_HOME, cx16-asm.cfg, host + X16 + test targets
  PLAN.md                  this document
  README.md
  docs/
    original-brief.md      the source brief, preserved
    environment.md         verified toolchain/emulator facts
    decisions.md           running log: decision, measurement, date
  include/x16mod.h         public library API
  src/
    vera_audio.s           FIFO, AFLOW IRQ, rate control
    mixer.s                the hot path
    cache.s                banked-RAM sample cache
    tables.s               generated (gen_tables.py)
    mod_load.c             parsing + banked loading
    mod_play.c             sequencer, effects
    mod_state.h            voice/sequencer state shared with host/
  host/
    host_main.c            native oracle build of the sequencer
    host_mixer.c           C model of mixer.s, bit-exact
  tools/
    tbrun.py  cycles.inc  wavfix.py  gen_tables.py  mkmod.py  cmp_trace.py
  tests/
    mods/  golden/  test_mixer.py  test_seq.py  test_effects.py
  vendor/micromod/         BSD-3 reference (fetched, not vendored blindly)
  examples/
    modplay/               MODPLAY.PRG
    minimal/               the 30-line proof that the library stands alone
  lib/x16mod.lib
```

## 9. Risks

| Risk | Likelihood | Mitigation |
|---|---|---|
| Mixer too slow at 16 kHz | Low — §3 estimates 32% | Phase 2 gate stops the project early; fall back to 12 kHz, then to 2-voice or mono |
| Cache refill spikes cause underruns at tick boundaries | Medium | Amortise refills across the tick rather than at its start; the 4 KB FIFO gives ~90 ms of slack at 16 kHz, which is generous |
| ProTracker effect quirks | High — this is where MOD players always bleed | Trace oracle, one effect at a time; budget real time for Phase 7 |
| cc65 code quality in the sequencer | Medium | It runs at 50 Hz; measure before assuming it matters |
| Banked-RAM exhaustion on large modules | Low | 2,040 KB available; detect and reject at load with a clear message |
| Real hardware differs from the emulator | Unknown | Emulator is r50 and well regarded, but nothing here has been run on real silicon — flag as an open validation item, not a solved problem |

## 10. Immediate next step

Phase 0. Concretely: create the repo skeleton, write the `Makefile`, port the
already-prototyped testbench driver and cycle-counter macros into `tools/`, and
fetch Micromod. That produces a working measurement harness in one sitting, and
Phase 1 can then answer the project's central question with numbers rather than
estimates.
