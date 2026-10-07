# The Aegis kill, measured on a clean baseline with the right instrument

2026-10-06. Two runs, both launched by `tools/run_watch.py --launch` (force-stop GameNative, start it,
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

## Run 2 (20:58): the no-trap FEX build hangs inside FEX

`libarm64ecfex.dll` replaced by `libarm64ecfex.notrap.dll` (SHA-1 `b4dbf32d`; patches 0001 and 0003; with it
`smctest` reports `RWX`, see [FIX-VERIFIED.md](FIX-VERIFIED.md)). Swapped by rename inside a running
session, and the hash of the installed file was checked before the launch.

For more than three minutes after Play:

- 5 Linux threads, all `S`, 0 % CPU;
- `suspinfo`: **one** Windows thread, the main thread (`start=RelicCardinal.exe+4fb0884`, the exe entry
  point), suspend 0, user 490 ms;
- no new `warnings.log` (its first line still named the 20:51 run).

`tstack` on that thread, with the offsets resolved through the DLL's own COFF symbols (20,113 of them):

| offset in `libarm64ecfex.dll` | symbol |
|---|---|
| `+3f1648`, `+3f1678` | data: `(anonymous namespace)::InvalidationTracker` +0x0 and +0x30 |
| `+1aa25c` | `FEX::Windows::InvalidationTracker::HandleMemoryProtectionNotification` +0x174 |
| `+183244` | `std::condition_variable::notify_all` +0x8 |
| `+1cec60` | `std::__libcpp_condvar_broadcast` +0x10 |
| `+1ceba0` | `std::__libcpp_recursive_mutex_unlock` +0x10 |

A stack scan also collects stale values, so this places the thread in FEX's invalidation tracker but does
not show which lock it waits on. What is established: the process stops at the exe entry point, inside
FEX, at 0 % CPU, before the game creates a second thread. Aegis's kill thread never exists in this run,
so this is a FEX hang and not a protection response.

`tctx` (which calls `SuspendThread` then `GetThreadContext`) hung on this thread as well and left it at
suspend 1. The `suspend=1` sample at 21:02 is that probe's doing.

## Related, not yet tested

- `SMCChecks=2` (full) also "hangs at launch from Play" ([EXPERIMENTS.md](EXPERIMENTS.md)). That config
  (`fexcfg6.txt`) dates from 00:12, before round 17, so the debug channels do not explain it. Full SMC and
  patch 0001 both send blocks through `ForceFullSMCDetection`. Whether the two hangs share a cause is
  **not tested**.

## Where this leaves the SMC question

Open. The trap leak is real ([SMC-CONFIRMED.md](SMC-CONFIRMED.md)) and the kill is real on the clean
baseline. No build that removes the trap has yet run the game far enough to show whether the kill still
fires.
