# Kill analysis — 2026-10-06 run (caught the suspending thread)

This run reproduced the freeze and, for the first time, captured the **exact moment** the
protection suspends the process's threads. It identifies the protection thread and localizes the
protection code region in `RelicCardinal.exe`.

## Method (all automated)

New tools in `tools/` (see below) drive the whole thing over adb:

- `tools/gn_nav.py` — navigates the GameNative UI (uiautomator dump + input tap) to launch the game.
- `tools/run_experiment.py` + `cp.bat` / `snap.bat` / `tstack.bat` — polls the game's
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

## Files

- `samples/si_1.txt` — kill-moment suspend snapshot (the smoking gun).
- `samples/si_2.txt … si_22.txt` — post-kill state (4 threads).
- `samples/tctx_1.txt` — tctx, truncated where it hangs on `tid 0174`.
- `samples/watch.txt` — the game's warnings.log at capture time (trigger at line 559).

## Reverse engineering (same run) — the protection is readable and uses xxHash64

Dumped the localized region from a live run with the new [`tools/dumprange.c`](../tools/dumprange.c)
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

- `tools/dumprange.c` — the memory-dump probe (built as `dumprange.exe`, pushed to `D:\`).
- `tools/disasm.py` — capstone disassembler for the dumps (`disasm.py <dump> <base> <rva>…`).
- `prot_main.bin` / `prot_754.bin` — the raw dumps (2 MB + 128 KB; regenerable, not committed).
