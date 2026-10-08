# FEX's self-modifying-code trap, seen by the game (patch 0004)

Three write-ups from 2026-10-06, joined in the order they were written: the hypothesis, its confirmation on the Thor, and the test that ruled it out as the cause of the kill. FEX patch 0004 comes from this work.

- [Part 1](#part-1-fex-leaks-its-self-modifying-code-trap-to-the-guest--and-aegis-is-watching-for-it): FEX leaks its self-modifying-code trap to the guest — and Aegis is watching for it
- [Part 2](#part-2-confirmed-fex-leaks-its-smc-write-trap-to-the-guest): CONFIRMED: FEX leaks its SMC write trap to the guest
- [Part 3](#part-3-hiding-fexs-smc-trap-does-not-stop-the-aegis-kill): Hiding FEX's SMC trap does not stop the Aegis kill

## Part 1: FEX leaks its self-modifying-code trap to the guest — and Aegis is watching for it

> **Ruled out (2026-10-06, 22:30).** Tested directly: with the trap hidden from the guest the kill still
> fires (5 of 5 runs). See [part 3](#part-3-hiding-fexs-smc-trap-does-not-stop-the-aegis-kill). The mechanism below is real; the
> link to the kill is not.

Lead hypothesis, with the mechanism located in FEX source. It explains every observation that nothing
else did.

### The defect

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

### Why this is the answer

| Observation | Explained |
|---|---|
| Aegis calls `NtQueryVirtualMemory` **35,248 times per run** (measured, round 17) | It is checking its own pages' protections |
| The kill is indifferent to CPUID, TLS, module names, debugger signals, session health | None of those touch page protections |
| `SMCChecks=mtrack` (default) → freeze | Trap armed → guest sees `PAGE_EXECUTE_READ` where it set RWX → tamper detected |
| `SMCChecks=none` → exits at ~2 min | Trap disabled, so nothing is visible — **but FEX then never invalidates self-modified code**, so the game runs stale translations and dies differently |
| `SMCChecks=full` → hangs at launch | Per-instruction CRC validation of every block — correct, and far too slow |
| The Mac passes | Rosetta's SMC handling does not re-protect guest pages this way |
| The image is byte-stable before the kill | This is a **protection** change, not a content change — byte-comparing probes could never see it |

### The fix

Intercept the guest's `NtQueryVirtualMemory` (`MemoryBasicInformation`) alongside the existing hooks,
and when the queried address falls inside an `RWXIntervals` entry whose current protection is the trap
protection, report `GetUntrapProt(Address)` instead. The guest then sees the protection it set, FEX
keeps its trap, and invalidation still works.

That is a small, upstreamable change, and the right end state: FEX should not leak its instrumentation.

### Evidence status

**Confirmed in source:** the trap exists, it removes write permission, it is on the ARM64EC path
(`Source/Windows/ARM64EC/Module.cpp` constructs the tracker, handles `HandleRWXAccessViolation`, and
calls `HandleMemoryProtectionNotification` from its memory hooks), and `InvalidationTracker.cpp` is in
`Source/Windows/Common/CMakeLists.txt`.

**Not yet directly observed:** Aegis reading a trapped protection. The inference is strong — the code
path is present, the `SMCChecks` sensitivity matches exactly, and Aegis's query volume matches — but
the decisive confirmation is a run with `NtQueryVirtualMemory` virtualised, which needs a FEX build.

### How to confirm

1. **Build FEX with the query hook** and run. This is the real test. The repo already builds FEX from
   source (llvm-mingw + CMake, ~12 s), so a patch is cheap — llvm-mingw just has to be reinstalled.
2. **Cheaper interim probe:** sample the game's runtime-allocated regions from a probe process. A region
   that was RWX and later reads `PAGE_EXECUTE_READ` without the guest asking for it is a trapped page.
3. **Differential:** log what the guest is told for its own code pages under `mtrack` vs `none`.

### Update (round 28): the fix exists, in FEX's own code

Reading further found that FEX already contains an **invisible** alternative to the write trap, so the
fix needs no new mechanism:

- `ForceFullSMCDetection` makes the core validate each translated instruction against guest memory at
  run time (`Core.cpp`, via `_ValidateCode`). It needs **no protection change**, so the guest sees
  nothing. It is currently enabled only for Mono hacks.
- The decoder already knows whether a block is in a writable executable region:
  `CheckRangeExecutable()` populates `ExecutableRangeWritable`, and on ARM64EC `QueryExecutableRange`
  returns `Writable = true` for exactly the `RWXIntervals` the trap is applied to.

So the patch is three lines in `Decoder::DecodeLoop`: set `BlockIt->ForceFullSMCDetection` for blocks in
writable executable regions. Cost is bounded to those blocks rather than the whole address space, which
is what makes `SMCChecks=full` unusable.

See [`patches/fex/0001-hide-smc-trap-from-guest.patch`](../../patches/fex/0001-hide-smc-trap-from-guest.patch).
**Written, not built or tested** — ARM64EC FEX needs llvm-mingw, which is not installed here.


### Update (round 31): the crux is verified in source

The hypothesis needs two things to be true, and both now check out by reading the code:

1. **FEX strips write permission from guest RWX pages.** `GetTrapProt()` returns `PAGE_EXECUTE_READ`, and
   `ProtectRWXIntervalsInternal` applies it through `NtProtectVirtualMemory` as soon as the guest marks a
   range executable.
2. **FEX never intercepts the query that would reveal it.** The ARM64EC syscall table tracks exactly four
   calls — `NtContinue`, `NtAllocateVirtualMemory`, `NtProtectVirtualMemory`, `NtRaiseException` — and
   the BT interface FEX registers with (`BTInterface.h`) has only `NotifyMemoryAlloc` /
   `NotifyMemoryProtect` / `NotifyMemoryFree`. **There is no query direction in either place**, so a
   guest's `NtQueryVirtualMemory` passes straight through.

So the mechanism is confirmed statically. What remains unconfirmed is the runtime observation — that a
guest really does read back `PAGE_EXECUTE_READ` for a page it set to `PAGE_EXECUTE_READWRITE`. That is
exactly what `tools/research/smctest.c` measures, and it takes seconds once a session can start.

Drafted upstream report: [UPSTREAM-FEX-ISSUE.md](../research/UPSTREAM-FEX-ISSUE.md).

## Part 2: CONFIRMED: FEX leaks its SMC write trap to the guest

> **Follow-up (2026-10-06, 22:30).** The leak is real, but it is not what triggers the Aegis kill: a FEX
> build that hides it was still killed in 5 of 5 runs, and in-game counters show no query touched a
> trapped page. See [part 3](#part-3-hiding-fexs-smc-trap-does-not-stop-the-aegis-kill).

Measured on the AYN Thor, in a live GameNative container, 2026-10-06.

`tools/research/smctest.c` runs **inside the guest** (emulated by FEX), allocates a page as `PAGE_EXECUTE_READWRITE`,
and asks `VirtualQuery` what protection it actually has.

### Result

```
VirtualAlloc(PAGE_EXECUTE_READWRITE) -> 0000000000BC0000 (err=0)

immediately after VirtualAlloc     Protect=RWX      (0x40)
after 100ms                        Protect=RWX      (0x40)

wrote 6 bytes of code into the page
after writing to the page          Protect=RWX      (0x40)
executed the page -> returned 42
after executing the page           Protect=RX       (0x20)   <-- write permission missing

VirtualProtect(..., PAGE_EXECUTE_READWRITE) ok, previous=0x20
after explicit VirtualProtect RWX  Protect=RWX      (0x40)
```

### What it means

**The page is `RWX` right up to the moment code in it is executed. Immediately afterwards it is `RX`** —
the guest's write permission has been removed, and the guest did not ask for that.

That is precisely the behaviour located in FEX source ([part 1](#part-1-fex-leaks-its-self-modifying-code-trap-to-the-guest--and-aegis-is-watching-for-it)):

- `InvalidationTracker::GetTrapProt()` returns `PAGE_EXECUTE_READ`;
- `ProtectRWXIntervalsInternal` applies it via `NtProtectVirtualMemory` when the guest marks a range
  executable;
- FEX intercepts the guest's `NtAllocateVirtualMemory` and `NtProtectVirtualMemory` but **not
  `NtQueryVirtualMemory`**, so the guest sees the trap.

The transition point is itself informative: the trap is armed when FEX **translates** the page, not when
the guest writes to it. And `VirtualProtect` restores `RWX` (previous reported as `0x20`), so this is a
protection change rather than a mapping change — which is why the byte-comparing memory probe never saw
anything.

### Why it matters

Aegis, AoE IV's anti-tamper, calls `NtQueryVirtualMemory` **35,248 times per run** (measured separately
from a 1 GB logcat capture). A page whose write permission has been silently removed is exactly the kind
of thing such a check exists to notice.

### Status

- **Confirmed:** the trap exists and is visible to the guest.
- **Not yet shown:** that Aegis acts on it. That needs the game run against the patched FEX
  (`fexcore-aoe-smcfix.wcp`) and judged by the only valid criterion — `warnings.log` still growing after
  five minutes.

The patch replaces the re-protection with `ForceFullSMCDetection`, which validates translated
instructions against guest memory at run time and needs no protection change at all — see
[`patches/fex/0001-hide-smc-trap-from-guest.patch`](../../patches/fex/0001-hide-smc-trap-from-guest.patch).

## Part 3: Hiding FEX's SMC trap does not stop the Aegis kill

Measured on the AYN Thor, 2026-10-06, 21:20 to 22:27. All runs on the clean baseline
(`WINEDEBUG` off, [WINEDEBUG-LEFTOVER.md](../research/WINEDEBUG-LEFTOVER.md)), launched by `tools/run_watch.py --launch`
and judged with `suspinfo` ([KILL-REMEASURED.md](../research/KILL-REMEASURED.md)).

**Result: with the trap hidden from the game, the kill fired in 5 of 5 runs.** Inside the game, 732,206
memory queries passed through the filter and not one of them touched a page the trap had changed. The SMC
trap leak ([part 2](#part-2-confirmed-fex-leaks-its-smc-write-trap-to-the-guest)) is a real FEX defect, but it is not what triggers the kill.

### The fix that was tested: patch 0004

[`patches/fex/0004-report-guest-protection-for-trapped-pages.patch`](../../patches/fex/0004-report-guest-protection-for-trapped-pages.patch),
against FEX `7d3090f` (the revision the device runs), built together with patch 0002 (CPUID) following
[BUILDING-FEX.md](../guides/BUILDING-FEX.md). The committed patch is the fix2 version described below; the "fix" build
is the same without fix2's two additions.

It keeps FEX's default `mtrack` trap, so self-modifying code is still caught, and corrects what the guest
reads back:

- **Where.** Wine's ARM64EC syscall stubs load their dispatcher from the exported pointer
  `__wine_syscall_dispatcher` on every call (`mov x8, #id; mov x9, x30; ldr x16, =ptr; ldr x16, [x16]; blr x16`,
  disassembled from the device's `ntdll.dll`). Both ways x64 code reaches a syscall end in those stubs: a call
  to the ntdll export, and a `syscall` instruction (Wine's `dispatch_syscall` jumps through its
  `arm64ec_syscalls` table to the same stubs). FEX replaces the pointer with a small filter
  (`SyscallDispatcherHook` in `Module.S`) that passes every syscall straight on, except two.
- **What.** `NtQueryVirtualMemory` (basic, capped and working-set-ex classes) and the old protection from
  `NtProtectVirtualMemory` report `PAGE_EXECUTE_READWRITE` for pages FEX itself trapped. A trapped page no
  longer splits the guest's region. FEX's own direct syscalls bypass the filter.
- **Which pages.** Only pages that were really writable when FEX trapped them, so a page the guest set to RX
  itself still reads back as RX. Any guest protection change, untrap or free clears the record.
- **Safety.** The filter installs only if the syscall number FEX derives for `NtQueryVirtualMemory` equals
  the `movz x8, #id` that ntdll's own stub starts with.
- **fix2** adds two things: queries through a real handle to the own process (not only `NtCurrentProcess()`)
  are corrected too, and counters (`SmcHideStats`, marker `SMCHIDE1`) can be read from outside with
  `tools/probes/fexstats.c`.

| build | SHA-1 | contents |
|---|---|---|
| ctl | `4bc9d3a9` | 7d3090f + 0002, source build (control) |
| fix | `2f578905` | ctl + 0004 |
| fix2 | `a5ec2726` | fix + own-process handles + counters |
| base | `460568b8` | the CPUID-patched binary that was on the device (run 1 in KILL-REMEASURED.md) |

### The fix works: `tools/research/smctest2.c`

Same probe, same session, only the DLL swapped. Raw output in
[`docs/research/samples/smc-trap-hidden-2026-10-06/`](../research/samples/smc-trap-hidden-2026-10-06/).

| check | ctl | fix |
|---|---|---|
| run code, then `VirtualQuery` the page | `RX` | `RWX` |
| region size from the first of 3 RWX pages | 0x1000 (split) | 0x3000 |
| `QueryWorkingSetEx` `Win32Protection` | `RX` | `RWX` |
| rewrite the code, run again (trap still catching writes?) | returns 43 | returns 43 |
| `VirtualProtect` old protection | `RX` | `RWX` |
| guest sets RX itself, then `VirtualQuery` | `RX` | `RX` |
| after RX/RWX round trip, rewrite and run | returns 44 | returns 44 |
| `VirtualQueryEx` through a real own-process handle (fix2 only) | | `RWX` |

Both builds catch every rewrite, so the trap is still armed under the fix. Only what the guest sees differs.

### The game: every run was killed

| run | FEX | start | session drop (`errno=10038`) | last log line | threads at suspend 1 | the `+0x3e69304` thread afterwards | end |
|---|---|---|---|---|---|---|---|
| 4 | fix | 21:28:55 | none | 21:30:58 | 61 | idle, 50 ms | hung |
| 5 | ctl | 21:35:49 | 21:37:52 | 21:38:04 | none seen | 30 ms | **exited** by 21:38:15 |
| A1 | fix | 21:41:09 | none | 21:43:12 | 60 | idle, 30 ms | hung |
| A2 | ctl | 21:46:50 | 21:48:57 | 21:49:44 | 59 | idle, 50 ms | hung |
| A3 | fix | 21:53:18 | 21:54:57 | 21:55:45 | 59 | **gone** (no such thread left) | hung |
| A4 | ctl | launch failed: GameNative hung on "Syncing cloud saves" | | | | | |
| A5 | fix | 22:04:12 | 22:06:03 | 22:07:05 | 60 | idle, 40 ms | hung |
| A6 | ctl | 22:14:30 | 22:16:02 | 22:17:32 | 60 | idle, 70 ms | hung |
| 7 | fix2 | 22:21:00 | 22:22:23 | 22:23:52 | 61 | idle, 60 ms | hung |

`suspinfo` sampled about once a minute, so the last log line is the best marker of the kill time: 2 min 3 s
to 3 min 2 s after start. With the trap hidden: killed in 5 of 5 runs. Control: killed in 2 of 3 valid
runs; the third exited instead, with no suspension seen in the sample 10 s before it was gone.

#### Inside the game: the filter was live, and no query ever saw the trap

Run 7 read the counters out of the game process every 30 s (`run7-fix2-fexstats.txt`):

| t after start | queries filtered | query results corrected | protect calls filtered | old protections corrected |
|---|---|---|---|---|
| 4 s | 12,254 | 0 | 25,710 | 9,845 |
| 64 s | 256,179 | 0 | 474,847 | 218,956 |
| 124 s | 492,193 | 0 | 879,518 | 410,081 |
| 184 s and later | 732,206 | **0** | 1,286,488 | 600,318 |

The marker `SMCHIDE1` was found in the game's `libarm64ecfex.dll`, so the filter ran inside the game. The
counters stop between 184 s and 214 s, which is when the game was frozen.

`query results corrected = 0`: across 732,206 `NtQueryVirtualMemory` calls on the game's own memory,
none was for a page the trap had changed. So in this run, no code in the game, Aegis included, could have
seen the trap through a memory query, and the kill still came.

The "old protections corrected" count is not evidence about the game: FEX's own trap calls go through the
same ntdll export and are counted too.

### What this settles, and what it does not

- **Settled:** the trap's visibility through `NtQueryVirtualMemory` and `NtProtectVirtualMemory` is not
  needed for the kill. Hypothesis 2 in the README, as stated, is ruled out.
- **Settled:** the session drop is not needed either. Runs 4 and A1 had no `errno=10038` and were killed on
  time.
- **Not covered by the fix:** timing (a trapped write costs a fault), and protection changes seen from
  another process. Neither is known to matter.
- **Observed, not explained:** after the suspension the `+0x3e69304` thread spins at 100 % on the base
  binary (run 1), sits idle on every source build, or is gone (A3). The suspension itself never varies.

### Side results

- `SMCChecks=full` and the no-trap build both stop at start-up ([KILL-REMEASURED.md](../research/KILL-REMEASURED.md));
  patch 0004 does not have that problem, because it adds no validation.
- Building FEX from a fresh clone of `7d3090f` with the three macOS fixes in BUILDING-FEX.md takes about a
  minute with `ninja arm64ecfex`.
