# Docs

To install and play, start with the [main README](../README.md). This folder has the details behind it.

## How it works

The problems the patches fix, one write-up each, and the protection itself.

| Doc | Covers |
|---|---|
| [AEGIS.md](how-it-works/AEGIS.md) | The protection: what it is, its build log, its blocklist and timing constants |
| [HOOK-CHECK.md](how-it-works/HOOK-CHECK.md) | **Problem 1, patch 0007.** The start-up API hook check, and why Wine's ARM64EC `FF 25` stubs fail it |
| [WATCHDOG.md](how-it-works/WATCHDOG.md) | **Problem 2, patch 0010.** The lateness bucket on the protection's loop, and the wineserver round trip per exception that filled it |
| [INSTRUCTION-STEPPER.md](how-it-works/INSTRUCTION-STEPPER.md) | **Problem 3, patches 0012 to 0014.** Code run one instruction at a time from a scratch buffer, and how FEX now handles it (26.7 → 43.7 FPS) |
| [SMC-TRAP.md](how-it-works/SMC-TRAP.md) | Patch 0004. FEX's self-modifying-code trap was visible to the game; hiding it did not stop the kill |
| [FEX-VENDOR-LEAK.md](how-it-works/FEX-VENDOR-LEAK.md) | Patch 0002. FEX's name in CPUID leaf `0x40000000` |
| [SYSCALL-RETURN.md](how-it-works/SYSCALL-RETURN.md) | Patch 0006. Registers after a raw x64 `syscall` |

The patch files themselves, with a status line each: [patches/fex](../patches/fex).

## Guides

| Doc | Covers |
|---|---|
| [TUNING.md](guides/TUNING.md) | What helps after the fixes (120 Hz display, driver choice, power profile, `WINEDEBUG`) and what was measured and reverted |
| [TESTING.md](guides/TESTING.md) | The automated test over adb: launch, intros, skirmish, camera turn, frame times and temperatures; the in-game agent |
| [BUILDING-FEX.md](guides/BUILDING-FEX.md) | Building ARM64EC FEX on macOS, and packaging it as a `.wcp` |
| [GAMENATIVE-UI.md](guides/GAMENATIVE-UI.md) | Driving GameNative's UI over adb, and how to recover from a bad FEX build |
| [CONTAINER-CONFIG.md](guides/CONTAINER-CONFIG.md) | Editing the container config from inside Wine; "Open container" |
| [AUTOMATION-PATHS.md](guides/AUTOMATION-PATHS.md) | GameNative's intents, and what did not work |
| [WINE-SOURCE.md](guides/WINE-SOURCE.md) | Which Wine build the Thor runs, its source commit, and what it lacks |
| [ANALYSIS-GOTCHAS.md](guides/ANALYSIS-GOTCHAS.md) | Two checks before trusting an offline analysis of the game binary |

## Research

The investigation, including the dead ends. [RESEARCH-LOG.md](research/RESEARCH-LOG.md) is the full log (the former
README): status history, verified facts, traps, and an index of every write-up below.

| Doc | Covers |
|---|---|
| [RESEARCH-LOG.md](research/RESEARCH-LOG.md) | The full log |
| [EXPERIMENTS.md](research/EXPERIMENTS.md) | Ledger of what was tried and what it gave |
| [KILL-ANALYSIS.md](research/KILL-ANALYSIS.md) | A captured kill and the hash hypothesis |
| [KILL-REMEASURED.md](research/KILL-REMEASURED.md) | The kill on a clean baseline, measured with `suspinfo` |
| [KILL-TIMER.md](research/KILL-TIMER.md) | The kill thread is a timed job |
| [KILL-STILL-OPEN.md](research/KILL-STILL-OPEN.md) | Historical: the state of the kill question before the SMC work |
| [DEATH-IS-NOT-THE-KILL.md](research/DEATH-IS-NOT-THE-KILL.md) | Retracted: a thread-state method that cannot see Wine's suspends |
| [FEX-PATCH-LIVE.md](research/FEX-PATCH-LIVE.md) | The CPUID patch verified live, and the kill surviving it |
| [FIX-VERIFIED.md](research/FIX-VERIFIED.md) | Historical: the no-trap build's `smctest` result |
| [NETWORK.md](research/NETWORK.md) | The network theory: the game's backend session and Wine's TLS |
| [BOX64-ROUTE.md](research/BOX64-ROUTE.md) | x86-64 Wine under Box64: blocked before the kill window |
| [CONTAINER-WONT-START.md](research/CONTAINER-WONT-START.md) | Why the container stalled, and how it was fixed |
| [NTDLL-NEVER-LOADED.md](research/NTDLL-NEVER-LOADED.md) | Why the ntdll patches had no effect |
| [MODULE-LIST.md](research/MODULE-LIST.md) | Loaded module names that Windows does not have |
| [WINE-GAPS.md](research/WINE-GAPS.md) | Wine behaviours the protection could notice |
| [AEGIS-TRACE.md](research/AEGIS-TRACE.md) | Tracing the game's syscalls inside FEX |
| [WINEDEBUG-LEFTOVER.md](research/WINEDEBUG-LEFTOVER.md) | Why some runs stalled: leftover Wine debug channels |
| [UPSTREAM-FEX-ISSUE.md](research/UPSTREAM-FEX-ISSUE.md) | Draft FEX issue: the SMC write trap is visible to the guest |
| [samples/](research/samples) | Redacted raw run data behind these write-ups |

Screenshots are in [img/](img).
