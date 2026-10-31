# Age of Empires II: Definitive Edition on the Thor

**Result (2026-10-08):** AoE II DE runs on the AYN Thor in GameNative 1.2.1 with this repo's FEX package. With
GameNative's own FEXCore 2512 the game shows a black screen with only the cursor and exits about 1 s after the start.
With the package it reached the main menu, played a 3-player skirmish against two A.I.s (Hardest) for 17 minutes
without a crash, and held **60 FPS** in it (59.3 to 60.0 FPS per 60 s window). In the game's heavy 8-player
benchmark it ran at 27 to 30 FPS.

Game build 101.103.54800.0 (#185872), Steam, installed by GameNative with three civilization DLCs and without the
Enhanced Graphics Pack (9.36 GiB download, 15.41 GiB on disk).

## Setup that worked

Open the game in GameNative, cog, **Edit container**:

| Tab | Setting | Value |
|---|---|---|
| General | Wine Version | `proton-11.0-99-arm64ec-1` (the same as for AoE IV) |
| General | Exec Arguments | `SKIPINTRO` |
| Emulation | FEXCore Version | `aoe4-perf5-23` (this repo's [release v1.2.0](https://github.com/tarikbc/ageofempires-android/releases/tag/v1.2.0)) |
| Environment | `FEX_TSOENABLED` | `0` (see Speed) |
| Environment | `FEX_EXP_SKIP_CALLRET_RESET` | `1` |
| Environment | `WINEDEBUG` | `-all` |

Everything else stayed as GameNative's "known config" set it: container variant `bionic`, graphics driver
`Wrapper` with `turnip_v26.0.0_R6`, DXVK. The display was at 120 Hz.

Before the first working start, the game's own VC++ 2022 runtime was installed into the container (see below).
Whether the package still needs it was not tested here; a user on a newer GameNative version reports that it is
needed (2026-10-10), so the README lists it as a step.

Other devices (user reports, 2026-10-10, not tested here): a Galaxy S25 (Snapdragon 8 Elite) runs the game with this
setup and the driver `Turnip gen8 v30`. On an Honor Magic V2 (Snapdragon 8 Gen 2, Adreno 740, 2026-10-11) the game
ran, but frames often came out almost white with the cursor still drawn; `TU_DEBUG=noconform,noubwc` (UBWC off)
stopped it. The driver in that report is not known, and the Thor never showed the flashing.

The same user later reported a tuned setup (2026-10-12): this repo's setup plus GameNative's DXVK async cache on,
the container's CPU list set to cores 3 to 7 (the Snapdragon 8 Gen 2's big cores; cores 0 to 2 are its little
cores), the FEXCore preset PERFORMANCE and `TU_DEBUG=noconform,noubwc`, at 1024 × 768. Result: faster loading and a
steady 40 FPS with the frame rate capped at 40, so it shows nothing about the top speed. With `FEX_TSOENABLED=0` the
preset adds little (README, section 3). The CPU list and the async cache were not measured here; for AoE IV,
moving its 8 `rcss worker` threads to cores 3 to 7 gave more long frames ([TUNING.md](TUNING.md)). AoE II DE was not
installed on the test Thor from 2026-10-12 to 2026-10-31, and these reports are not retested.

## What was wrong

1. **The start-up crash.** With the known config (`proton-9.0-arm64ec`, FEXCore 2512) the game process lived about
   1 s. Wine's log: `Unhandled page fault on read access to 0000000000000401 at address 0000000142C04C8E`, that is
   `AoE2DE_s.exe+0x2c04c8e`, with x64 `r14` (FEX's `x21`) = `0x401`. The code at that address is encrypted in the
   file (the game uses Arxan anti-tamper, per [PCGamingWiki](https://www.pcgamingwiki.com/wiki/Age_of_Empires_II:_Definitive_Edition)),
   so the fault is inside code the game decrypts at run time. GameNative's public reports
   (`api.gamenative.app/api/compatibility?gameId=8590`) for the Thor: 4 and 5 stars with this config in March and
   April 2026, only "does not open" / "no graphics" in September 2026. Linux players reported start crashes after
   a game update on 2026-06-02 ([Proton issue 3189](https://github.com/ValveSoftware/Proton/issues/3189)); not
   checked here.
2. **VC++ 2022 was not installed.** GameNative 1.2.1's `VcRedistStep` installs the VC++ runtimes 2005 to 2019 from
   a game's `_CommonRedist`, not 2022 (read in its source, tag `v1.2.1`). The game ships
   `_CommonRedist\vcredist\2022\VC_redist.x64.exe` and `.x86.exe`. Both ran in the container with
   `/install /quiet /norestart` and returned 0; Wine then loaded Microsoft's `MSVCP140.dll` (native). **The crash
   stayed**, at the same address.
3. **The FEX package fixed it.** Start results with `proton-11.0-99-arm64ec-1`:

   | FEXCore content | Result |
   |---|---|
   | 2512 (GameNative) | black screen, the game exits (also with `proton-9.0-arm64ec`) |
   | `aoe4-perf5-23` (v1.2.0) | main menu |
   | `aoe4-perf5-23` with `FEX_EXP_FASTCONTINUE=0` (patch 0010 off) | main menu |
   | `aoe4-perf2-20` (v1.0.0; patch 0007 there acts only in AoE IV's process) | main menu |
   | `aoe-fastcontinue2-11` (without patches 0012 to 0014) | main menu |
   | FEX-2610 upstream (`14c92681f`), no patches (2026-10-08, 10:18) | passes the 1 s crash, loads for about 5 minutes, exits before the main menu |
   | The same with two `CheckCall` fixes (a FEX pull request draft) | the same (09:39) |

   So neither 0007 nor 0010 nor 0012 to 0014 is the fix (and not 0002, which v1.2.0 does not have). Newer FEX gets
   past the start-up crash on its own, but plain upstream still exits before the menu, so at least one of this repo's
   other patches (0004, 0006 or 0009, the ones `aoe-fastcontinue2-11` has besides those ruled out; FEX-2610 is also a
   different base than `7d3090f`) is needed for the rest; not narrowed down further. The package was not
   tried with `proton-9.0-arm64ec`.

## Speed

Measured with `tools/frametimes.py` (compositor frame times, 60 s windows) and `tools/thermals.py`:

| Situation | FPS | frames > 50 ms | Notes |
|---|---|---|---|
| 1v1 start, idle (package only, before the three variables) | 60.0 | 0 | GPU 34 % busy at 680 MHz |
| 3-player skirmish (Hardest A.I.s, Fast speed, map visible), 7 windows over 17 minutes | 59.3 to 60.0 | 0 to 2 | One frame of 783 to 834 ms in 3 of 7 windows |
| Same game, camera on an A.I. base in the Imperial Age (game time 33:19) | 59.9 | 1 | p99 33.7 ms; CPU up to 93.9 °C |

The game never went above 60 FPS, although the display ran at 120 Hz, its own FPS limit was 120 and V-Sync was
off; where that cap comes from was not found.

**The game's benchmark** (Options, **Ranked Benchmark Test**; an 8-player Imperial Age game with large battles,
about 90 s, then a score):

| Container | Score | FPS in the battle part |
|---|---|---|
| Package, no extra variables | 1059.3 | 19 to 24 |
| + `FEX_EXP_SKIP_CALLRET_RESET=1`, `WINEDEBUG=-all` | 1069.5 (twice) | 18 to 24 |
| + `FEX_TSOENABLED=0` | 1114.8 | 27 to 30, far fewer frames over 50 ms |

The game's main thread used 95 % of one core in the benchmark and the GPU 26 to 40 %, so the main thread limits it.
`FEX_TSOENABLED=0` turns off FEX's x86 memory-order emulation: faster, but multi-threaded code can then misbehave.
The 17-minute skirmish above ran with it and did not crash; if the game crashes, remove that variable first.

## Late-game benchmark from a replay (2026-10-31)

A tester's replay of a 54-minute online 4v4 (Mountain Pass, large map, population 200 per player, Imperial Age) is
far heavier than the skirmish above. The tester saw large frame drops late in that game. The replay holds the
players' names, so it stays out of the repo.

**Setup.** AoE II DE was installed again on the Thor on 2026-10-31: build 101.103.57257.0 (#188329), the build of
the replay (a replay plays only on its own build), with the same three DLCs and the container settings above.
GameNative 1.2.1. The game's VC++ 2022 installers did not start this time (exit code `0xC0000135`, a DLL not found);
the game ran without them. The display mode was **Full desktop** (Options, Graphics): the game then fills the
1280 × 720 screen without the window frame (see Notes).

**Steps.** All clicks go through `mclick.exe` in `D:\aoe2\` (`tools/replay.py aoe2speed` for the speed buttons):

1. Copy the `.aoe2record` file to `%USERPROFILE%\Games\Age of Empires 2 DE\<SteamID>\savegame\` (a batch file run
   through `winhandler` with `copy`, or GameNative's container file explorer).
2. Single Player, Load Game, the **Replays** tab, the replay, Load Replay.
3. Run to minute 49 at the fastest speed (`replay.py aoe2speed fastest`, then `replay.py clock --aoe2` to read the
   clock); it took 5.5 minutes. Then `replay.py aoe2speed normal`.
4. Click the minimap at desktop (1180, 673): the camera goes to the red player's base, next to the armies. The
   replay has no recorded camera, so View Lock does not follow a player; the minimap click gives the same view.
5. `replay.py window LABEL` (90 s of frame times, then 20 s of per-thread CPU).

**Results** (`aoe4-perf5-23`, the three variables above, display at 120 Hz):

| Window | Camera | FPS | frames > 50 ms | Main thread | GPU busy |
|---|---|---|---|---|---|
| Minutes 39 to 42 | Empty ground at the top of the map | 58.8 | 15 | 80 % | 49 % |
| Minutes 45 to 48 | Same | 59.6 | 2 | 82 % | 50 % |
| Minutes 49 to 51 | Red player's base, next to the armies | 46.7 | 133 | 87 % | 44 % |
| Same, second run from a new load | Same | 48.5 | 128 | 87 % | 46 % |

Near the armies, about one frame in 35 took 50 ms or more. The game's main thread used 87 % of one core and the GPU
less than half of its time, so the main thread limits this part too, as in the Ranked Benchmark Test. A second
thread, `WidgetManager`, used about 44 % of one core. The CPU's hottest sensor read about 96 °C.

### Where the main thread's time goes, and the volpage6 package (2026-10-31)

`tools/agent.py` (`target AoE2DE_s.exe`, then `sample '#<tid>' 4000 5`) sampled the main thread 4,000 times, 5 ms
apart, in the army window. The main thread is the thread that starts at `exe+2d6edc4`; its id changes per start
(`agent.py threads`). With `aoe4-perf5-23`:

| Where | Samples | What |
|---|---|---|
| `AoE2DE_s.exe` | 66 % | Spread over 518 functions (from the exe's `.pdata`); none above 2.5 % |
| Anti-tamper page `exe+f77000` to `exe+f7a000` | 3.5 % | The page patch 0020 marks volatile ([VOLATILE-CODE-PAGES.md](../how-it-works/VOLATILE-CODE-PAGES.md)) |
| `ucrtbase.dll` | 6.7 % | Most calls come from that page, with pointers into it as the first argument; the rest is the game's `memcpy` and `longjmp` |
| No module | 4.5 % | Code outside every module, such as the protection's private buffer |
| `ntdll.dll`, `kernel32.dll` | 7.4 %, 4.0 % | Game calls; the first argument is often the process heap (`0x170000`) |
| `win32u.dll`, `d3d11.dll` | 6.2 %, 2.9 % | The window loop (`USER32!UpdateWindow`) and Direct3D. Samples in system calls include waits |
| `libarm64ecfex.dll` | 1.1 % | FEX itself |

So the anti-tamper code still runs during play, not only while a match loads. The test package
`aoe2-volpage6-runtime-42` ([v1.7.0-rc.1](https://github.com/tarikbc/ageofempires-android/releases/tag/v1.7.0-rc.1),
SHA-256 checked) in the same container, same window:

| Package | FPS | frames > 50 ms | Main thread | GPU busy | Anti-tamper page | ucrtbase | No module |
|---|---|---|---|---|---|---|---|
| `aoe4-perf5-23`, two runs | 46.7, 48.5 | 133, 128 | 87 % | 44 to 46 % | 3.5 % | 6.7 % | 4.5 % |
| `aoe2-volpage6-runtime-42`, one run | 53.7 | 48 | 84 % | 50 % | 0.4 % | 3.4 % | 0.3 % |

About +12 % FPS and 60 % fewer long frames in one run; a second run was not done. The samples were taken at minute
49 (perf5) and minute 53 (volpage6). The replay loaded in 13 s and 10 s (Play Again) with volpage6; the perf5 load
was not timed. The **first** start with volpage6 showed a black screen and no game process after 75 s; the second
start worked. The cause is not known (`WINEDEBUG=-all`, no log).

## Notes

- **Input:** the controller's A button did nothing in the game's menus, and GameNative's touch screen moves the
  cursor like a touchpad. The tests clicked through the menus from inside Wine with
  [`tools/probes/mclick.c`](../../tools/probes/mclick.c) (cursor moves, clicks and keys with `SendInput`). Playing
  with touch or a mouse was not tried.
- **Window frame:** from the second start on, the game showed in a window with a title bar and offered only
  1272 × 694; the first start was full screen at 1280 × 720. On 2026-10-31 the game again showed the frame with
  Display Mode "Full screen"; **Full desktop** (Options, Graphics) fills the 1280 × 720 screen without it.
- **Multiplayer** was not tested. One start showed "Reconnect to Multiplayer Services".
- **Autosave:** the single long frames (about 800 ms) fit the game's single-player autosave stutter that players
  report on PC; not checked.
