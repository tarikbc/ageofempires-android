# Draft: FEX issue — the SMC write trap is observable by the guest

> **Note (2026-10-06).** The leak is real and reproducible with `tools/smctest2.c`, but it is not what breaks
> AoE IV ([SMC-TRAP-HIDDEN.md](SMC-TRAP-HIDDEN.md)). A fix that keeps the trap and corrects the query results
> is `patches/fex/0004`. Separately, `SMCChecks=full` stops AoE IV at start-up
> ([KILL-REMEASURED.md](KILL-REMEASURED.md)), which may deserve its own issue.

Ready to file against [FEX-Emu/FEX](https://github.com/FEX-Emu/FEX). Written to stand on its own:
it is a correctness bug independent of any game.

---

**Title:** ARM64EC: the SMC write trap is visible to the guest through `NtQueryVirtualMemory`

**Summary.** With the default `SMCChecks=mtrack`, FEX detects self-modifying code by clearing the write
permission on the guest's own writable+executable pages, so a write faults and the translated block can
be invalidated. That mechanism leaks: a page the guest set to `PAGE_EXECUTE_READWRITE` reads back as
`PAGE_EXECUTE_READ` from `NtQueryVirtualMemory`. Internal instrumentation should not be visible in the
guest's view of its own address space, and software that checks its own page protections — anti-tamper
in particular, but also JITs and self-modifying runtimes — sees a page it never asked for.

**Where.**

`Source/Windows/Common/InvalidationTracker.cpp`:

```cpp
ULONG InvalidationTracker::GetTrapProt(uint64_t Address) const {
  if (DEPDisabled && DEPPromotedIntervals.Query(Address).Enclosed) return PAGE_READONLY;
  return PAGE_EXECUTE_READ;                 // write permission removed from a guest RWX page
}

bool InvalidationTracker::ProtectRWXIntervalsInternal(uint64_t Address, uint64_t Size, bool ForWriteLocked) {
  ...
  NtProtectVirtualMemory(NtCurrentProcess(), &TmpAddress, &TmpSize,
                         ForWriteLocked ? GetUntrapProt(Address) : GetTrapProt(Address), &TmpProt);
```

`Source/Windows/ARM64EC/Module.cpp` arms it as soon as the guest marks a range executable
(`MarkGuestExecutableRange` → `ReprotectRWXIntervals` → `ProtectRWXIntervalsInternal`).

**Why it is not hidden.** The ARM64EC syscall table tracks four calls by name —
`NtContinue`, `NtAllocateVirtualMemory`, `NtProtectVirtualMemory`, `NtRaiseException` — and FEX
registers with the ARM64EC BT interface for `NotifyMemoryAlloc` / `NotifyMemoryProtect` /
`NotifyMemoryFree`. **There is no query direction in either place**, so `NtQueryVirtualMemory` passes
through and reports the trap protection as if the guest had asked for it.

**Reproducer** (no game needed — it runs inside the guest):

```c
void *p = VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
MEMORY_BASIC_INFORMATION m;
VirtualQuery(p, &m, sizeof(m));
printf("Protect = 0x%lx\n", m.Protect);
```

Expected `0x40` (`PAGE_EXECUTE_READWRITE`). Observed under `SMCChecks=mtrack`: `0x20`
(`PAGE_EXECUTE_READ`). Under `SMCChecks=none` the write permission is retained, which confirms the
trap is the cause.

**Suggested fix.** FEX already has a mechanism that needs no protection change:
`ForceFullSMCDetection` makes the core validate each translated instruction against guest memory at run
time (`Core.cpp`, via `_ValidateCode`). It is currently enabled only for Mono hacks. The decoder already
knows whether a block lies in a writable executable region — `CheckRangeExecutable()` populates
`ExecutableRangeWritable`, and on ARM64EC `QueryExecutableRange` returns `Writable = true` for exactly
the `RWXIntervals` the trap would otherwise cover — so the change is small:

```cpp
if (!BlockIt->ForceFullSMCDetection && CheckRangeExecutable(BlockIt->Entry, 1) && ExecutableRangeWritable) {
  BlockIt->ForceFullSMCDetection = true;
}
```

in `Decoder::DecodeLoop`. Cost is bounded to blocks in writable executable regions rather than the
whole address space, which is what makes `SMCChecks=full` impractical.

Alternatively, if the trap is kept, the guest-visible protection should be virtualised — but note that
an external process's `VirtualQueryEx` would still see the real value, so masking only in-process is a
partial fix.

**Status.** The diagnosis is confirmed in source; the reproducer has not yet been run on hardware
(the test device is unavailable). Do not treat the "observed" line above as measured until it is.

---

## Notes for filing

- The `string(STRIP ${AARCH64_CPU} AARCH64_CPU)` bug at `CMakeLists.txt:525` is worth a separate,
  trivial report: the variable is unquoted, so an empty helper output aborts configuration with
  *"string sub-command STRIP requires two arguments"*.
- Our own patch, if preferred as a PR: [`patches/fex/0001-hide-smc-trap-from-guest.patch`](../patches/fex/0001-hide-smc-trap-from-guest.patch).
