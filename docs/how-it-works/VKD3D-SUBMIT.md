# What the GPU waited for: vkd3d-proton's staggered submissions (+12 %, 2026-10-08)

**Result:** `VKD3D_CONFIG=no_staggered_submit` in the container's environment gives **58.6 / 58.1 FPS** in the skirmish
benchmark (minutes 1 / 3) against 52.3 / 52.3 the same afternoon with the same driver, median frame 16.9 ms, the GPU
reported 95 % busy instead of 85 %. It is part of the setup since that afternoon (README, PR #25) and was announced
with release v1.4.0. It costs evenness in the late game (below).

## How it was found

GPU render-stage traces of the skirmish with a tracing build of the driver ([PERFORMANCE.md](../research/PERFORMANCE.md),
"GPU render-stage profile"), read with [`turnip_gpu_timeline.py`](../../tools/research/turnip_gpu_timeline.py),
[`turnip_submit_latency.py`](../../tools/research/turnip_submit_latency.py) and
[`turnip_cpu_events.py`](../../tools/research/turnip_cpu_events.py); two traces, 11 s each, same numbers in both:

- **The GPU worked only 37 % of the time.** The union of all "Command Buffer" GPU intervals covers 36 to 37 % of the
  trace, about 7 ms of each 19 ms frame. The kernel's "GPU busy" counter (85 %) counts clock-on time, not work. The
  idle time is 2,600 to 2,900 gaps of 1 to 2 ms and 2,200 of 0.5 to 1 ms per 11 s between consecutive command buffers.
- **Each submission waited before the GPU started it.** Matching the 4,644 `vkQueueSubmit` calls to their GPU
  execution by submission id gives a median 3.4 ms (p90 6.2 ms) between the end of `vkQueueSubmit` and the GPU
  start, a lower bound (the GPU clock in these traces has no usable sync to boot time, so the tightest submission is
  taken as zero latency). The GPU work of a submission is 1.5 ms median. In 2,618 of the 2,663 idle gaps the next
  submission had already been submitted when the GPU went idle.
- **The vkd3d queue thread spent 87 % of its time inside `vkWaitSemaphores`** (871 ms per second, 404 waits per
  second).

> **Note (2026-10-09, analysis, not yet re-measured).** A reading of the driver code suggests that the tracing build itself made most of these idle gaps: without `VKD3D_CONFIG=one_time_submit`, vkd3d-proton's command buffers are not one-time-submit, and the tracing Turnip then adds a timestamp copy that drains the GPU after every command buffer (`tu_queue.cc`) and after every render pass. The FPS gain of `no_staggered_submit` was measured without tracing and stands; the "37 %" does not describe the game without tracing. A clean trace (`one_time_submit` on) is the next step.

## Why

vkd3d-proton 2.14.1 waits on purpose when several D3D12 command queues share one Vulkan queue and were all active in
the last second (`d3d12_command_queue_needs_cpu_waits_locked` and
`d3d12_command_queue_needs_staggered_submissions_locked` in `libs/vkd3d/command.c`): fence waits are resolved on the
CPU instead of on the GPU, and each virtual queue waits for its previous command buffer to finish before it submits
the next one ("essentially allows one command buffer in flight"). Turnip exposes one queue family with one queue, so
the game's graphics, compute and copy queues all alias it. `VKD3D_CONFIG=no_staggered_submit` turns both behaviours
off.

## The late game

In the late-game replay (two runs, 2026-10-08 21:14 and 21:35 to 21:38) the variable gives 47.3 and 48.5 FPS at 48:23
and 47.0 at 46:13, against 46.1 without it, but with 86, 57 and 79 frames over 50 ms per window against 15: the
frame-time distribution widens (584 to 604 frames of one refresh and 165 to 200 of five per window, against 151 and
60), so the late game feels less even with it. The user chose to keep the variable.

A 60 s scheduler trace inside a late-game window with the variable (the tracing itself cost: 42.0 FPS and 191 frames
over 50 ms in that window, so its numbers are indicative) showed a mechanism the early game does not have: in the long
frames the main and render threads were runnable but not running for 17 to 45 ms, queued on the small cores 0 to 2,
while the big cores were busy. Measured over the game's own frames, the threads that add the most time in a long
frame are the protection's thread (+13 ms), the main thread (+11), the simulation thread (+8), each of the 8 job
workers (+4 to 6) and wineserver (+5). Without the variable, vkd3d-proton's CPU waits throttle the whole pipeline
and this contention does not show. Thread placement (the main thread on the prime core, the render thread on a big
core, the rest on the others; [PERFORMANCE.md](../research/PERFORMANCE.md), "Thread placement in the late game") is the
untested candidate; the late-game replay runs were stopped at this point in favour of the quicker skirmish benchmark.

## Also tried in the same session

On top of `no_staggered_submit`:

- A Turnip change that drops a timestamp wait on the submission's own KGSL context before the `IOCTL_KGSL_GPU_COMMAND`
  (in-order execution makes it redundant; the kernel otherwise routes even a same-context sync through its event and
  dispatcher path): 56.4 / 56.6 FPS, no gain, not kept.
- The GPU's inter-frame power collapse (`/sys/class/kgsl/kgsl-3d0/ifpc`) is on and fires about 16 times per second
  in the match with the new setting; the knob is root-only on the Thor, so it was not tested.
- Turnip's `tu_emulate_second_queue=true` (two Vulkan queues, so vkd3d-proton's queues do not alias): same FPS, more
  frames of three refreshes; [PERFORMANCE.md](../research/PERFORMANCE.md), "The hitches at 58 FPS".
