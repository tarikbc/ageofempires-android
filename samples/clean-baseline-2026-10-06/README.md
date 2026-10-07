# Clean-baseline runs, 2026-10-06

Raw data behind [docs/KILL-REMEASURED.md](../../docs/KILL-REMEASURED.md). Both runs used
`WINEDEBUG=-all` and were driven by `tools/run_watch.py`.

| File | What it is |
|---|---|
| `run1-timeline.tsv` | 20:51 run, FEX `460568b8`: one row per sample (seconds since the process appeared, log size, suspend counts, kill thread) |
| `run1-suspinfo.txt` | every `suspinfo` snapshot of that run, separated by `---- <time>` |
| `run1-log-excerpt.txt` | the loading steps, the `errno=10038` / `1006` lines and the last lines of the game's `warnings.log` (the full log holds account identifiers, so it is not committed) |
| `run2-timeline.tsv` | 20:58 run, no-trap FEX `b4dbf32d` |
| `run2-suspinfo.txt` | its `suspinfo` snapshots: one thread, the main thread |
| `run2-tstack.txt` | the main thread's stack scan, which places it inside `libarm64ecfex.dll` |

In `run1-timeline.tsv`, rows up to t=18 s took thread counts and CPU from the wrong process
(`explorer.exe`); the tool was fixed during the run. The log sizes and suspend counts in those rows are
correct, because both read the game by its exe name. The CPU value at t=170 s is negative because the
first tool version summed per-thread times and threads had exited; the tool now reads process totals.
