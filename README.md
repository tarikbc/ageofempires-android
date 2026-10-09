# Age of Empires on Android (GameNative)

Play **Age of Empires IV** and **Age of Empires II: Definitive Edition** on an Android handheld with
[GameNative](https://github.com/utkarshdalal/GameNative). Both games need the patched CPU emulator (FEX) package
from this repo: without it AoE IV's copy protection freezes the game within minutes, and AoE II DE does not start at
all. AoE IV also gets a newer graphics driver and one vkd3d-proton setting from here, together **+43 % FPS** over the
drivers in circulation.

| | Age of Empires IV | Age of Empires II: DE |
|---|---|---|
| Plays | A full 51-minute game against one A.I., to victory | A 3-player skirmish against two Hardest A.I.s, 17 minutes |
| Speed | **58 FPS** in the first minutes of a 1v1, **46 FPS** in the late-game benchmark | **60 FPS** in that skirmish (the game's own cap) |
| Controls | The Thor's controller, with the game's own controller UI | Touch screen as a touchpad, or a mouse |

![AoE IV: a skirmish with the controller UI, 15 minutes in](docs/img/controller-match-15min-2026-10-07.jpg)

| | |
|---|---|
| ![AoE IV main menu](docs/img/main-menu-2026-10-07.jpg) | ![AoE IV's controller tutorial](docs/img/controller-tutorial-2026-10-07.jpg) |
| ![AoE II DE main menu](docs/img/aoe2-main-menu-2026-10-08.jpg) | ![AoE II DE: an A.I. base in the Imperial Age, 33 minutes in](docs/img/aoe2-skirmish-imperial-2026-10-08.jpg) |

## Status

| | |
|---|---|
| Tested device | AYN Thor (Snapdragon 8 Gen 2, Adreno 740, 16 GB RAM, Android 13), GameNative 1.2.1 |
| Age of Empires IV | Anniversary Edition (Steam), build 16.3.11308. Main menu, tutorial, skirmish vs the A.I., a full 51-minute game to victory. The benchmarks below are automated and repeatable |
| Age of Empires II: DE | Steam, build 101.103.54800.0, three civilization DLCs, no Enhanced Graphics Pack. Main menu, skirmish vs the A.I. |
| Not tested | Multiplayer, the campaigns, other devices. The full AoE IV game was played before the repo's driver and the vkd3d-proton setting existed |

## What you need

- An Android device with a Snapdragon / Adreno GPU and GameNative 1.2.1. Only the AYN Thor was tested; other
  Snapdragon 8 Gen 2 devices are the most likely to work.
- The game on Steam, installed through GameNative.
- From the [latest release](https://github.com/tarikbc/ageofempires-android/releases/latest):
  - `fexcore-aoe4-perf6.wcp` (900 KB), the patched FEX for AoE IV (v1.4.0, with patch 0017). For AoE II DE use
    `fexcore-aoe4-perf5.wcp` from the same release: that is the package tested with AoE II.
  - For AoE IV: `turnip-main-c78a9e9.zip` (2.7 MB), Mesa's Turnip driver built from its 2026-10-08 main branch
    ([TURNIP.md](docs/how-it-works/GPU-DRIVER.md)).

## Setup

### 1. Install the package and the driver

1. Download [`fexcore-aoe4-perf6.wcp`](https://github.com/tarikbc/ageofempires-android/releases/latest/download/fexcore-aoe4-perf6.wcp)
   (AoE IV), [`fexcore-aoe4-perf5.wcp`](https://github.com/tarikbc/ageofempires-android/releases/latest/download/fexcore-aoe4-perf5.wcp)
   (AoE II DE) and, for AoE IV, [`turnip-main-c78a9e9.zip`](https://github.com/tarikbc/ageofempires-android/releases/latest/download/turnip-main-c78a9e9.zip)
   on the device; they land in the Download folder.
2. In GameNative: **Menu → Settings → Contents Manager → Import .wcp from device**, and pick the `.wcp`.
   They show up under the FEXCore type as `aoe4-perf6 (30)` and `aoe4-perf5 (23)`.
3. For AoE IV: **Menu → Settings → Driver Manager → Import ZIP from device**, and pick the driver zip.

### 2. Age of Empires IV: the container

Open the game in GameNative, tap the **cog** next to Play, then **Edit container**. Set:

| Tab | Setting | Value |
|---|---|---|
| General | Container Variant | `bionic` |
| General | Wine Version | `proton-11.0-99-arm64ec-1` (see the note below) |
| General | Executable Path | `RelicCardinal.exe` |
| Graphics | Graphics Driver / Version | `Wrapper` / **`turnip-main-c78a9e9`** |
| Graphics | DX Wrapper | `VKD3D` |
| Emulation | 64-bit Emulator | `FEXCore` |
| Emulation | FEXCore Version | **`aoe4-perf6-30`** |
| Environment | add `WINEDEBUG` | `-all` |
| Environment | add `FEX_EXP_SKIP_CALLRET_RESET` | `1` |
| Environment | add `VKD3D_CONFIG` | `no_staggered_submit` |

Then tap **Save** (top right). What the three variables do: `WINEDEBUG=-all` stops Wine's debug output even when
GameNative's Wine debug setting is on; `FEX_EXP_SKIP_CALLRET_RESET=1` turns on one of the package's patches (about
twice the FPS); `VKD3D_CONFIG=no_staggered_submit` stops vkd3d-proton from holding each GPU submission until the
previous one finished (+12 % FPS, [TURNIP.md](docs/how-it-works/GPU-DRIVER.md)).

| | |
|---|---|
| ![General tab](docs/img/setup-general.jpg) | ![Emulation tab](docs/img/setup-emulation.jpg) |
| ![Graphics tab](docs/img/setup-graphics.jpg) | ![Environment tab, the three variables at the bottom](docs/img/setup-environment.jpg) |

**About the Wine version:** the tested one is listed on the Thor as `proton-11.0-99-arm64ec-1`. It came from a
manual import, and research says it is the same build as GameNative's official `proton-11.0-1-arm64ec`
([WINE-SOURCE.md](docs/guides/WINE-SOURCE.md)). The official one was not run here; if you try it, please report back.

### 3. Age of Empires II: DE: the container

GameNative fills in a "known config" for this game. Keep its graphics settings (`Wrapper`, `turnip_v26.0.0_R6`,
DXVK) and change:

| Tab | Setting | Value |
|---|---|---|
| General | Wine Version | `proton-11.0-99-arm64ec-1` |
| General | Exec Arguments | `SKIPINTRO` |
| Emulation | FEXCore Version | **`aoe4-perf5-23`** |
| Environment | add `FEX_TSOENABLED` | `0` (about 30 % more FPS in big battles; remove it if the game crashes) |
| Environment | add `FEX_EXP_SKIP_CALLRET_RESET` | `1` |
| Environment | add `WINEDEBUG` | `-all` |

Before the tested setup, the game's own VC++ 2022 runtime was installed into the container (GameNative 1.2.1 skips
it); whether that is still needed was not tested. Details: [AOE2-DE.md](docs/guides/AOE2-DE.md).

### 4. Device settings

- **Display at 120 Hz** (the Thor's refresh-rate tile), set **before** the game starts. At 60 Hz a frame that misses
  a refresh waits a whole extra 16.7 ms.
- **AoE IV controller:** in the game, Settings → Controls → input **Gamepad**. The game closes itself once after
  that switch; start it again. The Thor's controller works in its standard mode, with no remapping.
- **Power:** GameNative's Power Control sets the CPU and GPU limits for the container at every start. The CPU needs
  its full clocks (capped at about 2 GHz the late game ran at 27.8 FPS instead of 36), and the GPU held at its top
  level gave about 2 FPS more in the late game. [TUNING.md](docs/guides/TUNING.md) has the values.

### 5. Play

Tap Play. The first start takes a few minutes. In AoE IV a press of the A button skips each intro film and the title
screen.

## What to expect

### Age of Empires IV

![Frame times of the automated skirmish, one panel per step of the work](docs/img/frametimes-progress.png)

*One bar per frame (higher is slower), the same 60 s of the automated skirmish at each step: the protection fixes
alone, the speed patches with the Turnip v26.2.0 R4 driver, the repo's Turnip driver, and the vkd3d-proton setting.*

The benchmark is an automated 1v1 skirmish in which the player does nothing and the camera turns: 90 s windows at
minutes 1 and 3, the game's frame times read from Android's compositor, display at 120 Hz
([TESTING.md](docs/guides/TESTING.md)).

| Setup | FPS (minutes 1 / 3) | median frame | frames over 50 ms |
|---|---|---|---|
| Protection fixes only (2026-10-07, 60 Hz display) | 26.7 / 26.6 | 33.4 ms | 721 / 734 |
| The package, Turnip v26.2.0 R4 (v1.1.0 to v1.2.0) | 42.1 / 42.1 | 25.3 ms | 2 / 2 |
| + the repo's Turnip driver (v1.3.0) | 52.3 / 52.3 | 16.9 ms | 4 / 5 |
| + `VKD3D_CONFIG=no_staggered_submit` | 58.6 / 58.1 | 16.9 ms | 9 / 16 |

Patch 0017 (v1.4.0, the setup above) was measured against the previous package in the same session, with the
benchmark's lighter thermal sampler: 58.3 / 57.8 FPS against 56.4 / 56.3 before and 56.6 / 56.5 after, and about a
fifth fewer frames of 25 ms (813 / 825 against 999 to 1,039 per window). Frames over 40 ms stayed within their spread.

A real game is heavier than the idle benchmark. The **late-game benchmark** is the replay of a full 51-minute game
against one A.I., a 90 s window at minute 48 with the player's own camera, the CPU at full clocks:

| Setup | FPS at minute 48 | frames over 50 ms |
|---|---|---|
| v1.2.0, Turnip v26.2.0 R4, GPU held at 680 MHz | 38.6 | 22 |
| v1.3.0, the repo's Turnip driver | 46.1 | 15 |
| + `VKD3D_CONFIG=no_staggered_submit` (two runs) | 47.3 and 48.5 (47.0 at minute 46) | 86 and 57 (79 at minute 46) |

In the late game the variable gains little and makes the frame times uneven: about 60 to 90 frames of 50 ms or more
per 90 s window against 15 without it ([TURNIP.md](docs/how-it-works/GPU-DRIVER.md)).

During that game, played before the repo's driver existed, GameNative's FPS counter read high 20s to low 30s, and
about 24 in the big late-game battles. The CPU runs hot in long sessions: the hottest CPU sensor read about 95 °C
during the tests, the GPU about 80 °C.

### Age of Empires II: DE

| Situation | FPS | frames over 50 ms |
|---|---|---|
| 3-player skirmish (two Hardest A.I.s, Fast speed, map visible), 7 × 60 s over 17 minutes | 59.3 to 60.0 | 0 to 2 per window |
| Same game, camera on an A.I. base in the Imperial Age | 59.9 | 1 |
| The game's Ranked Benchmark Test (8 players, big battles), battle part | 27 to 30 | many; score 1114.8 |

The game did not go above 60 FPS, although the display ran at 120 Hz and the game's own limit was 120. In the
benchmark the game's main thread used 95 % of one core: the emulated CPU work limits it, not the GPU.

## Known issues

**Age of Empires IV**

- **Sometimes it stops while loading** with "Failed to wait for DX12 fence (error 102)" in its log (several times on
  2026-10-08, on every driver). Start it again; the next start worked each time.
- **Do not stay long in GameNative's Quick Menu during a match.** It pauses the game; after a 24 s pause the game
  exited once (a 9 s pause was fine).
- **A "video card's installed driver version" dialog** can block loading after its one-day "Don't show this
  message" choice expires. GameNative's touch input did not reach its button in our tests; the repo's helper
  `tools/probes/dlgclick` clicks it from adb ([RESEARCH-LOG.md](docs/research/LOG.md), Traps).
- **GameNative's "Save Conflict" dialog** asks which save to keep when the local and the cloud save both changed.
  Pick the one from where you played last.
- **Other graphics drivers:** StevenMXZ's Turnip v26.2.0 R4 and v26.3.0-R6 run the game at 41 FPS, purple-turnip
  T30 about 2.5 FPS slower; **Balemuni Apex v2 crashes the game** after about two minutes
  ([TUNING.md](docs/guides/TUNING.md)).
- **The game needs AVX.** FEX provides it; hiding it makes the game refuse to start.
- **Display mode:** the option stored as `windowmode` 1 gave a black screen; borderless works.
- **End screen "Retrieving...":** after the 51-minute game the result panel said "Waiting to retrieve match results
  from the server" for minutes. The game was not frozen, and the match then appeared in Match History.

**Age of Empires II: DE**

- **The controller does nothing in the menus**, and GameNative's touch screen moves the cursor like a touchpad.
- **A window frame:** from the second start on, the game showed in a window with a title bar at 1272 × 694; the
  first start was full screen. Not resolved.
- **About every 5 minutes a frame of about 0.8 s**, likely the game's single-player autosave.

## How it works

Both games are Windows x86-64 programs. GameNative runs them with Wine (Windows compatibility), **FEX** (translates
x86 code to ARM on the fly), **vkd3d-proton** or DXVK (Direct3D on Vulkan) and a **Turnip** Vulkan driver for the
Adreno GPU. The patches change only the emulator, so it behaves more like Windows; the games and their protections
are not modified.

**Age of Empires IV** ships with Relic's anti-tamper protection, **Aegis**. On a real PC it is invisible, but under
the emulator three things went wrong:

1. **A start-up check failed.** The protection checks that Windows functions are not hooked. Wine's ARM64EC
   build lays out some function stubs (`jmp [addr]`, `FF 25`) in a way that looked like a hook, and the protection
   stopped the game 2 to 3 minutes after the start. **Patch 0007** rewrites those stubs into a form the check
   accepts. ([HOOK-CHECK.md](docs/how-it-works/HOOK-CHECK.md))
2. **A watchdog fired.** One protection thread runs a loop that must keep up with a time budget, and it raises
   thousands of exceptions per second. Each exception cost about 230 µs under Wine (a round trip to the
   wineserver), the loop fell behind, and 8 to 13 minutes in the protection froze the game. **Patch 0010** lets
   FEX resume from an exception directly: 2.5 µs. ([WATCHDOG.md](docs/how-it-works/WATCHDOG.md))
3. **It was slow.** The protection runs some code one instruction at a time from a 16 MB scratch buffer. For FEX
   every step was new code: a fault, a cache flush and a recompile, about 16,000 times per second, 44 % of the
   game's main thread. **Patches 0012 to 0014** recognise that buffer and reuse translations instead of
   recompiling: 26.7 → 43.7 FPS. ([INSTRUCTION-STEPPER.md](docs/how-it-works/INSTRUCTION-STEPPER.md))

**Patch 0016** (v1.2.0) answers the game's CPU-speed question from a short cache. The game asks for the MHz of every
core about 45 times per second, and Wine read two files per core for each answer: 11 % of the main thread in the
late game. ([POWER-INFORMATION.md](docs/how-it-works/POWER-INFORMATION.md))

**Patch 0017** (v1.4.0) delivers the protection's exceptions without a host trap. The protection raises about 44,000
illegal-instruction exceptions per second; FEX turned each one into a host trap, Wine's signal handler and two passes
through the exception dispatcher. FEX now raises the guest exception directly: about +1.5 FPS and a fifth fewer 25 ms
frames in the benchmark. ([TUNING.md](docs/guides/TUNING.md), "Exceptions without a host trap")

**The graphics driver** (v1.3.0) is Mesa's Turnip built from its 2026-10-08 main branch. The Turnip builds in
circulation waited for the GPU on every submit because of a kernel-driver quirk (fixed in Mesa that day), so the
game's render thread spent about 10 ms per frame waiting: 41 → 52 FPS. ([TURNIP.md](docs/how-it-works/GPU-DRIVER.md))

**The vkd3d-proton setting.** GPU traces then showed the GPU working only 37 % of the time. Turnip offers one Vulkan
queue, so the game's graphics, compute and copy queues all share it, and vkd3d-proton 2.14.1 then resolves fence
waits on the CPU and keeps one command buffer in flight per queue. `VKD3D_CONFIG=no_staggered_submit` turns that
off: 52 → 58 FPS. ([TURNIP.md](docs/how-it-works/GPU-DRIVER.md), "What the GPU waits for")

**Age of Empires II: DE** (protected with Arxan) crashed about 1 s after the start with GameNative's own FEX 2512,
inside code it decrypts at run time. Every FEX build from this repo fixes it. Plain upstream FEX-2610 gets past that
crash but exits before the menu, so this repo's patches are still needed; which one was not narrowed down.
([AOE2-DE.md](docs/guides/AOE2-DE.md))

<details>
<summary>All patches in the package</summary>

All against FEX `7d3090f`, in this order ([patches/fex](patches/fex)):

| Patch | What it does |
|---|---|
| 0004 | Reports the game's own memory protection back to it, while FEX keeps its write traps |
| 0006 | A raw x64 `syscall` returns registers like Windows does |
| 0007 | Rewrites Wine's `FF 25` export stubs, in every process that runs x64 code, so the protection's hook check passes |
| 0009 | Skips a costly per-fault reset (on only with `FEX_EXP_SKIP_CALLRET_RESET=1`; about twice the FPS) |
| 0010 | Resumes x64 code after an exception without a wineserver round trip |
| 0012 | Handles the protection's one-instruction-at-a-time buffer without faults and recompiles |
| 0013 | Larger code buffer (512 MB), so FEX clears its whole cache less often |
| 0014 | Reuses compiled code when the protection decrypts the same code again |
| 0015 | Fixes a FEX bug: a 32-bit value was loaded as 64 bits |
| 0016 | Answers the game's per-frame CPU-speed query from a 250 ms cache (v1.2.0) |
| 0017 | Delivers the protection's illegal-instruction exceptions (about 44,000 per second) without a host trap (v1.4.0) |

The package's DLL is `libarm64ecfex.dll`, SHA-1 `7e707379` in `aoe4-perf6` (v1.4.0; `b5e6e357` in `aoe4-perf5`, which stops at 0016). Since v1.1.0 the package leaves out patch 0002, which hid
FEX's name from the game: the game runs the same without it. To build it yourself:
[BUILDING-FEX.md](docs/guides/BUILDING.md) and [`tools/make_fex_wcp.py`](tools/make_fex_wcp.py). The driver:
[`tools/build_turnip.sh`](tools/build_turnip.sh).
</details>

<details>
<summary>For contributors: testing without touching the device</summary>

The speed work was measured from a Mac over adb: [`tools/bench.py`](tools/bench.py) starts AoE IV, skips the
intros, starts a skirmish with the camera turning (controller input written to the Thor's input device) and records
frame times from Android's compositor, with temperatures; [`tools/replay.py`](tools/replay.py) does the late-game
window on a replay; [`tools/fpsgraph.py`](tools/fpsgraph.py) shows a live frame-time graph in a browser;
[`tools/agent.py`](tools/agent.py) reads memory and threads inside the game without console windows; the
`tools/research/turnip_*.py` scripts read the GPU traces. See [TESTING.md](docs/guides/TESTING.md).
</details>

## Upstream

The fixes that belong in FEX or Wine were reported there on 2026-10-08: FEX pull request
[#6021](https://github.com/FEX-Emu/FEX/pull/6021) and issues [#6022](https://github.com/FEX-Emu/FEX/issues/6022) and
[#6023](https://github.com/FEX-Emu/FEX/issues/6023); Wine bugs [60461](https://bugs.winehq.org/show_bug.cgi?id=60461),
[60462](https://bugs.winehq.org/show_bug.cgi?id=60462) and [60463](https://bugs.winehq.org/show_bug.cgi?id=60463).
Which patch is which: [patches/fex](patches/fex#upstream-2026-10-08). The driver's gain comes from Mesa merge request
[!44838](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/44838), already upstream.

## The whole story

Getting here took a long investigation: what AoE IV's protection checks, what was ruled out, and every measurement.
Start at the [docs index](docs/README.md), or read the full [research log](docs/research/LOG.md).

| Folder | What is in it |
|---|---|
| [`docs/how-it-works/`](docs/how-it-works) | One write-up per problem the patches fix, and the protection itself |
| [`docs/guides/`](docs/guides) | The driver, tuning, testing over adb, building FEX, driving GameNative, AoE II DE |
| [`docs/research/`](docs/research) | The research log, the dead ends, and redacted raw run data |
| [`patches/fex/`](patches/fex) | The FEX patches in the package |
| [`patches/experiments/`](patches/experiments) | Earlier Box64, Wine and GameNative patches that are not needed |
| [`tools/`](tools) | Test, install and build scripts ([list](tools/README.md)); one-off scripts in `tools/research` |

The package and the driver are on the [releases page](https://github.com/tarikbc/ageofempires-android/releases).
This repo was called `aoe4-gamenative` until 2026-10-08; old links redirect here.

## Credits and license

Built on [GameNative](https://github.com/utkarshdalal/GameNative), [FEX-Emu](https://github.com/FEX-Emu/FEX),
Wine and Valve's Proton (via [GameNative/proton-wine](https://github.com/GameNative/proton-wine)),
[vkd3d-proton](https://github.com/HansKristian-Work/vkd3d-proton) and Mesa's Turnip driver.

Tools and scripts are MIT (see [LICENSE](LICENSE)). Patches follow their project: FEX MIT, Box64 MIT, Wine
LGPL-2.1-or-later, GameNative GPL-3.0. You need your own copy of the game.

Age of Empires is a trademark of Microsoft. Not affiliated with Microsoft, Relic, World's Edge, AYN or GameNative.
