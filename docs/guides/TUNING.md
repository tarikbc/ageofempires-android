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

## The late game, measured on that game's replay (2026-10-07 23:46 to 2026-10-08 00:02)

The replay of that game (profile, Match History, the match, X "View Replay"; X again in the replay locks the camera to
the player's recorded view), at 1X, measured with `tools/replay.py window` (90 s of compositor frame times, then 20 s
of per-thread CPU). Package v1.1.0. **The CPU clocks were capped by a Thor setting the user had changed at 23:32**
(`performance_mode=2`, governor `performance`): cores 3 to 6 at most 1.92 GHz and core 7 at most 1.98 GHz, against
2.80 and 3.19 GHz possible; the container's GameNative power profile file was unchanged (`SCHEDUTIL`, 307 MHz to
3.19 GHz). So these numbers are for those clocks.

| Window (game time at start) | FPS | median frame | p99 | frames > 50 ms / > 100 ms | GPU busy |
|---|---|---|---|---|---|
| 24:52 | 29.3 | 33.7 ms | 50.5 ms | 128 / 1 | 54 % at 615 MHz |
| 42:31 | 27.4 | 33.7 ms | 59.0 ms | 283 / 0 | 55 % |
| 46:13 | 27.8 | 33.7 ms | 59.0 ms | 270 / 0 | 54 % |
| 48:23 | 27.8 | 33.7 ms | 59.0 ms | 264 / 2 | 55 % |
| 50:42 (includes the end at 51:22 and the end screen) | 25.4 | 33.7 ms | 75.8 ms | 373 / 17 | 45 % |

The late game holds at about 27.7 FPS with many frames over 50 ms, which matches the user's reading. In the four windows
before the end the game's threads used 3.0 to 3.3 cores in total: main thread 55 to 58 % of one core, render thread 34 to 40 %, the
protection's loop about 26 %, eight `rcss worker` threads 12 to 23 % each, audio (`AK::EventManager`) 16 to 21 %,
`Simulation Thread` 15 to 19 %. No thread is saturated and the GPU is about half busy, so the stages wait on each
other, as in the early game.

At 8X the replay ran at about 3.3 times real time early in the game (00:41 to 08:40 in about 2 to 2.5 minutes); near 41
minutes it advanced only 59 game seconds between two clock crops taken about a minute apart (not timed exactly).

**Thread placement could not be tested.** All game threads had `Cpus_allowed_list: 0-5,7` (mask `bf`, core 6 left
out; where that comes from is not known). `tools/agent.py affin 0 f8 f8` reported 65 threads set to cores 3 to 7, but
`/proc` showed the list unchanged and threads kept running on cores 0 to 2; `taskset` from the adb shell is not
permitted (`Operation not permitted`). So the windows at 46:13 and 48:23 ran with the default placement. (The cause,
found the next day: a thread mask outside the process mask is refused; see the next section.)

## The late game at full clocks (2026-10-08, 00:37 to 03:25)

The same replay and windows at full CPU clocks. A fresh game start was enough: GameNative applies the container's
power profile at every start, and after it the limits were 2.02 GHz (cores 0 to 2), 2.80 GHz (cores 3 to 6) and
3.19 GHz (core 7), governor `schedutil`, while the Thor's `performance_mode` setting was still 2. 120 Hz, R4 driver,
the Thor's fan at Custom 88 % during the measured windows. Each row adds one change to the row above it, except the
last:

| Setup | FPS 46:13 / 48:23 | frames > 50 ms | GPU busy |
|---|---|---|---|
| CPU capped (the section above), v1.1.0 | 27.8 / 27.8 | 270 / 264 | 54 / 55 % at 615 MHz |
| Full clocks, v1.1.0 (`bc82c565`) | 36.0 / 36.2 | 19 / 15 | 66 / 68 % at 615 MHz |
| + patch 0016 (v1.2.0, `b5e6e357`) | 36.7 / 36.5 | 10 / 11 | 69 / 70 % at 615 MHz |
| + every game thread allowed on all 8 cores (`agent.py procaffin ff`, live) | 37.6 / 37.0 | 2 / 10 | 72 / 72 % at 615 MHz |
| + GPU held at 680 MHz (power profile GPU levels 8 and 8) | 40.1 / 40.1 | 1 / 3 | 69 / 70 % at 680 MHz |
| **v1.2.0 and GPU at 680 MHz, default core mask** (the package as released, fresh start) | **39.0 / 38.6** | 18 / 22 | 67 / 68 % at 680 MHz |

Mid game (25:43, full clocks, v1.1.0): 37.7 FPS and 16 frames over 50 ms, against 29.3 FPS at 24:52 with the cap. The
median frame was 25.3 ms in every full-clock window (33.7 ms capped). The two windows of one run differed by up to
0.6 FPS; the spread between runs was not measured. Hottest sensors in the last row: CPU 95.1 / 93.1 °C, GPU 78.8 / 77.6 °C.

**The GPU clock.** With levels 7 and 8 (sysfs levels 1 and 0) the GPU stayed at 615 MHz in every window; with 8 and 8
it ran at 680 MHz, and the late game gained about 2 FPS (36.7 / 36.5 to 39.0 / 38.6; 2.5 to 3.1 FPS with all cores
allowed). That run had more frames over 50 ms (18 / 22) than v1.2.0 at 615 MHz (10 / 11) and the all-cores run at
680 MHz (1 / 3); not explained, and one run each. In the early-game benchmark the same change gave nothing
("Tried and reverted" below). The setting: `"minGpuPowerLevel":8,"maxGpuPowerLevel":8` in the container's
`.config/.power-profile` (GameNative writes sysfs level = 8 - value). It was written with `tools/wincopy.py`; GameNative's
Power Control tab has GPU minimum and maximum controls and saves the profile when the game stops (read in its source,
not tried).

**The core mask.** The game's process affinity mask leaves out one core: `df` (core 5) at one start, `bf` (core 6) at
another. The container lists all 8 cores, so where it comes from is not known. A thread mask that is not inside the
process mask is refused, which is why `affin` changed nothing in the section above. `agent.py procaffin ff` sets the
process mask first, then every thread: 68 of 68 threads set, and `/proc` then showed `Cpus_allowed_list: 0-7`. The
gain (+0.9 / +0.5 FPS, fewer long frames) is close to the spread, so the package does not set it.

**Not kept** (each against "Full clocks, v1.1.0"):

| Change | FPS 46:13 / 48:23 | frames > 50 ms | Notes |
|---|---|---|---|
| Render scale 75 % (game Settings, Graphics; from 100 %) | 38.3 / 37.9 | 30 / 39 | Softer picture and more long frames; set back to 100 % |
| Render thread at `THREAD_PRIORITY_HIGHEST` (`agent.py prio Game/Render 2`) | 35.1 / 34.6 | 24 / 32 | Slower |

**Where the time goes now** (v1.2.0, full clocks, GPU at 615 MHz): the main thread used 47.7 / 48.6 % of one core
(55.6 % with v1.1.0) and spent 35.5 % of its samples waiting in `NtWaitForAlertByThreadId` (locks and condition
variables); the render thread 35 %; the GPU about 70 % busy. No stage is saturated, so the stages wait on each other.
The protection's loop still raises about 25,000 handled exceptions per second (`tools/excrate.py`); at 2.5 us each
(`exccost`) that is about 6 % of one core, an estimate. How patch 0016 was found:
[POWER-INFORMATION.md](../how-it-works/POWER-INFORMATION.md).

## Where the late game waits (2026-10-08, 09:19 to 09:23)

The replay at 46:12, 1X, v1.2.0 with the GPU at 680 MHz and the default core mask (the setup as released). First
`tools/threadwaits.py 20`, then 1,500 RIP samples per thread, 2 ms apart (`tools/agent.py sample '#tid' ...`), with
the system calls named by `tools/research/sysprof.py`:

| Thread | CPU | waits/s | pushed off its core /s | Where its samples are |
|---|---|---|---|---|
| Game/Main Thread | 52.3 % | 1,111 | 112 | game code 49 %; `NtWaitForAlertByThreadId` 34.9 %, `NtAlertThreadByThreadId` 5.1 %, `NtPowerInformation` 1.5 % |
| Game/Render thread | 36.5 % | 333 | 573 | game code 29 %; `NtWaitForAlertByThreadId` 17.1 %, `NtWaitForSingleObject` 14.5 %; `winevulkan.dll` 13.3 %, `d3d12core.dll` 7.4 % |
| The protection's loop (start `exe+3e1b04c`) | 23.0 % | 529 | 170 | `NtWaitForSingleObject` 49.1 %; file calls 13 % (`NtQueryInformationFile` 5.0 %, `NtCreateFile` 4.4 %, `NtClose` 3.6 %), all from the protection's code |
| 8 × rcss worker | 10.7 to 17.7 % each | 1,026 to 1,609 each | 186 to 589 | worker 00: `NtWaitForAlertByThreadId` 87.1 %, game code 7.9 % |
| Simulation Thread | 12.3 % | 358 | 25 | `NtWaitForSingleObject` 63.7 %, `NtWaitForAlertByThreadId` 16.6 % |
| AK::EventManager (audio) | 11.2 % | 114 | 49 | `NtWaitForSingleObject` 91.0 % |
| vkd3d_queue | 9.8 % | 894 | 161 | `winevulkan.dll` 91.1 % (inside the Vulkan driver) |

No single call stands out the way `NtPowerInformation` did before patch 0016. The main thread waits for other game
threads: 476 of its 515 wait samples have `exe+3254372` among the return addresses on the stack. The render thread
waits about a third of the time and is pushed off its core 573 times per second. The workers sleep and wake about
9,000 times per second together.

**What one sleep/wake costs** (`tools/probes/wakecost.c`, in an AoE IV container session with this package, no
game running): a plain system call (`SwitchToThread`) 1.09 µs; a `WaitOnAddress` / `WakeByAddressSingle` round trip
between two threads 16.95 µs (two hand-offs, about 8.5 µs each); the same with an SRW lock and a condition variable
17.64 µs. So Wine and FEX add about 1 µs per call and most of a hand-off is the Linux scheduler waking the other
thread. A faster path for these calls in FEX would save about 1 to 2 % of one core at 9,000 hand-offs per second
(an estimate from these numbers); not pursued.

The protection's file loop is off the frame path, and changing it would change how the anti-tamper behaves; not
pursued. Note for `callers`: during these waits RCX read FEX's `RetToEntryThunk`, not the waited-on address, so RCX
only shows a call's first argument when the game calls the stub's module directly (as in patch 0016's case).

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
| GPU fixed at 680 MHz (power profile GPU levels 8/8; normally 7/8 = 615 to 680 MHz) | 44.7 | GPU still 65 to 70 % busy at 680 MHz: no gain in the early game. **In the late game it gave about 2 FPS, so it is in use since 2026-10-08** ("The late game at full clocks"). |
| Main thread pinned to the prime core 7, all other threads to cores 0 to 6 (`tools/agent.py affin 4fb0884 80 7f`, live) | 44.6 | 53 frames > 50 ms and 2 > 100 ms, against 45.3 FPS, 3 and 0 just before. Not checked whether the masks took effect; a mask outside the game's process mask is refused ("The late game at full clocks"). |
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
