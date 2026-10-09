# Performance measurements, in date order

The speed measurements of AoE IV after the protection fixes, with their date and setup (the settings round of
2026-10-07 afternoon is in [TUNING.md](../guides/TUNING.md)). The settings that came out of
them are in [TUNING.md](../guides/TUNING.md), the fixes in [how-it-works](../how-it-works), and the story that ties
them together in [STORY.md](../STORY.md). The earlier speed work (patches 0009 and 0012 to 0014, 26.7 to 43.7 FPS)
is in [LOG.md](LOG.md), "Speed before 0012/0013", and [INSTRUCTION-STEPPER.md](../how-it-works/INSTRUCTION-STEPPER.md).

**Before comparing numbers across sections:**

- **Display:** sections before 2026-10-07 17:00 ran at 60 Hz (frames are whole 16.7 ms steps); later ones at 120 Hz
  (8.3 ms steps).
- **Windows:** the skirmish benchmark (`tools/bench.py`) measured 90 s windows at match minutes 1 and 5 (sometimes
  also 7 or 10) until the v1.3.0 driver test of 2026-10-08, then at minutes 1 and 3 (`--at 1,3`). The late-game benchmark is the replay of
  one 51-minute game, windows at 46:13 and 48:23 ([TESTING.md](../guides/TESTING.md)).
- **Heat:** runs after hours of back-to-back tests read 1 to 3 FPS lower than a cool Thor.
- **Thermal sampler:** from the 2026-10-09 session (00:40) the benchmark's temperature sampler uses shell builtins instead of
  starting two `cat` processes per thermal zone (190 per sample). In that session the same setup read 56.4 to 56.6 FPS
  against 58.6 the day before; the cause of the difference was not isolated. Compare only inside one session.
- **Driver and vkd3d-proton:** Turnip v26.2.0 R4 until the v1.3.0 driver test of 2026-10-08, then the repo's Turnip
  (`c78a9e9`); with `VKD3D_CONFIG=no_staggered_submit` from the vkd3d-proton session of 2026-10-08 (15:40 to 16:30) on.

## 2026-10-07, 16:50: where the frame time goes (60 Hz)

Main thread 52.6 % of one core, render thread 34.6 %, the protection's loop 21.9 %, `vkd3d_queue` 9.6 %, eight
`rcss worker` threads 5 to 10 % each; GPU 68 to 71 % busy at 615 MHz. No single stage is saturated, so the stages wait
on each other.

## 2026-10-07, 21:54 to 22:17: the v1.1.0 package runs at the same speed without 0002

The package without patch 0002, with 0007 in every process and with 0015 (`aoe4-perf3-21`, DLL `bc82c565`), against
the v1.0.0 package (`aoe4-perf2-20`, `6990a221`), back to back on a Thor already hot from earlier runs (120 Hz, R4
driver, scaling power profile):

| Package | FPS minute 1 / 5 | frames > 50 ms | frames > 100 ms | CPU hottest | GPU hottest |
|---|---|---|---|---|---|
| v1.1.0 (21:59, 22:03) | 41.3 / 40.7 | 6 / 5 | 0 / 0 | 96.3 / 95.1 °C | 79.6 / 81.2 °C |
| v1.0.0 (22:12, 22:16) | 41.0 / 40.8 | 3 / 7 | 0 / 0 | 96.3 / 96.3 °C | 81.2 / 82.0 °C |

The same within the test's spread. Earlier that evening a cooler Thor gave 42.6 FPS at minute 1 (v1.0.0, 21:20).

## 2026-10-07, 22:30 to 23:24: a full game

The user played a full skirmish to victory with the v1.1.0 package: 51 min 22 s on Danube River against one A.I.
(Intermediate). GameNative's FPS counter read high 20s to low 30s for most of the game and about 24 in the big
late-game battles (the user's reading). The skirmish benchmark measures the first minutes of an idle 1v1, so it is far
lighter than this. The game kept its replay (`playback\temp.rec`, 2.8 MB, overwritten by the next match); a copy
outside the repo can serve as a repeatable late-game test.

After the match the result panel said "Retrieving..." ("Waiting to retrieve match results from the server") for
minutes. The game log had no network error and nothing from the server after the match ended; the match then showed
in Match History.

## 2026-10-07 23:46 to 2026-10-08 00:02: the late game on that game's replay, CPU capped

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

## 2026-10-08, 00:37 to 03:25: the late game at full clocks

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
([TUNING.md](../guides/TUNING.md), "Power profile"). The setting: `"minGpuPowerLevel":8,"maxGpuPowerLevel":8` in the container's
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

## 2026-10-08, 09:19 to 09:23: where the late game waits

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

## 2026-10-08, 10:56 to 11:18: thread placement in the late game

The same replay windows, v1.2.0 with the GPU at 680 MHz, one replay pass per setup. `agent.py procaffin ff` first
(the process mask was `bf` in pass A and `df` in pass B), then per-thread masks; `/proc` showed them in effect for 68
threads, while about 10 threads that Windows does not list (likely Wine or driver threads) kept the process default.

| Setup | FPS 46:13 / 48:23 | frames > 50 ms | GPU busy | render thread pushed off its core /s |
|---|---|---|---|---|
| Default core mask (03:23, "The late game at full clocks") | 39.0 / 38.6 | 18 / 22 | 67 / 68 % | 573 (09:20 profile) |
| A: main thread on core 7 only, all others on cores 0 to 6 | 38.3 / 38.8 | 31 / 12 | 66 / 68 % | 432 |
| B: main on core 7, render thread on core 6, all others on cores 0 to 5 | 38.9 / 39.0 | 34 / 5 | 67 / 67 % | 304 |

Pass A's windows started about 12 s late (46:25, 48:35). Pinning halved how often the render thread lost its core,
but the FPS stayed within the spread, so the package does not set it. `agent.py taffin '#tid' mask` sets one thread.

## 2026-10-08, 11:52 to 12:14: what gates a late-game frame

Two replay passes, windows at 46:13 and 48:23, v1.2.0 with the GPU at 680 MHz. Between the windows a 5 s
`atrace -t 5 sched freq sync gfx` (the system `perfetto` crashed in `traced_probes` while building its ftrace table on
this firmware, so its ftrace data is empty), analysed with [`tools/research/at_analyze.py`](../../tools/research/at_analyze.py).

| Pass | FPS 46:13 / 48:23 | frames > 50 ms | render thread running | main thread running |
|---|---|---|---|---|
| Released setup | 39.8 / 39.7 | 15 / 9 | 41.0 % | 45.5 % |
| `FEX_TSOENABLED=0` | 39.4 / 39.3 | 14 / 10 | 36.6 % | 44.0 % |

What the trace shows (released setup):

- **The render thread waits for the GPU once per frame.** The `vkd3d_fence` thread woke it 185 times in 5 s (about
  once per frame), after a median wait of 9.8 ms, 38 % of the render thread's time. `vkd3d_fence` signals the game's
  own D3D12 fence events, so this is the game waiting for an earlier frame's GPU work, not vkd3d-proton's swapchain
  limit (that defaults to 3 frames, `VKD3D_SWAPCHAIN_LATENCY_FRAMES`, swapchain.c in 2.14.1).
- **The main thread waits for its job workers.** It ran 45.5 % and slept 50.7 %; the 8 `rcss worker` threads woke it
  most often. It also slept 783 times in a 1 ms timed wait (157 per second).
- **The workers queue for cores.** Each was runnable but not running 11 to 22 % of the time, and ran 15 to 25 % of
  its time on the small cores 0 to 2. The process mask left out one big core (`0-5,7` this start; the game's cpuset
  `top-app` allows 0-7, and GameNative logged CPU list 0-7, so something sets it explicitly; not found).
- **Frames sit on the 120 Hz grid:** about 60 % took 3 refreshes (25.3 ms), 20 % took 2 and 17 % took 4.

TSO off cut the render thread's CPU time by about a tenth and the time went into waiting; the FPS stayed the same, as
it did with pinned threads ("Thread placement in the late game" above). So the late game is not limited by how fast FEX runs the CPU
side: it is limited by the GPU work per frame and the game waiting on it. The levers left are on the GPU side: less
GPU work per frame (the 75 % render scale gave about 2 FPS, "Not kept" above) or a faster driver path.

*Later the same day:* the GPU traces in [VKD3D-SUBMIT.md](../how-it-works/VKD3D-SUBMIT.md) (a tracing build; see the note under the GPU render-stage profile) showed that the GPU itself worked only 37 % of the
time; the wait was vkd3d-proton holding each command buffer until the previous one finished
(`VKD3D_CONFIG=no_staggered_submit`, +12 % in the skirmish benchmark).

## 2026-10-08, 15:00 to 15:30: GPU render-stage profile

A Turnip built with `-Dperfetto=true -Dallow-fallback-for=perfetto` (the same build script plus those two options)
sends GPU timestamps of every command buffer, render pass, blit, clear and compute dispatch to Android's Perfetto
service. In the container: `MESA_GPU_TRACES=perfetto`. Record with
`perfetto --txt -c tools/research/turnip_renderstages.cfg -o /data/misc/perfetto-traces/x.pftrace` from `adb shell`
while the game runs (on Android the data source is `gpu.renderstages`, not `gpu.renderstages.msm`). The trace
processor rejects the GPU clock of this driver (`clock_sync_failure_unknown_source_clock`, 89,045 packets), so
[`tools/research/turnip_stages.py`](../../tools/research/turnip_stages.py) and
[`turnip_lrz_reasons.py`](../../tools/research/turnip_lrz_reasons.py) decode the events from the file instead.
The tracing driver itself costs about 15 % FPS (43.7 against 52.3 in the skirmish benchmark), so its numbers are
shares, not absolute times.

Skirmish benchmark, camera spinning, 11 s (spin2.pftrace):

| Stage | GPU ms per second | per second |
|---|---|---|
| Command Buffer (all GPU work) | 370 | 1,155 |
| Render Pass | 299 | 2,598 |
| Bypass (render passes in system memory) | 230 | 2,598 |
| Compute | 58 | 430 |
| Clear Sysmem | 8 | 833 |

About 50 render passes per frame, all in system-memory mode (the driver's built-in config picks
`tu_autotune_algorithm=prefer_sysmem` for vkd3d), and every pass ends with `lrzStatus = DISABLED`. The heaviest
passes are the full-screen depth passes (85 and 57 GPU ms per second, bandwidth 13 to 14 per sample, up to 1,700
draws), and in all of them LRZ writes are disabled at draw 1 with the reason "Depth write + blending": the first
draw of each pass writes depth with blending on, and the driver then stops LRZ writes for the whole pass
(`tu_lrz.cc`, conservative rule). The driver's switch for that rule, `disable_conservative_lrz=true` as a container
variable, changed nothing in the benchmark (51.0 / 50.8 FPS against 52.3), so either the rule is not the limit or
another reason disables LRZ too; not resolved. Per-draw stages (shader hashes) are off by default in the driver and
were not recorded.

*Update 2026-10-09:* a driver build that names the blend state behind each "Depth write + blending" case found a
color write mask of RGB without alpha (`a0:mask7/f`) and no blending, in passes of mostly 2 to 4 draws (last section).
How that fits the heavy passes above was not checked.

> **Note (2026-10-09, analysis, not yet re-measured).** A reading of the driver code suggests that the tracing build itself made most of the GPU idle time in traces like this one: without `VKD3D_CONFIG=one_time_submit`, vkd3d-proton's command buffers are not one-time-submit, and the tracing Turnip then adds a timestamp copy that drains the GPU after every command buffer (`tu_queue.cc`) and after every render pass. The FPS gain of `no_staggered_submit` was measured without tracing and stands; the "37 %" does not describe the game without tracing. A clean trace (`one_time_submit` on) is the next step.

The CPU side of the same trace: 1,100 `vkQueueSubmit` per second from `vkd3d_queue` (about 25 per frame) and
24,000 semaphore waits per second.

## 2026-10-08, 19:40 to 20:50: the hitches at 58 FPS

With the repo's driver and `VKD3D_CONFIG=no_staggered_submit` the skirmish benchmark runs at about 58 FPS, and a
few frames per minute still take 42 or 51 ms (5 or 6 refreshes of the 120 Hz display). In one 94 s window: 988
frames of one refresh, 3,271 of two, 868 of three, 163 of four, 56 of five and 11 of six or more. They are isolated:
the frames before and after are normal. A still camera gives 46 frames over 40 ms per window against 67 with the
camera turning, so streaming new content is not the main cause.

What a 40 s scheduler trace on the display's clock shows for the long frames
([`tools/research/stutter_align.py`](../../tools/research/stutter_align.py), method in [TESTING.md](../guides/TESTING.md)):

- **Not the emulator.** FEX's own counters (a build with a per-thread statistics table, sampled 4 times a second)
  give 4.6 ms of translation per second over all threads, the same in bins with a long frame as without (4.5 against
  4.6 ms/s); its signal time (0.9 against 1.0 ms/s) and self-modifying-code events (342 against 348 per second) are
  flat too.
- **Not the clocks, not other apps.** Inside long frames the CPU clocks are equal or higher (prime core 2,988 against
  2,899 MHz, big cores 2,672 against 2,641), the game's threads are rarely runnable-but-waiting, and other processes
  take a few ms at most. (Two exceptions were this repo's own measurement tools: `cat` of the thermal zones and
  `dumpsys` took up to 30 ms on a big core in a few frames.)
- **Two sources.** The game's own presents (the wrapper's `QueueSubmit` marks, 59.0 per second) and GameNative's
  `queueBuffer` to Android (58.9 per second) are both in the trace
  ([`present_chain.py`](../../tools/research/present_chain.py)). Of 22 long display frames, 14 contain a long frame of
  the game itself (a gap of 25 to 49 ms between its presents), and 8 have a normal game cadence (gaps of 11 to 16 ms)
  while GameNative queued the buffer late: the latency from a game present to GameNative's `queueBuffer` is 6.5 ms
  median, 15.5 ms p90, 30 ms max.
- **The game-side long frames are job bursts.** Measured in game time (frames between the game's presents, 57 over
  30 ms in 40 s): the main and render threads sleep 20 to 31 ms waiting to be woken by `rcss worker` threads, each
  of the 8 workers runs 9 to 10 ms instead of 1 to 2, and the protection's thread (`exe+3e1b04c`) runs 11.6 ms
  instead of 2.8. That thread works in 80 to 100 ms bursts every 0.26 s, a quarter of a core, but the bursts overlap
  the long frames only at chance level (27 of 55), so it is a bystander.

Tried against it:

| Change | Frames over 40 ms per 60 s | Result |
|---|---|---|
| The 8 `rcss worker` threads pinned to the big cores (mask `f8`), A-B-A in one match | 23, **58**, 22 | Worse: the workers then compete with the main and render threads. Default placement stays |
| `WRAPPER_DISABLE_PRESENT_WAIT=1` (GameNative's wrapper no longer waits for the game's present), skirmish benchmark | 24 / 35 per 90 s window | 58.8 / 57.0 FPS. Within the spread of the same setup without it (24 to 67 in six windows). Not kept |
| Turnip `tu_emulate_second_queue=true` (two Vulkan queues for vkd3d-proton) | 29 / 40 per 90 s window | 57.7 / 57.7 FPS, but 269 frames over 33 ms per window against 124 to 152: more 3-refresh frames. Not kept |

The counts of frames over 40 ms vary from 24 to 67 between windows of the same setup, so a single run cannot show a
small gain here. The two sources above are the game's own job system and GameNative's compositor; neither has a
knob in this repo. The compositor latency is reported with the numbers above for GameNative.

## 2026-10-09, 00:40 to 02:05: settings tried around patch 0017

Patch 0017 itself, its measurements and the thermal-sampler change are in [TRAPLESS-FAULTS.md](../how-it-works/TRAPLESS-FAULTS.md). Tried in the same session and not kept (each against the baseline of its hour):

| Change | Result |
|---|---|
| `VKD3D_FRAME_RATE=60` | 55.6 / 55.8 FPS, frames over 40 ms 39 / 40: lower FPS, no fewer long frames |
| `VKD3D_SWAPCHAIN_LATENCY_FRAMES=2` | 56.6 / 56.0 FPS, 33 / 47 over 40 ms |
| `MESA_VK_WSI_PRESENT_MODE=fifo` (the container uses mailbox) | 56.9 / 57.1 FPS but 335 / 312 frames over 33 ms and 70 / 61 over 40 ms |

Two findings that closed other leads:

- **GameNative adds little.** In the scheduler trace, GameNative's X server thread (`RequestHandler`) is woken by the
  game's Mesa WSI present thread (`WSI swapchain q`) a median 5.2 ms (p90 11.4) after the game's present: that wait is
  the WSI thread waiting for the GPU to finish the frame. From there to GameNative's `queueBuffer` takes about 0.1 ms
  in most frames ([`tools/research/gn_chain.py`](../../tools/research/gn_chain.py)).
- **LRZ.** A Turnip build that names the cause showed every "Depth write + blending" case as a color write mask of RGB
  without alpha (`a0:mask7/f`), no blending; the passes it affects are mostly 2 to 4 draws, so early depth rejection
  would save little there.

## 2026-10-09, 11:14 to 15:45: driver tests on GameNative 1.3.0

The Thor now runs the GameNative 1.3.0 test release ([LOG.md](LOG.md), "2026-10-09"). Setup as v1.4.0 (FEX
`aoe4-perf6-30`, `VKD3D_CONFIG=no_staggered_submit`), skirmish benchmark; until 14:30 windows at minutes 1 and 3, then
at minute 1 only (the two windows had agreed within about 0.4 FPS all day). The leads came from a reading of the Turnip
source and of the 2026-10-08 GPU traces.

| Run | Driver, variables | FPS |
|---|---|---|
| 11:14 | release `turnip-main-c78a9e9` | 58.8 / 58.9 |
| 11:29 | diagnostic build, `TU_DEBUG=perf` (log only) | 57.8 |
| 11:54 | release, `TU_DEBUG=noubwc` | 56.1 / 55.5 |
| 12:07 | UBWC forced on every format list | 60.4 / 60.3 |
| 12:22 | release | 59.2 / 58.8 |
| 12:37 | forced, with a log of incompatible views | 60.2 / 60.0 |
| 12:59 | `TU_UBWC_RGBA8_IGNORE_R32=1` | 59.8 / 59.9 |
| 13:12 | release, `TU_DEBUG=nolrz` | 58.9 / 59.1 |
| 13:23 | `TU_MAX_ANISO=1` | 58.5 / 58.6 |
| 13:35 | LRZ in sysmem passes, `TU_DEBUG=sysmem`, `disable_conservative_lrz=true` | 58.4 / 58.8 |
| 13:46 | release | 58.6 / 58.5 |
| 14:05 | both `TU_UBWC_` rules | 60.8 / 60.6 |
| 14:16 | `TU_UBWC_RGBA8_IGNORE_R32=1` | 59.0 / 58.9 |
| 14:28 | release | 59.0 / 58.3 |
| 14:42 | both rules | 58.5 |
| 14:52 | `TU_UBWC_RGBA16F_INT=1` | 58.0 |
| 15:02 | release | 57.8 |
| 15:12 / 15:21 / then | release, both rules, release, both rules | 58.1, 60.1, 58.6, 59.6 |

What the logs showed: no shader takes Turnip's software-float path; Turnip's performance log names the colour targets
without UBWC and the reason ("mutable formats"); a build that printed the format lists and a build that logged every
incompatible view are the basis of [UBWC.md](../how-it-works/UBWC.md). The FPS drifted down by about 1 over the
afternoon, so only runs close in time are compared. FEX's disk code cache was not retested: it was tried on
2026-10-07 ([WATCHDOG.md](../how-it-works/WATCHDOG.md)), and the source shows it conflicts with patches 0012 and 0014.
The Mesa shader cache (8 MB of its 512 MB) and vkd3d-proton's pipeline cache were both active.

