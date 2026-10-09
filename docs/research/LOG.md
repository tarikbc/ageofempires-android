# Research log, 2026-10-06 to 2026-10-09

> The project's working log, kept as it grew: the status history, how the stops were found, verified facts,
> hypotheses and traps. It was the README until 2026-10-07; its main sections stop at v1.2.0 (2026-10-08), and a short 2026-10-09 entry is at
> the end. The same
> history told forward, with every version, is [STORY.md](../STORY.md); the current setup is in the
> [README](../../README.md); later measurements are in [PERFORMANCE.md](PERFORMANCE.md).

Device: AYN Thor (Snapdragon 8 Gen 2, Adreno 740, 16 GB, Android 13), GameNative 1.2.1. Game build 16.3.11308
(24231237).

**Goal:** the game's copy protection, **Aegis** (Relic's in-house virtualization/anti-tamper), stopped the game 2 to
3.5 minutes in on a clean baseline ("2 to 4.5 minutes" in the first README). The user owns the game, so the aim was to
make the protection *accept* this environment, not to strip it out.

## The setup on 2026-10-08 (v1.2.0)

> A snapshot. Since then: package `aoe4-perf6-30` (v1.4.0), driver `turnip-main-c78a9e9`, `VKD3D_CONFIG=no_staggered_submit` and about 58 FPS; the Thor's controller in its standard mode with no swap. The current setup is in the [README](../../README.md).

AoE IV runs on the Thor and can be played with the Thor's controls (the game's controller UI). Tested matches ran
past 20 minutes. Speed in a 1v1 skirmish with the camera turning (compositor frame times, `tools/bench.py`):
**42 to 46 FPS**, median frame 25.3 ms at 120 Hz, frames over 100 ms 0 to 2 per 90 s. Late game (the replay of a
51-minute game, minutes 46 and 48, CPU at full clocks): 36.0 / 36.2 FPS with v1.1.0, 39.0 / 38.6 with v1.2.0 and
the GPU held at 680 MHz ([TUNING.md](../guides/TUNING.md)).

| Part | Setting |
|---|---|
| GameNative | 1.2.1, container variant `bionic` |
| Wine | `proton-11.0-99-arm64ec-1` |
| CPU emulator | FEXCore content `aoe4-perf5-23`: [`fexcore-aoe4-perf5.wcp`](https://github.com/tarikbc/ageofempires-android/releases/tag/v1.2.0) (release v1.2.0), FEX `7d3090f` + [patches](../../patches/fex) 0004, 0006, 0007, 0009, 0010, 0012, 0013, 0014, 0015, 0016 (`libarm64ecfex.dll`, SHA-1 `b5e6e357`; release v1.1.0 was the same without 0016, `bc82c565`; release v1.0.0 also had 0002 and the game-only 0007, `6990a221`) |
| Container `envVars` | `WRAPPER_MAX_IMAGE_COUNT=0 ZINK_DESCRIPTORS=lazy ZINK_DEBUG=compact,deck_emu MESA_SHADER_CACHE_DISABLE=false MESA_SHADER_CACHE_MAX_SIZE=512MB mesa_glthread=true WINEESYNC=1 MESA_VK_WSI_PRESENT_MODE=mailbox TU_DEBUG=noconform VKD3D_SHADER_MODEL=6_0 PULSE_LATENCY_MSEC=144 BOX64_AVX=1 VKD3D_DEBUG=warn WINEDEBUG=-all FEX_EXP_SKIP_CALLRET_RESET=1` (read back from both `.container` copies) |
| GPU driver | Turnip v26.2.0 R4 (v26.3.0-R6, T30 and Balemuni Apex v2 compared in [TUNING.md](../guides/TUNING.md)) |
| Display | 120 Hz (`peak_refresh_rate` and `min_refresh_rate` 120), set before the game starts |
| Power profile | `.config/.power-profile` of the container: `enablePowerControl` true, governor `SCHEDUTIL`, CPU 307200 to 3187200 kHz, GPU levels 8 and 8 (held at 680 MHz; 7 and 8 = 615 to 680 MHz until 2026-10-08, see [TUNING.md](../guides/TUNING.md)); holding the CPU at full clock gave no FPS and no lower temperature ([TUNING.md](../guides/TUNING.md)) |
| Game | `configuration_system.lua`: `resolution` 1280:720, `windowmode` 0 (the user's borderless choice), `verticalsync` false, `frameratelimit` 0; controls set to Gamepad in the game; A/B and X/Y swapped in GameNative's Edit Physical Controller |

Where the speed came from: [INSTRUCTION-STEPPER.md](../how-it-works/INSTRUCTION-STEPPER.md) (patches 0012 to 0014, 26.7 → 43.7
FPS), [TUNING.md](../guides/TUNING.md) (display, drivers, and what did not help). How to measure without touching the
Thor: [TESTING.md](../guides/TESTING.md). The `.wcp` files `aoe4-fixes.wcp` and `fexcore-2610-aoe-nofex*.wcp` (in the
repo root until 2026-10-07, now only in the git history) are from the investigation and do not run the game.

## Status history (2026-10-07)

AoE IV runs on the Thor and can be played, with the Thor's controls, past the points where the protection used to
stop it.

**Speed (later on 2026-10-07):** with FEX patches 0012 and 0013 added (build `20fdc47a`), a 1v1 skirmish with the
camera turning runs at **42 to 44 FPS with a median frame of 16.7 ms**, against 26.7 FPS and 33.4 ms with the
previous build, and frames over 100 ms fell from 71 to 2 in the first 90 s window (compositor frame times, same
automated test, [INSTRUCTION-STEPPER.md](../how-it-works/INSTRUCTION-STEPPER.md)). The cause was the protection running code one
instruction at a time from a scratch buffer, which cost FEX a fault and a compile per instruction. Patch 0014 (build
`6990a221`) keeps the same FPS (43.3 / 43.5 / 43.3 / 42.1 at minutes 1 / 5 / 10 / 20) and removes the full
recompile that still came every 2 to 3 minutes: no frame over 100 ms after the first minute in that run.

![Frame times before and after patches 0012 and 0013](../img/frametimes-before-after.png)

| | |
|---|---|
| ![Main menu](../img/main-menu-2026-10-07.jpg) | ![Controller tutorial](../img/controller-tutorial-2026-10-07.jpg) |
| Main menu, on screen at the first check, 2 min 43 s after the game process appeared | The game's controller UI (tutorial mission) |
| ![Skirmish, 15 minutes in](../img/skirmish-15min-2026-10-07.jpg) | ![Match with controller UI, 15 minutes in](../img/controller-match-15min-2026-10-07.jpg) |
| Skirmish with mouse UI, 15 minutes after launch | Match with the controller UI, 15 minutes after launch |

**Validated runs:**

- 09:27, job build `08172f64` (the patches below plus analysis code): watched for 1,514 s, the log last grew at
  1,456 s; a Skirmish was being played 15 minutes in. The protection's loop cycle averaged about 1.1 s (10-cycle means
  0.6 to 2.0 s) and its watchdog bucket was 0 at every check up to 1,469 s ([`WATCHDOG.md`, part 2](../how-it-works/WATCHDOG.md#part-2-fast-continue-the-watchdogs-real-cost-was-a-wineserver-round-trip-per-exception-2026-10-07)).
- 10:00, the exact repo patch set, build `86d6da39`: 8,200 continues/s took the fast path and 0 the slow one; past
  15 minutes (917 s) the log was still growing and a match was being played with the controller UI.
- 10:50, the same build installed from the `.wcp`: past 15 minutes (940 s) the log was still growing, and a
  Skirmish was being played at 25.3 FPS (HUD, game time 08:27). This run was meant to be without 0009, but the
  container still had `FEX_EXP_SKIP_CALLRET_RESET=1` (see the trap below), so 0009 was on.
- 11:24, build `eca1e25b` (0010 now on by default) from the `.wcp` `aoe-fastcontinue2-11`, container `envVars`
  with no `FEX_EXP_*` variable and with `FEX_TSOENABLED=0` (read back from `.container` after the start): fast path
  6,882/s, slow 0/s; past 15 minutes (1,003 s) the log was still growing, in the main menu (31.4 FPS on the HUD).
  So without 0009 the watchdog stayed quiet, in this run together with TSO off.

**What it takes (as tested):**

1. GameNative 1.2.1 container: Wine `proton-11.0-99-arm64ec-1`, variant `bionic`, 64-bit emulator FEXCore.
2. FEX `7d3090f` with [patches](../../patches/fex) 0002, 0004, 0006, 0007, 0009, 0010, 0012, 0013 and 0014
   ([BUILDING.md](../guides/BUILDING.md)); 0009 only acts with `FEX_EXP_SKIP_CALLRET_RESET=1`. Without 0012 to
   0014 the game also runs, at about 27 FPS.
3. That `libarm64ecfex.dll` packaged as a FEXCore content with [`tools/make_fex_wcp.py`](../../tools/make_fex_wcp.py)
   (the current one is in release v1.2.0: [`fexcore-aoe4-perf5.wcp`](https://github.com/tarikbc/ageofempires-android/releases/tag/v1.2.0), versionName `aoe4-perf5`,
   versionCode 23, DLL `b5e6e357`),
   imported in GameNative (Settings, Contents Manager, Import .wcp from device) and selected in the container's
   Emulation tab, FEXCore Version (tested 2026-10-07 10:34 with `aoe-fastcontinue-10`: at the next start logcat
   shows GameNative applying `fexcore-aoe-fastcontinue-10`, and the installed DLL hashed `86d6da39`; since 11:17
   `aoe-fastcontinue2-11`, DLL `eca1e25b`; since 14:52 `aoe4-perf-18`, DLL `20fdc47a`, with 0012 and 0013; since
   15:58 `aoe4-perf2-20`, DLL `6990a221`, with 0014 too; in the evening `aoe4-perf3-21`, DLL `bc82c565`, without
   0002 and with 0015; on 2026-10-08 `aoe4-perf5-23`, DLL `b5e6e357`, with 0016). The earlier runs installed the DLL by hand instead
   ([GAMENATIVE.md](../guides/GAMENATIVE.md)).
4. Container `envVars`: `WINEDEBUG=-all FEX_EXP_SKIP_CALLRET_RESET=1`. 0010 is on by default since build
   `eca1e25b` (`FEX_EXP_FASTCONTINUE=0` turns it off; earlier builds needed `FEX_EXP_FASTCONTINUE=1`). 0009
   (`FEX_EXP_SKIP_CALLRET_RESET=1`) is not needed against the watchdog (11:24 run), but it roughly doubles the
   match FPS (see Speed below).
5. For the controls: the Thor's controller set to Xbox style (Thor settings), and in the game Settings, Controls,
   input set to Gamepad. In the tested run the game then quit by itself (log: `Requesting game quit with reason:
   Contrast Change`), and it had to be started again from GameNative. To swap A/B and X/Y for this game only:
   GameNative's in-game Quick Menu, Controller tab, Edit Physical Controller, binding A to gamepad B, B to A, X to Y
   and Y to X (confirmed by the user on the Thor). Later on 2026-10-07 the user reported that the Thor's controller
   in its standard mode works properly without that swap (the device then shows as "Odin Controller"); the README
   gives that setup, and the test tools now use it too ([TESTING.md](../guides/TESTING.md)).
6. GameNative's power profile for the container (`.config/.power-profile`, see Speed below) with the CPU allowed up
   to full clock and the GPU held at its top level (680 MHz), and the game's display mode left at (or set back to) borderless.
   Earlier runs held the CPU minimum at full clock too; a later comparison showed no gain from that.
7. The Thor's display at 120 Hz (`peak_refresh_rate` and `min_refresh_rate` 120, set before the game starts): frames
   then step at 8.3 ms instead of 16.7 ms, and frames over 50 ms fell from 18 to 2 to 4 per 90 s. Other levers that
   were measured and did not help are in [TUNING.md](../guides/TUNING.md).

**Known limits:**

- **Speed before 0012/0013 (2026-10-07, match FPS on GameNative's HUD, Skirmish on Danube River, clocks held as
  below; for the current speed see the status above):**

  | TSO | 0009 | FPS |
  |---|---|---|
  | on | on | about 25 (user's reading, 10:5x run) |
  | off | off | 8.0 on the HUD at game time 00:42 (12:08); the user read 10 to 15 |
  | off | on | 22.8, 29.5 and 29.1 on the HUD (12:21 to 12:22) |

  So 0009 is what keeps the FPS up, and `FEX_TSOENABLED=0` brings nothing measurable; TSO stays at its default.
  The CPU side limits the FPS: in these matches the GPU was 22 to 38 % busy.
- **Power profile that worked:** `.config/.power-profile` with `"enablePowerControl":true`, `"minCpuFreq":3187200`,
  `"maxCpuFreq":3187200` (GameNative caps each core group at its own maximum, so all groups are held at full clock:
  2.02, 2.71 and 3.19 GHz here) and `"minGpuPowerLevel":7`, `"maxGpuPowerLevel":8` (GameNative writes
  sysfs level = 8 - value, so the GPU stays at levels 0 and 1, 615 to 680 MHz). These match what the Thor's "high
  performance" mode sets. A profile with `"minCpuFreq":307200` and GPU levels 0 to 8 let the GPU drop to 220 MHz
  (match about 10 FPS by the user's reading).
- **Display mode:** setting the game's display mode to the option stored as `windowmode` 1 in
  `My Games\Age of Empires IV\configuration_system.lua` gave a black screen (the game kept running). Writing
  `variantUInt = 2` back while GameNative was then force-stopped (so the game could not save over it) brought the
  picture back; the user then chose borderless full screen in the game.
- How the caps were found: **GameNative's Power Control profile for the container decides them.** It is the file
  `.config/.power-profile` in the container (`Z:\home\xuser-STEAM_1466860\.config\.power-profile`), read at
  every game start. Here it had `"enablePowerControl":true` with `"maxCpuFreq":2016000`, which GameNative applies
  to every core group as min(value, group maximum). Turning Power Control off in the Quick Menu did not last to the
  next start, and with it off the caps it had written stayed in place. A profile with `"maxCpuFreq":3187200`
  should give every group its full clock; tested since: [TUNING.md](../guides/TUNING.md), "Power profile".
- Measured speed: with those caps, 2.05 GHz (cores 3 to 6) and 1.98 GHz (core 7), GameNative's Performance HUD showed 13.7 FPS in a
  Skirmish (game time 04:36, 10:43) with the GPU 28 % busy, so the CPU side limited it. After the user turned it
  off in a match (logcat 10:54:55: `PowerControl: Clean restore executed`), the caps were 2.71 and 3.19 GHz and the
  HUD averaged about 25 FPS (the user's reading). The Thor's own performance mode was "high performance".
- **Do not stay long in GameNative's Quick Menu during a match.** It pauses the game. In the 10:34 run a 9.3 s
  pause did no harm, and the game exited with code 1 about 1 s after a 23.9 s pause (the game's log:
  `Adding 23908 ms extension to hang detection`, then nothing). Set clocks and other options before the launch.

## How we got here

- **The game runs and is playable (2026-10-07, 09:42).** With FEX patches 0007, 0009 and 0010
  (`FEX_EXP_FASTCONTINUE=1`), the main menu was on screen 2 min 43 s after the game process appeared, and a
  Skirmish against the AI was being played 15 minutes in ([screenshot](../img/skirmish-15min-2026-10-07.jpg)),
  past the point where every earlier run froze. The protection's loop cycle fell from about 2.6 to 3.2 s to about
  1.1 s and its watchdog bucket stayed at 0. The cause was one wineserver round trip per handled exception in
  Wine's ARM64EC `NtContinue` path; 0010 resumes x64 code without it. See [`WATCHDOG.md`, part 2](../how-it-works/WATCHDOG.md#part-2-fast-continue-the-watchdogs-real-cost-was-a-wineserver-round-trip-per-exception-2026-10-07).

- **The kill is measured on a clean baseline.** With `WINEDEBUG=-all` the game loads its menu world in under
  three minutes. Then, 2 min 3 s to 3 min 2 s after start, every thread but one goes to Windows suspend
  count 1 and the log never grows again. That happened in 8 of the 9 runs that got past start-up today; the
  ninth (a control build) exited instead. The thread that does it starts at `RelicCardinal.exe+0x3e69304`.
  See [KILL-REMEASURED.md](archive/KILL-REMEASURED.md) and [`SMC-TRAP.md`, part 3](../how-it-works/SMC-TRAP.md#part-3-hiding-fexs-smc-trap-does-not-stop-the-aegis-kill).
- **The evening runs (19:52 to 20:29) were slowed by leftover debug channels.** The container still had
  `WINEDEBUG=+thread,+sync,+virtual,+timestamp,+tid` from round 17. With it, a run stopped in
  `Property Bag Manager`; without it, the same step took 22 s. The "MapGen wall" was a misreading: that
  message appears in every run that gets further. See [WINEDEBUG-LEFTOVER.md](archive/WINEDEBUG-LEFTOVER.md).
- **The SMC trap is not the trigger.** FEX does leak its write trap to the guest
  ([`SMC-TRAP.md`, part 2](../how-it-works/SMC-TRAP.md#part-2-confirmed-fex-leaks-its-smc-write-trap-to-the-guest)), but a FEX build that hides it (patch 0004, verified with
  `smctest2`) was still killed in 5 of 5 runs. Inside the game, 732,206 memory queries passed the filter
  and none touched a trapped page. See [`SMC-TRAP.md`, part 3](../how-it-works/SMC-TRAP.md#part-3-hiding-fexs-smc-trap-does-not-stop-the-aegis-kill).
- **The session drop is not the trigger either.** Two runs had no `errno=10038` and were killed on time.
- **The kill is a timed job (2026-10-07).** Traced with `WINEDEBUG=+seh`: the kill thread's wait ends by timeout
  (`STATUS_TIMEOUT`) after 200.7 s, and its job then enters the suspend-all function. Two sibling threads run
  other jobs after 8.9 s and 98 s. See [KILL-TIMER.md](archive/KILL-TIMER.md).
- **x86-64 Wine under Box64 now gets through start-up (2026-10-07)** with two new Box64 patches, and stops in
  `Config File` after the protection's hook check reports a mismatch. See [BOX64-ROUTE.md](archive/BOX64-ROUTE.md).
- **Fixing the raw-syscall return registers does not stop the kill (2026-10-07).** On the Thor a raw x64 `syscall` returns
  `rcx` = status instead of the return address. FEX patch 0006 fixes that (verified with `syscallregs`), and
  the game still stopped in 3 of 3 runs. See [SYSCALL-RETURN.md](../how-it-works/SYSCALL-RETURN.md).
- **The start-up kill decision is an API hook check that fails only under ARM64EC Wine (2026-10-07).** The game
  checks 63 API functions for inline hooks; Wine's ARM64EC kernel32 exports 27 of them as bare `jmp [rip+x]`
  (`FF 25`) thunks, which the detector flags (x86-64 Wine adds a hot-patch prolog). FEX patch 0007 rewrites such
  thunks to `48 FF 25`. With it no record is flagged, start-up takes the Box64 branch, and two judged runs got past
  the old kill window with the log growing (to 479 s and 537 s). Both were still stopped later, 8 to 10 minutes in,
  by a decision on a protection worker thread. Found by dumping the decrypted code FEX compiles (patch 0008). See
  [HOOK-CHECK.md](../how-it-works/HOOK-CHECK.md).
- **The later stop is a watchdog on the protection's own loop (2026-10-07).** Each cycle of that loop may take
  2000 ms; the excess accumulates, and above 256 s the protection fails. On the Thor a cycle takes 2.2 to 5.8 s
  (mean 4.3 s), so it overflows after about 8.5 minutes; replaying the formula over the measured cycles hits the
  limit in the cycle where the failing check ran. About 40 % of that thread's time is FEX recompiling the
  protection's decrypt-on-demand code (2,267 compiles/s, 2,085 SMC events/s) and handling exceptions. See
  [`WATCHDOG.md`, part 1](../how-it-works/WATCHDOG.md#part-1-the-later-stop-a-lateness-bucket-on-the-protections-own-loop-2026-10-07).
- **First menu reached (2026-10-07, 07:37).** With FEX patches 0007 and 0009 (skip the per-thread call-ret discard on
  each SMC fault, an unsafe experiment), the game finished loading (`OnEndLoad` at 509 s) and drew its first-run
  Accessibility Settings screen on the Thor. The watchdog bucket still overflowed at about 793 s and the game froze
  about 13 minutes in. See [`WATCHDOG.md`, part 1](../how-it-works/WATCHDOG.md#part-1-the-later-stop-a-lateness-bucket-on-the-protections-own-loop-2026-10-07).

Read [LEDGER.md](LEDGER.md) first: it is the ledger of what was tried and what
happened, including the traps that produced wrong conclusions.

## The only success criterion

> The game's own log (`warnings.log`) is still growing **after ~5 minutes.**

"The process is still alive" is **not** success. The kill *suspends* every thread and leaves the process
hung, so `ps` keeps showing it and the log goes silent. Verified repeatedly.

## Verified facts

1. **The protection is Aegis.** Its build log is in the game folder (`Aegis_RelicCardinal.log`),
   referencing `Foreign/Aegis/Retail/internal/BlockList.json`; it ships a **386-item blocklist**.
2. **The kill:** a launch-time thread, entry `RelicCardinal.exe+0x3e69304`, whose function spans
   `+0x3e6b53c..+0x3e6cd0c`. It suspends all other threads via two indirect `SuspendThread` sites
   (`+0x49065a1`, `+0x490699d`), then spins at 100 % CPU. Its region `0x3e40000..0x3f90000` holds 1561
   registered functions.
3. **FEX advertised itself** — CPUID leaf `0x40000000` returned vendor `FEXIFEXIEMU`. Patched and
   verified live (`eax=0x0 vendor=''`). **The kill still fires.**
4. **The ntdll patches have never run.** Wine loads ntdll from *its own tree*, not
   `C:\windows\system32`. The mapped ntdll is pristine. This voids the earlier "patched ntdll" result.
5. **The game's backend session dies** (`TlsConnection::Shutdown errno=10038` → status `1006`), and the
   kill lands ~73 s later — but a run with a **healthy session throughout still died**. Correlational.
6. **`12152`/`12157` are noise.** They are Xbox Live (XAL) calls, downstream of the asio failure; the
   endpoints answer `200` from Wine.
7. **The kill's log tail is an `RtlWakeAddressAll` storm** — the Wine spinlock pathology
   `patches/experiments/proton-arm64ec-ntdll/waitq_fix.s` exists to fix, which has never been loaded.
8. **Wine TLS to the AoE backend is slow** (5–19 s vs 0.94 s native). `CertificateRevocation=0` fixes
   the timing, not the kill.

## Open hypotheses, best first

> Not needed since 2026-10-07: the stops were the start-up hook check and the watchdog ([HOOK-CHECK.md](../how-it-works/HOOK-CHECK.md), [WATCHDOG.md](../how-it-works/WATCHDOG.md)). Kept as they were written.

**Superseded (2026-10-07):** the two decisions that stopped the game were found and fixed: the start-up hook check
([HOOK-CHECK.md](../how-it-works/HOOK-CHECK.md), patch 0007) and the watchdog on the protection's loop
([`WATCHDOG.md`, part 1](../how-it-works/WATCHDOG.md#part-1-the-later-stop-a-lateness-bucket-on-the-protections-own-loop-2026-10-07), [`WATCHDOG.md`, part 2](../how-it-works/WATCHDOG.md#part-2-fast-continue-the-watchdogs-real-cost-was-a-wineserver-round-trip-per-exception-2026-10-07), patch 0010). None of the hypotheses
below was needed to run the game; they stay as notes and were not tested further.

1. **Aegis hashes a memory region and compares it to an expected value.** Most likely `ntdll.dll`, whose
   bytes differ between Wine's ARM64EC build (Thor) and its x86-64 build (Mac/Rosetta) — which is
   exactly where the behaviour differs. **Located:** Aegis carries its own xxHash implementation at RVA
   `0x3e42d34` (plus siblings `0x3e563cc`, `0x3e681cc`, …), each with exactly one caller — and
   `0x3e681cc` is called from `0x3e68e3f`, ~1.2 KB before the kill thread entry `0x3e69304`. The calls
   pass a fixed 192-byte high-entropy blob at RVA `0x56fbf40`. See
   [KILL-ANALYSIS.md](archive/KILL-ANALYSIS.md).
   *Test:* resolve the parameter flow (Ghidra) to recover the hashed range and the expected hash.
2. **~~FEX leaks its self-modifying-code trap to the guest, and Aegis checks for exactly that.~~ Ruled
   out 2026-10-06** ([`SMC-TRAP.md`, part 3](../how-it-works/SMC-TRAP.md#part-3-hiding-fexs-smc-trap-does-not-stop-the-aegis-kill)): with the trap hidden the kill still
   fires (5 of 5), and no in-game query ever touched a trapped page. Original text kept below.
   **FEX leaks its self-modifying-code trap to the guest, and Aegis checks for exactly that.**
   Under `SMCChecks=mtrack`, FEX re-protects the guest's RWX pages to `PAGE_EXECUTE_READ` to trap writes
   (`InvalidationTracker::GetTrapProt`), and it does **not** intercept the guest's
   `NtQueryVirtualMemory` — so the guest is told its own code page is read-only when it set it
   read-write. Aegis calls `NtQueryVirtualMemory` **35,248 times per run**. This explains the kill's
   indifference to everything environmental, the exact `SMCChecks` sensitivity (`none` → no trap but no
   invalidation → exits at 2 min; `full` → stops at start-up with the trap still armed, re-measured in
   [KILL-REMEASURED.md](archive/KILL-REMEASURED.md)), why the Mac passes, and why
   byte-comparing probes saw a stable image (it is a *protection* change). **Fix:** intercept
   `NtQueryVirtualMemory` and report the untrapped protection. See
   [`SMC-TRAP.md`, part 1](../how-it-works/SMC-TRAP.md#part-1-fex-leaks-its-self-modifying-code-trap-to-the-guest--and-aegis-is-watching-for-it).
3. **A Wine API returns something Windows would not.** One concrete instance:
   `NtSetInformationThread(ThreadHideFromDebugger)` returns `0xC0000002` where Windows returns success —
   a plausible dependency of Aegis's "Stealth-Startup". *Test:* fix it in Wine's unix-side `ntdll.so`
   (the PE export is a bare syscall stub).

## Next actions as of 2026-10-07 (done since)

> The leads of item 1 (290 blocks/s outside 0012's pattern, the code buffer's growth) were not pursued; the later
> speed work came from main-thread sampling (patch 0016) and the GPU side ([STORY.md](../STORY.md)). Item 2: TSO off gave no gain in AoE IV ([TUNING.md](../guides/TUNING.md)) and about 30 % more FPS in AoE II's big battles ([AOE2-DE.md](../guides/AOE2-DE.md)). Item 3 stands.

1. **Speed, what is left after 0012 to 0014:** about 290 blocks/s in the protection's slot buffer do not fit
   0012's pattern and are compiled on each visit, and the code buffer still grows (and so recompiles everything) a
   few times in the first minutes. At about minute 20 of the 0014 run the GPU was 69 to 71 % busy at 615 MHz (sysfs
   `gpu_busy_percentage`, 5 samples), against 22 to 38 % at the old 25 FPS, so the GPU's share of each frame is now
   large too ([INSTRUCTION-STEPPER.md](../how-it-works/INSTRUCTION-STEPPER.md)).
2. **Older speed note.** The game's EXE has no volatile metadata (`VolatileMetadataPointer` 0 in its load config), so FEX
   emulates x86 memory ordering (TSO) on every memory access. Test `FEX_TSOENABLED=0` for speed and stability. Also
   find what the main thread's about 3,000 short waits per second are (FEX locks or the game's own job system).
3. **Proton 11.0-2 was tried (2026-10-07):** no FPS gain, some stutters, and its exception resume path is still
   about 60 times slower than 0010's, so 0010 stays needed ([`WATCHDOG.md`, part 2](../how-it-works/WATCHDOG.md#part-2-fast-continue-the-watchdogs-real-cost-was-a-wineserver-round-trip-per-exception-2026-10-07)). The setup
   stays on 11.0-1.

Earlier open questions (x86-64 Wine under Box64, the kill job's 150 ms call, the xxHash callers,
`ThreadHideFromDebugger`, the waitq ntdll) are no longer needed to run the game; see
[BOX64-ROUTE.md](archive/BOX64-ROUTE.md), [KILL-TIMER.md](archive/KILL-TIMER.md), [KILL-ANALYSIS.md](archive/KILL-ANALYSIS.md)
and [WINE-GAPS.md](archive/WINE-GAPS.md).

## Traps (each cost real time)

- **Alive ≠ working.** See the criterion above.
- **`text.bin` is indexed by `RVA - 0x1000`**, not RVA. Use `RelicCardinal.unpacked.exe` (restored
  `.text`); the on-disk `.exe` is still packed.
- **`uiautomator dump` alone lies here** — use `uiautomator dump --windows`.
- **GameNative manages the Wine version and FEXCore content itself.** Editing `wineVersion` in the
  container config is silently ignored; `envVars` *is* honoured.
- **Anything relying on "the patched ntdll" is void** — it was never loaded.
- **`C06T13R-1X-*` is server connectivity**, not file integrity, and does not prevent playing.
- **A container saved from GameNative's UI gets GameNative's own copy of `envVars`.** On 2026-10-07 an
  `envVars` edit made in `.container` from Wine (10:20) was undone when the FEXCore Version was changed and saved
  in the container editor (10:31): the next two runs still had the removed variable. Change environment variables
  in the editor's Environment tab when the editor is used, and read `.container` back before a judged run.
- **Debug channels left in the container config slow every later run.** Check
  `findstr /c:"WINEDEBUG" "Z:\home\xuser\.container"` before any judged run
  ([WINEDEBUG-LEFTOVER.md](archive/WINEDEBUG-LEFTOVER.md)).
- **Find the game by process NAME.** `explorer.exe` and `winhandler.exe` carry the game's path in their
  arguments, and a match on arguments picks `explorer` first.
- **A modal "unable to determine your video card's installed driver version" dialog** stops loading at
  `Loading step: [Graphics driver check]` once its one-day "Don't show this message" choice has expired
  (seen 2026-10-07 from 01:39). The kill still comes on time. GameNative's touch input cannot reach the button;
  `tools/probes/dlgclick` clicks it, and `run_watch.py` starts it in every run.
- **`tctx` suspends the thread it reads**, and it can hang there, leaving the thread at suspend count 1.
  Use `suspinfo` (no suspend) to judge a kill.

## Setup that worked on 2026-10-08 (v1.2.0)

> A snapshot; the current setup is in the [README](../../README.md).

| Part | Value |
|---|---|
| Wine | `proton-11.0-99-arm64ec` |
| CPU emulator | FEXCore; container variant `bionic` |
| FEX DLL in use | `C:\windows\system32\libarm64ecfex.dll` built from FEX `7d3090f` + patches 0004, 0006, 0007, 0009, 0010, 0012, 0013, 0014, 0015, 0016 (SHA-1 `b5e6e357`), installed by GameNative from the FEXCore content `aoe4-perf5-23`. Before that: without 0016 (`bc82c565`, `aoe4-perf3-21`), with 0002 and the game-only 0007 (`6990a221`, `aoe4-perf2-20`), without 0014 (`20fdc47a`, `aoe4-perf-18`) and without 0012 to 0014 (`eca1e25b`, `aoe-fastcontinue2-11`) |
| FEX switches | none needed with build `eca1e25b` (0010 on by default); the 11:24 run also had `FEX_TSOENABLED=0` |
| DX wrapper | VKD3D (vkd3d-proton 2.14.1 + DXVK 2.4.1-gplasync) |
| GPU driver | Turnip v26.2.0 R4 |
| Executable | `RelicCardinal.exe` (set by hand after import) |
| Wine debug | `WINEDEBUG=-all` in `envVars` (set 2026-10-06 20:49). The session started from it does not define `WINEDEBUG` at all (read with `set` at 21:10), so no trace channels are on |

**Debug output slows the game.** Turn it off in Settings → Debug, in the container Environment tab, and
note Bionic Steam copies Settings channels into `WINEDEBUG` even when the switch is off. (Correction, 2026-10-07: GameNative 1.2.1's code passes the channels only when Wine debug is on, and it was on on this Thor; see [TUNING.md](../guides/TUNING.md).)

## Tools that work over ADB

- **Performance tests without touching the Thor:** `tools/bench.py run LABEL` launches the game, starts a
  skirmish with the camera turning (controller input from adb) and records compositor frame times;
  `tools/fpsgraph.py` shows them live; `tools/agent.py` reads memory and threads inside the game without cmd
  windows. See [TESTING.md](../guides/TESTING.md).
- **Run a Windows program in the live session:** `winhandler.exe` on UDP `127.0.0.1:7946`.
  [`tools/research/winhandler_exec.py`](../../tools/research/winhandler_exec.py) builds the packet. `cmd` + `/c D:\x.bat`
  (program + params must stay ≤ 51 bytes, so wrap in a `.bat`).
- **Open container** (cog → assistant panel) starts `explorer` + `winhandler` in ~30 s **with no game**.
  This is the fast path for probes and config work.
- **Drives:** `D:` = `/sdcard/Download`. Wine trees are `Z:\opt\<wine-name>` (**read-only**).
- **Container config:** `Z:\home\xuser\.container` — editable from inside Wine; `envVars` is honoured.
- **Per-game FEX settings:** `Z:\home\xuser\.fex-emu\AppConfig\RelicCardinal.exe.json`.
- **Emulator DLL name:** set by `HKLM\Software\Microsoft\Wow64\amd64`.
- **Before committing any log or run output:** `python3 tools/redact.py --check docs` must report
  nothing. Game logs carry the Steam name, SteamID64, Relic profile ID and session tokens;
  `tools/redact.py docs/research/samples` replaces them with placeholders.
- **Compare FEX builds:** [`tools/ab_fex.py`](../../tools/ab_fex.py) installs each build in turn (rename trick,
  hash checked) and judges a run on each. [`tools/research/smctest2.c`](../../tools/research/smctest2.c) shows whether the SMC trap
  is visible and still catching rewrites; [`tools/probes/fexstats.c`](../../tools/probes/fexstats.c) reads patch
  0004's counters from a live process.
- **Judge a run:** [`tools/run_watch.py`](../../tools/run_watch.py) `--launch` restarts GameNative, taps Play
  (and restarts the app if no game process appears within 90 s, for the "Syncing cloud saves" hang),
  copies `warnings.log` every 10 s and runs `suspinfo` every 20 s through
  [`tools/thor/mon.bat`](../../tools/thor/mon.bat) (push it to `D:\mon.bat`), and writes a timeline.
- **Probes:** [`tools/probes`](../../tools/probes) (`build.sh` builds all), each writing to `D:\` — `tctx`,
  `tstack`, `suspinfo`, `waitq`, `stk`, `stkscan`, `vq`, `vmmap`, `netprobe`, `selfchk`, `syscallregs`
  (registers after a raw `syscall`), `aegistrace` (copies the trace build's buffer out of the game),
  `waitexit` (exit code and final log when the game exits; `run_watch.py` starts it), `dlgclick` (clicks a
  dialog button by text; `run_watch.py` starts it for the driver-version dialog), `memwatch` (logs every
  change in a memory range of the game, with times), `peek` (hex dump of a range of the game's memory),
  `blkread` (copies patch 0008's block dump out of the game; `tools/research/blkparse.py` and `tools/research/blkmem.py` read it),
  `stkdump` (one thread's whole stack, for stale return addresses), `thunkprobe` (run as `RelicCardinal.exe`:
  counts the thunks patch 0007 rewrote), `cleancopy` (compares loaded system DLL exports with a fresh image
  mapping), `exccost` (time of one handled exception, as the protection uses them), `affin` (lists a process's threads with start address, CPU time and affinity; can pin the threads that start at one address), `modbase` (base address of one module in a process, for `peek` at a FEX global), `xinputprobe` (which XInput pads Wine sees; on 2026-10-07 it saw pad 0 connected. Its state log showed no change in two windows where it is not known whether the controls were used). `tools/research/dettable.py` decodes a `peek` dump of the hook-check table.
- **When the game exits instead of freezing, GameNative closes the container at once** (logcat: `Exit called:
  processes_exited` 34 ms after the game's window went away), so the 10 s log copies miss the end. `waitexit`
  copies the log at that moment. The game also keeps one `LogFiles\unhandled.<start time>.txt` per run
  (49 read on 2026-10-07: lag, network and login-throttling warnings only).
- **Syscall numbers:** [`tools/research/ntdll_syscall_table.py`](../../tools/research/ntdll_syscall_table.py) decodes them from an
  ntdll's own stubs; the device's differ from upstream Wine ([WINE-SOURCE.md](../guides/WINE-SOURCE.md)).

## 2026-10-09

- **The Thor's GameNative is now the 1.3.0 test release.** At 09:17 `gamenative-v1.3.0-prerelease.apk` (GitHub,
  2026-10-04, SHA-256 `90cb39b8…`) was installed over 1.2.1, to run GTA V: under 1.2.1 the Rockstar launcher's
  installer stopped at "Installing Service..." because `RockstarService.exe` crashed inside Wine's `rpcrt4`. The
  APK has the same signing certificate and the same version code (23) as 1.2.1, and still names itself "1.2.1".
  The library, the Steam login and the containers were kept. AoE IV has not been started on it yet. On a game page
  of 1.3.0, the button right of the Options cog uninstalls the game and asks for a confirmation first.
- **Steam log-offs (2026-10-08, about 14:45).** After about 20 game starts that day, Steam logged the account off
  right after each logon ("Logged off of Steam: Fail" about 2 s after "Connected to Steam"), and GameNative 1.2.1
  then crashed on every game page. The game ran again at 15:11 (the first GPU trace).
- **A reading of the Turnip code and the 2026-10-08 GPU traces (analysis, not tested on the device).** Without
  `VKD3D_CONFIG=one_time_submit`, vkd3d-proton's command buffers are not one-time-submit, and a Turnip tracing build
  then adds a timestamp copy that drains the GPU after every command buffer of a submit (`tu_queue.cc`), and another
  after every render pass (`tu_cmd_buffer.cc`). Most of the idle time in the traces lies inside single submissions,
  which fits these drains, so the "GPU worked 37 %" figure does not describe the game without tracing. In the same
  traces about 82 % of render-pass time writes colour targets without UBWC compression (depth has it), LRZ is off in
  every scene pass, and compute takes about 3 ms per frame. Next on the device: `TU_DEBUG=perf` to see why the
  colour targets lose UBWC, a trace with `one_time_submit`, and `TU_DEBUG=noubwc` / `nolrz` as calibration runs.

- **AoE IV on GameNative 1.3.0.** The v1.4.0 setup ran at 58.8 / 58.9 FPS in the skirmish benchmark (11:14), as on
  1.2.1. The Thor's controller had been switched to Xbox style for GTA V; `tools/thor_pad.py` refuses that mode, and
  the benchmark needs the standard mode ("Odin Controller" in `getevent -lp`).
- **Driver work** (11:14 to 15:45): [PERFORMANCE.md](PERFORMANCE.md), "driver tests on GameNative 1.3.0", and
  [UBWC.md](../how-it-works/UBWC.md); release v1.5.0.

## Elsewhere

The patches: [patches/fex](../../patches/fex). Every doc: [docs/README.md](../README.md). License: [README](../../README.md#credits-and-license).
