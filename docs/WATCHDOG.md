# The later stop: a lateness bucket on the protection's own loop (2026-10-07)

**Result:** with FEX patch 0007 the game runs past the old kill window, then stops 8 to 10 minutes in. That stop is a
watchdog. A protection thread runs a loop and measures how long each cycle takes. Every millisecond a cycle takes
beyond 2000 ms goes into a bucket; when the bucket holds more than 256 s, the protection takes its failure path and
creates the kill jobs. On the Thor a cycle takes 2.2 to 5.8 s (mean 4.3 s in a 515 s run), so the bucket overflows
after about 8.5 minutes. About 40 % of that thread's time goes to FEX recompiling the protection's
decrypt-on-demand code and to exception handling.

## Method: an instruction watch in FEX (job-local experiment)

On top of the block dump of [0008](../patches/fex/0008-block-dump-experiment.patch): before each watched guest
instruction, FEX emits its `Print` IR op for a marker and the 16 guest registers (optionally also 8 stack slots), and
the frontend routes `PrintValue` into the dump buffer. Only `cmp`/`test` instructions and plain stores were watched,
and FEX spills NZCV around the call, so the guest flags are kept. A second experiment gives every game thread a FEX
`ThreadStats` slot in an in-memory table, so FEX's own JIT-time, signal-time and SMC counters run without the
shared-memory unix helper. These two experiments are not yet in `patches/`.

## The loop and the bucket

Thread `016c` (start `RelicCardinal.exe+0x3e1b04c`; `0168` in some runs) runs a cycle every few seconds. In each
cycle, at `0x143e3f3f2`, it compares two `GetTickCount64` values (the protection reads the tick count inline from
`KUSER_SHARED_DATA`, function `0x143f765f0`): `r12` = now, `r13` = the previous cycle's time, both in ms. With
`E = r12 - r13`, the code at `0x143e3f614` computes

```
new = max(0, old + 8 * (E - 2000))          // lea rcx,[rdi+r12*8]; shl rcx,7; cmp rcx,0x1f4000; cmovae; shr rdi,7
```

and at `0x143e3f71e` compares `new` with `0x1f4000` (2,048,000): `jbe` continues normally, anything above enters
the failing check. The unit is 1/8 ms, so the limit is 256 s of total lateness. The bucket value is kept encoded in
memory.

Measured in one run (112 cycles to 515 s): `E` from 2,182 to 5,842 ms, mean 4,301 ms. Replaying the formula over
those 112 values crosses the limit at the cycle logged at 515.175 s; the failing check ran in that cycle.

## What happens on overflow

The failing check (`0x143e40324` / `0x143e4033f`) draws a value from a second range and tests it against a first
range. The protection keeps both ranges and the value encoded at `0x147af3c08..0x147af3c30`, and every writer builds
the second range above the first (`lo2 = hi1 + 1 + random`), so this check fails whenever it runs. It ran only at the
overflow (watched in two runs: 511.29 s, and 515.85 s). The failure path calls the reaction function `0x143dd2550`
from `0x143e41cfa` with `r8 = 0x021c0001`, which creates the kill jobs (`+0x3e69428`, settings `rbx = 1000`,
`r12 = 1`).

## Why the cycles are slow on the Thor

FEX per-thread counters, 42 s window during loading (about 1 to 1.7 minutes into a run):

| thread | JIT time | exception (signal) time | JIT compiles | SMC events |
|---|---|---|---|---|
| `016c` (the loop) | 0.157 s/s | 0.239 s/s | 2,267/s | 2,085/s |
| `013c` (main) | 0.155 s/s | 0.114 s/s | 1,960/s | 1,114/s |

So the loop thread spends about 40 % of its time compiling code and handling exceptions. The protection keeps its
functions encrypted and decrypts one when it is called (the `c000001d` traps of [KILL-TIMER.md](KILL-TIMER.md)), so
under FEX each call means a self-modifying-code event, a recompile, and an exception.

`tools/probes/exccost.c` measured a handled illegal-instruction exception (vectored handler, `ud2`) at 111 us
under FEX on the Thor, while the game was running; an empty call costs 0.003 us and `GetTickCount64` 0.013 us.

Not changed by: full TSO (`FEX_VECTORTSOENABLED=1 FEX_MEMCPYSETTSOENABLED=1 FEX_HALFBARRIERTSOENABLED=0`, the
kill thread again spun from about 555 s), and limiting the game to cores 3 to 7 (cycles 2.4 to 2.9 s, as before).
The protection code has no x87 instructions (none in 1.2 million decoded), so `FEX_X87REDUCEDPRECISION` is not
involved.

## Making the loop cheaper: what was measured

Cycle time from the watched compare at `0x143e3f3f2`, cycles between 25 s and 115 s after the first dumped block,
`WINEDEBUG=-all`, patch 0007 in every case:

| setting | cycles | mean cycle | bucket growth |
|---|---|---|---|
| defaults | 26 | 3,186 ms | 20.6 s per minute |
| `FEX_MULTIBLOCK=0` | 31 | 2,623 ms | 12.9 s per minute |
| `FEX_DISKCACHE=1` | 27 | 3,093 ms | 19.7 s per minute |
| `FEX_EXP_SKIP_CALLRET_RESET=1` (patch [0009](../patches/fex/0009-skip-callret-reset-experiment.patch)) | 31 | 2,573 ms | 12.0 s per minute |

- **`FEX_MULTIBLOCK=0`**: a full run still overflowed (at 547 s), the game's own loading got slower, and in 2 of 3
  runs the game died at `Loading step: [Config File]` about 1 s after start, the step where the Box64 route dies.
- **`FEX_DISKCACHE=1`**: the loop thread's compiles fell from about 2,267/s to 16/s (1,359 cache hits/s), but its
  JIT time stayed at about 0.22 s/s, so a cache hit cost about as much as a compile.
- **Where an SMC fault's time goes** (thread `015c`, 1,442 faults/s, defaults): 0.266 s/s in FEX's handler, of which
  0.179 s/s in the invalidation loop (code buffers, then every one of about 90 threads; about 124 us per fault),
  0.051 s/s waiting for `ThreadCreationMutex` and `CodeInvalidationMutex`, and 0.017 s/s in the re-protect call. In
  that loop, each thread that had code cached in the page gets its call-ret stack discarded, which is a syscall.
  With 0009 the loop took 0.072 s/s (about 49 us per fault).

## Patch 0009 reached the game's first menu

Full run at 07:24 with 0007 and 0009 (tested build: the working tree with the stats and watch experiments; it read
the environment variable on every invalidation, the patch reads it once): `Cheat Menu` at about 407 s,
`WPFGFrontEnd loading` at 465 s, `OnEndLoad` at 509 s, and the game drew its first-run **Accessibility Settings**
screen ([screenshot](img/first-run-menu-2026-10-07.jpg), about 12.5 minutes in). Taps and Enter sent through `adb`
did not reach it.

The watchdog still fired. Replaying the measured cycles, the bucket held 120 s at 360 s, 206 s at 600 s, and reached
256 s at 793 s; in the menu the cycles still averaged 2,800 ms (89 cycles after 520 s). Right after, two threads with
the kill entry `+0x3e69304` existed, and the main thread's CPU time stopped increasing (212,010 ms in two samples 40 s
apart).

A second run with the same patches (game process at 08:59:26) got further: past the Accessibility screen into the
tutorial's opening scene and its first task, game clock 00:40 at 09:09
([screenshot](img/first-gameplay-2026-10-07.jpg)). Replaying the bucket over that run's dumped cycles put the overflow
at 824 s (09:13:15); at 09:13:52 the game process used 1 % CPU (5 ticks in 5 s), and the screen no longer changed.

0009 is not safe in general: a return can land in the old translation of code that changed while its caller was
suspended. The protection's decrypt-on-demand pattern did not visibly break in this run.

## What followed

In the menu the loop needed to get from about 2.8 s to under 2 s per cycle. Most of the missing time was not
compiling: the loop thread was blocked most of each cycle, waiting for wineserver once per handled exception. FEX
patch 0010 removes that wait; the cycle then averaged about 1.1 s and the bucket stayed at 0 in a run that was
still being played 15 minutes in. See [FAST-CONTINUE.md](FAST-CONTINUE.md).
