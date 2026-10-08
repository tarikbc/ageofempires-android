# The API hook check, and why it fails only under ARM64EC Wine (2026-10-07)

**Result:** at start-up the game checks 63 Windows API functions for inline hooks. Under FEX with Wine's ARM64EC
DLLs, 27 of them look hooked, because Wine's ARM64EC kernel32 exports them as bare `jmp [rip+disp32]` thunks
(`FF 25`). x86-64 Wine and Windows do not. With those thunks rewritten to the Windows form (`48 FF 25`, FEX patch
0007), no record is reported, the start-up decision takes the same branch as under Box64, and the game ran past the
old kill window with its log growing. It was still stopped later (about 8 to 10 minutes in), by something else.

## How the code was read: a dump of decoded blocks (patch 0008)

The protection's code is encrypted in the file (the bytes at `+0x3ebb5f8`, `+0x3eb5be8` and `+0x3e69428` in
`RelicCardinal.exe` are random). The `c000001d` traps that `WINEDEBUG=+seh` shows at those addresses are first calls
of encrypted functions: the vectored handler decrypts the function and resumes 5 bytes later (the first block FEX
compiled for `+0x3ebb5f8` starts at `+0x3ebb5fd`).

FEX sees the decrypted code when it compiles it. Patch [0008](../../patches/fex/0008-block-dump-experiment.patch)
copies every distinct decoded block in `0x143800000..0x145000000` of the game process into a 96 MB in-memory
buffer, with thread id and `cntvct_el0` time. No syscall happens on that path. (A first version that wrote a file
from the compiler hung the game after one record.) `tools/probes/blkread.c` copies the buffer out of the running or
suspended game; `tools/research/blkparse.py` and `tools/research/blkmem.py` list and disassemble it.

One 50 s run gave 85,709 blocks. The game behaved as without the patch (the start-up jobs got the same settings).

## What the code does

- `+0x3eb5be8` resolves APIs: it walks the loader's module list and the export names, hashing with FNV-1a
  (`0x811c9dc5`, `0x1000193`).
- `+0x3ebb5f8` walks a vector of 0x38-byte records (begin and end pointers at `0x147af6da0` and `0x147af6da8`):
  +0x00 function address, +0x08 callback, +0x10 argument, +0x18 id, +0x20 "reported" byte, +0x21 skip byte, then a
  flag byte and a copy of the function's first bytes. For each record not yet reported it calls `+0x3e5d090` on the
  function address. A detected record is marked reported and, when its id is not `0xff`, passed to a report call.
  The function runs again later (in one run 11 s, 39 s and 78 s after its first call).
- `+0x3e5d090` is an inline-hook detector. After a whitelist lookup (`+0x3dde560`) it returns non-zero when the
  function starts with `E9`, `90 E9`, `90 68 .. C3`, `90 FF 25`, `90 90 ?? E9`, `8B FF E9`, `8B FF FF 25`,
  `68 xx xx xx xx C3`, `FF 25`, `B8`/`A1 xx xx xx xx FF E0`, or `B8`/`A1 xx xx xx xx 50 C3`.

`tools/research/dettable.py` decodes a `tools/probes/peek.c` dump of the vector.

## The table under FEX

63 records (kernel32, kernelbase, ntdll, user32, advapi32). With the unpatched FEX tree, 27 were reported. Every
reported function started with `FF 25`; all of them are in kernel32's `.text` (for example `+0x62710`:
`ff 25 da e9 01 00` followed by 10 zero bytes). The other records point at fast-forward sequences (`48 8B C4 ...`)
or ntdll syscall stubs (`4C 8B D1 B8 ...`), which the detector does not flag.

## Why x86-64 Wine passes

`tools/winebuild/spec32.c` (`output_exports`, Valve Wine `proton_11.0`) gives every `-import` export a hot-patch
prolog, `48 8D A4 24 00 00 00 00`, before `jmp *__imp_x(%rip)`. That branch only runs for `CPU_i386` and
`CPU_x86_64`. The ARM64EC kernel32 has bare `FF 25` thunks instead: its `.text` holds 933 `FF 25 disp32` sequences
followed by padding.

## Patch 0007: rewrite exported `FF 25` thunks

[0007](../../patches/fex/0007-rewrite-ff25-export-thunks.patch) rewrites each exported `FF 25 disp32` in an ARM64X
image (one with a `.hexpthk` section) as `48 FF 25 disp32-1`: the same jump, one byte longer. It only rewrites when
the byte after the jump is padding and the 7 bytes stay inside an executable section. It runs only in
`RelicCardinal.exe`: on the first image mapping after the main thread has a CPU area, for every module already
loaded, and for each image mapped after that.

`tools/probes/thunkprobe.c`, run as `RelicCardinal.exe`, counted the rewritten exports: kernel32 798, kernelbase 2,
user32 208, advapi32 200, ntdll 0, ws2_32 0.

**The bounds check matters.** kernelbase's `.text` ends at RVA `0xf5af6` (its `VirtualSize`), and its last thunk is at
`0xf5af0`. The 7-byte form ends past the section, FEX does not run code outside a section's `VirtualSize`, and the
game failed with an execute fault at `kernelbase+0xf5af0` (called from advapi32). It printed `Application crashed.`
at `Loading step: [ModManager (RR deferred)]`, 35 s in. With the check that thunk is left alone.

Wine's own `arm64x_check_call` (`dlls/ntdll/signal_arm64ec.c`) follows bare `FF 25` thunks natively. Rewritten
thunks are not followed, so calls from native code through them go to FEX instead.

## Results with 0007

Tested builds: the working FEX tree (FEX `7d3090f` with 0002, 0004, 0006, the disabled trace code of 0005, the
0008 hook compiled in but not installed, and `NtDllRedirectionLUTSize` aligned, see below) plus 0007. The first
test used 0007 without the bounds check.

- `WINEDEBUG=+seh`, 60 s (no bounds check): after `+0x3ebb5f8` the next trap is `+0x3ebb98c`, the branch Box64
  takes. No trap at `+0x3e69428` (the start-up job creation) in those 60 s. Without 0007 the next trap is
  `+0x3e69428`, with the settings `rax=10000 rbx=2000`, `60000/80000`, `90000/120000`
  ([KILL-TIMER.md](../research/KILL-TIMER.md)).
- Judged run (04:27, `WINEDEBUG=-all`): the log still grew at 479 s, and no thread had suspend count 1 up to then.
  The old kill came 123 to 182 s in. Loading was slow: `CPU AI` took from 04:32:09 to 04:35:03.
- The same run, watched on: by 04:37:26 (about 10 minutes in) 65 threads had suspend count 1, and the log had
  stopped at `Loading step: [Cheat Menu]`. Read from the suspended process, all 63 records had "reported" 0.
  So this later stop is not the hook check.

## The later stop, seen in the block dump

Run at 04:45 with 0007 and 0008 (the working tree above plus the dump installed), `WINEDEBUG=-all`: the log grew up to
537 s; at 581 s 61 threads had suspend count 1 and the log had stopped at `Cheat Menu`. The dump (291,121 blocks over
517.7 s) shows, in seconds after the first dumped block:

- 510.75: thread `016c` (start `+0x3e1b04c`) runs `0x143dd2550` for the first time. About 130 call sites in the
  protection's code call that function. In this run it calls `+0x3e691f4`, which calls `+0x3e69428` (510.77), the job
  creation function.
- 510.82: a new thread starts at `+0x3e69304`, the kill thread entry.
- 512.74: thread `0590` (start `+0x3e69304`) runs the suspend-all function `+0x3e6b53c` for the first time.
- No block in the dumped range was compiled by any thread between 480 s and 510.75 s, so the check that fired ran in
  code compiled earlier. Thread `016c` compiled 25,340 blocks in its first 50 s, 43 between 50 and 100 s (among them
  AES key expansion and GHASH code), and none between 100 and 500 s.
- The game log has `TlsConnection::Shutdown ... errno=10038` and `statusCode=1006` at 04:53:59, about 512 s after
  the process was found at 04:45:27. Which came first, the decision or the socket error, is not established.

In the unpatched dump the same functions ran on the main thread at start-up: `0x143dd2550` at 1.091 s and
`+0x3e691f4` at 1.093 s.

What fires at about 510 s is a watchdog on thread `016c`'s loop: see [`WATCHDOG.md`, part 1](WATCHDOG.md#part-1-the-later-stop-a-lateness-bucket-on-the-protections-own-loop-2026-10-07).

## Refuted on the way: the declared code range

ARM64EC kernelbase declares `.text` as its code range (`BaseOfCode 0x10000`, `SizeOfCode 0xf0000`), while the x64
view of `GetWsChangesEx` is at `0x102480`, in `.hexpthk`. A FEX build that widened `SizeOfCode` in memory to cover
`.hexpthk` (verified in the game process: `0xf5350`) changed nothing: the start-up jobs still got the punish
settings, and `r12` at `+0x3ebb5f8` was still `0x96fce`.

## Two traps

- **Link error after adding a global to FEX's Module.cpp:** `ld.lld: error: misaligned ldr/str offset`. `CheckCall`
  in `Module.S` loads `NtDllRedirectionLUTSize` (a `uint32_t`) with a 64-bit `ldr`. When the data layout moves it to
  a 4-byte boundary the link fails. `alignas(8)` on that variable fixes it.
- **An intermittent start-up failure:** in 4 runs the game process failed in `loader_init` with an execute fault at
  the entry of its own `ucrtbase.dll` (`ucrtbase.dll+0x63c10`). In all 4 the game process had ntdll at
  `0x7fff9a0000` and ucrtbase at `0x7f..`; a run that started normally had ntdll at `0x6fff9a0000`. One of the builds
  involved started normally in another run. Not explained.
