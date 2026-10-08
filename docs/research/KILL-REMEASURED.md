# The Aegis kill, measured on a clean baseline with the right instrument

2026-10-06. Three runs, both launched by `tools/run_watch.py --launch` (force-stop GameNative, start it,
open AoE IV, tap Play) with `WINEDEBUG=-all` ([WINEDEBUG-LEFTOVER.md](WINEDEBUG-LEFTOVER.md)).

`suspinfo` reads each thread's **Windows** suspend count (`NtQueryInformationThread(ThreadSuspendCount)`)
and suspends nothing. `/proc` thread states cannot see Wine's `SuspendThread`
([DEATH-IS-NOT-THE-KILL.md](DEATH-IS-NOT-THE-KILL.md)), so this is the kill detector.

## Run 1 (20:51): the kill fires

FEX: `C:\windows\system32\libarm64ecfex.dll`, SHA-1 `460568b8` (CPUID-patched, SMC trap present). `modlist`
on the game process confirms that path is the loaded emulator.

`t` counts from the game process appearing (20:51:44).

| t | wall | log bytes | suspend counts | kill thread (`start=RelicCardinal.exe+3e69304`) | loading step |
|---|---|---|---|---|---|
| 18 s | 20:52:02 | 14,779 | 17 at 0 | tid 0174, suspend 0, user 0 ms | `Property Bag Manager` |
| 112 s | 20:53:36 | 77,794 | 67 at 0 | tid 0174, suspend 0, user 0 ms | `IEngineInitializable` |
| 170 s | 20:54:34 | 89,477 | **60 at 1, 1 at 0** | tid 0174, **suspend 0, user 4,350 ms** | `GEWorld` |
| 228 s | 20:55:32 | 89,477 | 60 at 1, 1 at 0 | user 62,420 ms | no change |
| 286 s | 20:56:30 | 89,477 | 60 at 1, 1 at 0 | user 120,560 ms | no change |

After the kill the process uses one full core: the kill thread gains about 58 s of user time per 58 s.
The log never grows again.

From the game's own log:

```
20:53:47.021  TlsConnection::Shutdown: socket 0/1368 SSL shut down failed; ... errno=10038
20:53:47.181  WebSocketConnection::OnConnectionClose; statusCode=1006
20:54:20.360  Loading step: [GEWorld]
20:54:27.992  last line ever written
```

So the kill landed between 20:54:28 and 20:54:34: **41 to 47 s after the session drop**, about 2 min 50 s
after the process started. That matches the morning capture in [KILL-ANALYSIS.md](KILL-ANALYSIS.md): the
same thread entry, every other thread suspended, the kill 65 s after `errno=10038`.

Where the 60 suspended threads were parked: 56 at ntdll wait sites (`+c63f4` 33, `+c6448` 10, `+c6480` 8,
`+c5178` 4, `+c55d8` 1), 2 at addresses outside any module (one is the main thread), 1 in `win32u.dll`,
1 in `mmdevapi.dll`.

## Run 2 (20:58): the no-trap FEX build stops at start-up

`libarm64ecfex.dll` replaced by `libarm64ecfex.notrap.dll` (SHA-1 `b4dbf32d`; patches 0001 and 0003; with it
`smctest` reports `RWX`, see [FIX-VERIFIED.md](FIX-VERIFIED.md)). Swapped by rename inside a running
session, and the hash of the installed file was checked before the launch.

For more than three minutes after Play:

- 5 Linux threads, all `S`, 0 % CPU;
- `suspinfo`: **one** Windows thread, the main thread (`start=RelicCardinal.exe+4fb0884`), suspend 0,
  user 490 ms;
- no new `warnings.log` (its first line still named the 20:51 run).

The game never creates a second thread, so Aegis's kill thread never exists. This is a start-up hang, not
the kill.

## Run 3 (21:13): `SMCChecks=full` stops the same way, with the trap still armed

Baseline FEX (`460568b8`, trap present), with `"SMCChecks":"2"` added to
`Z:\home\xuser\.fex-emu\AppConfig\RelicCardinal.exe.json` (restored to `{"Config":{"HideHypervisorBit":"1"}}`
afterwards). In this FEX revision full mode does **not** remove the trap: `MarkGuestExecutableRange` runs
whenever a compiled block first covers a page, in every mode, and only `SMCChecks=none` stops `ProtectRWXIntervalsInternal`
(`SMCDetectionDisabled`). Full mode adds per-instruction validation on top.

Result, for two minutes: 5 Linux threads, 0 % CPU, one Windows thread (the main thread, user 1,700 ms),
no new log. The same signature as run 2.

This reproduces the earlier "`SMCChecks=full` hangs at launch from Play" ([EXPERIMENTS.md](EXPERIMENTS.md)),
whose config (`fexcfg6.txt`) dates from 00:12, before the round-17 debug channels.

## What the two hangs have in common

| run | trap | full-SMC validation | result |
|---|---|---|---|
| 1 | armed | off (`mtrack`) | loads to `GEWorld`, then the kill |
| 2 | removed (0003) | on for writable+executable blocks (0001) | stops at start-up |
| 3 | armed | on for every block (`SMCChecks=full`) | stops at start-up |

Removing the trap is **not needed** to produce this stop: run 3 stops the same way with the trap armed. The
factor the two stopping runs share is full-SMC validation (`ForceFullSMCDetection` / `SMCChecks=full`).
Two runs, two FEX binaries; the mechanism is not known.

## Where the main thread is blocked: not known

`tstack` on the main thread gives the same picture in runs 2 and 3, resolved through each DLL's COFF
symbols: pointers to the `InvalidationTracker` object, `std::condition_variable::notify_all`,
`__libcpp_recursive_mutex_unlock`, and `InvalidationTracker::HandleMemoryProtectionNotification` +0x174.
Disassembly shows +0x174 is the return address after a call to `__shared_mutex_base::unlock`, an unlock
that **completed**. These are leftovers of earlier work in the stack area, not the current blocking
point. A stack scan cannot show where a blocked thread is now.

`tctx` (which calls `SuspendThread` then `GetThreadContext`) hung on the run 2 main thread and left it at
suspend 1; the `suspend=1` sample at 21:02 is that probe's doing. So the context of the blocked thread could
not be read either.

## Where this leaves the SMC question

Open. The trap leak is real ([`SMC-TRAP.md`, part 2](../how-it-works/SMC-TRAP.md#part-2-confirmed-fex-leaks-its-smc-write-trap-to-the-guest)) and the kill is real on the clean
baseline. No build that removes the trap has yet run the game far enough to show whether the kill still
fires. The only trap-removing build so far relies on full-SMC validation, and full-SMC validation with the
trap still armed also stops the game at start-up (run 3). A test of the trap needs a way to hide it that keeps the
default `mtrack` invalidation.
