# Settings for AoE IV: what helps and what does not

The settings of the tested setup, each with the measurement behind it, and every setting that was measured and not
kept. The setup itself (where to click) is in the [README](../../README.md#setup). The dated measurements are in
[PERFORMANCE.md](../research/PERFORMANCE.md); the fixes in the package and the driver are in
[how-it-works](../how-it-works).

## In use

| Setting | Where | Value | Why |
|---|---|---|---|
| Display refresh rate | Thor quick settings, before the game starts | 120 Hz | Frames wait for the next 8.3 ms step instead of 16.7 ms; long frames almost disappear (below) |
| FEX package | container, Emulation | `aoe4-perf6-30` | The protection fixes and the speed patches ([patches/fex](../../patches/fex)) |
| Graphics driver | container, Graphics | `turnip-main-c78a9e9-ubwc` | +27 % FPS ([GPU-DRIVER.md](../how-it-works/GPU-DRIVER.md)); the v1.5.0 build adds one patch |
| `TU_UBWC_RGBA8_IGNORE_R32`, `TU_UBWC_RGBA16F_INT` | container, Environment | `1` each | The patched driver keeps UBWC on the game's largest colour images: +1.5 FPS (+2.6 %) ([UBWC.md](../how-it-works/UBWC.md)) |
| `VKD3D_CONFIG` | container, Environment | `no_staggered_submit` | +12 % FPS in the skirmish, a less even late game ([VKD3D-SUBMIT.md](../how-it-works/VKD3D-SUBMIT.md)) |
| `FEX_EXP_SKIP_CALLRET_RESET` | container, Environment | `1` | Turns on patch 0009. On 2026-10-07, before patches 0012 to 0014, the match HUD read 8.0 FPS without it and 22.8 to 29.5 with it, both with TSO off ([LOG.md](../research/LOG.md), "Speed before 0012/0013"). Not re-measured on the current package |
| `WINEDEBUG` | container, Environment | `-all` | Keeps Wine's debug output off even when GameNative's Wine debug setting is on (below) |
| Power profile, CPU | container power profile | scaling, 307 MHz to 3187 MHz, `SCHEDUTIL` | Holding the maximum gave no FPS and no lower temperature; a cap near 2 GHz cost the late game 36.0 → 27.8 FPS (below) |
| Power profile, GPU | container power profile | levels 8 and 8 (680 MHz) | About +2 FPS in the late game, nothing in the early game (below) |
| Game graphics | game settings | 1280 × 720, render scale 100 %, `verticalsync` off, `frameratelimit` 0 | As found; a 75 % render scale gave +2 FPS in the late game but a softer picture and more long frames |

## The display at 120 Hz

The Thor's main screen supports 60 and 120 Hz (`dumpsys display`: modes 1 and 2), but the system settings
`peak_refresh_rate` and `min_refresh_rate` were both `60.0`. At 60 Hz **every frame was a whole number of 16.7 ms
refreshes** (0 of 4,103 frames off a step): 2,568 frames took one refresh and 1,517 took two, so a frame that needed
18 ms was shown for 33.4 ms.

With both settings at 120 (`settings put system peak_refresh_rate 120.0`, the same for `min_refresh_rate`, or the
refresh-rate tile in quick settings), skirmish benchmark of 2026-10-07 (the settings round, 15:59 to 18:43, FEX build
`6990a221`, the v1.0.0 package) with Turnip R4:

| | FPS minute 1 / 5 | median frame | frames > 50 ms (per 90 s) |
|---|---|---|---|
| 60 Hz (`6990a221` run, 15:59) | 43.3 / 43.5 | 16.7 ms | 18 / 18 |
| 120 Hz (17:06) | 45.6 / 45.3 | 25.3 ms | 2 / 3 |
| 120 Hz (17:58, same setup again) | 43.8 / 44.2 | 25.3 ms | 4 / 1 |

At 120 Hz the steps are 8.3 ms: 1,868 frames took two refreshes and 2,305 took three, so the game's own frame time is
about 20 to 22 ms. The gain in average FPS is small, but the long frames almost disappear. Run-to-run spread with the
same setup was about ±1 FPS.

**The game must be started after the change.** GameNative votes a frame rate for its game surface when it creates it:
its refresh-rate limit if set, else the screen's current rate (`VulkanRenderer.applyScanoutFrameRateHint`). With the
game already running at 60 Hz, the main screen stayed at 60 Hz after the settings change (SurfaceFlinger
`refresh-rate: 60.00 Hz`, only the second screen switched); after the next start it ran at 120 Hz.

## Power profile

GameNative applies the container's power profile (`.config/.power-profile`) at every game start. The tests wrote it
with `tools/wincopy.py`; GameNative's Power Control tab has controls for it (read in its source, not tried). It sets the CPU limits per core group and the GPU power levels (GameNative writes sysfs level = 8 minus
the value).

**CPU: let it scale.** Until 2026-10-07 20:00 the profile held every CPU core at its maximum clock (`minCpuFreq` =
`maxCpuFreq` = 3187200). With the minimum released (`"minCpuFreq":307200`, `"maxCpuFreq":3187200`, governor
`SCHEDUTIL`, GPU levels 7 and 8), same test, R4 driver, right after the run with the held clock:

| CPU minimum | FPS minute 1 / 5 / 7 | frames > 50 ms | frames > 100 ms | CPU hottest / mean | GPU hottest | prime core |
|---|---|---|---|---|---|---|
| held at 3187 MHz | 42.1 / 42.1 / 42.2 | 2 / 2 / 2 | 2 / 0 / 0 | 95.1 to 95.5 / 81.9 to 83.6 °C | 75.2 to 77.2 °C | 3187 MHz |
| 307 MHz (scaling) | 43.9 / 44.1 / 44.0 | 1 / 2 / 2 | 0 / 1 / 0 | 94.7 to 95.9 / 83.3 to 84.1 °C | 76.4 to 77.2 °C | 729 to 3187 MHz |

Holding the clock brought no FPS and no lower temperature, so the scaling profile is the one in use. The full clocks
matter: with the Thor's own `performance_mode=2` capping the big cores at about 1.9 GHz, the late game ran at 27.8 FPS
instead of 36.0 ([PERFORMANCE.md](../research/PERFORMANCE.md), "the late game at full clocks"). A fresh game start
re-applies the profile and lifts that cap.

**GPU: held at 680 MHz.** With levels 7 and 8 the GPU stayed at 615 MHz; with `"minGpuPowerLevel":8,"maxGpuPowerLevel":8`
it runs at 680 MHz. In the early-game benchmark that gave nothing (44.7 FPS, GPU still 65 to 70 % busy); in the late
game it gave about 2 FPS (36.7 / 36.5 to 39.0 / 38.6), so it is in use since 2026-10-08.

## `WINEDEBUG=-all` in the container

GameNative 1.2.1 sets `WINEDEBUG` itself and then merges the container's `envVars`, which win: `-all` when Settings →
Debug → Wine debug is off, else `+` and the channels listed on that screen (`XServerScreen.kt`). Read from
GameNative's own logcat line `Env Vars (Final Guest)` at three "Open container" starts on 2026-10-07, 20:39 to 20:43:

| Container `envVars` | `WINEDEBUG` given to Wine |
|---|---|
| with `WINEDEBUG=-all` (as set up) | `-all` |
| without it | `+warn` |
| with it again | `-all` |

GameNative's Wine debug setting was on here with the channel `warn` (its `wine_debug.log` was written at that
start), so without the variable Wine would print its warnings. The container's config was put back byte for byte
afterwards. Leftover debug channels slowed the whole game on 2026-10-06
([archive/WINEDEBUG-LEFTOVER.md](../research/archive/WINEDEBUG-LEFTOVER.md)). With Wine debug off in GameNative the
variable should change nothing (from the code; not tested).

## Graphics drivers compared (2026-10-07, 19:02 to 19:57)

Skirmish benchmark at 120 Hz, the drivers installed with `tools/gn_driver.py` (GameNative Driver Manager, then the
container's Graphics Driver Version), v1.0.0 package. These runs came after hours of back-to-back tests, so the Thor
was hot; earlier the same R4 setup gave 43.8 to 45.6 FPS.

| Driver | FPS minute 1 / 5 / 7 | frames > 50 ms | frames > 100 ms | CPU hottest / mean | GPU hottest |
|---|---|---|---|---|---|
| Turnip v26.2.0 R4 (in use until 2026-10-08) | 42.1 / 42.1 / 42.2 | 2 / 2 / 2 | 2 / 0 / 0 | 95.1 to 95.5 / 81.9 to 83.6 °C | 75.2 to 77.2 °C |
| Turnip v26.3.0-R6 (StevenMXZ, 2026-09-30) | 42.4 / 42.6 / 41.9 | 4 / 0 / 2 | 1 / 0 / 1 | 94.3 / 81.7 °C (minute 7) | 75.2 °C |
| Turnip T30 (MrPurple666 purple-turnip, Mesa 26.3.0, 2026-08-17) | 39.7 / 39.8 / - | 6 / 2 | 2 / 0 | not recorded | GPU 69 to 73 % busy |
| Balemuni Apex v2 ULTIMATE SD 8 Gen 2 (Mesa 26.3.0-devel `b9a2bf3`, 2026-08-26) | - | - | - | - | - |

**Balemuni Apex v2 stops the game** about two minutes after the start, twice in two runs (19:04:32 and 19:09:19):
`Failed to wait for DX12 fence (error 102). Initial value: 3688, Expected value: 3689, Actual value: 3688` then
`-- FATAL EXIT --` in the game's log (error 102 is a wait timeout: the GPU did not finish the submitted work). Its
`meta.json` name contains `/`, and GameNative installed it under the folder name `tmp`. The Balemuni and T30 files on
the Thor matched the SHA-256 digests of their GitHub release assets.

R4 and R6 are the same within the test's spread. On 2026-10-08 a Turnip built from Mesa main gave 52.3 FPS in this
test against 41.0 for R4 the same hour ([GPU-DRIVER.md](../how-it-works/GPU-DRIVER.md)).

## Measured and not kept

Each change was tested alone against a baseline of the same session and reverted. "Skirmish" is the automated
benchmark (FPS at two match minutes), "late game" the replay windows at 46:13 and 48:23. Most tests are detailed in
[PERFORMANCE.md](../research/PERFORMANCE.md) under their date; the 2026-10-07 settings round is on this page, and the
driver options in [GPU-DRIVER.md](../how-it-works/GPU-DRIVER.md).

| Change | Test, date | Result | Against |
|---|---|---|---|
| FEX TSO off (`FEX_TSOENABLED=0`) | skirmish, 2026-10-07 | 42.7 / 42.2 | 43.8 to 45.6 |
| | late game, 2026-10-08 | 39.4 / 39.3; the render thread ran a tenth less and waited more | 39.8 / 39.7 |
| Main thread on the prime core 7, all others on cores 0 to 6 (`tools/agent.py affin 4fb0884 80 7f`, live) | skirmish, 2026-10-07 | 44.6, 53 frames > 50 ms and 2 > 100 ms; not checked whether the masks took effect (a mask outside the game's process mask is refused) | 45.3, 3 and 0 frames |
| | late game, 2026-10-08 | 38.3 / 38.8 | 39.0 / 38.6 |
| Main thread on core 7, render thread on core 6, all others on cores 0 to 5 | late game, 2026-10-08 | 38.9 / 39.0; the render thread lost its core half as often | 39.0 / 38.6 |
| Every game thread allowed on all 8 cores (`agent.py procaffin ff`, live) | late game, 2026-10-08 | 37.6 / 37.0, fewer long frames | 36.7 / 36.5; within the spread, so the package does not set it |
| The 8 `rcss worker` threads on the big cores (mask `f8`) | skirmish, 2026-10-08 | 58 frames over 40 ms per 60 s | 23 and 22 (A-B-A) |
| Render thread at `THREAD_PRIORITY_HIGHEST` | late game, 2026-10-08 | 35.1 / 34.6 | 36.0 / 36.2 |
| Render scale 75 % (from 100 %) | late game, 2026-10-08 | 38.3 / 37.9, 30 / 39 frames > 50 ms, softer picture | 36.0 / 36.2, 19 / 15 frames |
| `shadows` 4 → 2 and `volumetriclighting` 3 → 1 in `configuration_system.lua` | skirmish, 2026-10-07 | 34.3 / 34.2, GPU 71 to 78 % busy | 43.8 to 45.6. The file has no labels, so these values may not mean "lower"; the file was put back byte for byte |
| AVX hidden from the game (`FEX_HOSTFEATURES=disableavx`) | start | The game stops: "Your CPU needs to support AVX instructions to run this game." | |
| Turnip forced to tile rendering (`TU_DEBUG=noconform,gmem`, R4) | skirmish, 2026-10-07 | 28.8, rendered correctly | 43.8 to 45.6 |
| `TU_DEBUG=gmem` on the repo's driver | skirmish, 2026-10-08 | no clean run: two starts stopped with the DX12 fence error | |
| `TU_AUTOTUNE_ALGO=bandwidth` | skirmish, 2026-10-08 | 50.5 / 50.7 | 52.3 / 52.3 |
| `disable_conservative_lrz=true` | skirmish, 2026-10-08 | 51.0 / 50.8 | 52.3 / 52.3 |
| Turnip with Mesa MR !43714 | skirmish, 2026-10-08 | 52.5 / 52.6 | 52.3 / 52.3 |
| Turnip without the same-context timestamp wait before each submit | skirmish, 2026-10-08 | 56.4 / 56.6 | 58.6 / 58.1 |
| `TU_DEBUG=nolrz` (LRZ off) | skirmish, 2026-10-09 | 58.9 / 59.1 | 58.5 to 59.2 that day |
| LRZ writes kept in sysmem passes (a Turnip patch, `TU_DEBUG=sysmem` and `disable_conservative_lrz=true`) | skirmish, 2026-10-09 | 58.4 / 58.8 | 58.5 to 59.2 |
| Anisotropic filtering capped at 1 (a Turnip patch, `TU_MAX_ANISO=1`) | skirmish, 2026-10-09 | 58.5 / 58.6 | 58.5 to 59.2 |
| UBWC off everywhere (`TU_DEBUG=noubwc`) | skirmish, 2026-10-09 | 56.1 / 55.5 | 58.8 / 58.9 |
| UBWC forced on every format list (a Turnip patch) | skirmish, 2026-10-09 | 60.4 / 60.3, but the game views two of those images in incompatible formats: unsafe ([UBWC.md](../how-it-works/UBWC.md)) | 58.5 to 59.2 |
| Turnip `tu_emulate_second_queue=true` | skirmish, 2026-10-08 | 57.7 / 57.7, 269 frames over 33 ms per window | 124 to 152 frames over 33 ms |
| vkd3d-proton 3.0.1-4559a01d (R4 driver) | skirmish, 2026-10-08 | 40.3 / 40.2 | 41.0 / 41.1 |
| `VKD3D_FRAME_RATE=60` | skirmish, 2026-10-09 | 55.6 / 55.8, 39 / 40 frames over 40 ms | that hour's baseline, not recorded in the docs; the session's release-FEX runs read 56.4 to 56.6 FPS, 23 to 37 frames over 40 ms |
| `VKD3D_SWAPCHAIN_LATENCY_FRAMES=2` | skirmish, 2026-10-09 | 56.6 / 56.0, 33 / 47 frames over 40 ms | as above |
| `MESA_VK_WSI_PRESENT_MODE=fifo` (the container uses mailbox) | skirmish, 2026-10-09 | 56.9 / 57.1 but 335 / 312 frames over 33 ms | 56.4 / 56.3 and 112 / 124 frames over 33 ms (the release-FEX run of that session) |
| GameNative's wrapper without present wait (`WRAPPER_DISABLE_PRESENT_WAIT=1`) | skirmish, 2026-10-07 (R4) | 37.0, GPU 55 to 58 % busy | 43.8 to 45.6 |
| | skirmish, 2026-10-08 (repo's driver) | 58.8 / 57.0, frames over 40 ms within the spread | 24 to 67 frames over 40 ms in identical runs |
| Cache for unpacked BCn textures (`WRAPPER_USE_BCN_CACHE=1`) | skirmish, 2026-10-07 | 43.7 / 43.2 | no change |

Patch experiments inside FEX that were not kept are in [LEDGER.md](../research/LEDGER.md) and
[patches/fex](../../patches/fex).

## Notes

- Container `envVars` set by the user win over the values GameNative computes for the graphics driver: GameNative
  merges `container.envVars` after them (`XServerScreen.kt`, `envVars.putAll(container.envVars)`).
- BCn textures: a GameNative debug log of this game from 2026-09-21 shows `WRAPPER_EMULATE_BCN=3`, GameNative's
  "auto". Analysis, 2026-10-09: in the wrapper source of the Winlator-bionic Mesa fork
  (`src/vulkan/wrapper/wrapper_physical_device.c`), "auto" turns emulation off when the driver is Turnip, which
  supports BCn natively. Whether GameNative's wrapper build is exactly that code was not checked; the BCn cache made
  no difference, which fits.
