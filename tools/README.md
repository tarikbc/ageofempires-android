# Tools

Scripts that run on a Mac with the Thor connected over adb. How they fit together: [TESTING.md](../docs/guides/TESTING.md).

## Testing and measuring

| Tool | What it does |
|---|---|
| [`bench.py`](bench.py) | The repeatable benchmark: starts the game, skips the intros, starts a 1v1 skirmish with the camera turning, and records frame times and temperatures |
| [`fpsgraph.py`](fpsgraph.py) | Live frame-time graph in the Mac's browser (port 8790) |
| [`frametimes.py`](frametimes.py) | Records the game's frame times from Android's compositor |
| [`framecmp.py`](framecmp.py) | Compares frame times before and after an event in a recording |
| [`frameplot.py`](frameplot.py) | Draws frame-time recordings as a chart |
| [`thermals.py`](thermals.py) | Temperatures, GPU load and CPU clocks of the Thor |
| [`threadcpu.py`](threadcpu.py) | CPU use of each thread of the running game |
| [`thor_pad.py`](thor_pad.py) | Presses the Thor's controller buttons from adb (evdev events) |
| [`agent.py`](agent.py) | Reads memory and threads inside the game without console windows (host side of [`probes/aoeagent.c`](probes/aoeagent.c)) |
| [`run_watch.py`](run_watch.py) | Starts a run and watches it: the game process, its log, Windows suspend counts; answers GameNative's Save Conflict dialog |
| [`ab_fex.py`](ab_fex.py) | Runs FEX builds in turn and summarises each run |

## GameNative and packaging

| Tool | What it does |
|---|---|
| [`make_fex_wcp.py`](make_fex_wcp.py) | Packages a `libarm64ecfex.dll` as a GameNative FEXCore `.wcp` |
| [`gn_import_wcp.py`](gn_import_wcp.py) | Imports a `.wcp` into GameNative's Contents Manager |
| [`gn_select_fex.py`](gn_select_fex.py) | Selects the FEXCore version of the AoE IV container |
| [`gn_driver.py`](gn_driver.py) | Imports and selects a graphics driver |
| [`gn_nav.py`](gn_nav.py) | GameNative UI automation, used by the scripts above |
| [`redact.py`](redact.py) | Removes personal data (Steam name, IDs, tokens) from logs before they are committed |

## Folders

| Folder | What is in it |
|---|---|
| [`probes/`](probes) | Small Windows programs that run inside the Wine session; build them with [`probes/build.sh`](probes/build.sh) (needs mingw-w64) |
| [`thor/`](thor) | Batch files that the watch scripts run inside Wine |
| [`research/`](research) | One-off scripts and probes from the investigation |
