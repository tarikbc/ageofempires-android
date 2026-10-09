# Exceptions without a host trap (patch 0017, v1.4.0, 2026-10-09)

**Result:** about **+1.5 FPS** in the skirmish benchmark (58.3 / 57.8 against 56.4 / 56.3 and 56.6 / 56.5 for the
previous package before and after) and about a fifth fewer frames of 25 ms. Frames over 40 ms stayed within their
spread. Shipped in `aoe4-perf6-30` (DLL `7e707379`, release v1.4.0).

## The problem

The protection's loop raises exceptions on purpose and handles them itself (see [WATCHDOG.md](WATCHDOG.md) for how
patch 0010 made each one cheap enough for its watchdog). The rate changed with the build and the scene: 6,600 to 8,200 per
second in the first runs with patch 0010, 17,600 in the skirmish before patch 0012 and 37,900 after it
([INSTRUCTION-STEPPER.md](INSTRUCTION-STEPPER.md)), about 25,000 in the late game with v1.2.0, and about **44,000 per
second** in the skirmish with the v1.3.0 driver and the vkd3d-proton setting.

Instruction-pointer samples of the main thread in the skirmish (2 ms, 50 s, through `tools/agent.py sample`) put
13.7 % of it at the entry of one of the protection's vectored exception handlers (`exe+3cd9430`), and patch 0010's own
counter (`FSTCONT1`, read through the agent) showed about 44,000 resumed exceptions per second in the game process.
Counting them by FEX's guest signal (`FASTTRP1`, a counter in the experiment build): **all of them are illegal-instruction
faults (SIGILL)** raised by JIT code. Each one cost a `hlt` host trap, Wine's signal handler, a first
`KiUserExceptionDispatcher` pass whose only job was to rethrow it to the guest, and a unix `NtRaiseException`.

## The fix

[Patch 0017](../../patches/fex/0017-trapless-guest-faults.patch) changes two places:

- **No host trap.** FEXCore's guest-signal stubs (SIGILL, SIGTRAP, SIGSEGV in `Dispatcher.cpp`) load a per-thread
  pointer, `Pointers.GuestSignalTrapNative`. When the frontend has set it, the stub branches there instead of trapping
  (`hlt` for SIGILL, `brk` for SIGTRAP). The ARM64EC frontend's `FastTrapEntry` builds the guest exception the way `RethrowGuestException` does.
- **Fast raise.** The frontend then enters `KiUserExceptionDispatcher` directly, with the exception frame on the
  guest stack in the layout FEX already uses for emulated syscalls. The unix `NtRaiseException` stays as the fallback.

Both parts are on by default; `FEX_EXP_FASTTRAP=0` and `FEX_EXP_FASTRAISE=0` turn them off. A debugger does not see
these first-chance events any more.

## Measurements

Skirmish benchmark, the same session, in this order (frames at the 120 Hz display; 3 refreshes = 25 ms):

| Run | FPS (minutes 1 / 3) | frames of 3 refreshes | of 2 refreshes | over 40 ms |
|---|---|---|---|---|
| Release FEX (`aoe4-perf5-23`), 00:52 | 56.4 / 56.3 | 1,038 / 1,039 | 3,371 / 3,363 | 23 / 37 |
| 0017 without the fast raise (`FEX_EXP_FASTRAISE=0`) | 57.6 / 57.6 | 840 / 808 | 3,752 / 3,731 | 25 / 21 |
| **0017** | **58.3 / 57.8** | **813 / 825** | 3,695 / 3,634 | 21 / 40 |
| Release FEX again, 02:02 | 56.6 / 56.5 | 999 / 1,014 | 3,446 / 3,365 | 26 / 26 |

With 0017 every exception took both new paths (`FASTTRP1` 44,175/s, `FASTRAIS` 44,264/s, no fallback). The fast raise
alone, on the old trap path, gave no measurable change (56.0 / 55.8 against 56.9 / 56.8).

Release check of the package `aoe4-perf6-30` (the same DLL), 02:29 to 02:48: windows at match minutes 1, 5, 10 and 15
gave 57.8, 57.4, 57.4 and 54.7 FPS (the base grows over the match), no stop in 19 minutes, and the counters still read
43,582 trap-less exceptions and 43,396 fast raises per second.

**Why 56 and not 58.6 for the old package:** the benchmark's thermal sampler was changed at the start of this
session. It had run two `cat` processes per thermal zone (95 zones) every 3 s, which took up to 30 ms of a big core
inside measured frames; now it uses shell builtins. The same setup then read 56.4 to 56.6 FPS against 58.6 the day
before; the cause of that difference was not isolated. Numbers from before and after the change are not comparable;
inside this table they are.

## Not tested yet

- The late-game replay with 0017.
- AoE II DE with `aoe4-perf6-30` (it stays on `aoe4-perf5-23`).
- Reported upstream: not yet ([UPSTREAM.md](../research/UPSTREAM.md)).
