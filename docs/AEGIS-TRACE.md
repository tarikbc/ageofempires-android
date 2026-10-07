# Tracing the syscalls made inside the game

Work in progress, 2026-10-06/07. The goal: record what the game, and Aegis in particular, asks the OS, up to the
kill, so the first point where it behaves differently from a working machine can be found.

## How the trace works

FEX patch 0004 already replaces Wine's `__wine_syscall_dispatcher` pointer with a filter that every syscall in
the process passes ([SMC-TRAP-HIDDEN.md](SMC-TRAP-HIDDEN.md)). The trace build adds, behind
`AEGIS_TRACE=1` in the container's `envVars` and only for `RelicCardinal.exe`:

- a logger called from that filter, which writes 64-byte entries (time, thread, syscall number, three
  arguments, flags) into a 16 MiB ring buffer (262,144 entries) inside the game process;
- a per-thread record of the guest RIP of a raw x64 `syscall` instruction (FEX's `HandleSyscall`), logged as
  "direct";
- a watch list of thread/process calls (`NtSuspendThread`, `NtGetNextThread`, `NtGetContextThread`, ...),
  logged whatever the caller, with their numbers decoded from the device's own ntdll stubs at start-up.

[`tools/probes/aegistrace.c`](../tools/probes/aegistrace.c) copies the buffer out of the running game;
[`tools/parse_aegistrace.py`](../tools/parse_aegistrace.py) decodes it with the device's syscall table
([`tools/ntdll_syscall_table.py`](../tools/ntdll_syscall_table.py), see [WINE-SOURCE.md](WINE-SOURCE.md): the
numbers differ from upstream Wine).

"Direct" turned out to be broad: under FEX's ARM64EC setup, an x64 call to an ntdll function runs ntdll's x64
thunk, which contains a real `syscall` instruction. The recorded RIP separates them: a RIP inside ntdll is an
ordinary API call, a RIP elsewhere is code issuing its own syscall.

## What went wrong on the way (measured)

| variant | change | result |
|---|---|---|
| A | also recorded the x64 caller in FEX's `ExitFunctionEC` / `RetToEntryThunk` | `winhandler.exe` could no longer start programs (Play and the command channel both dead). Recovered by selecting FEXCore `2610-aoe-nofex2-3`, which reinstalls `460568b8` |
| B | A without the two assembly changes | `winhandler` works. The game stalls in `Property Bag Manager` with 205 threads at 0 % CPU; the trace shows 206 `NtCreateThreadEx` calls |
| C | B on top of patch 0006, saving all 32 vector registers around the logger, wrapped calls decoded by following the wrapper's `bl` | built (`1de00759`), not yet run |

Why B stalls is **not established**. Variant C tests one candidate: the logger is ordinary C++ code and may
change vector registers that a raw syscall must preserve ([SYSCALL-RETURN.md](SYSCALL-RETURN.md)).

## First findings from variant B (before the stall)

From the last dump of that run (`trace_04`, 5,124 entries, up to 184.6 s after the first entry):

- **Most of the game's raw syscalls go through one gateway in private memory.** 4,343 of the 4,783 entries
  with a recorded `syscall` RIP come from RIP `0x2dc04f0`, which is in no loaded image: 30 different syscalls (file,
  wait, event, section, memory, thread and process calls) from 21 threads, starting 0.292 s after the first
  entry. So `0x2dc04f0` is a general syscall gateway, not code dedicated to one check. Who builds it is not
  established.
- **A second gateway sits inside the exe, just past the region the kill thread lives in.** From 0.292 s:
  `NtProtectVirtualMemory` (166) and `NtQuerySystemTime` (7) at `+0x3f9113a`, `NtAllocateVirtualMemory` (24)
  and `NtQueryPerformanceCounter` (1) at `+0x3f9105c`, `NtFlushInstructionCache` (48) at `+0x3f911ad`. That
  is allocate, protect and flush-icache: the calls an unpacker or code generator makes.
- The remaining 194 come from ntdll's own x64 code (`0x6fffa8b0b5`): 192 `NtCreateThreadEx`, 2
  `NtQuerySystemTime`. 192 thread creations from 2 threads is not normal; it matches the variant-B stall
  (205 threads), so it is probably an effect of the trace build.
- **One thread reads the loader's module list through `NtReadVirtualMemory` on its own process.** All 1,023
  `NtReadVirtualMemory` calls (handle `-1`) are on thread `13c`, through the `0x2dc04f0` gateway. From 0.297 s
  every ~15 ms it reads `0x7ffd0018`, `ntdll+0x150f40` and a heap address: `ntdll+0x150f40` is `ldr+0x20` in
  the device ntdll's symbols, the `InMemoryOrderModuleList` head of `PEB_LDR_DATA`, and `PEB+0x18` is the
  `Ldr` pointer on x64. At 3.384 s it reads a run of heap addresses 0x140 bytes apart.

Whose code this is is an inference: it starts before `main`, the exe-side gateway allocates and protects
memory at the same moment, and it avoids plain memory reads, which fits Aegis's "Pre-Main Stealth Startup"
and its module blocklist (AEGIS.md). It is **not proven** to be Aegis.

Why it matters for patch 0006: every one of these syscalls returns through Wine's `invoke_arm64ec_syscall`,
so all of them saw `rcx` = status on the stock setup ([SYSCALL-RETURN.md](SYSCALL-RETURN.md)).
