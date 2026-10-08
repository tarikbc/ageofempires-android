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

## Not tested yet

- A full game played by a person on the new driver (the benchmark skirmish and the replay ran without a crash).
- AoE II DE on the new driver (its container uses `turnip_v26.0.0_R6`).
- Turnip run-time options: for engines named `DXVK|vkd3d` the driver's built-in config selects
  `tu_autotune_algorithm=prefer_sysmem`; `TU_AUTOTUNE_ALGO=bandwidth|prefer_gmem`, `TU_DEBUG=gmem` and
  `disable_conservative_lrz` are the candidates for a later A/B.
