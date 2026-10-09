# Kill analysis — 2026-10-06 run (caught the suspending thread)

> **Archived.** The first captured kill (2026-10-06). The cause was found the next day: the protection's start-up hook check failed on Wine's ARM64EC export stubs ([HOOK-CHECK.md](../../how-it-works/HOOK-CHECK.md)) and its watchdog fired on slow exceptions ([WATCHDOG.md](../../how-it-works/WATCHDOG.md)). The ntdll wait-queue lead below was void ([NTDLL-NEVER-LOADED.md](NTDLL-NEVER-LOADED.md)). "The README" here is the first README (git `7e7ae2a`). The raw files are in [samples/kill-2026-10-06](../samples/kill-2026-10-06) (`si_1.txt`, `si_2.txt`, `tctx_1.txt`, `tstack_kill.txt` and `watch.txt`; the other `si_*` files were not kept). The story: [STORY.md](../../STORY.md).

This run reproduced the freeze and, for the first time, captured the **exact moment** the
protection suspends the process's threads. It identifies the protection thread and localizes the
protection code region in `RelicCardinal.exe`.

## Method (all automated)

New tools in `tools/` (see below) drive the whole thing over adb:

- `tools/gn_nav.py` — navigates the GameNative UI (uiautomator dump + input tap) to launch the game.
- `tools/research/run_experiment.py` + `cp.bat` / `snap.bat` / `tstack.bat` — polls the game's
  `warnings.log` (via winhandler → `D:\`) for the trigger, then samples thread state in the kill
  window.

## Timeline (device clock, 07:41 launch)

| Time | Event |
|---|---|
| 07:41:55 | `RelicCardinal.exe` starts (build 24231237, Bionic Steam, original `ntdll.dll`). |
| 07:44:04 | Menu / online presence working (GetPartyStatsByID, PresenceMessage). |
| 07:44:10 | Match key created; the user starts a skirmish (`no_win_condition`, human + 2 AI). |
| **07:44:15.237** | **Trigger:** `TlsConnection::Shutdown … errno=10038`, then websocket `statusCode=1006` + reconnect. |
| 07:44:22 | Match load proceeds (`Default World`, `FXReflection`, …), then the game log goes silent. |
| ~07:45:15–07:45:23 | **Kill:** all threads suspended. `si_1.txt` catches it live. |
| 07:45:29+ | ~57 of 61 threads are gone; 4 remain, one spinning at 100 % CPU. Game hangs. |

The ~65 s gap between the `10038` and the kill matches the README's "about one minute later".

## Finding 1 — the protection thread is identified

At the kill instant (`si_1.txt`), every one of the 61 threads is suspended (`suspend=1`)
**except one**:

```
tid 0174 suspend=0 user=45400ms start=RelicCardinal.exe+3e69304 name=[]
```

Everything else is frozen in `ntdll.dll` wait code (`+c63f4`, `+c6448`, `+c6480`, `+c5178`).
`tid 0174` is the thread that suspends all the others.

- It is a **persistent thread created at launch**: the earlier healthy snapshot `si_12.txt`
  (previous session) shows the same entry point `RelicCardinal.exe+3e69304` sitting at
  `user=0ms` — dormant until the trigger.
- After the kill its user time climbs continuously (45400 → 73570 → 205600 ms) while every
  other thread's user time is frozen, i.e. **it becomes the 100 %-CPU spinner**.
- It **cannot be suspended/queried**: `tctx.exe` (which does `SuspendThread`+`GetThreadContext`)
  hangs the moment it reaches `tid 0174`. Only memory reads (`tstack.exe`, no suspend) work.

## Finding 2 — the protection code region is localized

`tstack.exe` read `tid 0174`'s stack without suspending it. The return addresses on its stack
cluster in a contiguous `RelicCardinal.exe` region:

```
+3e46f01 +3e563ab +3e56ba1 +3e6cd0b +3e6db0b +3e69304 (entry) +3e7232c +3e724cc +3e7252c +3e7254c
+3f2b774 +3f2c000   +3f77392 +3f90aaf   …   +754a800
```

So the protection lives around **`RelicCardinal.exe +0x3e40000 … +0x3f90000`** (≈1.5 MB) plus
`+0x754a800`. Its stack is a 4 MB region (base `0x4C90000`, limit `0x4892000`), consistent with
the previous session's "spinner stack" observation.

## Finding 3 — the spinner is in the RtlWaitOnAddress region

`tid 0174`'s stack is full of `ntdll.dll` pointers in the wait-queue code, exactly the area the
`waitq_fix` patch touches:

- `ntdll+ce404 / +ce594 / +ce5fc` — right around the `RtlWakeAddressAll` spinlock site
  (`+ce524` in `selfchk.c` / `apply.py`), and
- `ntdll+156e30` — the 256-bucket `RtlWaitOnAddress` hash table (`waitq.c` default).

This is the first direct evidence that the protection thread actually traverses Wine's
`RtlWaitOnAddress`/`RtlWakeAddress*` wait queues while it spins — the same code the `waitq_fix`
patch makes suspension-safe. Worth revisiting: the patch was tested against the *freeze* and
"did not stop it", but this trace shows the thread is genuinely in that code path, so the
spinlock deadlock (suspend-while-holding-a-bucket) remains a live candidate for the *hang*
(even if the protection also does other damage first).

## Finding 4 — the kill is two-phase

- Phase 1 (`si_1`, 07:45:23): 61 threads all suspended, `tid 0174` running.
- Phase 2 (`si_2`+, 07:45:29): only 4 threads remain — `0138` (Game/Main Thread), `0154`,
  `0170`, `0174`. The other ~57 are gone (terminated / stack removed).

## What this means / next steps

The "who" and "where" are now pinned. The open question is still the "why" — which check in the
`+0x3e4xxxx…+0x3f9xxxx` region decides to kill. Options, in order:

1. **Reverse the localized code.** The addresses above now anchor the `aegis_code.bin` dump (or a
   fresh dump of the `RelicCardinal.exe` image in that range) so Ghidra/rizin work becomes
   targeted instead of scanning a 5 MB blob.
2. **Test the waitq deadlock theory again**, now that we know the spinner is really in
   `RtlWaitOnAddress`/`RtlWakeAddress`: run the same capture with `ntdll.dll.waitqfix` loaded and
   see whether `tid 0174` still spins in `+ce5xx`/`+156e30` or moves past it.
3. **Instrument the protection entry** (`+3e69304`): set a hardware/int3 breakpoint (dbgguard) on
   that address to catch the first instruction it executes when it wakes up at the kill.

## The executable is packed: `.text` is encrypted at rest

Compared the **on-disk** `RelicCardinal.exe` (copied out of the Wine prefix) against a **memory**
dump of the loaded image. They do not match — the code is decrypted at load time:

| Section | Identical to disk |
|---|---|
| `.text`  | **~21 %** (79 % of pages differ) |
| `.rdata` | ~91–97 % (the differing part is the loader-filled IAT) |
| `.pdata` | **100 %** |
| `.rdata` import descriptors | **100 %** |

Evidence it is encryption and not corruption:

- 4 KB entropy: **7.15 bits/byte on disk vs 6.10 in memory** — and where the two differ, memory
  holds **valid x86-64** while disk holds random bytes.
- `0xCC` (`int3`) padding bytes are **byte-identical** between disk and memory, while the
  instruction bytes around them differ — i.e. the packer encrypts instructions but leaves padding.
- The PE headers, checksum (`0x807d142`), section table, entry point and `.pdata` are intact, so
  the image is otherwise untouched.
- The file carries a **10 MB high-entropy overlay** (10,049,828 bytes) after the last section.

The entry point `+0x4fb0884` and the single TLS callback `+0x4fb0b6c` are **unencrypted stock CRT
code** (the TLS callback is the normal `_initterm` dynamic-initializer loop), so the decryption is
not driven from the normal CRT startup path.

**Consequence:** the whole `+0x3e4xxxx…+0x3f9xxxx` analysis below is performed on the **decrypted**
image, so it is valid. It also means an *unpacked* image is trivially reconstructible — the memory
dumps are exactly that.

## Import map (1298 imports resolved)

Parsed the import descriptors from a `.rdata` dump so IAT slots can be named
([`tools/research/impmap.py`](../../../tools/research/impmap.py)). Thread/memory APIs actually used by the game:

| API | IAT slot | Call sites in `.text` |
|---|---|---|
| `SuspendThread`   | `0x1456de648` | **2** — `+0x49065a1`, `+0x490699d` |
| `ResumeThread`    | `0x1456df968` | 11 — incl. `+0x49065d1`, `+0x4906761`, `+0x49069cd` |
| `OpenThread`      | `0x1456de560` | 1 — `+0x3b1e5e7` |
| `CreateToolhelp32Snapshot` | `0x1456de820` | 20 |
| `VirtualFree`     | `0x1456df8f8` | 64 |
| `VirtualProtect`  | `0x1456df908` | 28 |

The two `SuspendThread` sites are thin `thiscall` wrappers in a thread-object class:

```
+0x4906590  mov rax,[rcx+8] ; mov rcx,[rax+0x10] ; test rcx,rcx ; je …
+0x49065a1  call [SuspendThread] ; cmp eax,-1 ; setne al ; ret
+0x49065c0  … same shape … +0x49065d1 call [ResumeThread]
```

Neither wrapper has a direct `call` — they are reached through function pointers, which is why the
protection's suspend-all path is not visible as a plain call graph.

## Files

- `docs/research/samples/kill-2026-10-06/si_1.txt` — kill-moment suspend snapshot (the smoking gun).
- `docs/research/samples/kill-2026-10-06/si_2.txt` — post-kill state (4 threads; `si_3` to `si_22` were not kept).
- `docs/research/samples/kill-2026-10-06/tctx_1.txt` — tctx, truncated where it hangs on `tid 0174`.
- `docs/research/samples/kill-2026-10-06/watch.txt` — the game's warnings.log at capture time (trigger at line 559).

## Reverse engineering (same run) — the protection is readable and uses xxHash64

Dumped the localized region from a live run with the new [`tools/research/dumprange.c`](../../../tools/research/dumprange.c)
probe: `RelicCardinal.exe +0x3e00000..+0x4000000` (2 MB) and `+0x7540000..+0x7560000` (128 KB).
Disassembling the thread entry and its call-chain addresses shows the protection is **ordinary MSVC
x86-64 with `/GS` stack canaries** (`__security_check_cookie` at `+0x44fb0d50`) — **not** a
VM/obfuscator. The earlier `aegis_code.bin` looked like junk only because it is an unanchored blob.

What the code does:

- **Thread entry** `+0x3e69304` → real routine at `+0x3e69309`: generates randomness
  (`call +0x3f765f0` twice, `not/shl/xor` mixing, then `div` = modulo) and reads `[arg+0x1a8]`
  / `[arg+0x1ac]` counters → the checker picks **random targets/times**.
- **xxHash64** — `+0x563cc` and `+0x56bc0` load the exact xxHash64 primes
  (`0x9E3779B185EBCA87`, `0xC2B2AE3D27D4EB4F`, `0x165667B19E3779F9`,
  `0x85EBCA77C2B2AE63`, `0x27D4EB2F165667C5`) and run the round mix → the protection hashes a
  memory region.
- **A repeated ntdll call pair** — 34 indirect `call [rip+…]` sites in the 2 MB region resolve to
  three IAT slots; two of them (`0x1456dfa68`, `0x1456dfa70`) are called **16× each** and point at
  `ntdll+0x907a0` / `+0x922e0`. That ntdll neighborhood is `A_SHA*`/`MD4`/`MD5` plus
  `RtlQueryProcessDebugInformation` — a hash/debug helper, **not** `NtSuspendThread`
  (`+0x676a0`). So the actual suspend/free calls are elsewhere (dynamic resolution, or outside the
  dumped window).

### Structure refinements (full `.text` dumped)

- `.text` is 91 MB (RVA `0x1000..0x056dd000`); the xxHash64 routines have **no direct `call
  rel32` callers anywhere in `.text`** → they are reached through a function pointer/vtable, i.e.
  the protection is a C++ object graph, not a flat call tree.
- `+0x754a800` is in `.data` (`0x7542000..`), so that stack entry is a **data pointer**, not code.
- The thread entry `+0x3e69309` is a **one-shot setup wrapper**: it seeds a random value (rdtsc ×2,
  mixed with `not/shl/xor`), then `call [rdi+8]` — the **virtual function that is the real
  protection loop** — and finally frees the object (`[rdi]`, `[rdi+0x1b0]`, `[rdi+0x1b8]`). The
  actual check loop lives behind that pointer (`rdi` = the thread's argument object, with fields at
  `+0x10`, `+0x1a8`, `+0x1ac`, `+0x1b0`, `+0x1b8`).

### Working hypothesis (the "why it fails")

The protection periodically computes **xxHash64 (and/or SHA) over a memory region** and compares it
to an expected value; a mismatch triggers the suspend-all. The most likely hashed region is
**`ntdll.dll` (or the protection's own image)** — whose bytes differ between Wine's **ARM64EC**
build (Thor) and Wine's **x86-64** build (Mac/Rosetta). That is precisely why the same check "acts
differently under FEX/ARM64EC and under Rosetta".

### Next step

Trace the callers of the `+0x563cc` / `+0x56bc0` xxHash64 routines (they are reached indirectly or
from outside the 2 MB window) to learn the **hashed address range and the expected hash**. Then the
fix is either (a) patch the stored expected hash, or (b) make the hashed region match (e.g. run an
x86-64 `ntdll` under FEX).

### Files

- `tools/research/dumprange.c` — the memory-dump probe (built as `dumprange.exe`, pushed to `D:\`).
- `tools/research/disasm.py` — capstone disassembler for the dumps (`disasm.py <dump> <base> <rva>…`).
- `prot_main.bin` / `prot_754.bin` — the raw dumps (2 MB + 128 KB; regenerable, not committed).


## Update (round 21): the hash routines located by their constants

The next step above said to trace the callers of the "`+0x563cc` / `+0x56bc0` xxHash64 routines".
Those offsets did not hold up, so the routines were found the reliable way — **by searching for the
xxHash constants** — and the result is better than expected.

### Aegis carries its own hash implementation

The xxHash64 and xxHash32 primes cluster in two places. One is the game engine's own implementation
(RVAs `0x3ddd3xx`–`0x3ddd9xx`). **The other is inside Aegis's region:**

```
PRIME64_4  0x85ebca77c2b2ae63   at RVA 0x3e42d9c, 0x3e43064, 0x3e432e7
PRIME64_5  0x27d4eb2f165667c5   at RVA 0x3e42db3, 0x3e4307d, 0x3e43300
```

The function at **RVA `0x3e42d34`** loads all eight primes into stack slots and hashes in 4-lane SIMD
(`pmuludq` / `paddq`) blocks of 1 KB — it computes `(len - 1) >> 10` as its outer count. Signature:
`(rcx = pointer, rdx = length)`, returning a digest.

**Each of these hash functions has exactly one direct caller**, e.g.:

| hash routine | called from | note |
|---|---|---|
| `0x3e42d34` | `0x3e439a7` (in function `0x3e43694`) | |
| `0x3e563cc` | `0x3e5703f` | matches the "`+0x563cc`" from earlier notes |
| **`0x3e681cc`** | **`0x3e68e3f`** | **~1.2 KB before the kill thread entry `0x3e69304`** |

That last row is the interesting one: a hash call immediately upstream of the kill thread's entry point.

### The calls take a fixed high-entropy blob

Every call site has the same shape:

```asm
lea  r9, [rip + 0x189310d]        ; -> RVA 0x56fbf40
mov  qword ptr [rsp+0x20], 0xc0   ; size 192
xor  r8d, r8d
call 0x3e681cc                    ; the hash routine
```

RVA `0x56fbf40` is in `.rdata` and is **192 bytes of high-entropy material** (138/256 distinct byte
values) — 24 qwords, or six 32-byte values:

```
+0x00  b8 fe 6c 39 23 a4 4b be 7c 01 81 2c f7 21 ad 1c
+0x10  de d4 6d e9 83 90 97 db 72 40 a4 a4 b7 b3 67 1f
...
+0xb0  45 cb 3a 8f 95 16 04 28 af d7 fb ca bb 4b 40 7e
```

It is passed *into* the hash, so it is input (key/salt/blinding material) rather than an expected
digest. Where the returned value is compared is still unresolved.

### Why manual tracing stalls here

`0x3e43694`'s callers are a generated stub table — 68 entries spaced about `0x927` apart. That is
Aegis's obfuscation, and following it by hand is the wrong approach. Ghidra is running on
`RelicCardinal.unpacked.exe` to resolve the parameter flow properly.

### Method note

The repo's stated next step was correct; what failed was trusting remembered offsets. **Locate code by
its constants, not by addresses quoted from an earlier session** — `tools/research/callers.py` does the
enclosing-function and caller lookup once a routine is found.


## Update (round 22): what the hash actually digests, and a trap in `text.bin`

### The signature record

Tracing one of the 68 stub call sites shows exactly what is hashed. At `0x3de8040..0x3de80b9`:

```asm
lea  rdx, [rbp+0xe0]
lea  rcx, [rbp+0x2a8]
call 0x3f76f5c                     ; fills [rbp+0x2a8]
xorps xmm0, xmm0
movups [rbp+0x128], xmm0           ; zero 32 bytes
movups [rbp+0x138], xmm0
mov  rax, [rbp+0x2a8]
mov  [rbp+0x118], rax              ; record[0..8)   = qword A
call 0x3f765f0
mov  [rbp+0x120], rax              ; record[8..16)  = qword B
mov  r8d, 0x440313
lea  rdx, [rbp+0x128]
lea  rcx, [rip + 0x3d0f172]        ; -> RVA 0x7af8204 (.data)
call 0x3ddea6c                     ; writes 32 bytes -> record[16..48)
mov  edx, 0x30                     ; length = 48
lea  rcx, [rbp+0x118]
call 0x3e43694                     ; hash(record, 48)
mov  rcx, rax
shr  rcx, 0x20
xor  rax, rcx                      ; fold 64 -> 32 bits
mov  [rip + 0x3d0b68f], rax        ; store to .data (~RVA 0x7af3748)
```

So each record is **two qwords plus a 32-byte digest**, and 48 = 8 + 8 + 32 exactly. The result is
folded to 32 bits and written into a `.data` table. With 68 call sites of this shape, Aegis is
**building per-module signature records** — which is consistent with the integrity hypothesis, and
notably the game loads 85–86 modules.

### Trap: `text.bin` has Aegis's self-modification baked in

`call 0x3ddea6c` is a `call` whose target holds **high-entropy data, not code**:

```
RVA 0x3ddea50: ... 5e c3 c2 00 00 cc          <- end of the previous function (pop rsi; ret)
RVA 0x3ddea6c: d5 d8 be e7 9e d9 5f 9d 7d fe c4 90 ...   <- data
```

That is only possible if the bytes were **overwritten at runtime**. `text.bin` was recovered from the
live process, so Aegis's self-modification is frozen into it, and any `call` into such a region looks
nonsensical statically. The original `.text` on disk is still packed and differs.

**Consequence: static analysis of the restored `.text` alone will keep hitting walls like this.**
Cross-check any surprising control flow against the on-disk image, and prefer runtime observation for
the self-modified parts.

### Also checked

The SHA-256 round constants (`K[0..3]` = `0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5`) appear at
RVAs `0xb4c0`, `0xb4d0`, `0x5bdc0`, `0x5bdd0`, `0x68820` — **none inside Aegis's region**. The SHA-256
initialisation vector does not appear as a contiguous 4-dword run anywhere, so the 32-byte digest is
not obviously a stock SHA-256.


## Update (round 23): the dispatch is runtime-built — static analysis is exhausted here

Chased every statically-visible lead to the kill, and they all dead-end the same way.

**Zero references, as 8-byte pointers, anywhere in the unpacked image:**

| address | what it is | 8-byte refs |
|---|---|---|
| `0x3e69304` | kill thread entry | 0 (only a `.pdata` RUNTIME_FUNCTION) |
| `0x3e6b53c` | kill function | 0 |
| `0x4906590` | `SuspendThread` thunk A | 0 |
| `0x4906990` | `SuspendThread` thunk B | 0 |

The `SuspendThread` sites are thin thunks with no direct callers:

```asm
0x4906990: sub rsp, 0x28
0x4906994: mov rcx, qword ptr [rcx + 0x10]   ; handle out of a context struct
0x490699d: call qword ptr [rip + ...]         ; SuspendThread via IAT
```

So every piece of kill machinery is reached through a dispatch table that **Aegis builds at runtime**. There
is no static function-pointer table to follow, and the kill thread's entry is never passed literally to
`CreateThread`. Combined with the self-modification trap found in round 22, this means:

> **The hash-versus-expected comparison cannot be located statically.** The control flow is
> runtime-resolved, and the code bytes in the recovered image are partly runtime-written.

### Structure notes gathered on the way

- The `.data` global at RVA `0x7af3748` (where a computed signature is stored) has **474 readers**,
  clustered in a regular pattern — shared Aegis state rather than a signature table.
- The evenly-spaced caller tables (32 callers ~`0x241` apart; 68 callers ~`0x927` apart) read as **the
  same check function instantiated once per protected module**, not as a VM handler table.
- The 48-byte records are therefore one per module, which is consistent with the game's 85–86 loaded
  modules.

### Consequence: the next step has to be runtime

Static work has taken this as far as it goes. The remaining move is **in-process instrumentation**:
patch the hash routine's entry (or its single call site at `0x3e439a7`) so it records `(rcx, rdx)` —
the pointer and length — and then continues into the original code. That answers "what does Aegis
hash" directly, without a debugger, which matters because Aegis is anti-debug.

Risk to weigh: Aegis verifies its own image, so a patch could itself trip the check. Worth trying, and
informative either way — if the game dies earlier when patched, that is itself a result.


## Ghidra result (round 31, job started round 24)

Ghidra 11.3.1 finished a full auto-analysis of `RelicCardinal.unpacked.exe` (4055 s, ~68 min) and the
decompiler was pointed at the kill path and the two hash routines. The output was in
`tools/ghidra_decomp.txt` (248 KB). That file left the repo on 2026-10-07: it is decompiled game code.

**The kill path cannot be decompiled, and that is itself the result:**

```
=== 0x143e69304  kill thread entry (+0x3e69304)
no function contains this address

=== 0x143e6b53c  kill function (+0x3e6b53c)
/* WARNING: Control flow encountered bad instruction data */
void FUN_143e6b53c(void) {
  halt_baddata();
}
```

A full commercial-grade decompiler, given the restored image, finds **no function** at the kill thread
entry and **no valid instructions** in the kill function. That independently confirms rounds 22–23 from a
different direction: the bytes there are not executable code as they stand, because Aegis writes them at
runtime. My hand-rolled disassembly was never going to succeed there, and neither was Ghidra's.

This is the strongest evidence yet for the runtime-only conclusion — and, read alongside the SMC
findings, for the idea that the protection's own self-modification is central rather than incidental.

**The hash helper, by contrast, decompiles cleanly.** RVA `0x563cc` resolves to
`FUN_140055940` (starting at `0x140055940`), 2839 lines, signature:

```c
void FUN_140055940(undefined1 (*param_1)[32], undefined8 *param_2, uint param_3)
```

`param_1` is a pointer to **32-byte objects**, which matches the 48-byte signature records (two qwords
plus a 32-byte digest) traced in round 22. So the hashing is ordinary code and fully analyzable; it is
only the control flow around it that is obscured.
