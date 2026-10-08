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
| Emulation | FEXCore Version | `aoe4-perf5-23` (this repo's [release v1.2.0](https://github.com/tarikbc/aoe4-gamenative/releases/tag/v1.2.0)) |
| Environment | `FEX_TSOENABLED` | `0` (see Speed) |
| Environment | `FEX_EXP_SKIP_CALLRET_RESET` | `1` |
| Environment | `WINEDEBUG` | `-all` |

Everything else stayed as GameNative's "known config" set it: container variant `bionic`, graphics driver
`Wrapper` with `turnip_v26.0.0_R6`, DXVK. The display was at 120 Hz.

Before the first working start, the game's own VC++ 2022 runtime was installed into the container (see below).
Whether the package still needs it was not tested.

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

   So neither 0007 nor 0010 nor 0012 to 0014 is the fix (and not 0002, which v1.2.0 does not have). What remains is
   the FEX base version (`7d3090f`) or patches 0004 and 0006; not narrowed down further. The package was not tried
   with `proton-9.0-arm64ec`.

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

## Notes

- **Input:** the controller's A button did nothing in the game's menus, and GameNative's touch screen moves the
  cursor like a touchpad. The tests clicked through the menus from inside Wine with
  [`tools/probes/mclick.c`](../../tools/probes/mclick.c) (cursor moves, clicks and keys with `SendInput`). Playing
  with touch or a mouse was not tried.
- **Window frame:** from the second start on, the game showed in a window with a title bar and offered only
  1272 × 694; the first start was full screen at 1280 × 720. Not resolved.
- **Multiplayer** was not tested. One start showed "Reconnect to Multiplayer Services".
- **Autosave:** the single long frames (about 800 ms) fit the game's single-player autosave stutter that players
  report on PC; not checked.
