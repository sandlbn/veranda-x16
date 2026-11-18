Micromod is a good behavioral reference because it already implements ProTracker MOD sequencing/effects in ANSI C under BSD-3-Clause; cc65 has an explicit cx16 target and documents the X16’s 8 KB banked-RAM window at $A000-$BFFF.

Phase 1 — prove the audio path

Do not start with MOD parsing. First prove that a stock 8 MHz 65C02 can sustain the mixer.

Build something like:

modplay/
├── src/
│   ├── main.c
│   ├── vera_audio.s
│   ├── mixer.s
│   └── test_samples.s
├── include/
├── tests/
└── Makefile

Implement only:

audio_init(sample_rate);
audio_start();
audio_stop();
mixer_fill();

Take four fixed 8-bit samples and play them simultaneously at different pitches.

Benchmark:

8 kHz
12 kHz
16 kHz
20 kHz
24 kHz
32 kHz

The important measurement is how much CPU remains while continuously keeping VERA's PCM FIFO fed.

Exit criterion: four independent sampled voices play indefinitely without underruns on an 8 MHz X16.

Phase 2 — build the optimized 65C02 voice mixer

Represent each voice with a tiny state structure:

struct mod_voice {
    uint32_t phase;
    uint32_t step;

    uint8_t *sample;
    uint16_t length;
    uint16_t loop_start;
    uint16_t loop_length;

    uint8_t volume;
};

Conceptually each output sample does:

phase += step
sample_position = phase >> N
sample = sample_data[position]
output += scaled_sample

But the actual hot path should be 65C02 assembly, not cc65-generated C.

Optimize in roughly this order:

1. Put frequently accessed voice state in zero page.
2. Fixed-point stepping, no division.
3. No multiplication in inner loop.
4. Volume lookup tables.
5. Unroll four-channel mixer.
6. Special handling for inactive channels.
7. Optimize stereo mixing.

Original Amiga stereo mapping:

CH1 ─┐
     ├── Left
CH4 ─┘

CH2 ─┐
     ├── Right
CH3 ─┘

That's useful because each stereo side only requires mixing two channels.

Don't chase exact Paula analog behavior yet. Nearest-neighbor 8-bit samples are enough.

Exit criterion: reliable 4-channel stereo mixer at your chosen baseline. I'd initially aim for 16 kHz, then see whether 20–24 kHz is practical.

Phase 3 — define a clean replay-engine/mixer boundary

This is important because later you may want a 65C816 mixer without touching MOD playback.

Use an interface roughly like:

typedef struct {
    uint8_t sample;
    uint16_t period;
    uint8_t volume;
    uint32_t offset;
} mod_channel_t;

void mixer_set_sample(uint8_t ch, ...);
void mixer_set_period(uint8_t ch, uint16_t period);
void mixer_set_volume(uint8_t ch, uint8_t volume);
void mixer_trigger(uint8_t ch);

Then:

MOD engine
    │
    │ commands
    ▼
4 channel states
    │
    ▼
65C02 mixer
    │
    ▼
VERA PCM

The MOD engine should know nothing about VERA.

Phase 4 — minimal MOD loader

Now implement .MOD parsing.

Initially support only classic:

M.K.
M!K!
4CHN
FLT4

and 31-instrument, 4-channel modules.

Parse:

MOD header
├── song name
├── 31 sample descriptors
├── song length
├── pattern order table
├── signature
├── patterns
└── sample data

Ignore exotic trackers and non-four-channel formats initially.

The parser can stay mostly in C. There is little reason to optimize it.

Have a simple command-line program:

MODPLAY.PRG MUSIC.MOD

Initially just print:

Title: Enigma
Patterns: 23
Samples: 31
Length: 42

before attempting playback.

Phase 5 — solve X16 sample memory

This deserves its own phase.

cc65's documented X16 layout exposes banked RAM through an 8 KB $A000-$BFFF window, so large MODs cannot simply be treated as one flat pointer.

I would store:

Base RAM
────────────────────────
player
pattern state
channel state
PCM buffers
critical sample cache

Banked RAM
────────────────────────
patterns
sample 1
sample 2
sample 3
...
sample 31

But avoid this:

for every PCM output sample:
    select RAM bank
    read sample
    select another bank
    read sample
    ...

That will probably kill performance.

Instead use a sample cache.

For example:

Banked MOD data
       │
       ▼
┌──────────────────┐
│ sample cache     │
│ 2–8 KB chunks   │
└────────┬─────────┘
         │
         ▼
       mixer

Cache upcoming portions of active samples into regular RAM while the mixer consumes the current block.

This is likely the most interesting engineering problem in the whole player.

Phase 6 — minimal ProTracker sequencer

Now make an intentionally incomplete player.

Implement:

pattern order
row progression
speed
tempo
sample trigger
period/pitch
volume
sample loops

Initially support effects:

Cxx  Set volume
Fxx  Set speed/tempo
Bxx  Position jump
Dxx  Pattern break

At this stage, quite a few MODs should already produce recognizable music.

The tracker engine should run on ticks, not audio samples:

PCM mixer:       ~16,000–24,000 Hz

MOD engine:            ~50-ish Hz

Therefore effect processing itself should consume relatively little CPU.

Phase 7 — ProTracker effect compatibility

Then implement the remaining effects incrementally:

0xy  Arpeggio
1xx  Portamento up
2xx  Portamento down
3xx  Tone portamento
4xy  Vibrato
5xy  Tone porta + volume slide
6xy  Vibrato + volume slide
7xy  Tremolo
9xx  Sample offset
Axy  Volume slide

E1x  Fine porta up
E2x  Fine porta down
E5x  Set finetune
E6x  Pattern loop
E9x  Retrigger
EAx  Fine volume up
EBx  Fine volume down
ECx  Note cut
EDx  Note delay
EEx  Pattern delay

Don't implement all of these simultaneously. Add one, add tests, then continue.

Phase 8 — use Micromod as your oracle

This is where Micromod is especially valuable. It describes itself as a ProTracker MOD player and includes an ANSI C implementation.

Keep a native Linux build alongside the X16 implementation:

tests/reference/
    micromod

tests/x16/
    your replay engine

For each test MOD:

                MOD
                 │
        ┌────────┴────────┐
        ▼                 ▼
    Micromod           X16 engine
        │                 │
        ▼                 ▼
    PCM-reference.raw   PCM-x16.raw
        │                 │
        └────── compare ──┘

Compare things such as:

row/tick progression
sample triggers
period
volume
loop positions
eventually PCM output

This is far easier than debugging by ear.

Create tiny synthetic MODs specifically for tests:

arpeggio.mod
vibrato.mod
sample-loop.mod
pattern-break.mod
note-delay.mod
tempo.mod
Phase 9 — improve the mixer only after profiling

Once real MODs work, profile.

Possible optimizations include pre-scaled sample tables:

volume 0:
  sample[-128..127]

volume 1:
  sample[-128..127]

...

volume 64:
  sample[-128..127]

But:

65 × 256 = 16,640 entries

and using 16-bit mixed values roughly doubles the storage.

You may find a smaller lookup arrangement is better.

Other useful tricks:

special mixer for 1 active channel
special mixer for 2 active channels
special mixer for 3 active channels
4-channel full mixer

Rather than checking every channel for every output sample:

lda channel_active
beq ...

choose an appropriate mixer routine whenever voice state changes.

That's a very 6502-friendly optimization.

Phase 10 — library API

Once it works, separate the replay engine from the example application.

Something like:

mod_init();
mod_load(...);

mod_play();
mod_pause();
mod_stop();

mod_get_position();
mod_set_position();

mod_set_master_volume();

mod_update();

and possibly:

mod_title();
mod_sample_name(n);
mod_pattern();
mod_row();

That allows games and demos to use it rather than restricting it to a standalone music player.

I would make the repository primarily a library, perhaps:

x16-mod/

with:

lib/
    x16mod.lib

examples/
    modplay/
    minimal/
Phase 11 — player UI

Only after the library works.

A nice X16 application could show:

──────────────────────────────────────
       Commander X16 MOD Player

        "Space Debris"
        Purple Motion

Pattern  08/21
Row      31/64

CH1 ██████████████
CH2 ███████
CH3 ███████████
CH4 ████

00 C-3 01 40 000
01 --- -- -- ---
02 G-3 02 32 A02
03 E-3 03 48 000

          02:13 / 04:51
──────────────────────────────────────

Eventually you could add a tracker-style pattern display, scopes and sample names.

But none of that should exist until playback is solid.

One thing I would change from my earlier suggestion

I wouldn't port Micromod's C engine first.

I'd use this sequence instead:

1. VERA PCM
       ↓
2. 1-channel 65C02 sampler
       ↓
3. 4-channel 65C02 mixer
       ↓
4. memory/bank/cache solution
       ↓
5. minimal MOD parser
       ↓
6. minimal tracker sequencer
       ↓
7. effects
       ↓
8. compatibility
       ↓
9. UI

That attacks the one question that can potentially kill the project—can the 8 MHz 65C02 mix four sample voices at acceptable quality while dealing with X16 memory? Everything after that is normal software engineering.

I would also use cc65 + ca65: C for loading, parsing, UI and most tracker state; assembly for PCM/mixing and probably the sample-cache hot paths. cc65 directly supports the Commander X16 and ca65 gives you the appropriate assembly/linking path.

If Phase 2 demonstrates 4-channel stereo at ≥16 kHz on stock 8 MHz hardware, I'd consider the project technically validated and proceed with the full MOD implementation.