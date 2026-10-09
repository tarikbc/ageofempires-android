# Docs

To install and play, start with the [main README](../README.md). This folder has everything behind it.

**Read in this order:**

1. [STORY.md](STORY.md): the whole project told forward, from a frozen game to 58 FPS, with a table of every step
   and version. Start here.
2. [how-it-works/](how-it-works): one page per fix, each with the problem, how it was found, the fix and the numbers.
3. [guides/](guides): how to set up, measure, build and drive the device yourself.
4. [research/](research): the dated log, the measurements, the experiment ledger, the upstream reports, and the
   archived investigation notes.

Releases and their files: [CHANGELOG.md](../CHANGELOG.md). The patch files: [patches/fex](../patches/fex) and [patches/turnip](../patches/turnip).

## How it works: one page per fix

In the order they were found:

| Page | Fix | Effect |
|---|---|---|
| [AEGIS.md](how-it-works/AEGIS.md) | (the protection itself) | What Aegis is, from its own build log |
| [SMC-TRAP.md](how-it-works/SMC-TRAP.md) | patch 0004 | FEX's self-modifying-code trap hidden from the game; needed by later patches, not the cause of the stops |
| [SYSCALL-RETURN.md](how-it-works/SYSCALL-RETURN.md) | patch 0006 | Registers after a raw x64 `syscall`, as on Windows; not the cause either |
| [HOOK-CHECK.md](how-it-works/HOOK-CHECK.md) | patch 0007 | The start-up API hook check passes: the stop 2 to 3.5 minutes in is gone |
| [WATCHDOG.md](how-it-works/WATCHDOG.md) | patches 0009, 0010 | The protection's watchdog no longer fires: the game is playable |
| [INSTRUCTION-STEPPER.md](how-it-works/INSTRUCTION-STEPPER.md) | patches 0012 to 0014 | Code run one instruction at a time no longer recompiles every step: 26.7 → 43.7 FPS with 0012 and 0013 |
| [POWER-INFORMATION.md](how-it-works/POWER-INFORMATION.md) | patch 0016 | The game's per-frame CPU-speed query answered from a cache: late game faster |
| [GPU-DRIVER.md](how-it-works/GPU-DRIVER.md) | Turnip from Mesa main | No more CPU-GPU lock step on fence checks: 41 → 52 FPS |
| [VKD3D-SUBMIT.md](how-it-works/VKD3D-SUBMIT.md) | `VKD3D_CONFIG=no_staggered_submit` | More than one command buffer in flight: 52 → 58 FPS |
| [TRAPLESS-FAULTS.md](how-it-works/TRAPLESS-FAULTS.md) | patch 0017 | The protection's 44,000 exceptions per second without a host trap: +1.5 FPS |
| [UBWC.md](how-it-works/UBWC.md) | Turnip patch 0001, two variables | The game's largest colour images keep UBWC compression: +1.5 FPS (+2.6 %) |

Patches 0002 (dropped) and 0015 (a FEX bug fix) are described in [patches/fex](../patches/fex).

## Guides

| Page | Covers |
|---|---|
| [TUNING.md](guides/TUNING.md) | The settings in use and the evidence for each; every setting measured and not kept |
| [TESTING.md](guides/TESTING.md) | The automated benchmark over adb, the late-game replay, the in-game agent, scheduler and GPU traces |
| [BUILDING.md](guides/BUILDING.md) | Building the FEX package, the Turnip driver and the probes on a Mac |
| [GAMENATIVE.md](guides/GAMENATIVE.md) | Driving GameNative over adb, the container config, installing packages and drivers, recovering from a bad build |
| [AOE2-DE.md](guides/AOE2-DE.md) | Age of Empires II: DE with this package: the start-up crash, the setup that works, speed |
| [WINE-SOURCE.md](guides/WINE-SOURCE.md) | Which Wine build the Thor runs, its source, and what it lacks |

## Research

| Page | Covers |
|---|---|
| [PERFORMANCE.md](research/PERFORMANCE.md) | The speed measurements after the fixes, in date order, with notes on what is comparable |
| [LOG.md](research/LOG.md) | The working log of 2026-10-06 to 2026-10-09: status history, verified facts, hypotheses, traps, tools |
| [LEDGER.md](research/LEDGER.md) | What was tried against the protection's stops, and what each gave |
| [UPSTREAM.md](research/UPSTREAM.md) | What was reported to FEX, Wine and Mesa, and where it stands |
| [archive/](research/archive) | The investigation notes of 2026-10-06 and 07, each with a verdict line ([index](research/archive/README.md)) |
| [samples/](research/samples) | Redacted raw run data behind those notes |

Screenshots and graphs are in [img/](img).
