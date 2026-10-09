# Upstream: what was reported where

The fixes that belong in FEX, Wine or Mesa, and where each one stands. Reports filed on 2026-10-08; Wine's source was
checked at master `63f62f7cd696` and FEX's at main `14c92681f` (FEX-2610) first. Per-patch details:
[patches/fex](../../patches/fex).

| Fix | Upstream | Status |
|---|---|---|
| FEX patch 0015 (a 32-bit value loaded as 64 bits in `CheckCall`) | FEX pull request [#6021](https://github.com/FEX-Emu/FEX/pull/6021), with a second fix found while porting it (`CheckCall`'s bound used `b.hi`, which let an offset equal to the table size through; `b.hs`) | Open. Both fixes built on upstream main; a build of this package plus `b.hs` ran AoE II DE (menu, its ranked benchmark, score 1079.3) and AoE IV (skirmish, 41.2 / 41.4 FPS) |
| FEX patches 0012 and 0014 (the instruction stepper) | FEX issue [#6022](https://github.com/FEX-Emu/FEX/issues/6022), asking how the maintainers want it before a clean PR | Open |
| FEX patch 0004 (the SMC write trap is visible to the guest) | FEX issue [#6023](https://github.com/FEX-Emu/FEX/issues/6023); reproducer [`tools/probes/smcquery.c`](../../tools/probes/smcquery.c) | Open. The original draft is [Appendix A](#appendix-a-fex-issue-6023-the-smc-write-trap-is-observable-by-the-guest) |
| FEX patch 0006 (registers after a raw x64 `syscall`) | Wine bug [60461](https://bugs.winehq.org/show_bug.cgi?id=60461): the cause is Wine's `invoke_arm64ec_syscall`, still the same in master | Open |
| FEX patch 0016 (CPU-speed query) | Wine bug [60462](https://bugs.winehq.org/show_bug.cgi?id=60462): `NtPowerInformation(ProcessorInformation)` opens two cpufreq files per CPU per call in master | Open |
| FEX patch 0007 (Wine's ARM64EC export stubs fail the hook check) | Wine bug [60463](https://bugs.winehq.org/show_bug.cgi?id=60463) | Open. The text is [Appendix B](#appendix-b-wine-bug-60463-arm64ec--import-exports-start-with-a-bare-x64-jmp-ripx) |
| FEX patch 0010 (resume without a wineserver round trip) | Not reported: the round trip is gone in Proton 11.0-2 and Wine master (`NtContinueEx` only calls the server for alertable continues) | With the Wine the package runs on (11.0-99) it still helps |
| FEX patch 0017 (exceptions without a host trap) | Not reported yet | |
| FEX patches 0009, 0013, 0002, 0008 | Not proposed: 0009 is unsafe in general, 0013 is a tuning constant, 0002 hid FEX from the game, 0008 is an analysis tool | |
| The driver's gain | Mesa merge request [!44838](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/44838) (not ours), merged 2026-10-08 | Upstream; the repo's driver is a build of Mesa main that contains it |

## Appendix A: FEX issue #6023, the SMC write trap is observable by the guest

This is the draft as it was before filing; the filed issue was rewritten for current main. Two things in it are
outdated: the reproducer was run on hardware before filing (numbers in the table above), and its suggested fix
(`ForceFullSMCDetection` for writable code, patch 0001) stops AoE IV at start-up; the filed issue maps to patch 0004.

> **Filed (2026-10-08)** as FEX issue [#6023](https://github.com/FEX-Emu/FEX/issues/6023), rewritten for current main,
> with the reproducer measured: [`tools/probes/smcquery.c`](../../tools/probes/smcquery.c) reads `0x40` before the
> page's code runs and `0x20` after with GameNative's FEX-2512, and stays `0x40` with patch 0004. The text below is
> the older draft.

> **Note (2026-10-06).** The leak is real and reproducible with `tools/research/smctest2.c`, but it is not what breaks
> AoE IV ([`SMC-TRAP.md`, part 3](../how-it-works/SMC-TRAP.md#part-3-hiding-fexs-smc-trap-does-not-stop-the-aegis-kill)). A fix that keeps the trap and corrects the query results
> is `patches/fex/0004`. Separately, `SMCChecks=full` stops AoE IV at start-up
> ([KILL-REMEASURED.md](archive/KILL-REMEASURED.md)), which may deserve its own issue.

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

### Notes for filing

- The `string(STRIP ${AARCH64_CPU} AARCH64_CPU)` bug at `CMakeLists.txt:525` is worth a separate,
  trivial report: the variable is unquoted, so an empty helper output aborts configuration with
  *"string sub-command STRIP requires two arguments"*.
- Our own patch, if preferred as a PR: [`patches/fex/0001-hide-smc-trap-from-guest.patch`](../../patches/fex/0001-hide-smc-trap-from-guest.patch).

## Appendix B: Wine bug 60463, ARM64EC `-import` exports start with a bare x64 `jmp [rip+x]`

Filed on 2026-10-08 as Wine bug [60463](https://bugs.winehq.org/show_bug.cgi?id=60463). This is the source-level cause of the hook-check failure in [HOOK-CHECK.md](../how-it-works/HOOK-CHECK.md),
written as a Wine bug report. FEX patch 0007 works around it inside the emulator; a fix in Wine would make that patch
unnecessary. Sources were read on 2026-10-07 at Wine master `63f62f7cd696` and llvm-project main `1a5b507c6c6f`.

### Summary

On ARM64EC, functions that a DLL re-exports from another DLL (`-import` in the `.spec` file; kernel32.spec has many)
are exported as lld's x64 import thunk, which is a bare `FF 25 00000000` (`jmp *__imp_X(%rip)`). x64 code that resolves
such an export (`GetProcAddress`) sees a function that starts with a 6-byte indirect jump. x64 hook detectors in
games treat that start as an inline hook. On x86_64 Wine the same export starts with winebuild's hot-patch prolog
instead.

### What the code does

- **winebuild, x86_64:** the end of `output_exports` in
  [`tools/winebuild/spec32.c`](https://github.com/wine-mirror/wine/blob/63f62f7cd696/tools/winebuild/spec32.c#L585-L622)
  emits, for every `-import` entry, eight `nop`s, the label `__wine_spec_imp_<name>`, the hot-patch prolog
  `48 8D A4 24 00 00 00 00` (`lea rsp,[rsp+0]`) and `jmp *__imp_<name>(%rip)`. The export points at that thunk.
- **winebuild, ARM64EC:** the start of the same function
  ([lines 414 to 439](https://github.com/wine-mirror/wine/blob/63f62f7cd696/tools/winebuild/spec32.c#L414-L439))
  writes only `.drectve` directives `-export:Name=Name,@ord` and returns. The plain `Name` of an import is lld's x64
  import thunk.
- **lld:** the x64 import thunk is `importThunkX86[] = {0xff, 0x25, 0x00, 0x00, 0x00, 0x00}`
  ([`lld/COFF/Chunks.h`](https://github.com/llvm/llvm-project/blob/1a5b507c6c6f/lld/COFF/Chunks.h#L545-L547)).
  For exports whose target is ARM64EC code, lld emits a fast-forward sequence (`ECExportThunkCode`, `48 8B C4 48 89
  58 20 55 5D E9 ...`, [same file](https://github.com/llvm/llvm-project/blob/1a5b507c6c6f/lld/COFF/Chunks.h#L839-L847)),
  as Microsoft's toolchain does for all DLL exports ([ARM64EC ABI, "Fast-forward
  sequences"](https://learn.microsoft.com/en-us/windows/arm/arm64ec-abi)); an import thunk gets none.

### How it shows

AoE IV (Relic's Aegis) checks 63 Windows API functions at start-up. Under GameNative's Proton 11.0 ARM64EC with FEX,
27 of them were flagged, all in kernel32's `.text`, all starting with `FF 25` (measured, see
[HOOK-CHECK.md](../how-it-works/HOOK-CHECK.md)). Presenting the same jumps as `48 FF 25` (FEX patch 0007) made the
check flag nothing.

### Possible fix (not tested)

The following was proposed by reading the source; none of it was built or run here.

1. In `output_exports`, for ARM64EC `-import` entries, emit the same x86_64 thunk as on x86_64 (nop pad, hot-patch
   prolog, `jmp *__imp_<name>(%rip)`) and point the `-export:` directive at it. Wine merge request
   [!11975](https://gitlab.winehq.org/wine/wine/-/merge_requests/11975) (winebuild assembling ARM64EC output as
   x86_64) looks like the base for this.
2. `arm64x_check_call` in
   [`dlls/ntdll/signal_arm64ec.c`](https://github.com/wine-mirror/wine/blob/63f62f7cd696/dlls/ntdll/signal_arm64ec.c#L1897-L1979)
   follows a bare `FF 25` to its target so that ARM64EC callers skip the emulator. It would need to skip the hot-patch
   prolog as well, or calls through these exports would run in the emulator (correct, but slower).

Not checked: what Microsoft's own ARM64X kernel32 or link.exe produce for a re-exported import, and whether the
game's detector accepts the `lea` prolog form. The game's start-up decision under Box64, which runs x86_64 Wine, took
the same branch as with patch 0007 ([HOOK-CHECK.md](../how-it-works/HOOK-CHECK.md)).
