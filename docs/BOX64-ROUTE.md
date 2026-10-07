# x86-64 Wine under Box64: blocked long before the Aegis kill

Tested on the AYN Thor, 2026-10-06, 22:39 to 23:08. The question was whether Aegis also kills the game
when it runs through x86-64 Wine under Box64 instead of ARM64EC Wine + FEX, which would tell whether the
trigger is specific to ARM64EC. **It cannot answer that yet: both x86-64 builds die within seconds of
start, long before the 2 to 3 minute kill window.**

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
