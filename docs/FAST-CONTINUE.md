# Fast continue: the watchdog's real cost was a wineserver round trip per exception (2026-10-07)

**Result:** every exception the game handles and resumes made one blocking wineserver request, and the protection
loop of [WATCHDOG.md](WATCHDOG.md) handles thousands of them per cycle. FEX patch
[0010](../patches/fex/0010-fast-continue-to-x64.patch) resumes x64 code without that request. With it the loop's
cycle fell from about 2.6 to 3.2 s to about 1.15 s, the watchdog bucket stayed at 0, and the main menu was on screen
2 min 43 s after the process appeared (the first time the screen was checked).

## How it was found

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
5. **Wine source, GameNative/proton-wine `7c98acd6` (the device's build, see [WINE-SOURCE.md](WINE-SOURCE.md)):**
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
round trip should be gone without patch 0010. **Not tested:** 11.0-2 has not run on the Thor yet
([WINE-SOURCE.md](WINE-SOURCE.md): its profile asks for a fresh ARM64EC container).

**Side effect to know:** `WINEDEBUG=+server` in one process turned on the request trace for the whole
wineserver. After the first `exccost` run with it, the trace kept logging the game's requests too, until the
container was restarted.

## The patch

[`0010-fast-continue-to-x64.patch`](../patches/fex/0010-fast-continue-to-x64.patch), on top of 0004 (it uses
0004's `__wine_syscall_dispatcher` hook), off unless `FEX_EXP_FASTCONTINUE=1`. For an `NtContinue` with
`alertable == FALSE`, a full context (control, integer and floating point) and a target that `RtlIsEcCode` does
not report as ARM64EC code, the hook does what Wine does after the round trip: it copies the context near the top
of the emulator stack and jumps to ntdll's `KiUserEmulationDispatcher` there. `KiUserEmulationDispatcher` converts
the context into the CPU area, and FEX's `BeginSimulation` then resets `sp` to the emulator stack base, so the
copy is used before anything can overwrite it. Other continues go to Wine unchanged. Counters
`FastContinueStats` (marker `FSTCONT1`) count both paths.

What it skips besides the round trip: the unix-side check for a pending suspend in `signal_set_full_context`. A
thread that was asked to suspend then stops at its next syscall instead.

## Measurements

| what | without | with `FEX_EXP_FASTCONTINUE=1` |
|---|---|---|
| `exccost`, 20,000 handled exceptions (09:25, same session) | 230.3 us each | **2.5 us each** (handled count correct: 20,050) |
| loop cycle, game run (cycles to 156 s) | 2.6 to 3.2 s in earlier runs | mean about 1.15 s (0.57 to 2.0 s) |
| watchdog bucket at 258 s | about 60 s (earlier runs) | **0 s** |
| game process appeared to a menu on screen | first menu after about 8.5 min (patch 0009 run) | main menu at **2 min 43 s** or earlier (first check) |

The 230.3 us was measured while the wineserver request trace was on (see above); without the trace the same probe
measured 111 to 130 us earlier the same day ([WATCHDOG.md](WATCHDOG.md)).

In the game: 6,614 continues/s took the fast path and 0 the slow one (10 s window at about 75 s). Build: the job
tree with 0002, 0004, 0006, 0007, 0009, the block dump and stats experiments, and this change (`08172f64`), with
`FEX_EXP_SKIP_CALLRET_RESET=1` (0009) also set.

## The two validation runs

| run | build | result |
|---|---|---|
| 09:27 | job tree with 0010 and analysis code (`08172f64`) | watched 1,514 s; 10-cycle means of the loop 0.6 to 2.0 s (about 1.1 s typical), bucket 0 at every check up to 1,469 s; log last grew at 1,456 s; a Skirmish played 15 minutes in |
| 10:00 | exact repo set 0002+0004+0006+0007+0009+0010 (`86d6da39`) | fast path 8,200/s, slow path 0/s (14.6 s window at about 100 s); log still growing at 989 s; a match played with the game's controller UI 15 minutes in |

The 10:00 build has no block dump, so its loop cycles were not measured; the evidence is that it ran and grew its
log past every earlier stopping point (about 8.5 to 13.7 minutes). Both runs had `FEX_EXP_SKIP_CALLRET_RESET=1`
(0009) set as well.

## Also measured on the way (no effect)

- **Pinning the loop thread to the fast core.** GameNative caps the CPU clocks during a game (logcat
  `PowerControl: Baseline captured`; measured `scaling_max_freq` 2,054,400 kHz on cores 3 to 6 of 2,803,200, and
  1,977,600 kHz on core 7 of 3,187,200). `tools/probes/affin.c` set the loop thread to core 7 and the other game
  threads to cores 0 to 6: cycles between 82 and 112 s averaged about 2.6 s, against about 2.9 s in an earlier
  unpinned run, and later rose to 3.0 to 3.5 s. The thread was not limited by CPU speed.
- **Answering self-queries of `NtQueryInformationThread` in FEX.** No such calls reached the syscall hook (see
  point 4 above).
