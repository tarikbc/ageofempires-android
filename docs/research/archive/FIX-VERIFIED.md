# Status: the trap fix works but regresses the game; the wall is now a MapGen asset error

> **Archived.** The no-trap build (patches 0001 and 0003) stops the game at start-up ([KILL-REMEASURED.md](KILL-REMEASURED.md), run 2). Section 1 stands; sections 2 and 3 were corrected inside. The story: [STORY.md](../../STORY.md).

> **Correction (2026-10-06, 21:10).** Every run in this document was launched with leftover debug channels
> (`WINEDEBUG=+thread,+sync,+virtual,+timestamp,+tid`), which slowed the game badly
> ([WINEDEBUG-LEFTOVER.md](WINEDEBUG-LEFTOVER.md)). What still stands and what does not:
>
> - **Stands:** section 1, the `smctest` result (`RWX` with the no-trap build). It does not depend on the
>   game's speed.
> - **Wrong:** section 3. MapGen is not a wall; the message appears in every run that gets further, and
>   loading continues after it.
> - **Wrong label:** "stock FEX" here is `libarm64ecfex.stock.dll`, SHA-1 `460568b8`, which is the
>   CPUID-patched build, not stock.
> - **Re-tested clean:** the no-trap build still does not start the game, and `SMCChecks=full` with the trap
>   armed stops the same way. The shared factor is full-SMC validation, not the trap removal
>   ([KILL-REMEASURED.md](KILL-REMEASURED.md)).

Corrected twice, both times after being wrong.

## 1. The SMC trap fix is real, and verified

`tools/research/smctest.c` inside the guest:

| | stock FEX | patched FEX |
|---|---|---|
| protection after executing an RWX page | `RX` (0x20) | **`RWX` (0x40)** |
| `VirtualProtect` reports previous | `0x20` | **`0x40`** |

FEX no longer silently removes write permission from the guest's own pages. That defect was real
([`SMC-TRAP.md`, part 2](../../how-it-works/SMC-TRAP.md#part-2-confirmed-fex-leaks-its-smc-write-trap-to-the-guest)) and is fixed. This part stands.

## 2. But that patch regresses the game — and my first claim about it was wrong

I claimed an earlier run "survived 10 minutes" and that the kill was gone. **That was the false positive
this repo already warns about**: the kill suspends threads and leaves the process hung, so `ps` keeps
showing it. The correct criterion is `warnings.log` still growing, and I ignored it. A user challenge
("isn't this a false positive?") caught it.

With the trap disabled the game gets *less* far, not more:

| FEX build | game's log |
|---|---|
| stock | fresh log each run, reaches **`MapGen`** |
| no-trap (patches 0001 + 0003) | **no new log at all**; process sits at **5 threads, state `S`, 0% CPU** |

So `ForceFullSMCDetection` is **not** a sufficient replacement for the trap. Removing the leak without
breaking invalidation needs a different approach. Stock FEX has been restored.

## 3. The wall is a MapGen asset error, and it is not Steam

Both stock and patched runs end identically:

```
(I) [20:13:16.381] [000000316]: MapGen - Failed to validate: !m_texturePath.empty()
    Failed to validate an attribute data field for map generation.
```

Checked and ruled out:

- **Steam ticket** — no `C00T01R04X`, no "Found 0 profiles" anywhere in the log, and
  `.steam_coldclient_used` does not exist, so GameNative is already writing a fresh ticket each launch.
- **Aegis** — the same failure appears with the stock build.

An empty texture path during map generation points at **missing or incomplete game data**. Worth noting
the install reports **45.31 GiB**, while a full AoE IV install with expansions is considerably larger, so
an incomplete download is a plausible cause. The standard remedy is Steam's *verify integrity of game
files*, which is a UI action.

## The useful thing learned this round

**Check the log's own first line before trusting it.** `cp.bat` copies whatever `warnings.log` currently
holds, which may be a *previous* run's file — I spent a whole monitoring cycle reading a log whose first
line said `started at 19:35` while the run under test had started at 20:10 and written nothing. Comparing
the log's start timestamp against the run is the guard.
