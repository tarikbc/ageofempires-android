# FEX patches

| Patch | Status (2026-10-06) |
|---|---|
| `0001` + `0003` | Hide the trap by replacing it with `ForceFullSMCDetection`. `smctest` shows `RWX`, but the game stops at start-up; `SMCChecks=full` stops the same way, so the validation path is the problem ([KILL-REMEASURED.md](../../docs/KILL-REMEASURED.md)). |
| `0002` | Hides the CPUID `0x40000000` vendor. Verified live. Does not stop the kill. |
| `0004` | Keeps the trap and reports the guest's own protection from `NtQueryVirtualMemory` / `NtProtectVirtualMemory`. Verified with `smctest2`; the game runs normally with it. **Does not stop the kill** (5 of 5 runs, [SMC-TRAP-HIDDEN.md](../../docs/SMC-TRAP-HIDDEN.md)). |

Build: fresh clone of FEX `7d3090f`, apply 0002 and 0004, the three macOS fixes from
[BUILDING-FEX.md](../../docs/BUILDING-FEX.md), then `ninja arm64ecfex`.

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

**Status: written, not built or tested.** Building ARM64EC FEX needs llvm-mingw, which is not currently
installed on this machine. It also has not been validated against the FEX revision GameNative ships.
Treat it as a candidate fix, not a verified one.

**Also untested:** whether `CheckRangeExecutable` is cheap enough to call once per decoded block. It
caches its range, so repeated calls within a region should be near free, but that has not been measured.

**Upstreaming.** This is a general correctness fix rather than a game-specific hack — the framing for a
FEX issue is "the SMC write trap is observable by the guest through `NtQueryVirtualMemory`".

## `0004-report-guest-protection-for-trapped-pages.patch`

Keeps the default `mtrack` trap and corrects what the guest reads back, through Wine's exported
`__wine_syscall_dispatcher` pointer, which every ntdll syscall stub loads on each call. Design and
measurements: [SMC-TRAP-HIDDEN.md](../../docs/SMC-TRAP-HIDDEN.md).

The committed file is the fix2 version: applied with 0002 to `7d3090f` it builds `a5ec2726`. Runs 4 and A1 to
A6 used the first version (`2f578905`), which lacks two parts of this file: own-process handles other than
`NtCurrentProcess()` (`IsCurrentProcess`) and the `SmcHideStats` counters that `tools/probes/fexstats.c` reads.
