# FEX leaks its self-modifying-code trap to the guest — and Aegis is watching for it

Lead hypothesis, with the mechanism located in FEX source. It explains every observation that nothing
else did.

## The defect

FEX emulates x86-64 on ARM64EC. To catch self-modifying code under the default `SMCChecks=mtrack`, it
takes pages the guest marked **writable + executable** and **removes the write permission**, so a write
faults and FEX can invalidate the translated block.

`Source/Windows/Common/InvalidationTracker.cpp`:

```cpp
ULONG InvalidationTracker::GetTrapProt(uint64_t Address) const {
  ...
  return PAGE_EXECUTE_READ;              // <-- WRITE removed from a guest RWX page
}
ULONG InvalidationTracker::GetUntrapProt(uint64_t Address) const {
  ...
  return PAGE_EXECUTE_READWRITE;         // restored when the write fault is handled
}

bool InvalidationTracker::ProtectRWXIntervalsInternal(uint64_t Address, uint64_t Size, bool ForWriteLocked) {
  ...
  NtProtectVirtualMemory(NtCurrentProcess(), &TmpAddress, &TmpSize,
                         ForWriteLocked ? GetUntrapProt(Address) : GetTrapProt(Address), &TmpProt);
```

So a page the guest set to `PAGE_EXECUTE_READWRITE` **really is** `PAGE_EXECUTE_READ` for as long as the
trap is armed.

**And FEX does not hide this.** It intercepts the guest's `NtAllocateVirtualMemory` and
`NtProtectVirtualMemory` (see `WineNtAllocateVirtualMemorySyscallId` /
`WineNtProtectVirtualMemorySyscallId` in `Source/Windows/ARM64EC/Module.cpp`), but **not
`NtQueryVirtualMemory`**. A guest query therefore goes straight through and reports FEX's trap
protection as if it were the guest's own.

That is a correctness bug in FEX independent of any game: **internal instrumentation must not be
visible in the guest's view of its own address space.**

## Why this is the answer

| Observation | Explained |
|---|---|
| Aegis calls `NtQueryVirtualMemory` **35,248 times per run** (measured, round 17) | It is checking its own pages' protections |
| The kill is indifferent to CPUID, TLS, module names, debugger signals, session health | None of those touch page protections |
| `SMCChecks=mtrack` (default) → freeze | Trap armed → guest sees `PAGE_EXECUTE_READ` where it set RWX → tamper detected |
| `SMCChecks=none` → exits at ~2 min | Trap disabled, so nothing is visible — **but FEX then never invalidates self-modified code**, so the game runs stale translations and dies differently |
| `SMCChecks=full` → hangs at launch | Per-instruction CRC validation of every block — correct, and far too slow |
| The Mac passes | Rosetta's SMC handling does not re-protect guest pages this way |
| The image is byte-stable before the kill | This is a **protection** change, not a content change — byte-comparing probes could never see it |

## The fix

Intercept the guest's `NtQueryVirtualMemory` (`MemoryBasicInformation`) alongside the existing hooks,
and when the queried address falls inside an `RWXIntervals` entry whose current protection is the trap
protection, report `GetUntrapProt(Address)` instead. The guest then sees the protection it set, FEX
keeps its trap, and invalidation still works.

That is a small, upstreamable change, and the right end state: FEX should not leak its instrumentation.

## Evidence status

**Confirmed in source:** the trap exists, it removes write permission, it is on the ARM64EC path
(`Source/Windows/ARM64EC/Module.cpp` constructs the tracker, handles `HandleRWXAccessViolation`, and
calls `HandleMemoryProtectionNotification` from its memory hooks), and `InvalidationTracker.cpp` is in
`Source/Windows/Common/CMakeLists.txt`.

**Not yet directly observed:** Aegis reading a trapped protection. The inference is strong — the code
path is present, the `SMCChecks` sensitivity matches exactly, and Aegis's query volume matches — but
the decisive confirmation is a run with `NtQueryVirtualMemory` virtualised, which needs a FEX build.

## How to confirm

1. **Build FEX with the query hook** and run. This is the real test. The repo already builds FEX from
   source (llvm-mingw + CMake, ~12 s), so a patch is cheap — llvm-mingw just has to be reinstalled.
2. **Cheaper interim probe:** sample the game's runtime-allocated regions from a probe process. A region
   that was RWX and later reads `PAGE_EXECUTE_READ` without the guest asking for it is a trapped page.
3. **Differential:** log what the guest is told for its own code pages under `mtrack` vs `none`.
