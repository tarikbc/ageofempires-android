# AoE II volatile-code test

The code and build checks pass. Game tests remain pending.
The new packages are test builds. Keep a copy of the package that works on your device.

## Packages

Download the [test prerelease](https://github.com/tarikbc/ageofempires-android/releases/tag/v1.7.0-rc.1).

| File | Purpose |
|---|---|
| `fexcore-aoe2-volpage6-runtime.wcp` | Main test candidate. Runtime changes without optional logger and observer patches 0018/0019. |
| `fexcore-aoe2-volpage6-reviewed.wcp` | Reviewed research build. It includes 0018/0019; logging stays off unless requested. |

The original `fexcore-aoe2-volpage6.wcp` from Discord predates the review fixes.
Use the new file names and package versions when reporting results.

The known comparison package is [v1.6.0's perf5 download](https://github.com/tarikbc/ageofempires-android/releases/download/v1.6.0/fexcore-aoe4-perf5.wcp).
Its selected version is `aoe4-perf5-23`. It ends at patch 0016.
The v1.6.0 perf7 package includes 0017 and is a different baseline.
The candidate includes 0017; perf5 does not. This comparison measures the full package change and does not isolate patch 0020.

## One comparison window

1. Record your device, game version, GameNative version, Wine version, driver, DXVK version, FEX preset, and selected FEX package.
2. Record resolution, FPS cap, CPU list, and environment variables.
3. Select perf5 as the baseline. Keep the other settings unchanged.
4. Start a known campaign or single-player save. Record the time from loading confirmation to usable gameplay.
5. Exit the game normally. Select `aoe2-volpage6-runtime-42` and restart the same scenario.
6. Record loading time again. Keep the same FPS cap and game settings.
7. Play for 30 minutes. Record FPS, visible faults, crashes, and the last working game action.

Use one baseline run and one candidate run first.
Repeat only if a crash, login delay, update, or other interruption makes the comparison invalid.
Keep background apps and cooling settings consistent. Record any changed condition.
The earlier 40 FPS report used a 40 FPS cap. It does not establish the maximum FPS.

## First Thor result (2026-10-31)

Two candidate runs and three perf5 runs on the AYN Thor in the 4v4 replay benchmark, no FPS cap. The candidate ran
at 53.7 and 55.6 FPS with 48 and 21 frames over 50 ms; perf5 at 41.4 to 48.5 FPS with 128 to 230. The replay
loaded in 13 s with the candidate and 45 s with perf5. Twice the first start after a FEXCore change did not reach the
game, once with each package; a third change started at once, and later starts worked. Details in [AOE2-DE.md](AOE2-DE.md), "Where the main thread's time
goes". A 30-minute play test is still pending.

## Result record

| Field | Result |
|---|---|
| Device and RAM | Pending |
| Game and GameNative versions | Pending |
| Wine, driver, DXVK | Pending |
| FEX preset and full settings | Pending |
| Baseline file, selected version, and SHA-256 | Pending |
| Candidate file, selected version, and SHA-256 | Pending |
| Scenario and loading-time start/end points | Pending |
| Baseline loading time | Pending |
| Candidate loading time | Pending |
| FPS cap and observed FPS | Pending |
| 30-minute gameplay result | Pending |
| Crashes or visible faults | Pending |

Share only redacted logs and configuration exports.
The package checksums and build details are in the prerelease assets.

## Stable release gate

A stable release needs a confirmed comparison and sustained gameplay without a new fault.
Record the actual result before updating the default AoE II package in the setup guide.
AoE IV needs a separate regression test before this package replaces its tested setup.
