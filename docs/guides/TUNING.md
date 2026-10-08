# Tuning after the FEX fixes: what helps and what does not

Measured on 2026-10-07, 15:59 to 18:43, with FEX build `6990a221` (patches through 0014) and the automated skirmish test
(`tools/bench.py run`, 90 s of compositor frame times at match minutes 1 and 5). Each change was tested alone and
reverted unless kept. Run-to-run spread with the same setup was about ±1 FPS (43.8 / 44.2 against 45.6 / 45.3 for two
120 Hz runs).

## Kept: the display at 120 Hz

The Thor's main screen supports 60 and 120 Hz (`dumpsys display`: modes 1 and 2), but the system settings
`peak_refresh_rate` and `min_refresh_rate` were both `60.0`. At 60 Hz **every frame was a whole number of 16.7 ms
refreshes** (0 of 4,103 frames off a step): 2,568 frames took one refresh and 1,517 took two, so a frame that needed
18 ms was shown for 33.4 ms.

With both settings at 120 (`settings put system peak_refresh_rate 120.0`, the same for `min_refresh_rate`, or the
refresh-rate tile in quick settings):

| | FPS minute 1 / 5 | median frame | frames > 50 ms (per 90 s) |
|---|---|---|---|
| 60 Hz (`6990a221` run, 15:59) | 43.3 / 43.5 | 16.7 ms | 18 / 18 |
| 120 Hz (17:06) | 45.6 / 45.3 | 25.3 ms | 2 / 3 |
| 120 Hz (17:58, same setup again) | 43.8 / 44.2 | 25.3 ms | 4 / 1 |

At 120 Hz the steps are 8.3 ms: 1,868 frames took two refreshes and 2,305 took three, so the game's own frame time is
about 20 to 22 ms. The gain in average FPS is small, but the long frames almost disappear.

**The game must be started after the change.** GameNative votes a frame rate for its game surface when it creates it:
its refresh-rate limit if set, else the screen's current rate (`VulkanRenderer.applyScanoutFrameRateHint`). With the
game already running at 60 Hz, the main screen stayed at 60 Hz after the settings change (SurfaceFlinger
`refresh-rate: 60.00 Hz`, only the second screen switched); after the next start it ran at 120 Hz.

## Graphics drivers (19:02 to 19:57)

Same test at 120 Hz, the drivers installed with `tools/gn_driver.py` (GameNative Driver Manager, then the container's
Graphics Driver Version). Temperatures from `tools/thermals.py` (hottest CPU sensor and the mean of all CPU sensors,
hottest GPU sensor), sampled every 3 s in each window. These runs came after hours of back-to-back tests, so the
Thor was hot; earlier the same R4 setup gave 43.8 to 45.6 FPS.

| Driver | FPS minute 1 / 5 / 7 | frames > 50 ms | frames > 100 ms | CPU hottest / mean | GPU hottest |
|---|---|---|---|---|---|
| **Turnip v26.2.0 R4** (in use) | 42.1 / 42.1 / 42.2 | 2 / 2 / 2 | 2 / 0 / 0 | 95.1 to 95.5 / 81.9 to 83.6 °C | 75.2 to 77.2 °C |
| Turnip v26.3.0-R6 (StevenMXZ, 2026-09-30) | 42.4 / 42.6 / 41.9 | 4 / 0 / 2 | 1 / 0 / 1 | 94.3 / 81.7 °C (minute 7) | 75.2 °C |
| Turnip T30 (MrPurple666 purple-turnip, Mesa 26.3.0, 2026-08-17) | 39.7 / 39.8 / - | 6 / 2 | 2 / 0 | not recorded | GPU 69 to 73 % busy |
| Balemuni Apex v2 ULTIMATE SD 8 Gen 2 (Mesa 26.3.0-devel `b9a2bf3`, 2026-08-26) | - | - | - | - | - |

**Balemuni Apex v2 stops the game** about two minutes after the start, twice in two runs (19:04:32 and 19:09:19):
`Failed to wait for DX12 fence (error 102). Initial value: 3688, Expected value: 3689, Actual value: 3688` then
`-- FATAL EXIT --` in the game's log (error 102 is a wait timeout: the GPU did not finish the submitted work). Its
`meta.json` name contains `/`, and GameNative installed it under the folder name `tmp`.

R4 and R6 are the same within the test's spread; R4 stays. Checksums: the Balemuni and T30 files on the Thor matched
the SHA-256 digests of their GitHub release assets.

## Power profile: let the CPU scale (20:00)

Until then the container's GameNative power profile held every CPU core at its maximum clock (`minCpuFreq` =
`maxCpuFreq` = 3187200). With the minimum released (`"minCpuFreq":307200`, `"maxCpuFreq":3187200`, governor
`SCHEDUTIL`, GPU levels 7 and 8 as before), same test, R4 driver, right after the R4 run above:

| CPU minimum | FPS minute 1 / 5 / 7 | frames > 50 ms | frames > 100 ms | CPU hottest / mean | GPU hottest | prime core |
|---|---|---|---|---|---|---|
| held at 3187 MHz | 42.1 / 42.1 / 42.2 | 2 / 2 / 2 | 2 / 0 / 0 | 95.1 to 95.5 / 81.9 to 83.6 °C | 75.2 to 77.2 °C | 3187 MHz |
| 307 MHz (scaling) | 43.9 / 44.1 / 44.0 | 1 / 2 / 2 | 0 / 1 / 0 | 94.7 to 95.9 / 83.3 to 84.1 °C | 76.4 to 77.2 °C | 729 to 3187 MHz |

Holding the clock brought no FPS and no lower temperature, so the scaling profile is the one in use.

## `WINEDEBUG=-all` in the container: still needed here (20:39 to 20:43)

GameNative 1.2.1 sets `WINEDEBUG` itself and then merges the container's `envVars`, which win: `-all` when Settings →
Debug → Wine debug is off, else `+` and the channels listed on that screen (`XServerScreen.kt`). Read from
GameNative's own logcat line `Env Vars (Final Guest)` at three "Open container" starts:

| Container `envVars` | `WINEDEBUG` given to Wine |
|---|---|
| with `WINEDEBUG=-all` (as set up) | `-all` |
| without it | `+warn` |
| with it again | `-all` |

GameNative's Wine debug setting was on here with the channel `warn` (its `wine_debug.log` was written at that start),
so without the variable Wine would print its warnings. The variable stays. With Wine debug off in GameNative it should
change nothing (from the code; not tested). The container's config was put back byte for byte afterwards.

## A full game (2026-10-07, 22:30 to 23:24)

The user played a full skirmish to victory with the v1.1.0 package: 51 min 22 s on Danube River against one A.I.
(Intermediate). GameNative's FPS counter read high 20s to low 30s for most of the game and about 24 in the big
late-game battles (the user's reading). The benchmark below measures the first minutes of an idle 1v1, so it is far
lighter than this. The game kept its replay (`playback\temp.rec`, 2.8 MB, overwritten by the next match); a copy
outside the repo can serve as a repeatable late-game test.

After the match the result panel said "Retrieving..." ("Waiting to retrieve match results from the server") for
minutes. The game log had no network error and nothing from the server after the match ended; the match then showed
in Match History.

## v1.1.0 package: same speed without 0002 (21:54 to 22:17)

The package without patch 0002, with 0007 in every process and with 0015 (`aoe4-perf3-21`, DLL `bc82c565`), against
the v1.0.0 package (`aoe4-perf2-20`, `6990a221`), back to back on a Thor already hot from earlier runs (120 Hz, R4
driver, scaling power profile):

| Package | FPS minute 1 / 5 | frames > 50 ms | frames > 100 ms | CPU hottest | GPU hottest |
|---|---|---|---|---|---|
| v1.1.0 (21:59, 22:03) | 41.3 / 40.7 | 6 / 5 | 0 / 0 | 96.3 / 95.1 °C | 79.6 / 81.2 °C |
| v1.0.0 (22:12, 22:16) | 41.0 / 40.8 | 3 / 7 | 0 / 0 | 96.3 / 96.3 °C | 81.2 / 82.0 °C |

The same within the test's spread. Earlier that evening a cooler Thor gave 42.6 FPS at minute 1 (v1.0.0, 21:20).

## Where the frame time goes (60 Hz, 16:50, `tools/threadcpu.py`)

Main thread 52.6 % of one core, render thread 34.6 %, the protection's loop 21.9 %, `vkd3d_queue` 9.6 %, eight
`rcss worker` threads 5 to 10 % each; GPU 68 to 71 % busy at 615 MHz. No single stage is saturated, so the stages wait
on each other.

## Tried and reverted

| Change | FPS minute 1 / 5 | Notes |
|---|---|---|
| GPU fixed at 680 MHz (power profile GPU levels 8/8; normally 7/8 = 615 to 680 MHz) | 44.7 | GPU still 65 to 70 % busy at 680 MHz: no gain. |
| Main thread pinned to the prime core 7, all other threads to cores 0 to 6 (`tools/agent.py affin 4fb0884 80 7f`, live) | 44.6 | 53 frames > 50 ms and 2 > 100 ms, against 45.3 FPS, 3 and 0 just before. |
| FEX TSO off (`FEX_TSOENABLED=0`) | 42.7 / 42.2 | Cheaper CPU code did not raise the FPS. |
| `shadows` 4 → 2 and `volumetriclighting` 3 → 1 in `configuration_system.lua` | 34.3 / 34.2 | GPU 71 to 78 % busy. The file has no labels, so these numbers may not mean "lower"; the original file was put back byte for byte. |
| Turnip forced to tile rendering (`TU_DEBUG=noconform,gmem`) | 28.8 | Rendered correctly, a third slower. |
| AVX hidden from the game (`FEX_HOSTFEATURES=disableavx`) | — | The game stops at start: "Your CPU needs to support AVX instructions to run this game." |
| GameNative's wrapper without present wait (`WRAPPER_DISABLE_PRESENT_WAIT=1`) | 37.0 | GPU only 55 to 58 % busy: present wait helps the pacing here. |
| Cache for unpacked BCn textures (`WRAPPER_USE_BCN_CACHE=1`) | 43.7 / 43.2 | No change. |

Notes for these tests:

- Container `envVars` set by the user win over the values GameNative computes for the graphics driver: GameNative
  merges `container.envVars` after them (`XServerScreen.kt`, `envVars.putAll(container.envVars)`).
- The game's own settings already were 1280×720, `verticalsync = false`, `frameratelimit = 0`.
- GameNative's wrapper unpacks BCn textures for Adreno (`WRAPPER_EMULATE_BCN=3` in a GameNative debug log of this
  game from 2026-09-21), which the GPU then reads uncompressed.
