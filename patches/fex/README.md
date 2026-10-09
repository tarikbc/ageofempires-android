# FEX patches

| Patch | Status |
|---|---|
| `0001` + `0003` | Hide the trap by replacing it with `ForceFullSMCDetection`. `smctest` shows `RWX`, but the game stops at start-up; `SMCChecks=full` stops the same way, so the validation path is the problem ([KILL-REMEASURED.md](../../docs/research/archive/KILL-REMEASURED.md)). |
| `0002` | Hides the CPUID `0x40000000` vendor. Verified live. Does not stop the kill. **Not in the package since v1.1.0:** a build without it (`bc82c565`) ran the same test at the same speed as v1.0.0 (2026-10-07, [PERFORMANCE.md](../../docs/research/PERFORMANCE.md)); the game then reads vendor `FEXIFEXIEMU` from leaf `0x40000000` (hypervisor bit in leaf 1: 0, read with `tools/research/dbgprobe.c`). |
| `0004` | Keeps the trap and reports the guest's own protection from `NtQueryVirtualMemory` / `NtProtectVirtualMemory`. Verified with `smctest2`; the game runs normally with it. **Does not stop the kill** (5 of 5 runs, [`SMC-TRAP.md`, part 3](../../docs/how-it-works/SMC-TRAP.md#part-3-hiding-fexs-smc-trap-does-not-stop-the-aegis-kill)). |
| `0006` | On top of 0004: a raw x64 `syscall` returns `rcx` = return address and keeps `rdx`/`r10`, like hardware. Verified with `syscallregs`. **Does not stop the kill** (3 of 3 runs stopped: 2 suspended, 1 exited; [SYSCALL-RETURN.md](../../docs/how-it-works/SYSCALL-RETURN.md)). |
| `0007` (2026-10-07) | In every process where FEX runs (since v1.1.0; v1.0.0 did it only in `RelicCardinal.exe`): rewrites every exported `FF 25 disp32` thunk of an ARM64X image as `48 FF 25 disp32-1` when padding follows and the 7 bytes stay inside an executable section. **The game's hook check then flags nothing, and the start-up kill is gone**; 2 of 2 judged runs grew their log past the old window, then stopped 8 to 10 minutes in ([HOOK-CHECK.md](../../docs/how-it-works/HOOK-CHECK.md)). Checked 2026-10-07 with `tools/agent.py`: kernel32 `+0x62710` reads `48 ff 25` in the game, `aoeagent.exe` and `winhandler.exe`; `explorer.exe` keeps `ff 25`, because FEX is not loaded there. The stubs come from Wine's build ([UPSTREAM-WINE-ISSUE.md](../../docs/research/UPSTREAM.md#appendix-b-wine-bug-60463-arm64ec--import-exports-start-with-a-bare-x64-jmp-ripx)). |
| `0009` (2026-10-07) | Experiment, off unless `FEX_EXP_SKIP_CALLRET_RESET=1`: on code invalidation, do not discard the call-ret stacks of threads that had code cached in the range. Cuts the per-SMC-fault cost from about 124 to 49 us for the protection's thread. With 0007 the game reached its first menu, then the watchdog fired at about 13 minutes ([`WATCHDOG.md`, part 1](../../docs/how-it-works/WATCHDOG.md#part-1-the-later-stop-a-lateness-bucket-on-the-protections-own-loop-2026-10-07)). Unsafe in general. With 0010 a run without it (and with `FEX_TSOENABLED=0`) passed 15 minutes, so the watchdog does not need it; but match FPS fell from about 25 to 8 to 15 without it ([RESEARCH-LOG.md](../../docs/research/LOG.md), Speed). |
| `0010` (2026-10-07) | On top of 0004, on unless `FEX_EXP_FASTCONTINUE=0` (the first builds were off unless `=1`): an `NtContinue` to x64 code (full context, not alertable) re-enters the emulator through `KiUserEmulationDispatcher` directly instead of Wine's unix path, which waits for one wineserver `get_thread_info` request per call. A handled exception then costs 2.5 us instead of 111 to 130 us (230 us with the wineserver trace on) (`exccost`), the protection loop's cycle falls to about 1.15 s and the watchdog bucket stays at 0 ([`WATCHDOG.md`, part 2](../../docs/how-it-works/WATCHDOG.md#part-2-fast-continue-the-watchdogs-real-cost-was-a-wineserver-round-trip-per-exception-2026-10-07)). Verified with the job tree build `08172f64` and with the exact set 0002+0004+0006+0007+0009+0010 (`86d6da39`): both ran past 15 minutes with the game played. |
| `0012` (2026-10-07) | On by default (the code reads `FEX_EXP_VOLATILE=0` as off; that switch was not tested): **volatile code regions.** The protection runs code one instruction at a time from 32-byte slots in a 16 MB RWX buffer, which cost FEX a write fault, an invalidation and a compile per instruction (about 16,000/s). A private allocation of at most 64 MB whose page takes 16 write faults is no longer write-trapped, its blocks are never cached by address or linked, and blocks of the form [one instruction][`jmp` out of the region] share host code by content. Faults fell to about 300/s and skirmish FPS rose from 25.6 to 43 to 44 ([INSTRUCTION-STEPPER.md](../../docs/how-it-works/INSTRUCTION-STEPPER.md)). Verified with the exact set 0002+0004+0006+0007+0009+0010+0012+0013 (`20fdc47a`). |
| `0013` (2026-10-07) | FEX's code buffer cap from 128 to 512 MB. With 0012 the 128 MB buffer was still replaced (full recompile, a stutter of up to 300 ms) about every 30 s; with 512 MB every 2 to 3 minutes. |
| `0014` (2026-10-07) | On by default (the code reads `FEX_EXP_REUSE=0` as off; that switch was not tested): **reuse translations of re-decrypted code.** The protection scrambles some of its functions after use and decrypts them again; FEX recompiled them each time and still replaced its 512 MB code buffer (a full recompile) every 2 to 3 minutes. After a compile in a writable executable range FEX keeps the entry's host code with a hash of the decoded guest bytes, and puts it back when the same bytes return. 0 mismatches in 759,256 reuses; the code buffer then stayed at 256 MB for the 13.9 minutes checked ([INSTRUCTION-STEPPER.md](../../docs/how-it-works/INSTRUCTION-STEPPER.md)). Verified with the set 0002+0004+0006+0007+0009+0010+0012+0013+0014 (`6990a221`). |
| `0015` (2026-10-07) | `CheckCall` in `Module.S` loads the `uint32_t` `NtDllRedirectionLUTSize` with a 64-bit `ldr`: its bound then includes the next 4 bytes in memory, and the link fails (`misaligned ldr/str offset`) when the variable is not 8-byte aligned, which happened in the build without 0002. Loads it as 32 bits. A FEX bug, not specific to this game. |
| `0016` (2026-10-08) | **Caches `powrprof!CallNtPowerInformation(ProcessorInformation)` for x64 callers, 250 ms.** The game calls it about 45 times per second on its frame path, and Wine opens two cpufreq files per core for each call (228.5 us per call at the menu, `tools/probes/pwrcost.c`). When x64 code enters that function, `ExitFunctionEC` (`Module.S`) calls `FEXCachedCallNtPowerInformation` instead, through the original function's entry thunk; the target is found when `powrprof.dll` is mapped. No memory the game can see changes. The call's share of the main thread fell from 11.1 to 1.7 %, and the late-game replay went from 36.0 / 36.2 to 36.7 / 36.5 FPS ([POWER-INFORMATION.md](../../docs/how-it-works/POWER-INFORMATION.md)). Verified in the package `aoe4-perf5-23` (`b5e6e357`). Two first versions used the wrapper's own entry thunk and froze the game at start. |
| `0017` (2026-10-09) | On by default, two switches (`FEX_EXP_FASTTRAP=0`, `FEX_EXP_FASTRAISE=0`): **guest faults of JIT code without a host trap.** The protection raises about 44,000 illegal-instruction exceptions per second in the match (`FASTTRP1` counter: all SIGILL). FEX turned each into a `hlt`/`brk` host trap, Wine's signal handler, a first `KiUserExceptionDispatcher` pass that only rethrew it, and the unix `NtRaiseException`. FEXCore's guest-signal stubs (SIGILL, SIGTRAP, SIGSEGV) now branch through a per-thread pointer, `Pointers.GuestSignalTrapNative`, to the frontend's `FastTrapEntry`, which builds the guest exception as `RethrowGuestException` does and enters `KiUserExceptionDispatcher` directly with the frame on the guest stack (the layout FEX already uses for syscalls); the unix raise stays as the fallback. Skirmish benchmark: 58.3 / 57.8 FPS against 56.4 / 56.3 and 56.6 / 56.5 (baselines before and after), and about 20 % fewer frames of 25 ms (3 refreshes at 120 Hz). Tested on AoE IV only (a 19-minute run without a stop); a debugger does not see these first-chance events. [TRAPLESS-FAULTS.md](../../docs/how-it-works/TRAPLESS-FAULTS.md). |
| `0008` (2026-10-07) | Analysis tool: copies every distinct decoded block in `0x143800000..0x145000000` of the game process into a 96 MB buffer; `tools/probes/blkread.c` reads it out. The game runs normally with it. |

Build: fresh clone of FEX `7d3090f`, the macOS fixes from [BUILDING.md](../../docs/guides/BUILDING.md), then the
package's patches in this order: 0004, 0006, 0007, 0009, 0010, 0012, 0013, 0014, 0015, 0016, 0017 (release v1.4.0;
v1.2.0 and v1.3.0 stopped at 0016), then `ninja arm64ecfex`. The v1.4.0 DLL is `7e707379`; its set was checked to apply cleanly to `7d3090f`, and 0017 matches the tested
build's code apart from comments (a byte comparison of a clean build was not made). Checked 2026-10-08: a clean build of these files gives the tested DLL `b5e6e357` except the 4 bytes
of the build time stamp (PE header and debug directory). Checked 2026-10-07 the same way: the v1.1.0 set (the same
without 0016) gives `bc82c565`, and the v1.0.0 set (0002, 0004, 0006, 0007 in its game-only form, 0009, 0010, 0012,
0013, 0014; tag `v1.0.0`) gives the released `6990a221`.

## Upstream (2026-10-08)

Where each problem was reported or proposed. Wine's source was checked at master `63f62f7cd696` and FEX's at main
`14c92681f` (FEX-2610) first.

| Patch | Upstream |
|---|---|
| 0015 | FEX pull request [#6021](https://github.com/FEX-Emu/FEX/pull/6021), with a second fix found while porting it: `CheckCall`'s bound used `b.hi`, which let an offset equal to the table size through and read one entry past the end (`b.hs`). Both built on upstream main; a build of this package plus `b.hs` ran AoE II DE (menu, its ranked benchmark, score 1079.3) and AoE IV (skirmish, 41.2 / 41.4 FPS). |
| 0012, 0014 | FEX issue [#6022](https://github.com/FEX-Emu/FEX/issues/6022), asking how the maintainers want it before a clean PR. |
| 0004 | FEX issue [#6023](https://github.com/FEX-Emu/FEX/issues/6023). Its reproducer is [`tools/probes/smcquery.c`](../../tools/probes/smcquery.c): with GameNative's FEX-2512 a fresh RWX page reads `0x40` before its code runs and `0x20` after; with this package it stays `0x40` (AoE II container session, 2026-10-08). |
| 0006 | Wine bug [60461](https://bugs.winehq.org/show_bug.cgi?id=60461): the cause is Wine's `invoke_arm64ec_syscall`, still the same in master. |
| 0016 | Wine bug [60462](https://bugs.winehq.org/show_bug.cgi?id=60462): `NtPowerInformation(ProcessorInformation)` still opens two cpufreq files per CPU per call in master. |
| 0007 | Wine bug [60463](https://bugs.winehq.org/show_bug.cgi?id=60463) (text in [UPSTREAM.md](../../docs/research/UPSTREAM.md#appendix-b-wine-bug-60463-arm64ec--import-exports-start-with-a-bare-x64-jmp-ripx)). |
| 0010 | Not reported: the wineserver round trip it avoids is gone in Proton 11.0-2 and Wine master (`NtContinueEx` only calls the server for alertable continues); with the Wine this package runs on (11.0-99) it still helps. |
| 0017 | Not reported yet (2026-10-09). |
| 0009, 0013, 0002, 0008 | Not proposed: 0009 is unsafe in general, 0013 is a tuning constant, 0002 hid FEX from the game, 0008 is an analysis tool. |

## `0001-hide-smc-trap-from-guest.patch`

**The problem.** Under the default `SMCChecks=mtrack`, FEX catches self-modifying code by clearing the
write permission on the guest's own writable+executable pages, so a write faults and the translated
block can be invalidated. That works, but the guest can see it:

```c
ULONG InvalidationTracker::GetTrapProt(...) { return PAGE_EXECUTE_READ; }   // write removed
```

and FEX does not hide it — it intercepts the guest's `NtAllocateVirtualMemory` and
`NtProtectVirtualMemory` (via the ARM64EC BT interface and `WineNtProtectVirtualMemorySyscallId`) but
**not `NtQueryVirtualMemory`**. So a page the guest set to `PAGE_EXECUTE_READWRITE` reads back as
`PAGE_EXECUTE_READ`. Internal instrumentation must not be visible in the guest's view of its own
address space, and anti-tamper code checks exactly this: AoE IV's Aegis calls `NtQueryVirtualMemory`
**35,248 times per run**.

**The fix.** FEX already contains an invisible alternative. `ForceFullSMCDetection` makes the core
validate each translated instruction against guest memory at run time (`Core.cpp`, via
`_ValidateCode`), which needs no protection change at all — it is currently only enabled for Mono
hacks. This patch sets it for any block that lies in a writable executable region, which the decoder
already knows: `CheckRangeExecutable()` populates `ExecutableRangeWritable`, and on ARM64EC the
underlying `QueryExecutableRange` returns `Writable = true` for precisely the `RWXIntervals` the trap
would otherwise be applied to.

```cpp
if (!BlockIt->ForceFullSMCDetection && CheckRangeExecutable(BlockIt->Entry, 1) && ExecutableRangeWritable) {
  BlockIt->ForceFullSMCDetection = true;
}
```

Cost is bounded: only blocks in writable executable regions pay for validation, not the whole address
space as with `SMCChecks=full`.

**Status: built and tested later (with 0003, DLL `b4dbf32d`): the game stops at start-up** (see the table above). The
rest of this section is the text as written before the build. Building ARM64EC FEX needs llvm-mingw, which is not currently
installed on this machine. It also has not been validated against the FEX revision GameNative ships.
Treat it as a candidate fix, not a verified one.

**Also untested:** whether `CheckRangeExecutable` is cheap enough to call once per decoded block. It
caches its range, so repeated calls within a region should be near free, but that has not been measured.

**Upstreaming.** This is a general correctness fix rather than a game-specific hack — the framing for a
FEX issue is "the SMC write trap is observable by the guest through `NtQueryVirtualMemory`".

## `0004-report-guest-protection-for-trapped-pages.patch`

Keeps the default `mtrack` trap and corrects what the guest reads back, through Wine's exported
`__wine_syscall_dispatcher` pointer, which every ntdll syscall stub loads on each call. Design and
measurements: [`SMC-TRAP.md`, part 3](../../docs/how-it-works/SMC-TRAP.md#part-3-hiding-fexs-smc-trap-does-not-stop-the-aegis-kill).

The committed file is the fix2 version: applied with 0002 to `7d3090f` it builds `a5ec2726`. Runs 4 and A1 to
A6 used the first version (`2f578905`), which lacks two parts of this file: own-process handles other than
`NtCurrentProcess()` (`IsCurrentProcess`) and the `SmcHideStats` counters that `tools/probes/fexstats.c` reads.

## Numbers not in this folder

Patch numbers 0005 and 0011 are not in this folder: 0005, an experiment build, logged the game's syscalls
([archive/AEGIS-TRACE.md](../../docs/research/archive/AEGIS-TRACE.md)); no record says what 0011 was. The counter builds
of the speed work (`THRSTAT1` per-thread statistics, `FASTTRP1` signal counts) are not committed either.
