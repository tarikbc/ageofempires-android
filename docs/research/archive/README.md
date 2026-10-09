# Archive: the investigation notes

These notes were written during the investigation of 2026-10-06 and 2026-10-07, while the cause of the game's stops
was unknown. They are kept as they were, with a one-line verdict at the top of each; numbers in them are correct for
their date and build. The outcome is in [STORY.md](../../STORY.md) and the fixes in
[how-it-works](../../how-it-works); the full log is [LOG.md](../LOG.md).

**The kill: what stopped the game 2 to 3.5 minutes in**

| Note | What it holds |
|---|---|
| [KILL-ANALYSIS.md](KILL-ANALYSIS.md) | The first captured kill, the kill thread and code region, the packed `.text`, static reverse engineering and Ghidra |
| [KILL-REMEASURED.md](KILL-REMEASURED.md) | The kill on a clean baseline, measured with `suspinfo`; the no-trap build stops at start-up |
| [KILL-TIMER.md](KILL-TIMER.md) | The kill thread is a timed job; the always-running hook-check loop |
| [KILL-STILL-OPEN.md](KILL-STILL-OPEN.md) | The state before the SMC work: memory-integrity and "Wine HTTP" theories dead |
| [DEATH-IS-NOT-THE-KILL.md](DEATH-IS-NOT-THE-KILL.md) | Retracted: a method that could not see Wine's suspends |

**Leads that were ruled out**

| Note | Lead |
|---|---|
| [FEX-VENDOR-LEAK.md](FEX-VENDOR-LEAK.md) and [FEX-PATCH-LIVE.md](FEX-PATCH-LIVE.md) | FEX's name in CPUID (patch 0002) |
| [FIX-VERIFIED.md](FIX-VERIFIED.md) | Hiding the SMC trap by disabling it (patches 0001 and 0003): the game stops at start-up |
| [NETWORK.md](NETWORK.md) and [WINE-GAPS.md](WINE-GAPS.md) | The game's backend session and Wine's TLS |
| [NTDLL-NEVER-LOADED.md](NTDLL-NEVER-LOADED.md) | ntdll patches that Wine never loaded |
| [MODULE-LIST.md](MODULE-LIST.md) | Loaded module names that Windows does not have |
| [AEGIS-TRACE.md](AEGIS-TRACE.md) | Tracing the game's syscalls inside FEX |
| [BOX64-ROUTE.md](BOX64-ROUTE.md) | x86-64 Wine under Box64 as a control |

**Infrastructure on the way**

| Note | What it holds |
|---|---|
| [CONTAINER-WONT-START.md](CONTAINER-WONT-START.md) | Why the container stalled: a Wine Version that named a missing tree |
| [WINEDEBUG-LEFTOVER.md](WINEDEBUG-LEFTOVER.md) | Leftover Wine debug channels that slowed runs |
| [CONTAINER-CONFIG.md](CONTAINER-CONFIG.md) and [AUTOMATION-PATHS.md](AUTOMATION-PATHS.md) | Editing the container config from Wine; intents, taps and the locked device (now in [GAMENATIVE.md](../../guides/GAMENATIVE.md)) |
| [ANALYSIS-GOTCHAS.md](ANALYSIS-GOTCHAS.md) | Checks before trusting an offline analysis of the game binary |
