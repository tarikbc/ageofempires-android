# A newer Turnip driver: +27 % FPS (2026-10-08)

**Result:** a Turnip (Mesa's Vulkan driver for Adreno) built from Mesa `main` of 2026-10-08 runs AoE IV at
**52.3 FPS** in the skirmish benchmark and **46.1 FPS** in the late-game replay, against 41 and about 39 with the
Turnip v26.2.0 R4 the setup used until then. Picture unchanged in the tests; the GPU went from 61 to 85 % busy.

| Test | Turnip v26.2.0 R4 (same hour) | Mesa main `c78a9e9` Turnip |
|---|---|---|
| Skirmish benchmark, minutes 1 / 3 (`tools/bench.py`) | 41.0 / 41.1 FPS, GPU 61 % | **52.3 / 52.3 FPS**, GPU 85 %, median frame 16.9 ms |
| Late-game replay, 48:23 (`tools/replay.py window`) | 38.6 (03:25) and 39.7 (11:55) FPS, GPU 68 % | **46.1 FPS**, GPU 82 %, median frame 16.9 ms |

The release asset is `turnip-main-c78a9e9.zip` (SHA-256 `b2e7bf9e81e400cc1511b5d9ada3ef654fe2f3567351cb1cb685e68364e69112`);
in GameNative it shows as `turnip-main-c78a9e9`. vkd3d-proton 3.0.1-4559a01d, tested the same hour with R4, gave
nothing (40.3 / 40.2 FPS); the setup stays on 2.14.1.

## Why

The late-game frame trace ([TUNING.md](TUNING.md), "What gates a late-game frame") showed the render thread waiting
about 10 ms per frame on the game's GPU fence. In the KGSL (Qualcomm kernel driver) back end of Turnip up to Mesa
26.2, a fence check with a zero timeout waited until the GPU finished the work, because KGSL reads a zero timeout as
"wait forever"; vkd3d-proton checks pending fence points on every submit, so CPU and GPU ran in lock step. Mesa merge
request [!44838](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/44838) (merged 2026-10-08) polls instead
of waiting in that case. R4 (Mesa 26.2.0-devel of 2026-05-12) and StevenMXZ's v26.3.0-R6 (2026-09-29) do not have it.

With the new driver the render thread's fence waits in a 5 s trace fell from 1,882 ms (median 9.8 ms) to 777 ms
(median 5.7 ms), and the main and render threads ran 58 % and 45 % of the time instead of 45 % and 41 %.

Attribution of the gain to that one change is from the trace and the merge request's description; the driver also
carries five months of other Turnip work (for example the ir3 compiler and LRZ changes). A build of Mesa main
without !44838 was not made.

## Build it yourself

[`tools/build_turnip.sh MESA_SRC NAME`](../../tools/build_turnip.sh) on a Mac: Android NDK r27 (the `darwin-x86_64`
toolchain runs natively on arm64 Macs), `meson` and `mako` in a Python environment on `PATH`, Homebrew `bison` 3.8
and `flex` (the system bison 2.3 is too old for the ir3 parser), `glslang`, `ninja`. A shallow clone of
`https://gitlab.freedesktop.org/mesa/mesa.git` builds in about two minutes on an M-series Mac:

```sh
python3 -m venv ~/.venvs/mesa && ~/.venvs/mesa/bin/pip install meson mako pyyaml packaging
brew install bison flex glslang ninja
git clone --depth 1 https://gitlab.freedesktop.org/mesa/mesa.git
PATH=~/.venvs/mesa/bin:$PATH tools/build_turnip.sh mesa turnip-main-$(git -C mesa log --format=%h -1)
```

Meson options: release build (Mesa refuses LTO), `platforms=android`, `platform-sdk-version=36`,
`android-stub=true`, `vulkan-drivers=freedreno`, `freedreno-kmds=kgsl`, `vulkan-beta=true`, API 33 clang. The zip
holds `libvulkan_freedreno.so` (stripped, 14.4 MB) and `meta.json`; GameNative reads `name`, `libraryName` and
`driverVersion` from it and refuses a `name` that is already installed, so each build needs a new name.

Install: **Menu → Settings → Driver Manager → Import ZIP from device**, then the container's Graphics tab, Graphics
Driver Version. From adb: `tools/gn_driver.py import turnip-main-c78a9e9.zip turnip` and
`tools/gn_driver.py select turnip-main`. GameNative's log shows the load:
`hook_android_dlopen_ext: loading custom driver: .../adrenotools/turnip-main-c78a9e9/libvulkan_freedreno.so`.

## GPU render-stage profile (2026-10-08, 15:00 to 15:30)

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

The CPU side of the same trace: 1,100 `vkQueueSubmit` per second from `vkd3d_queue` (about 25 per frame) and
24,000 semaphore waits per second.

## What the GPU waits for: vkd3d-proton's staggered submissions (2026-10-08, 15:40 to 16:30)

**Result:** `VKD3D_CONFIG=no_staggered_submit` in the container's environment gives **58.6 / 58.1 FPS** in the skirmish
benchmark (minutes 1 / 3) against 52.3 / 52.3 the same afternoon with the same driver, median frame 16.9 ms, the GPU
reported 95 % busy instead of 85 %. It is in the README setup. The late-game replay was not re-measured yet.

How it was found, from the render-stage traces above
([`turnip_gpu_timeline.py`](../../tools/research/turnip_gpu_timeline.py),
[`turnip_submit_latency.py`](../../tools/research/turnip_submit_latency.py),
[`turnip_cpu_events.py`](../../tools/research/turnip_cpu_events.py); two traces, 11 s each, same numbers in both):

- **The GPU worked only 37 % of the time.** The union of all "Command Buffer" GPU intervals covers 36 to 37 % of the
  trace, about 7 ms of each 19 ms frame. The kernel's "GPU busy" counter (85 %) counts clock-on time, not work. The
  idle time is 2,600 to 2,900 gaps of 1 to 2 ms and 2,200 of 0.5 to 1 ms per 11 s between consecutive command buffers.
- **Each submission waited before the GPU started it.** Matching the 4,644 `vkQueueSubmit` calls to their GPU
  execution by submission id gives a median 3.4 ms (p90 6.2 ms) between the end of `vkQueueSubmit` and the GPU
  start, a lower bound (the GPU clock in these traces has no usable sync to boot time, so the tightest submission is
  taken as zero latency). The GPU work of a submission is 1.5 ms median. In 2,618 of the 2,663 idle gaps the next
  submission had already been submitted when the GPU went idle.
- **The vkd3d queue thread spent 87 % of its time inside `vkWaitSemaphores`** (871 ms per second, 404 waits per
  second). vkd3d-proton 2.14.1 does that on purpose when several D3D12 command queues share one Vulkan queue and were
  all active in the last second (`d3d12_command_queue_needs_cpu_waits_locked` and
  `d3d12_command_queue_needs_staggered_submissions_locked` in `libs/vkd3d/command.c`): fence waits are resolved on
  the CPU instead of on the GPU, and each virtual queue waits for its previous command buffer to finish before it
  submits the next one ("essentially allows one command buffer in flight"). Turnip exposes one queue family with one
  queue, so the game's graphics, compute and copy queues all alias it. `VKD3D_CONFIG=no_staggered_submit` turns both
  behaviours off.

Also tried in the same session, on top of `no_staggered_submit`:

- A Turnip change that drops a timestamp wait on the submission's own KGSL context before the `IOCTL_KGSL_GPU_COMMAND`
  (in-order execution makes it redundant; the kernel otherwise routes even a same-context sync through its event and
  dispatcher path): 56.4 / 56.6 FPS, no gain, not kept.
- The GPU's inter-frame power collapse (`/sys/class/kgsl/kgsl-3d0/ifpc`) is on and fires about 16 times per second
  in the match with the new setting; the knob is root-only on the Thor, so it was not tested.

## Not tested yet

- A full game played by a person on the new driver (the benchmark skirmish and the replay ran without a crash).
- AoE II DE on the new driver (its container uses `turnip_v26.0.0_R6`).
- Run-time options tried in the skirmish benchmark on this driver (52.3 FPS as released): `TU_AUTOTUNE_ALGO=bandwidth`
  50.5 / 50.7, a build with Mesa MR !43714 52.5 / 52.6, `disable_conservative_lrz=true` 51.0 / 50.8. `TU_DEBUG=gmem`
  got no clean run (two starts stopped with the DX12 fence error). Nothing beats the default.
- Turnip's `tu_emulate_second_queue=true` (a second Vulkan queue on the same kernel submit queue, which would give
  vkd3d-proton's compute and copy queues their own Vulkan queue): the run was stopped before it measured.
- The late-game replay with `VKD3D_CONFIG=no_staggered_submit`.
