# Caching volatile self-rewriting code (patch 0020)

Lenin reports faster match loading in Age of Empires II: DE on an Honor Magic V2 with Snapdragon 8 Gen 2.
His reported loading time fell from 7–10 minutes to 1.5–2 minutes with the original patch 0020 build.
He reports stable 40 FPS with a 40 FPS cap and the main thread about half idle.
These are contributor measurements from 2026-10-27/28. They do not measure the review fixes below or higher uncapped FPS.

The exact baseline package needs confirmation. The original report names `aoe4-perf5-23` but also calls it the v1.6.0 set with 0017.
Those are different sets: perf5 ends at 0016; v1.6.0 is perf7 and includes 0017.
Do not treat the loading times as a controlled comparison until the baseline and test conditions are recorded.
The counters below come from diagnostic patches 0018 and 0019. Independent game and Thor measurements remain pending.

This builds directly on patch **0012** (volatile code regions) and **0014** (reuse of re-decrypted translations), and
is the same idea pushed further: 0012 stops write-trapping the 16 MB one-instruction buffer and shares
`[instruction][jmp]` slots by content; this patch extends content sharing to the cases 0012 left recompiling on every
visit, applies it to the image pages the protection rewrites, and removes the per-visit lock those visits were taking.

## What was still slow after 0012/0014

The contributor's diagnostic log reports these counters during match loading, before patch 0020:

- **~6,700 SMC write faults per second**, 95 % of them on a single image page (`AoE2DE_s.exe+0xf77000`). 0012 handles
  the *private* 16 MB buffer, but the protection also rewrites code in its own image pages, which stayed write-trapped:
  every rewrite faulted, invalidated and recompiled.
- **~13,500 `NtProtectVirtualMemory` and ~27,000 `NtQueryVirtualMemory` calls per second** from the protection.
- **~20,000 `c000001d` (illegal instruction) exceptions per second** at four fixed trap sites plus a per-instruction
  stepper.
- Every SMC fault took the `CodeInvalidationMutex` **exclusively**. While one thread invalidated, the others blocked in
  `CompileBlock`; the `PhoenixTaskManagerThread`s spent ~160 s each waiting during a load.
- Inside the stepper, the blocks 0012 could not share were recompiled on every visit: the four trap sites
  (~24,000/s — an invalid instruction decodes to size 0, so they fell out of the content cache), the
  `[instruction][jmp]` slots whose jmp returns into the protection's own (volatile) code (~24,000/s, excluded only
  because the target was volatile), and lone `ret` slots (~12,000/s, not the `[instruction][jmp]` shape at all).

0014 already keeps translations of re-decrypted code, but these volatile blocks are deliberately never put in the
lookup cache, so 0014 does not cover them.

## What the patch does

**Volatile image pages.** An image page that takes 16 write faults within one second is marked a *volatile code page*
(like 0012's private regions, `FEX_EXP_VOLPAGE=0` to disable). It is no longer write-trapped; its blocks are compiled
as ordinary multiblocks (loops stay inside one block) and cached by guest RIP plus a hash of the bytes of every
sub-block, re-checked on every entry against the page's current contents and the thread's code buffer. SMC faults on
these pages fell from ~6,700/s to **~170/s** and `NtProtect` calls from ~13,500/s to **~400/s**.

**Trap sites cached by their real window.** A trap the protection places (an instruction FEX cannot decode) ends a
block with decode size 0. The decoder now records how many bytes it examined before rejecting the instruction
(`FailExaminedSize`), and the content key uses exactly those bytes, only when the trap is the first instruction of the
entry block. The bytes *after* the trap — which can change between visits while the trap itself does not — are left out
of the key. The four trap sites stopped recompiling (~24,000/s → a handful).

**`[instruction][jmp]` with a volatile target shared.** 0012 shared these only when the jmp target was non-volatile.
A target in volatile code is now allowed: an exit to volatile code is never backpatched (`ExitFunctionLink` returns
without patching the stub for a volatile `GuestRIP`), so the block re-resolves its target through the dispatcher on
every execution — exactly what a fresh compile would do. The host code is identical wherever the slot sits, so it is
keyed by `(instruction bytes, absolute target)`.

**Lone `ret` / indirect `jmp` shared by content.** A one-instruction block whose exit target is only known at run
time — `ret`, `ret imm16`, or `jmp r/m` without a RIP-relative operand — produces host code that reads nothing of its
own address (`RETOp` pops the target and exits through the call-ret stack or the dispatcher; `JUMPAbsoluteOp` exits
through the dispatcher). Every copy of the same bytes shares one translation, keyed by the bytes and a dynamic-target
sentinel. Recognised by the dispatch-handler pointer, not the raw opcode. (`call` indirect is excluded: it pushes a
return address that depends on the slot's location.) In the stepper this collapsed ~12,000 recompiles/s to a couple
hundred.

**A per-thread lock-free cache.** Volatile code is never in the lookup cache and is re-validated against the guest
bytes on every visit, so the `CodeInvalidationMutex` protects nothing for it. A small direct-mapped per-thread cache
sits in front of the shared one, checked at the top of `CompileBlock` before the mutex. An entry is valid only while
the thread's code buffer, an executable-range epoch (bumped whenever any memory stops being executable), and the byte
hash all still match. In a loading match it served ~73 % of volatile visits (~135,000/s) with no lock taken.

### Correctness guards

- Not shared under the trap flag (single-step produces different code) or under `SMCChecks=full` (full SMC checks embed
  each instruction's guest address).
- A translation only enters the content cache if the decode it was compiled from still hashes to the decode the key was
  taken from. If another thread rewrites the slot in between, the code is correct for that one visit but is not cached
  under a key it no longer matches.
- Exits to volatile targets are never backpatched (relied on above), so a shared block never holds a stale pointer into
  a volatile target's old translation.
- An ordinary block stops before volatile code. A crossing entry instruction uses FEX's existing runtime SMC checks.
  Further instructions on the volatile page exit through the dispatcher.
- Diagnostic byte samples read no more than the decoded block size. Failed decodes with no valid bytes produce a zero sample.
- Translation reuse checks the SMC policy, decode status, and examined-byte count as well as the instruction bytes.
  A failed entry also hashes the accessible bytes that caused the rejection.
  Disk-cache hits that touch volatile pages require a fresh compile.
- Protection changes still invalidate cached translations and reset decoder permissions, including on volatile pages.
- Invalid crossing instructions validate their examined bytes. Execute-permission failures do not read inaccessible bytes or request zero-byte validation.
  An invalid tail only ends the block. Its trap and byte validation belong to a separate compile at that address.

## Diagnostics (patches 0018 and 0019)

This research series requires both diagnostic patches before 0020. Logging starts only when `FEX_EXP_STATSLOG` is set.
Keep the current release packages unchanged. A future production package needs a separate runtime-only patch series and game tests.

- **0018** writes a per-second log (`FEX_EXP_STATSLOG=<folder>`, `1` = the Download folder; `FEX_EXP_STATSHZ` sets the
  sampling rate): per-thread CPU, JIT time and count, SMC faults, exception time, and a sampled RIP histogram of the
  busiest threads (suspend + `NtGetContextThread`, as a profiler does).
- **0019** adds the observers the log reads: SMC-fault pages with the writing RIP, volatile compiles, disk-cache
  reloads, and guest exceptions by code and site.

Image lookup queries memory regions instead of reading PE headers that can disappear during DLL unload.
Exception keys keep the complete status code and instruction address. The log prints all eight hexadecimal status digits.

## Upstream

The volatile-code-region idea (0012/0014) is FEX issue
[#6022](https://github.com/FEX-Emu/FEX/issues/6022). This patch is an extension of the same approach and would belong
in the same discussion before a clean FEXCore PR; nothing in it is specific to this game beyond the heuristic that
promotes a frequently-rewritten page to a volatile one.

## Build

Applied after the v1.6.0 set, in order: 0004, 0006, 0007, 0009, 0010, 0012, 0013, 0014, 0015, 0016, 0017, then 0018,
0019, 0020, then `ninja arm64ecfex`. The source base is `7d3090f78237267b2adf2d32116f0105b8d665cb`.
Use the toolchain and host setup in [BUILDING.md](../guides/BUILDING.md).

The original contributor package is `fexcore-aoe2-volpage6.wcp`, version `aoe2-volpage6-40`.
Its SHA-256 is `1b5018a7c54f0b3f9eaa67f81d338a4e727e9787fdfd6a1235a96f2a9f43588f`.
Its DLL SHA-256 is `aaa42012fb54e1db508e4789fcb333f6b6835b7e6de51ab4339038ade5875ce4`.
That package predates the review fixes. It is not a build of the fixed source.
The first macOS rebuild differs beyond timestamps and uses different toolchain runtime files.
An exact match needs the contributor's toolchain, flags, source revision, and submodule revisions.

## Source regression checks

Run these checks from this repository after applying the complete series to a FEX checkout:

```sh
python3 tools/tests/test_volatile_patches.py /path/to/patched/FEX
```

The host needs Python 3 and a C++20 compiler. The script uses the FEX checkout's existing fmt headers.
The tests compile the affected production source fragments with small compatibility stubs.
They check boundary reads, crossing entry instructions, inaccessible image headers, and complete exception codes.
They also check reuse policy, disk-cache filtering, protection changes, and failed-decode validation.
They do not execute Windows/FEX or measure game performance.
