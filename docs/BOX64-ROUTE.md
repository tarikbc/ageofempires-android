# x86-64 Wine under Box64: blocked long before the Aegis kill

Tested on the AYN Thor, 2026-10-06, 22:39 to 23:08. The question was whether Aegis also kills the game
when it runs through x86-64 Wine under Box64 instead of ARM64EC Wine + FEX, which would tell whether the
trigger is specific to ARM64EC. **It cannot answer that yet: both x86-64 builds die within seconds of
start, long before the 2 to 3 minute kill window.**

**Update 2026-10-07:** both Proton 11 blockers are fixed in Box64 (patches 0002 and 0003); the game now runs
its own start-up and stops in `Config File`, after the protection's illegal-instruction phase. See the last
section.

Setup changed for the test: container Wine Version (GameNative UI, General tab). With an x86-64 Proton the
64-bit emulator is fixed to Box64; the container had Box64 `0.4.5-aoefix-1` (this repo's
`patches/box64/` build), preset Compatibility. Everything else as on the clean baseline. The game's
settings folder and both container configs were backed up first, and the container was switched back to
`proton-11.0-99-arm64ec-1` afterwards (verified: FEX DLL `460568b8` in place, `WINEDEBUG` undefined).

## `proton-11.0-1-x86_64-1`: the game's `ucrtbase.dll` entry point cannot execute

The process loads `RelicCardinal.exe` and its DLLs and ends 1.6 s after `BugSplat64.dll` loads; no
`warnings.log` is written. With `WINEDEBUG=+seh` and `BOX64_SHOWSEGV=1` (two launches, same result):

```
Emit Signal 11 at IP=0x6ffc813c10 (.../Age of Empires IV/ucrtbase.dll + 0x63c10)
Signal 11: si_addr=0x6ffc813c10, TRAPNO=14, ERR=21, RIP=0x6ffc813c10, prot=3
00c4:trace:seh:dispatch_exception code=c0000005 (EXCEPTION_ACCESS_VIOLATION) addr=0000006FFC813C10
00c4:trace:seh:dispatch_exception  info[0]=0000000000000008     <- execute access
backtrace: ucrtbase.dll+0x63C10  <- ntdll.dll+0x2C340 <- ntdll.dll+0x30B16
```

`0x63c10` is that DLL's `AddressOfEntryPoint` (`llvm-objdump -p`), in `.text`; `rcx` is its base and
`rdx=1` (`DLL_PROCESS_ATTACH`). So the loader calls the entry point of the game's own `ucrtbase.dll` and
Box64 treats the page as non-executable (`prot=3`, read/write).

Measured fact that may be related: the game folder lives on `/storage/emulated`, which is mounted
`noexec` (`mount`). Whether that is why Box64 has no execute permission recorded for the page is **not
tested**; Wine's own DLLs, which are not on that mount, run fine.

## `proton-10.0-4-x86_64-1`: starts, then dies in `Config File`

The game process starts and writes 16 lines of `warnings.log` (started 23:03, last line
`Loading step: [Config File]` at 23:03:07), then dies. Under FEX this step takes half a second and the
game goes on for minutes.

`WINEDEBUG=+seh` recorded 16,394 `c000001d` (`EXCEPTION_ILLEGAL_INSTRUCTION`) exceptions in that minute,
14,268 of them on one thread, at 1,382 distinct addresses, **all inside `RelicCardinal.exe`**. 1,023 of
those addresses (15,527 of the events) are inside the region attributed to Aegis
(`+0x3e40000..+0x3f90000`). The most frequent:

| address (`RelicCardinal.exe` +) | count |
|---|---|
| `0x3F551CD`, `0x3F551CF`, `0x3F551D2`, `0x3F551D6`, `0x3F551DB`, `0x3F551DE`, `0x3F551E1`, `0x3F551E4` | 881 to 882 each |
| 271 other addresses | 20 each |

Eight consecutive instructions faulting the same number of times means something catches each fault and
resumes after it, 882 times over. Aegis's build log says it inserted "233 out of 172731 fake
instructions" (AEGIS.md); deliberate invalid opcodes handled by an exception handler would look like
this. That reading is **not verified**, and whether the same exceptions occur under FEX has **not been
measured**.

Also seen: Proton's seccomp trap (`SIGSYS`) for a raw `syscall` instruction at `rip 0x18d026a`, an
address outside every module (run-time-generated code), with `rax=0xa0`. Wine numbers syscalls like
Windows; in Wine's 64-bit table `0xa0` is `NtGetNextThread` (`dlls/ntdll/ntsyscalls.h`, Wine master;
the Proton 10 build's table was not checked). So code outside any module enumerates threads with a
direct syscall.

## Related report

[GameNative discussion #1938](https://github.com/utkarshdalal/GameNative/discussions/1938) (another
user, same device family): Age of Empires II: DE and Age of Mythology: Retold die within seconds under
FEX (a fault in FEX-translated code); with `proton-11.0-1-x86_64` under Box64 that crash goes away but
the game hangs in its VC++ redistributable install. Different games and failures; listed because it
covers the same two routes on the same hardware.

## Consequence

The Box64 route is not a quick way to split the question. Making it useful would mean fixing Box64's
handling of the game's own DLLs (Proton 11) or of whatever ends the run in `Config File` (Proton 10)
first. The trace of Aegis's syscalls under FEX is the next step instead.

## 2026-10-07: both Proton 11 blockers fixed in Box64; the game now stops in `Config File`

Setup: container Wine `proton-11.0-1-x86_64-1`, Box64 built from GameNative's fork (`Pipetto-crypto/box64`
`eb6fb21f`) with this repo's [`patches/box64/`](../patches/box64) 0001 to 0003, NDK r26b, API 31, imported as
`.wcp`; preset Compatibility; `WINEDEBUG=+seh BOX64_SHOWSEGV=1` for the diagnostic launches. Settings folder
and both container configs backed up first.

1. **The execute fault was the `noexec` mount.** Box64 records a guest page as executable only after the
   host `mmap`/`mprotect` with `PROT_EXEC` succeeds, and the host refuses that for files on
   `/storage/emulated`. Patch 0002 retries without host exec and keeps the guest's protection. With it the
   loader runs `ucrtbase.dll`'s entry and the game's own code starts (run 01:05).
2. **Raw syscalls ran as Linux syscalls.** Wine logs, in every process:
   `install_bpf Native libs are being loaded in low addresses, sc_seccomp 0x3f00094b80, syscall 0x600201a0,
   not installing seccomp`. Box64 hands a raw `syscall` from Windows code to Wine only through Wine's
   `SIGSYS` handler, which Wine installs together with the seccomp filter, so here the game's raw syscalls
   ran as Linux syscalls. The result: a read of address 0 at `RelicCardinal.exe+0x3fa0d3a`, in a check that
   loads a function pointer from `.data` (`0x14754a8b0`, still 0), reads the first 8 bytes of its target and
   compares the top 2; the same pointer is the jump target at `+0x3fa0ce0`.
3. **Patch 0003 sends those syscalls to Wine's dispatcher** the way Wine's own x86-64 stubs do
   (`call [0x7ffe1000]` when `KUSER_SHARED_DATA+0x308` bit 0 is set; checked in this build's `ntdll.dll`).
   The first version pushed the plain return address, and the game crashed at `+0x3f91053`: 0xb bytes before
   the instruction after the `syscall` at `+0x3f9105c`, inside the exe's own syscall gateway (which hides its
   return address in `r15` during the call). Pushing the address + 0xb, as in Wine's stub layout, passes.
4. **With 0001 to 0003 (run 01:18) the game writes its log and stops in `Config File`**, the step where
   Proton 10 stopped on 2026-10-06: started 01:19, last line `Loading step: [Config File]` at 01:19:23.578.
   Then 682 `c000001d` exceptions, each resumed by the game's vectored handler at `+0x3e46ff8`
   (`returned ffffffff`). After the last one, at `+0x3e768f9`, execution reached address 0 (Box64:
   `Emit Signal 11 at IP=0x0`), the game called `__fastfail` (`int 0x29`, `c0000409` at `+0x4fb0dc9`), and
   GameNative closed the container (`guest_terminated`).
5. **Interpreter only, for the game** (`[RelicCardinal.exe]` `BOX64_DYNAREC=0` in `Z:\etc\config.box64rc`,
   which Box64 applies after the environment): `__fastfail` at the same address 2.6 s after the settings were
   applied, with no `c000001d` before it. Not a clean test of the dynarec: the interpreter is much slower, and
   the protection has timing checks.

The code around `+0x3e768f9` is encrypted on disk, so what the handler does with that exception is not
known. Under FEX the game passes this step in under a second.

Two things that matter for tooling: under Box64 every Wine process is renamed `wine` (Box64 log: `Rename
process to "wine"`), so `run_watch.py`'s match on the process name does not find the game; and when the game
exits, GameNative closes the container, so read `warnings.log` from a new session.
