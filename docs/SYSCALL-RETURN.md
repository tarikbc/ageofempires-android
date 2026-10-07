# Registers after a raw `syscall`: Wine ARM64EC differs from hardware (FEX patch 0006)

2026-10-06/07. Aegis issues raw x64 `syscall` instructions from code outside every module
([AEGIS-TRACE.md](AEGIS-TRACE.md)). So what those instructions leave in the registers is something it can
check, the same way it can check timing.

## What hardware does

`SYSCALL` copies the return address into `rcx` and `rflags` into `r11`; `SYSRET` returns through them. After
any syscall on x86-64, `rcx` holds the address of the next instruction and `r11` the flags. This is defined by
the instruction set, not by Windows.

## What the Thor does (measured)

Under Wine ARM64EC an x64 `syscall` is emulated: FEX reports it, Wine's `dispatch_syscall` resumes the x64 code
at Wine's helper `invoke_arm64ec_syscall`, which calls the syscall like a function and returns.

[`tools/probes/syscallregs.c`](../tools/probes/syscallregs.c) loads marker values, issues one raw
`NtYieldExecution` (number taken from ntdll's own x64 stub: `0x46`), and reports every register afterwards.
Run in the live container through winhandler, twice with the baseline FEX `460568b8`, once with patch 0006
(`d6adae8a`):

| register | hardware | baseline FEX `460568b8` (2 runs, same result) | with patch 0006 |
|---|---|---|---|
| `rax` | status | `0x40000024` | `0x40000024` |
| `rcx` | return address `0x1400016cb` | **`0x40000024`** (the status) | `0x1400016cb` |
| `r11` | `rflags` (`0x212` before) | `0x212` | `0x212` |
| `rdx` | not defined by the instruction | **changed** (a stack address) | kept |
| `r10` | not defined by the instruction | **`0x1400016cb`** (the return address) | kept |
| `r8`, `r9` | not defined by the instruction | kept | kept |
| `xmm0`-`xmm5` | not defined by the instruction | kept | kept |

So on the stock setup, code that checks `rcx == return address` after its own syscall sees a difference from
any real x86-64 machine. Whether Aegis checks this is **not known**.

## Patch 0006

[`patches/fex/0006-syscall-return-registers.patch`](../patches/fex/0006-syscall-return-registers.patch), on top
of 0004. At start-up FEX finds `invoke_arm64ec_syscall` in ntdll by its first 15 bytes (exactly one match is
required, else the patch does nothing and logs why), reads the address of Wine's `arm64ec_syscalls` table from
its `lea`, writes a replacement into a new executable page and puts a 14-byte jump at the start of Wine's
helper. The replacement ([`patches/fex/syscall_return_fix.s`](../patches/fex/syscall_return_fix.s)) copies 14
stack arguments, calls the table entry, and returns with `rcx` = return address, `r11` = flags, and every other
register except `rax` as it was.

The design is the one in [`patches/proton-arm64ec-ntdll/invoke_arm64ec_syscall.s`](../patches/proton-arm64ec-ntdll/invoke_arm64ec_syscall.s),
an ntdll binary patch that never ran, because Wine loads ntdll from its own tree
([NTDLL-NEVER-LOADED.md](NTDLL-NEVER-LOADED.md)). Patch 0006 is the first time this fix is live. The old notes
say Wine's helper also changes `r8` and `r9`; the probe above shows them kept.

Limits, by construction: a syscall with more than 18 arguments would get garbage beyond the 18th; for a thread
inside a syscall, the x64 return address on its stack now points into the new page, which belongs to no
module, instead of into ntdll.

With it installed, `winhandler`, `suspinfo` and `syscallregs` work (checked 2026-10-06).

## The game with patch 0006

Runs so far (A/B against fix2 in progress):

| run | FEX | start | session drop | last log line | last loading step | threads at suspend 1 | end |
|---|---|---|---|---|---|---|---|
| s1 | 0006 | 23:53:28 | 23:55:39 | 23:57:00.887 (3 min 32 s) | `Scenario Lua System` | 59 | hung |
| ab2-1 | 0006 | 00:01:45 | 00:03:44.774 | 00:04:26.118 (2 min 41 s) | `Load Resources from Precache` | none seen | **exited** about 00:04:26 |

Run ab2-1 exited like control run 5 in [SMC-TRAP-HIDDEN.md](SMC-TRAP-HIDDEN.md). GameNative logged
`Exit called: processes_exited` at 00:04:26.610 and closed the container. The game's own
`LogFiles\unhandled.<start>.txt` for that run holds only lag warnings, no crash record. The exit code was not
recorded; [`tools/probes/waitexit.c`](../tools/probes/waitexit.c), now started by `run_watch.py`, records it
and copies the log at the moment of an exit.
