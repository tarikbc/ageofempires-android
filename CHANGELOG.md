# Changelog

Every release, with what changed, the files and the speed measured for it. AoE IV numbers are from the automated
skirmish benchmark (FPS at two match minutes, 90 s windows) and the late-game replay (minutes 46 and 48), on the AYN
Thor with GameNative 1.2.1. How the versions connect: [docs/STORY.md](docs/STORY.md).

## v1.4.0, 2026-10-09: steadier frames, +1.5 FPS

- **New FEX package `aoe4-perf6` (30)**: adds patch 0017, guest faults without a host trap. The protection's 44,000
  illegal-instruction exceptions per second no longer go through a host trap and a second dispatcher pass
  ([TRAPLESS-FAULTS.md](docs/how-it-works/TRAPLESS-FAULTS.md)). Switches: `FEX_EXP_FASTTRAP=0`, `FEX_EXP_FASTRAISE=0`.
- **`VKD3D_CONFIG=no_staggered_submit` in the setup** (documented since 2026-10-08 afternoon): vkd3d-proton 2.14.1 no
  longer holds each GPU submission until the previous one finished ([VKD3D-SUBMIT.md](docs/how-it-works/VKD3D-SUBMIT.md)).
- AoE II DE stays on `aoe4-perf5-23`, which is attached again.

| Measured | Result |
|---|---|
| Skirmish, 0017 against the previous package, same session | 58.3 / 57.8 FPS against 56.4 / 56.3 and 56.6 / 56.5; frames of 25 ms 813 / 825 against 999 to 1,039 |
| Skirmish, `no_staggered_submit` (2026-10-08, v1.3.0 driver) | 58.6 / 58.1 FPS against 52.3 / 52.3 |
| Late game, `no_staggered_submit` (without 0017) | 47.3 and 48.5 FPS against 46.1, but 57 to 86 frames over 50 ms per window against 15 |
| Release check | windows at minutes 1, 5, 10, 15: 57.8, 57.4, 57.4, 54.7 FPS; no stop in a 19-minute run (the release note says 15 minutes, the last measured window) |

| File | SHA-256 |
|---|---|
| `fexcore-aoe4-perf6.wcp` (DLL SHA-1 `7e70737984259c2b383f9a221e441e6fa9d7e339`) | `23bd9da0791d0310d551af25b0e82866600d8aadc4cf7ad699500bacbe09ff7b` |
| `fexcore-aoe4-perf5.wcp` (same file as v1.2.0) | `20a32eabd5c3cb50082aa0a8f0fb835d66123b05339c48af4284364f6f90119f` |
| `turnip-main-c78a9e9.zip` (same file as v1.3.0) | `b2e7bf9e81e400cc1511b5d9ada3ef654fe2f3567351cb1cb685e68364e69112` |

## v1.3.0, 2026-10-08: +27 % FPS from a newer Turnip driver

- **New driver `turnip-main-c78a9e9`**, Mesa's Turnip built from its 2026-10-08 main branch. It contains Mesa !44838:
  the KGSL back end no longer waits for the GPU on a zero-timeout fence check, so the CPU and GPU stop running in lock
  step ([GPU-DRIVER.md](docs/how-it-works/GPU-DRIVER.md)). Build script: `tools/build_turnip.sh`.
- The FEX package is unchanged (`aoe4-perf5-23`).
- Checked: vkd3d-proton 3.0.1 instead of 2.14.1 changes nothing (40.3 / 40.2 FPS with R4).

| Measured | Turnip v26.2.0 R4 | `turnip-main-c78a9e9` |
|---|---|---|
| Skirmish, minutes 1 / 3 | 41.0 / 41.1 FPS | 52.3 / 52.3 FPS |
| Late game, minute 48 | 38.6 to 39.7 FPS | 46.1 FPS |

| File | SHA-256 |
|---|---|
| `turnip-main-c78a9e9.zip` | `b2e7bf9e81e400cc1511b5d9ada3ef654fe2f3567351cb1cb685e68364e69112` |
| `fexcore-aoe4-perf5.wcp` (same file as v1.2.0) | `20a32eabd5c3cb50082aa0a8f0fb835d66123b05339c48af4284364f6f90119f` |

## v1.2.0, 2026-10-08: faster late game

- **New FEX package `aoe4-perf5` (23)**: adds patch 0016. The game asks for every core's MHz about 45 times per
  second, and Wine read two cpufreq files per core for each answer; FEX now answers from a 250 ms cache
  ([POWER-INFORMATION.md](docs/how-it-works/POWER-INFORMATION.md)).
- **Setting: the GPU held at 680 MHz** (power profile GPU levels 8 and 8).
- **New benchmark:** the replay of a full 51-minute game, measured at minutes 46 and 48 (`tools/replay.py`).

| Late game, minutes 46 / 48 | FPS |
|---|---|
| v1.1.0, full CPU clocks | 36.0 / 36.2 |
| v1.2.0 | 36.7 / 36.5 |
| v1.2.0 and the GPU at 680 MHz | 39.0 / 38.6 |
| CPU capped at about 2 GHz (v1.1.0) | 27.8 / 27.8 |

| File | SHA-256 |
|---|---|
| `fexcore-aoe4-perf5.wcp` (DLL SHA-1 `b5e6e357d638590026128a368393ced2bb9a94b2`) | `20a32eabd5c3cb50082aa0a8f0fb835d66123b05339c48af4284364f6f90119f` |

AoE II DE was first run with this package, less than two hours after the release ([AOE2-DE.md](docs/guides/AOE2-DE.md)).

## v1.1.0, 2026-10-07: same speed, less hiding

- **New FEX package `aoe4-perf3` (21)**: drops patch 0002 (FEX's name hidden in CPUID; the game runs the same
  without it), applies patch 0007 in every process that runs x64 code instead of the game only, and adds patch 0015
  (a FEX bug: a 32-bit value loaded as 64 bits in `CheckCall`).
- Skirmish, back to back on a hot Thor: 41.3 / 40.7 FPS against 41.0 / 40.8 for v1.0.0 (minutes 1 / 5).
- A full 51-minute game against one A.I. was played to victory with this package the same evening (22:30 to
  23:24); its replay became the late-game benchmark.

| File | SHA-256 |
|---|---|
| `fexcore-aoe4-perf3.wcp` (DLL SHA-1 `bc82c565dc01c84bc63d8d2b7326b95619114c15`) | `0c5d756d78eab5535fe35ee0d6cb9e1b713047ea67a93306bbba6c58290df98e` |

## v1.0.0, 2026-10-07: the first package

- **FEX package `aoe4-perf2` (20)**: FEX `7d3090f` with patches 0002, 0004, 0006, 0007 (game process only), 0009,
  0010, 0012, 0013 and 0014. It gets the game past three problems of its anti-tamper protection under the emulator:
  a start-up hook check (0007), a watchdog on slow exceptions (0010) and a one-instruction-at-a-time code stepper
  (0012 to 0014).
- Setup: Turnip v26.2.0 R4 (StevenMXZ), `WINEDEBUG=-all`, `FEX_EXP_SKIP_CALLRET_RESET=1`, display at 120 Hz.
- About 42 to 46 FPS in a 1v1 skirmish: 43.3 / 43.5 at 60 Hz, 45.6 / 45.3 and 43.8 / 44.2 at 120 Hz, 42.1 on a hot
  Thor. Matches past 20 minutes.

| File | SHA-256 |
|---|---|
| `fexcore-aoe4-perf2.wcp` (DLL SHA-1 `6990a2212162428d7d53f965a6609e905846a8c2`) | `cf92fdb73cb573034b64f9769b153df3ad4aa2020ed1603a7ff4fccbae123641` |
