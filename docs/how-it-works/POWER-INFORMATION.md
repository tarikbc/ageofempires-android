# CPU frequency queries on every frame (patch 0016)

**Result:** in the late game the main thread spent 11.1 % of its time (17.7 % at the main menu) inside one Wine system
call, `NtPowerInformation`. The game asks for the MHz of every CPU core about 45 times per second, and Wine answers by
reading cpufreq files for each core. FEX patch 0016 answers repeat questions from a 250 ms cache. With it that call's
share of the main thread fell to 1.7 %. Measured on 2026-10-08 on the AYN Thor with the replay benchmark
([TESTING.md](../guides/TESTING.md), "Late-game benchmark from a replay"), full CPU clocks.

## How it was found

1. **Where the main thread is.** `tools/agent.py sample Game/Main 1500 2 FILE` suspends the main thread 1,500 times,
   2 ms apart, and records its instruction pointer. Late game, v1.1.0: Wine DLLs 49.4 %, game code 38.9 %, the
   protection's code region 11.7 %. Nearly all Wine samples were in `ntdll.dll` at the return of a system-call stub;
   with the stub numbers decoded by `tools/research/ntdll_syscall_table.py`: `NtWaitForAlertByThreadId` (lock and
   condition-variable waits) 25.3 %, **`NtPowerInformation` 11.1 %**, `NtAlertThreadByThreadId` 4.1 %.
2. **Who calls it.** `agent.py callers Game/Main 1500 2 LO HI` keeps the samples inside that stub and collects the
   game's return addresses on the stack. All of them led to `exe+3aeaa10`, which calls
   `CallNtPowerInformation(ProcessorInformation, NULL, 0, buffer, n * 24)` (`mov ecx, 0xb`: one
   `PROCESSOR_POWER_INFORMATION` per core) and is called from `exe+8d2348` on the frame path.
3. **What one call costs.** `tools/probes/pwrcost.c` makes the same call: **228.5 us per call** (200 calls, the game
   idle at its menu). Wine's implementation (`dlls/ntdll/unix/system.c`, case `ProcessorInformation`) opens two
   cpufreq files per core on every call, `cpuinfo_max_freq` and `scaling_max_freq` (read in Wine master
   [`63f62f7cd696`](https://github.com/wine-mirror/wine/blob/63f62f7cd696/dlls/ntdll/unix/system.c); GameNative's
   Proton copy was not read). The cache counters below showed about 45 calls per second in the game; at that rate 11 % of the
   main thread would mean about 2.4 ms per call under load. That last figure is an estimate, not a measurement.

## The fix

[0016](../../patches/fex/0016-cache-power-information.patch) works at FEX's crossing from x64 code into native code.
When x64 code enters `powrprof!CallNtPowerInformation`, `ExitFunctionEC` (`Module.S`) looks up that function's entry
thunk as usual and then calls `FEXCachedCallNtPowerInformation` instead of the function. For `ProcessorInformation`
with no input buffer it returns the last answer if it is younger than 250 ms (`KUSER_SHARED_DATA.InterruptTime`) and
of the same size; everything else, and the first call, goes to the real function. The target is found when
`powrprof.dll` is mapped, by following its export's x64 fast-forward sequence to the native code. No memory the game
can see is changed: not the game, not its import table, not Wine's DLLs.

Two first versions failed, both with the wrapper's **own** entry thunk: its return path jumps through the module's
`__os_arm64x_dispatch_ret`, which is never set in FEX's own DLL. `pwrcost` then crashed with an access violation at
address 0 (`rip 0`, `rax 0`: the wrapper had returned), and the game froze at `Loading step: [InitThreadingModel]`
until its hang detector stopped it after 240 s. Using the original function's entry thunk (same signature, set up
by `powrprof.dll`) fixed it: `pwrcost` then got status 0, the right MHz for all 8 cores, and 2,000 calls took less
than 0.05 us each.

Counters, readable with `tools/agent.py peek libarm64ecfex.dll 3ef2e8 28` (marker `PWRCACH1`; the same RVA in the
v1.2.0 DLL `b5e6e357` and the v1.4.0 DLL `7e707379`):
target, calls, cached calls, redirect target. In the game: 8,819 calls and 8,217 answered from the cache after the
start and the first minutes of a replay; 45 calls and 41 cached per second at 8X.

## Results

Late-game windows of the replay (game time at start), 1X, the player's camera, full CPU clocks, display at 120 Hz:

| Package and setting | 46:13 | 48:23 |
|---|---|---|
| v1.1.0 (`bc82c565`) | 36.0 FPS, 19 frames > 50 ms | 36.2 FPS, 15 |
| v1.2.0 (`b5e6e357`, with 0016) | 36.7 FPS, 10 | 36.5 FPS, 11 |

The main thread after 0016, same late game: `NtPowerInformation` 1.7 %, `NtWaitForAlertByThreadId` 35.5 %, CPU 46 %
of one core (55 % before). The freed time became waiting: the main thread is not what limits the frame. The larger
step came from the GPU clock ([PERFORMANCE.md](../research/PERFORMANCE.md), "the late game at full clocks").
