# Hiding FEX's SMC trap does not stop the Aegis kill

Measured on the AYN Thor, 2026-10-06, 21:20 to 22:27. All runs on the clean baseline
(`WINEDEBUG` off, [WINEDEBUG-LEFTOVER.md](WINEDEBUG-LEFTOVER.md)), launched by `tools/run_watch.py --launch`
and judged with `suspinfo` ([KILL-REMEASURED.md](KILL-REMEASURED.md)).

**Result: with the trap hidden from the game, the kill fired in 5 of 5 runs.** Inside the game, 732,206
memory queries passed through the filter and not one of them touched a page the trap had changed. The SMC
trap leak ([SMC-CONFIRMED.md](SMC-CONFIRMED.md)) is a real FEX defect, but it is not what triggers the kill.

## The fix that was tested: patch 0004

[`patches/fex/0004-report-guest-protection-for-trapped-pages.patch`](../patches/fex/0004-report-guest-protection-for-trapped-pages.patch),
against FEX `7d3090f` (the revision the device runs), built together with patch 0002 (CPUID) following
[BUILDING-FEX.md](BUILDING-FEX.md). The committed patch is the fix2 version described below; the "fix" build
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

## The fix works: `tools/smctest2.c`

Same probe, same session, only the DLL swapped. Raw output in
[`samples/smc-trap-hidden-2026-10-06/`](../samples/smc-trap-hidden-2026-10-06/).

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

## The game: every run was killed

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

### Inside the game: the filter was live, and no query ever saw the trap

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

## What this settles, and what it does not

- **Settled:** the trap's visibility through `NtQueryVirtualMemory` and `NtProtectVirtualMemory` is not
  needed for the kill. Hypothesis 2 in the README, as stated, is ruled out.
- **Settled:** the session drop is not needed either. Runs 4 and A1 had no `errno=10038` and were killed on
  time.
- **Not covered by the fix:** timing (a trapped write costs a fault), and protection changes seen from
  another process. Neither is known to matter.
- **Observed, not explained:** after the suspension the `+0x3e69304` thread spins at 100 % on the base
  binary (run 1), sits idle on every source build, or is gone (A3). The suspension itself never varies.

## Side results

- `SMCChecks=full` and the no-trap build both stop at start-up ([KILL-REMEASURED.md](KILL-REMEASURED.md));
  patch 0004 does not have that problem, because it adds no validation.
- Building FEX from a fresh clone of `7d3090f` with the three macOS fixes in BUILDING-FEX.md takes about a
  minute with `ninja arm64ecfex`.
