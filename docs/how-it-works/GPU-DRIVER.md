# A newer Turnip driver: +27 % FPS (v1.3.0, 2026-10-08)

**Result:** a Turnip (Mesa's Vulkan driver for Adreno) built from Mesa `main` of 2026-10-08 runs AoE IV at
**52.3 FPS** in the skirmish benchmark and **46.1 FPS** in the late-game replay, against 41 and about 39 with the
Turnip v26.2.0 R4 the setup used until then. Picture unchanged in the tests; the GPU went from 61 to 85 % busy.

| Test | Turnip v26.2.0 R4 (same hour) | Mesa main `c78a9e9` Turnip |
|---|---|---|
| Skirmish benchmark, minutes 1 / 3 (`tools/bench.py`) | 41.0 / 41.1 FPS, GPU 61 % | **52.3 / 52.3 FPS**, GPU 85 %, median frame 16.9 ms |
| Late-game replay, 48:23 (`tools/replay.py window`) | 38.6 (03:25) and 39.7 (11:55) FPS, GPU 68 % | **46.1 FPS**, GPU 82 %, median frame 16.9 ms |

The release asset is `turnip-main-c78a9e9.zip` (SHA-256 `b2e7bf9e81e400cc1511b5d9ada3ef654fe2f3567351cb1cb685e68364e69112`);
in GameNative it shows as `turnip-main-c78a9e9`. vkd3d-proton 3.0.1-4559a01d, tested the same hour with R4, gave
nothing (40.3 / 40.2 FPS); the setup stays on 2.14.1. How to build the driver: [BUILDING.md](../guides/BUILDING.md),
"The Turnip driver".

## How it was found

A late-game scheduler trace ([PERFORMANCE.md](../research/PERFORMANCE.md), "What gates a late-game frame") showed the
render thread waiting about 10 ms per frame on the game's GPU fence, while the GPU was only 60 to 70 % busy. The
waiting thread was `vkd3d_fence`, which signals the game's own D3D12 fence events, so the game was waiting for an
earlier frame's GPU work.

## Why

In the KGSL (Qualcomm kernel driver) back end of Turnip up to Mesa 26.2, a fence check with a zero timeout waited
until the GPU finished the work, because KGSL reads a zero timeout as "wait forever"; vkd3d-proton checks pending
fence points on every submit, so CPU and GPU ran in lock step. Mesa merge request
[!44838](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/44838) (merged 2026-10-08) polls instead of
waiting in that case. R4 (Mesa 26.2.0-devel of 2026-05-12) and StevenMXZ's v26.3.0-R6 (2026-09-29) do not have it.

With the new driver the render thread's fence waits in a 5 s trace fell from 1,882 ms (median 9.8 ms) to 777 ms
(median 5.7 ms), and the main and render threads ran 58 % and 45 % of the time instead of 45 % and 41 %.

Attribution of the gain to that one change is from the trace and the merge request's description; the driver also
carries five months of other Turnip work (for example the ir3 compiler and LRZ changes). A build of Mesa main
without !44838 was not made.

## What came next

With the CPU no longer held in lock step, GPU traces showed the next wait: vkd3d-proton itself kept only one command
buffer in flight. [VKD3D-SUBMIT.md](VKD3D-SUBMIT.md) has that fix (+12 %). The driver options tried on top of this
build (autotune, LRZ, forced tile rendering, a second queue, a merge request and a submit patch) gave nothing; they are
listed in [TUNING.md](../guides/TUNING.md), "Measured and not kept", and the GPU profile behind them is in
[PERFORMANCE.md](../research/PERFORMANCE.md), "GPU render-stage profile".

## Not tested yet

- A full game played by a person on this driver (the benchmark skirmish and the replay ran without a crash).
- AoE II DE on this driver (its container uses `turnip_v26.0.0_R6`).
