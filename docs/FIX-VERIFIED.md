# Status: the SMC trap is fixed; the kill is NOT shown to be gone

**Corrected.** An earlier version of this file claimed the Aegis kill was gone. That claim was wrong,
and the evidence for it was exactly the false positive this repo already warns about.

## The rule I broke

> **"The process is still alive" is not success.** The kill *suspends* the threads and leaves the process
> hung in place, so `ps` keeps showing it. The only valid criterion is the game's own log
> (`warnings.log`) still growing after ~5 minutes.

I wrote that rule, put it in [EXPERIMENTS.md](EXPERIMENTS.md), and then cited "alive after 10 minutes" as
proof anyway. It is not proof. A hung process and a working one look identical to `ps`.

## What is actually established

**1. The SMC trap is fixed — this part is solid, and is a real result.**

`tools/smctest.c` inside the guest:

| | before | after |
|---|---|---|
| protection after executing an RWX page | `RX` (0x20) | **`RWX` (0x40)** |
| `VirtualProtect` reports previous | `0x20` | **`0x40`** |

FEX no longer silently removes write permission from the guest's own pages. That was the confirmed defect
([SMC-CONFIRMED.md](SMC-CONFIRMED.md)) and it is now confirmed fixed.

**2. The kill is not shown to be gone.**

| run | process | log (`warnings.log`) |
|---|---|---|
| stock FEX | gone at ~2 min | stops at `[Property Bag Manager]` |
| patched FEX (19:35 run) | alive at 10 min | **stops at `MapGen`, at 19:38:11** |
| patched FEX (single relaunch) | alive, **5 threads, all state `S`, 0% CPU** | **never logged at all** |

The log is the criterion, and **it stops in every case**. The last two runs left a process that `ps`
shows and that does nothing — which is precisely what a suspended process looks like.

So the patched FEX gets the game *further* (`MapGen` versus `[Property Bag Manager]`) but it does not
demonstrably survive, and it is certainly not playable.

## What went wrong with the measurement

The 19:35 run's log stopped at `MapGen` at 19:38:11 and never grew again, while the process stayed
alive — and I read the aliveness rather than the log. Then my "single clean instance" launch
(`start "" "A:\RelicCardinal.exe"` from `cmd`) produced a process with 5 threads and no log output at
all, i.e. one that never started properly; I read its aliveness as success too.

Direct launches from the session are not equivalent to GameNative's own launch path. The 19:35 run — the
only one that reached `MapGen` — came from Play.

## Next steps that would actually settle it

1. **One clean run via Play**, with nothing else launched into the session, judged only by whether
   `warnings.log` keeps growing past five minutes.
2. **Capture the thread state** in the kill window. The kill's signature is ~60 threads with all but one
   suspended and that one spinning; a 5-thread 0%-CPU process is a different failure entirely and should
   not be confused with it.
3. **Separate the `MapGen` error** — `!m_texturePath.empty()` may be an asset problem independent of both
   the trap and the kill.
