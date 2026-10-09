# Probes

Small Windows programs that run inside the game's Wine session on the Thor. `build.sh` builds them all as static
x86-64 programs with Homebrew's `mingw-w64` ([BUILDING.md](../../docs/guides/BUILDING.md), "The probes"). Push the
`.exe` to `/sdcard/Download` (the session's `D:` drive) and start it through `winhandler`
([GAMENATIVE.md](../../docs/guides/GAMENATIVE.md), "The container's config from inside Wine"). Most write their
result to a file on `D:\`. The ones marked GUI open no console window and can run during a game.

**Used by the current tests**

| Probe | What it does |
|---|---|
| `aoeagent` (GUI) | One long-running helper inside the session that replaces the one-shot probes: memory reads, thread lists, CPU time, affinity, RIP sampling. Driven by [`tools/agent.py`](../agent.py) |
| `dlgclick` (GUI) | Waits for a dialog by title and clicks the button whose text matches; `run_watch.py` starts it for AoE IV's "video card's installed driver version" dialog |
| `mclick` (GUI) | Mouse and keyboard input inside the session, for menus that touch input cannot reach |
| `gcopy` (GUI) | Copies files inside the session without a console window; paths may use `%VARIABLES%` |
| `lsgame` (GUI) | Lists the game's `My Games\Age of Empires IV` folder with sizes and times |
| `waitexit` (GUI) | Waits for the game to end, then records its exit code and copies the game log at once (GameNative closes the container 34 ms after an exit) |
| `wakecost` (GUI) | Cost of one sleep/wake hand-off between two threads (`WaitOnAddress`, SRW lock and condition variable) |
| `pwrcost` (GUI) | Cost of `CallNtPowerInformation(ProcessorInformation)` the way AoE IV calls it every frame (patch 0016) |
| `smcquery` (GUI) | The reproducer for FEX issue #6023: whether FEX's SMC write trap shows through `NtQueryVirtualMemory` |
| `exccost` | Cost of one illegal-instruction exception handled by a vectored handler, as the protection uses them |
| `fexstats` | Reads patch 0004's SMC-trap counters from the game's `libarm64ecfex.dll` |
| `xinputprobe` | Which XInput pads Wine sees, and their state for a few seconds |

**One-shot probes that the agent replaces** (`aoeagent.c` says it does their work, and `blkread`'s, inside one
long-running process)

| Probe | What it does |
|---|---|
| `peek` | Hex dump of a memory range of a running process |
| `modbase` | Base address and size of one module in a process, for `peek` at a FEX global |
| `affin` | Lists a process's threads with start address, CPU time and affinity; can pin the threads that start at one address |
| `tpause`, `tduty` | Suspend the threads that start at one address for a time, or duty-cycle them, to see what one thread costs the others |

**From the kill investigation of 2026-10-06 and 07** ([archive](../../docs/research/archive))

| Probe | What it does |
|---|---|
| `suspinfo` | Every thread of a process with its suspend count; reads the context of already-suspended threads without suspending others |
| `tctx` | Samples the x64 context of every thread, to find a spinning thread |
| `tstack` | Code pointers on the busiest thread's stack |
| `stk` | Per thread: suspend count and the state of the top stack pages |
| `stkscan` | Return addresses inside a given ntdll range on every thread's stack |
| `stkdump` | One thread's whole committed stack, for offline search of stale return addresses |
| `vq` | Memory state of the pages around an address, and which thread's stack it is on |
| `vmmap` | Summary of a process's address space; tries to start one remote thread in it |
| `waitq` | Wine ntdll's `RtlWaitOnAddress` hash table, to find a held spinlock |
| `selfchk` | Checks that the wait-queue ntdll patch is loaded and drives those code paths (it never was: [NTDLL-NEVER-LOADED.md](../../docs/research/archive/NTDLL-NEVER-LOADED.md)) |
| `syscallregs` | What a raw x64 `syscall` leaves in the registers (patch 0006) |
| `thunkprobe` | Run as `RelicCardinal.exe`: reports the export thunks patch 0007 rewrote |
| `cleancopy` | Compares a loaded system DLL's exports with a fresh mapping of its file, as a "clean copy" check would |
| `memwatch` | Logs every 8-byte change in a memory range of the game, with times |
| `netprobe` | Tests the Wine network and crypto paths AoE IV touches at start |
| `aegistrace` | Copies the syscall trace of a trace build of FEX (an unreleased patch) out of the game |
| `blkread` | Copies patch 0008's decoded-block dump out of the game; `tools/research/blkparse.py` and `blkmem.py` read it |

Note: `tools/research` has two different programs with the same names as two of these (`memwatch.c`, `netprobe.c`).
