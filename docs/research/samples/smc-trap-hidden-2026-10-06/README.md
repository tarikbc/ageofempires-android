# SMC trap hidden: probe and game runs, 2026-10-06

Raw data behind [`SMC-TRAP.md`, part 3](../../../how-it-works/SMC-TRAP.md#part-3-hiding-fexs-smc-trap-does-not-stop-the-aegis-kill).

| Files | What |
|---|---|
| `smctest2-ctl.txt`, `smctest2-fix.txt`, `smctest2-fix2.txt` | `tools/research/smctest2.c` under the control build, patch 0004 (first version), and fix2 |
| `run4-fix-*`, `run5-ctl-*` | first fix and control runs (21:28, 21:35) |
| `ab-01-fix-*` to `ab-06-ctl-*` | the alternating series from `tools/ab_fex.py`; `ab-driver-output.txt` is its console output (run 04 was a failed launch and has no files) |
| `run7-fix2-*` | fix2 run (22:21); `run7-fix2-fexstats.txt` has the in-game filter counters every 30 s |

`*-timeline.tsv` is one row per `run_watch.py` sample; `*-suspinfo.txt` holds every `suspinfo` snapshot.
Game logs are not committed (they hold account identifiers); the timelines carry the last log line per
sample, with identifiers redacted.
