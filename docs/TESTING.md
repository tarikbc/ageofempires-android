# Testing on the Thor without touching it

How a performance test runs from the Mac, start to end, and what each tool does. Everything here was tested on
2026-10-07 (GameNative 1.2.1, AoE IV 16.3.11308, the user's Thor).

## One command

```sh
python3 tools/bench.py run LABEL --at 1,5,10 --out bench_out
```

It starts the game, gets through the intro films, starts a 1v1 skirmish, holds the camera turning, and records 90 s
of frame times at each listed match minute. One line per window goes to `bench_out/results.tsv`, for example:

```
clean18  minute 1  2026-10-07 14:59  4136 frames in 94.6 s: 43.7 FPS; frame ms mean 22.9 median 16.7 p99 33.4 max 166.8; >50 ms 29, >100 ms 2
```

Select the FEX build to test first (`tools/gn_import_wcp.py`, then `tools/gn_select_fex.py`), because the run starts
the game with whatever the container has. `--attach` starts from a game that already shows its main PLAY page.
The helper programs the launcher starts in the session (`waitexit`, `dlgclick`) and the agent are copied to
`Download/aoe/` only, and all three are built as GUI programs, so they open no console window.

The parts, which also work alone:

| Step | Tool | What it does |
|---|---|---|
| Launch | `tools/run_watch.py --launch --quiet` | Restarts GameNative, opens the AoE IV page (waits for the suggested-game card, checks the title), taps Play, answers GameNative's **Save Conflict** dialog with **Keep local**, then only watches the process over adb. |
| Intros | `tools/bench.py boot` | Presses physical A (the game's B) until the main PLAY page shows (the purple "The Crucible" tile). It skips each intro film and the title screen, and closes notices shown over the menu, such as the "Server Maintenance" notice of 2026-10-07, whose game-A button would open a browser. The screen is checked before every press. |
| Skirmish | `tools/bench.py skirmish [--from-menu]` | Single Player, Skirmish, "Solo Battle vs A.I." (1v1, Standard, Danube River), Start, waits for the load screen's Play button, starts the match, holds the right stick. |
| Camera | `tools/bench.py spin on/off` | Holds the right stick fully right (camera keeps turning) or centres it. |
| Frames | `tools/bench.py record SECONDS`, `tools/frametimes.py` | Frame times from Android's compositor, as a summary line. |
| Live graph | `tools/fpsgraph.py` | A frame-time graph in the Mac's browser, see below. |
| Threads | `tools/threadcpu.py [SECONDS]` | CPU use of each game thread (from `/proc` over adb, by the game's thread names) and GPU load and clock. |
| Heat | `tools/thermals.py [SECONDS]` | Hottest CPU sensor and mean of all CPU sensors (`cpu-*`, `cpuss-*` thermal zones), hottest GPU sensor (`gpuss-*`), GPU load and clock, mid and prime core clocks. `bench.py` samples it every 3 s in each window and adds it to the result line. |
| Drivers | `tools/gn_driver.py import FILE.zip WORD`, `gn_driver.py select TEXT` | Imports an adrenotools driver zip through GameNative's Driver Manager, and selects an installed driver (drawn white in the list; online ones are grey) in the container's Graphics tab. |
| Compare | `tools/framecmp.py CSV` | Splits a recording at the longest gap (a Quick Menu visit pauses the game) and compares before and after. |

The player does nothing in these matches. The A.I. kills the idle villagers by about match minute 20 (seen at
00:21:41 in one run) and can destroy the town (defeat at 00:19:51 in another), so windows up to minute 15 measure a
running match; later ones may measure the end screen.

## Controller input from adb

`tools/thor_pad.py` writes evdev events into the Thor's built-in controller, so the game (and GameNative) see them as
real input:

- The controller (Xbox style in the Thor's settings) is "Xbox Wireless Controller", `/dev/input/event9` on the
  tested unit (found by name). The node is writable by adb's shell user, so `sendevent` works without root.
- Buttons: A 304, B 305, X 307, Y 308, LB 310, RB 311, SELECT 314, START 315; D-pad on `ABS_HAT0X`/`ABS_HAT0Y`;
  right stick `ABS_Z`/`ABS_RZ` (-32767 to 32767).
- With GameNative's Edit Physical Controller A/B and X/Y swap, **physical B is the game's A** (confirm). START
  starts the match in the skirmish lobby.
- **One stick write stays held.** After `thor_pad.py stick R 1 0` the camera kept turning, and `getevent -lp`
  still read `ABS_Z` 32767 more than 10 minutes later. There is no need to repeat the event (or to wedge the stick).

## Frame times without instrumentation

`dumpsys SurfaceFlinger --latency '<layer>'` lists the present time of the last 128 frames of one surface. The game
is drawn into GameNative's `SurfaceView[app.gamenative/...](BLAST)` layer, so these are the game's own frames. Nothing
runs inside Wine or the game, and the Thor's only extra work is one `dumpsys` per second.

- In a 24 s check it gave 26.7 FPS while GameNative's HUD showed between 15.8 and 35.5 in the same seconds; the
  compositor values are per frame, the HUD's are 1 s averages.
- Frames are 16.7 ms or 33.4 ms or longer: presentation follows the 60 Hz display.

`tools/fpsgraph.py` keeps one `adb shell` loop running that prints the window once a second, merges the windows on the
Mac and serves a live graph (one bar per frame, like Minecraft's frame graph) at `http://127.0.0.1:8790`, with FPS,
1 % low, median, and the count of frames over 50 and 100 ms. Other scripts can put a labelled marker on it:
`curl -s 'http://127.0.0.1:8790/mark?label=loop%20paused'`.

## GameNative's Performance HUD

With the HUD on, 28.2 FPS (187 s); after the user turned it off in the Quick Menu, 29.1 FPS (130 s), with long frames
(over 100 ms) at 0.55/s and 0.45/s. The difference is within the spread of this test, so the HUD costs a few percent
at most. Its source (GameNative's `PerformanceHudView`) samples CPU, GPU, RAM, battery and temperatures once a second.

The Quick Menu could not be opened from adb: neither `input keyevent 4` (back) nor `input keycombination 59 111`
(Shift+Esc) opened it during a match. Use the Thor's own input for it.

## Measuring inside the game without cmd windows

The older probes run as `cmd /c D:\x.bat` through GameNative's `winhandler` (UDP 7946). Each call opens a console
window on the Wine desktop and starts a new process under FEX. `tools/run_watch.py` used to do that every 10 s
(`mon.bat`) for the whole run; `--quiet` turns it off.

`tools/probes/aoeagent.c` replaces them: a GUI-subsystem exe (no console) started once with `tools/agent.py start`,
then driven through files in `D:\aoe\agent\` (one `adb shell` call per command, about 0.17 s round trip):

```sh
python3 tools/agent.py start
python3 tools/agent.py threads                       # tid, start address (module+offset), CPU ms, affinity
python3 tools/agent.py mod libarm64ecfex.dll         # module base and size in the game
python3 tools/agent.py peek libarm64ecfex.dll 3ee000 40
python3 tools/agent.py peekfile 0 149c40000 1000 D:\\aoe\\agent\\slots.bin   # raw bytes into a file (0 = absolute)
```

Tested through the agent: `ping`, `threads`, `mod`, `peek`, `peekfile`, `affin`. A screenshot after starting it and running
commands showed no window. It also has `pause`, `duty` and `blkdump`, ported from the tested probes
`tpause`, `tduty` and `blkread`; those three were not run through the agent yet.

## Things that cost time

- A `cmd` window from the start-up helpers (`dlgclick`) can stay on the Wine desktop for a while; it closes by itself.
- A button press on an intro film can skip both the film and the title screen, which is why `bench.py run` uses
  `boot` and then `skirmish --from-menu`.
- A game process from an earlier run stays alive until the launcher force-stops GameNative; `bench.py run` waits for
  the launcher's own `pid=` line before it presses anything.
- The first launch attempt of a run failed while the Save Conflict dialog was open; `run_watch.py` now answers it.
