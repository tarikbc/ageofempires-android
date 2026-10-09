# The first captured kill (2026-10-06)

Raw files behind [archive/KILL-ANALYSIS.md](../../archive/KILL-ANALYSIS.md), redacted. The descriptions are the ones
that note gives.

| File | What it is |
|---|---|
| `si_1.txt` | kill-moment suspend snapshot |
| `si_2.txt` | post-kill state (4 threads); the later snapshots were not kept |
| `tctx_1.txt` | `tctx`, truncated where it hangs on `tid 0174` |
| `tstack_kill.txt` | `tstack` output for the kill thread `0174` (start `RelicCardinal.exe+3e69304`): stack base, limit and code pointers |
| `watch.txt` | the game's `warnings.log` at capture time (trigger at line 559) |
