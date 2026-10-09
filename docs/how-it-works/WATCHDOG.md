# The watchdog, and why exceptions were slow (patch 0010)

Two write-ups from 2026-10-07, joined: what the watchdog measures and why it fired, then the cause (one wineserver round trip per handled exception) and the fix, FEX patch 0010.

- [Part 1](#part-1-the-later-stop-a-lateness-bucket-on-the-protections-own-loop-2026-10-07): The later stop: a lateness bucket on the protection's own loop (2026-10-07)
- [Part 2](#part-2-fast-continue-the-watchdogs-real-cost-was-a-wineserver-round-trip-per-exception-2026-10-07): Fast continue: the watchdog's real cost was a wineserver round trip per exception (2026-10-07)

## Part 1: The later stop: a lateness bucket on the protection's own loop (2026-10-07)

**Result:** with FEX patch 0007 the game runs past the old kill window, then stops 8 to 10 minutes in. That stop is a
watchdog. A protection thread runs a loop and measures how long each cycle takes. Every millisecond a cycle takes
beyond 2000 ms goes into a bucket; when the bucket holds more than 256 s, the protection takes its failure path and
creates the kill jobs. On the Thor a cycle takes 2.2 to 5.8 s (mean 4.3 s in a 515 s run), so the bucket overflows
after about 8.5 minutes. About 40 % of that thread's time goes to FEX recompiling the protection's
decrypt-on-demand code and to exception handling.

### Method: an instruction watch in FEX (job-local experiment)

On top of the block dump of [0008](../../patches/fex/0008-block-dump-experiment.patch): before each watched guest
instruction, FEX emits its `Print` IR op for a marker and the 16 guest registers (optionally also 8 stack slots), and
the frontend routes `PrintValue` into the dump buffer. Only `cmp`/`test` instructions and plain stores were watched,
and FEX spills NZCV around the call, so the guest flags are kept. A second experiment gives every game thread a FEX
`ThreadStats` slot in an in-memory table, so FEX's own JIT-time, signal-time and SMC counters run without the
shared-memory unix helper. These two experiments are not yet in `patches/`.

### The loop and the bucket

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

### What happens on overflow

The failing check (`0x143e40324` / `0x143e4033f`) draws a value from a second range and tests it against a first
range. The protection keeps both ranges and the value encoded at `0x147af3c08..0x147af3c30`, and every writer builds
the second range above the first (`lo2 = hi1 + 1 + random`), so this check fails whenever it runs. It ran only at the
overflow (watched in two runs: 511.29 s, and 515.85 s). The failure path calls the reaction function `0x143dd2550`
from `0x143e41cfa` with `r8 = 0x021c0001`, which creates the kill jobs (`+0x3e69428`, settings `rbx = 1000`,
`r12 = 1`).

### Why the cycles are slow on the Thor

FEX per-thread counters, 42 s window during loading (about 1 to 1.7 minutes into a run):

| thread | JIT time | exception (signal) time | JIT compiles | SMC events |
|---|---|---|---|---|
| `016c` (the loop) | 0.157 s/s | 0.239 s/s | 2,267/s | 2,085/s |
| `013c` (main) | 0.155 s/s | 0.114 s/s | 1,960/s | 1,114/s |

So the loop thread spends about 40 % of its time compiling code and handling exceptions. The protection keeps its
functions encrypted and decrypts one when it is called (the `c000001d` traps of [KILL-TIMER.md](../research/archive/KILL-TIMER.md)), so
under FEX each call means a self-modifying-code event, a recompile, and an exception.

`tools/probes/exccost.c` measured a handled illegal-instruction exception (vectored handler, `ud2`) at 111 us
under FEX on the Thor, while the game was running; an empty call costs 0.003 us and `GetTickCount64` 0.013 us.

Not changed by: full TSO (`FEX_VECTORTSOENABLED=1 FEX_MEMCPYSETTSOENABLED=1 FEX_HALFBARRIERTSOENABLED=0`, the
kill thread again spun from about 555 s), and limiting the game to cores 3 to 7 (cycles 2.4 to 2.9 s, as before).
The protection code has no x87 instructions (none in 1.2 million decoded), so `FEX_X87REDUCEDPRECISION` is not
involved.

### Making the loop cheaper: what was measured

Cycle time from the watched compare at `0x143e3f3f2`, cycles between 25 s and 115 s after the first dumped block,
`WINEDEBUG=-all`, patch 0007 in every case:

| setting | cycles | mean cycle | bucket growth |
|---|---|---|---|
| defaults | 26 | 3,186 ms | 20.6 s per minute |
| `FEX_MULTIBLOCK=0` | 31 | 2,623 ms | 12.9 s per minute |
| `FEX_DISKCACHE=1` | 27 | 3,093 ms | 19.7 s per minute |
| `FEX_EXP_SKIP_CALLRET_RESET=1` (patch [0009](../../patches/fex/0009-skip-callret-reset-experiment.patch)) | 31 | 2,573 ms | 12.0 s per minute |

- **`FEX_MULTIBLOCK=0`**: a full run still overflowed (at 547 s), the game's own loading got slower, and in 2 of 3
  runs the game died at `Loading step: [Config File]` about 1 s after start, the step where the Box64 route dies.
- **`FEX_DISKCACHE=1`**: the loop thread's compiles fell from about 2,267/s to 16/s (1,359 cache hits/s), but its
  JIT time stayed at about 0.22 s/s, so a cache hit cost about as much as a compile.
- **Where an SMC fault's time goes** (thread `015c`, 1,442 faults/s, defaults): 0.266 s/s in FEX's handler, of which
  0.179 s/s in the invalidation loop (code buffers, then every one of about 90 threads; about 124 us per fault),
  0.051 s/s waiting for `ThreadCreationMutex` and `CodeInvalidationMutex`, and 0.017 s/s in the re-protect call. In
  that loop, each thread that had code cached in the page gets its call-ret stack discarded, which is a syscall.
  With 0009 the loop took 0.072 s/s (about 49 us per fault).

### Patch 0009 reached the game's first menu

Full run at 07:24 with 0007 and 0009 (tested build: the working tree with the stats and watch experiments; it read
the environment variable on every invalidation, the patch reads it once): `Cheat Menu` at about 407 s,
`WPFGFrontEnd loading` at 465 s, `OnEndLoad` at 509 s, and the game drew its first-run **Accessibility Settings**
screen ([screenshot](../img/first-run-menu-2026-10-07.jpg), about 12.5 minutes in). Taps and Enter sent through `adb`
did not reach it.

The watchdog still fired. Replaying the measured cycles, the bucket held 120 s at 360 s, 206 s at 600 s, and reached
256 s at 793 s; in the menu the cycles still averaged 2,800 ms (89 cycles after 520 s). Right after, two threads with
the kill entry `+0x3e69304` existed, and the main thread's CPU time stopped increasing (212,010 ms in two samples 40 s
apart).

A second run with the same patches (game process at 08:59:26) got further: past the Accessibility screen into the
tutorial's opening scene and its first task, game clock 00:40 at 09:09
([screenshot](../img/first-gameplay-2026-10-07.jpg)). Replaying the bucket over that run's dumped cycles put the overflow
at 824 s (09:13:15); at 09:13:52 the game process used 1 % CPU (5 ticks in 5 s), and the screen no longer changed.

0009 is not safe in general: a return can land in the old translation of code that changed while its caller was
suspended. The protection's decrypt-on-demand pattern did not visibly break in this run.

### What followed

In the menu the loop needed to get from about 2.8 s to under 2 s per cycle. Most of the missing time was not
compiling: the loop thread was blocked most of each cycle, waiting for wineserver once per handled exception. FEX
patch 0010 removes that wait; the cycle then averaged about 1.1 s and the bucket stayed at 0 in a run that was
still being played 15 minutes in. See [part 2](#part-2-fast-continue-the-watchdogs-real-cost-was-a-wineserver-round-trip-per-exception-2026-10-07).

## Part 2: Fast continue: the watchdog's real cost was a wineserver round trip per exception (2026-10-07)

**Result:** every exception the game handles and resumes made one blocking wineserver request, and the protection
loop of [part 1](#part-1-the-later-stop-a-lateness-bucket-on-the-protections-own-loop-2026-10-07) handles thousands of them per cycle. FEX patch
[0010](../../patches/fex/0010-fast-continue-to-x64.patch) resumes x64 code without that request. With it the loop's
cycle fell from about 2.6 to 3.2 s to about 1.15 s, the watchdog bucket stayed at 0, and the main menu was on screen
2 min 43 s after the process appeared (the first time the screen was checked).

### How it was found

1. **The loop thread mostly waits.** Pinned run, 2026-10-07 09:02, thread `016c` (Linux tid 18275), from
   `/proc/<pid>/task/<tid>/schedstat` over 10 s: 0.27 s/s on a CPU, 0.14 s/s waiting for a CPU, the rest blocked.
   `/proc/.../sched`: 1,270,109 voluntary switches in its life, 890,937 of them "sync" wakeups (the pattern of a
   pipe or socket reply), average run between switches 84 us. In a later run (09:19) the same thread had
   3,303 voluntary switches/s, 2,498 of them sync.
2. **`exccost` blocks once per exception.** `tools/probes/exccost.c` (100,000 handled `ud2` exceptions, while the
   game ran): 7,418 voluntary switches/s, all sync, at about 7,700 exceptions/s, and the thread ran 0.40 s/s.
3. **The request is `get_thread_info` on the thread itself.** `exccost` run with `WINEDEBUG=+server` (20 counted
   exceptions plus 50 warm-up ones, logcat streamed to a file): its thread made exactly 70 `get_thread_info`
   requests, each `handle=fffffffe` (the current-thread pseudo-handle), `access=00000000`. The game's threads
   showed the same request in long runs (`016c` 7,639 trace lines in one capture, all `get_thread_info`).
4. **A FEX hook on `NtQueryInformationThread` saw none of them** (job-local experiment through the patch 0004
   syscall hook: 0 self-queries/s of any class while the loop thread blocked 3,303 times/s). So the query comes from
   inside Wine's unix side.
5. **Wine source, GameNative/proton-wine `7c98acd6` (the device's build, see [WINE-SOURCE.md](../guides/WINE-SOURCE.md)):**
   - `dlls/ntdll/unix/signal_arm64.c`, `signal_set_full_context()` (the unix side of `NtContinue`): when the target
     `pc` is not ARM64EC code, it calls `NtGetContextThread(GetCurrentThread(), user_context)`, puts that context
     below the target stack and returns into `KiUserEmulationDispatcher`.
   - `NtGetContextThread()` in the same file starts with
     `NtQueryInformationThread(handle, ThreadBasicInformation, ...)` to decide whether `handle` is the current
     thread, for every handle, including `GetCurrentThread()`. In `dlls/ntdll/unix/thread.c` that class is a
     `get_thread_info` server request.

   So every `NtContinue` back to x64 code, which is how every handled exception ends, waits for wineserver once.

**Where that query comes from (source read 2026-10-07).** Upstream Wine (wine-mirror `master`) and Valve's
`proton_11.0` decide "self" with `self = (handle == GetCurrentThread())` and make no request for the current
thread. The query was added in GameNative/proton-wine by `189b5e87` "WIP: ntdll: ARM64EC suspend support"
(2026-04-09) and reverted there by `a301e77d` (2026-07-17). The device's build `7c98acd6` (release of 2026-05-02)
still has it; GameNative's Proton 11.0-2 (`555aa70f`, 2026-09-28) has the plain compare again. So with 11.0-2 the
round trip should be gone without patch 0010.

**Tested 2026-10-07 12:27 to 12:40 (11.0-2 imported in the Wine/Proton Manager, selected in the existing AoE IV
container, which GameNative then set up again as a first boot):** `exccost` (20,000 and 100,000 handled
exceptions) measured 89.6 and 87.2 us each with `FEX_EXP_FASTCONTINUE=0`, and 1.5 us with 0010 on. So 11.0-2 is
faster than 11.0-1 on this path (111 to 130 us), but its own resume path is still about 60 times slower than 0010's.
The game ran on 11.0-2 with patch 0007 and 0010 (fast path 16,015 continues/s); the match HUD read 31.5, 18.8 and
27.1 FPS (on 11.0-1 the same kind of match read 22.8, 29.5 and 29.1), and the user saw 25 to 30 FPS with some
stutters. No gain over 11.0-1.

**Side effect to know:** `WINEDEBUG=+server` in one process turned on the request trace for the whole
wineserver. After the first `exccost` run with it, the trace kept logging the game's requests too, until the
container was restarted.

### The patch

[`0010-fast-continue-to-x64.patch`](../../patches/fex/0010-fast-continue-to-x64.patch), on top of 0004 (it uses
0004's `__wine_syscall_dispatcher` hook), off unless `FEX_EXP_FASTCONTINUE=1`. For an `NtContinue` with
`alertable == FALSE`, a full context (control, integer and floating point) and a target that `RtlIsEcCode` does
not report as ARM64EC code, the hook does what Wine does after the round trip: it copies the context near the top
of the emulator stack and jumps to ntdll's `KiUserEmulationDispatcher` there. `KiUserEmulationDispatcher` converts
the context into the CPU area, and FEX's `BeginSimulation` then resets `sp` to the emulator stack base, so the
copy is used before anything can overwrite it. Other continues go to Wine unchanged. Counters
`FastContinueStats` (marker `FSTCONT1`) count both paths.

What it skips besides the round trip: the unix-side check for a pending suspend in `signal_set_full_context`. A
thread that was asked to suspend then stops at its next syscall instead.

### Measurements

| what | without | with `FEX_EXP_FASTCONTINUE=1` |
|---|---|---|
| `exccost`, 20,000 handled exceptions (09:25, same session) | 230.3 us each | **2.5 us each** (handled count correct: 20,050) |
| loop cycle, game run (cycles to 156 s) | 2.6 to 3.2 s in earlier runs | mean about 1.15 s (0.57 to 2.0 s) |
| watchdog bucket at 258 s | about 60 s (earlier runs) | **0 s** |
| game process appeared to a menu on screen | first menu after about 8.5 min (patch 0009 run) | main menu at **2 min 43 s** or earlier (first check) |

The 230.3 us was measured while the wineserver request trace was on (see above); without the trace the same probe
measured 111 to 130 us earlier the same day ([part 1](#part-1-the-later-stop-a-lateness-bucket-on-the-protections-own-loop-2026-10-07)).

In the game: 6,614 continues/s took the fast path and 0 the slow one (10 s window at about 75 s). Build: the job
tree with 0002, 0004, 0006, 0007, 0009, the block dump and stats experiments, and this change (`08172f64`), with
`FEX_EXP_SKIP_CALLRET_RESET=1` (0009) also set.

### The two validation runs

| run | build | result |
|---|---|---|
| 09:27 | job tree with 0010 and analysis code (`08172f64`) | watched 1,514 s; 10-cycle means of the loop 0.6 to 2.0 s (about 1.1 s typical), bucket 0 at every check up to 1,469 s; log last grew at 1,456 s; a Skirmish played 15 minutes in |
| 10:00 | exact repo set 0002+0004+0006+0007+0009+0010 (`86d6da39`) | fast path 8,200/s, slow path 0/s (14.6 s window at about 100 s); log still growing at 989 s; a match played with the game's controller UI 15 minutes in |

The 10:00 build has no block dump, so its loop cycles were not measured; the evidence is that it ran and grew its
log past every earlier stopping point (about 8.5 to 13.7 minutes). Both runs had `FEX_EXP_SKIP_CALLRET_RESET=1`
(0009) set as well.

### Also measured on the way (no effect)

- **Pinning the loop thread to the fast core.** GameNative caps the CPU clocks during a game (logcat
  `PowerControl: Baseline captured`; measured `scaling_max_freq` 2,054,400 kHz on cores 3 to 6 of 2,803,200, and
  1,977,600 kHz on core 7 of 3,187,200). `tools/probes/affin.c` set the loop thread to core 7 and the other game
  threads to cores 0 to 6: cycles between 82 and 112 s averaged about 2.6 s, against about 2.9 s in an earlier
  unpinned run, and later rose to 3.0 to 3.5 s. The thread was not limited by CPU speed.
- **Answering self-queries of `NtQueryInformationThread` in FEX.** No such calls reached the syscall hook (see
  point 4 above).
