# Verified development environment

Everything in this file was **executed and confirmed** on this machine on
2026-09-06, not read from documentation. Where a doc and an experiment
disagreed, the experiment wins and the discrepancy is noted.

## Toolchain

| Thing | Value |
|---|---|
| cc65 | `cl65 V2.19 - Git 547d923`, binaries in `~/.local/bin` |
| cc65 data | `~/.local/share/cc65` (cfg, lib, asminc, include) |
| Emulator | `~/Projects/x16-emulator/build/x16emu` — r50 (next), `9e9a2635` |
| ROM | `~/Projects/x16-rom/build/x16/rom.bin` (256 KB) |
| Docs | `~/Projects/x16-docs` — PRG chapters 01-15 + appendices |
| Host oracles | `gcc`, `python3`, `openmpt123`, `ffmpeg`; network reachable |
| Absent | `micromod` (must be fetched), `xmp`, `sox`, any `.mod` files |

### `CC65_HOME` is mandatory

`cl65` on this machine does **not** find its own config files. Without it:

```
ld65: Error: Cannot find config file 'cx16.cfg'
```

Every build must export:

```sh
export CC65_HOME=$HOME/.local/share/cc65
```

### Use `cx16-asm.cfg`, not `cx16.cfg`, for pure-assembly targets

`cx16.cfg` expects the C runtime's segments and fails on an assembly-only
program with:

```
cx16.cfg:15: Error: Start address of memory area 'BSS' is not constant
```

`-C cx16-asm.cfg` links cleanly. Verified working invocation:

```sh
cl65 -t cx16 -C cx16-asm.cfg --cpu 65c02 -o OUT.PRG -Ln out.lbl src.s
```

`cx16-asm.cfg` also pins down the zero-page budget, which matters a great
deal for the mixer:

| Region | Range | Size |
|---|---|---|
| `ZEROPAGE` segment | `$0022-$007F` | 94 bytes |
| `EXTZP` segment (free if BASIC/math unused) | `$00A9-$00FF` | 87 bytes |

So roughly **180 bytes of zero page** are available to the player.

## Emulator facts that shape the test harness

### There is a guest-readable cycle counter — this is the key instrument

`src/memory.c` maps `$9FB0-$9FBF` to `emu_write`/`emu_read`, and reads and
writes at the same address do *different* things:

| Address | Write | Read |
|---|---|---|
| `$9FB8` | latch `clock_base = clockticks6502` | snapshot `clockticks6502 - clock_base`, return bits 0-7 |
| `$9FB9` | print `User debug 1: $xx` | snapshot bits 8-15 |
| `$9FBA` | print `User debug 2: $xx` | snapshot bits 16-23 |
| `$9FBB` | print byte as a character (poor-man's stdout) | snapshot bits 24-31 |

Reading `$9FB8` takes the snapshot; `$9FB9-$9FBB` then read the remaining
bytes of *that* snapshot, so the 32-bit value is coherent.

**Verified.** This sequence poked at `$6000` and run:

```asm
sta $9FB8      ; latch origin
lda $9FB8      ; snapshot + low byte
sta $6100
rts
```

returned `7` — the exact cycle distance between the `sta` write cycle and the
`lda` read cycle. This gives cycle-exact profiling from inside the guest, which
is far better than inferring headroom from underruns.

### `-prg` does NOT load in `-testbench` mode

This cost real debugging time and must not be rediscovered later.

`-prg` is implemented (`main.c:1847`) by **pasting a BASIC `LOAD"..."` command**
into the keyboard buffer. It needs the guest CPU to boot and run BASIC.
Testbench mode holds the CPU stopped until the first `RUN`, so the program is
never loaded.

The trap is that `RQM` still returns plausible-looking bytes — they are just
uninitialised BASIC memory, not your program. A `-prg` + `RUN` test hangs
forever because it executes garbage.

**Load test code by poking it with `STM` instead.** Measured throughput is
**~105,000 bytes/sec** (4 KB in 0.04 s), so an 8 KB blob loads in under a
tenth of a second. This is fast enough that it is a non-issue.

### Testbench protocol details

- Commands are whitespace-separated with a **mandatory space**: `RQM 0801`,
  not `RQM0801`. The C side rejects short lines (`len < 9`) with
  `ERR Invalid command`.
- `RUN <addr>` pushes a return address of `$FFFC` and runs until `PC == $FFFD`,
  i.e. until your routine's final `RTS`.
- `regs.sp` is a **16-bit** register that already includes the `$01xx` page
  (the emulator also supports the 65C816). `SSP` writes only its low byte.
  Do not "helpfully" set the stack pointer; the post-reset value is correct.
- `importViceLabels()` consumes the `-Ln` label file, so tests can address
  symbols by name rather than hardcoded addresses.

Reference harness: `~/Projects/x16-emulator/testbench/testbench.py`
(`selftest.py` and `example.py` alongside it show the intended usage).

### WAV capture works, but a killed emulator leaves a broken header

`-wav out.wav,auto` records the mixed audio output at 48 kHz, 16-bit stereo.
`wav_recorder_shutdown()` is only called on a *clean* exit and only when not
headless (`main.c:1322`), and the emulator has no guest-triggered quit — so a
`timeout`-killed run leaves the RIFF/data sizes as zero.

The PCM payload is intact from byte 44 onward. Verified: a 25 s warp-mode run
of a square-wave test produced 22,226,568 bytes = 11,113,262 stereo samples at
full scale (peak 32767). A small `tools/wavfix.py` that rewrites the header
from the actual file size is all that is needed.

Warp mode generated ~231 s of audio in 25 s wall-clock (~9x real time), so
long listening renders are cheap.

## Hardware numbers confirmed from the reference

### VERA PCM (`X16 Reference - 09`)

| Register | Address | Contents |
|---|---|---|
| `AUDIO_CTRL` | `$9F3B` | b7 FIFO-full (r) / FIFO-reset (w), b6 FIFO-empty (r), b5 16-bit, b4 stereo, b0-3 volume |
| `AUDIO_RATE` | `$9F3C` | 0 = stopped, 128 = 48828.125 Hz, `>128` invalid |
| `AUDIO_DATA` | `$9F3D` | write-only, one byte into the FIFO |

- FIFO is **4 KB**. `AFLOW` fires when it is less than 1/4 full.
- **`AFLOW` can only be cleared by refilling the FIFO to at least 1/4.** An
  interrupt handler that writes fewer than 1024 bytes will re-fire immediately.
  This sets a floor on the mixer's block size.
- Samples are two's-complement signed. 8-bit stereo order is `<left><right>`.
- Setup order matters: set `AUDIO_RATE = 0`, fill the FIFO, *then* set the rate.

`AUDIO_RATE = R` gives `R x 381.4697 Hz`:

| R | Output rate | Stereo frames/s | 65C02 cycles per frame @ 8 MHz |
|---:|---:|---:|---:|
| 21 | 8,010.9 | 8,011 | 999 |
| 32 | 12,207.0 | 12,207 | 655 |
| 42 | 16,021.7 | 16,022 | 499 |
| 48 | 18,310.5 | 18,311 | 437 |
| 56 | 21,362.3 | 21,362 | 374 |
| 64 | 24,414.1 | 24,414 | 328 |
| 84 | 32,043.5 | 32,044 | 250 |

### Memory (`X16 Reference - 08`)

- `$0000` = RAM bank register, `$0001` = ROM bank register. A bank switch is a
  single `sta $00` — **3 cycles**, cheaper than the brief assumed.
- Banked RAM window `$A000-$BFFF` (8 KB), banks 1-255 free (2,040 KB).
- Fixed RAM `$0800-$9EFF` (~38.4 KB) for code, tables and caches.
- Bank 0 is KERNAL/DOS; `$BF00-$BFFF` in bank 0 is a parameter-passing area.

### The PCM FIFO does not drain in `-testbench` mode

`audio_init()` is called at `main.c:1290`, inside `if (!headless)`. Testbench
mode sets `headless = true`, so `audio_dev` stays 0 — and `audio_render()`
(`audio.c:255`) returns immediately when `audio_dev == 0`. `pcm_render()` is
therefore never called and the FIFO fills up and stays full forever. The same
happens with `-sound none`.

Two consequences that shape every test:

1. **Never poll `FIFO-full` in a testbench test — it deadlocks.** Unconditional
   writes to `$9F3D` are safe (a write to a full FIFO is discarded, per the
   reference) and still cost their real cycles, so cycle measurement of the
   write loop is valid. It is only the *wait* that hangs.
2. **Underrun testing cannot happen in testbench mode.** It needs a real audio
   device.

So Phase 1 splits its measurements across two rigs:

| Rig | Used for |
|---|---|
| `-testbench` (headless, no audio) | Cycle-exact cost of fill loops, IRQ overhead, mixer inner loops. Render to a RAM buffer, compare bit-exactly. |
| Windowed + `-warp` + real audio device | Sustained playback, underrun counting, WAV capture. |

Warp mode is safe for the second: the FIFO drain rate is driven by *emulated
CPU clocks* (`audio.c:236-245` advances `vera_samp_pos_hd` by `max_cpu_clks`),
not by wall-clock time. The cycles-per-FIFO-byte relationship is therefore
identical in warp and real time — warp just gets there ~9x sooner.

**Empirically confirmed:** the square-wave test in this document polls
`FIFO-full` and ran correctly under `-warp` with a real audio device, producing
231 s of continuous full-scale audio. The same test with `-sound none` produced
nothing at all.
