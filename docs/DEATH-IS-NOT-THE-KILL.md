# Measured: the run does not end with the kill's signature

First trustworthy run in this session with thread-state sampling. It changes the working theory.

## The run

Launched through **Play** after a clean app restart. Verified as a real run by the guard recorded in
[EXPERIMENTS.md](EXPERIMENTS.md): the process began at 5 threads and **grew to 18–22**.

Sampled every 5 seconds with `/proc/<pid>/task/*/stat`, state parsed after the last `)` so a comm
containing spaces cannot shift the field.

```
t=  9s  states[21 S]
t= 28s  states[1 R 20 S]
t= 38s  states[21 S]
t= 47s  states[22 S]
t= 56s  states[2 R 20 S]
t= 66s  states[4 R 17 S]
t= 75s  states[20 S]
t= 84s  states[1 R 19 S]
...
t=145s  states[15 S]        <- thread count dropping
t=155s  *** PROCESS GONE ***
```

## What this shows

**Ordinary activity throughout, then an exit.** Threads alternated between all-sleeping and
one-or-two-running, which is normal for a loading game. There was:

- **no mass suspension** — never a wall of `T`, nor all-but-one suspended, and
- **no persistent spinner** — many samples showed `20 S` with nothing running at all, which a
  100 %-CPU kill thread could not produce.

So **this death is not the Aegis kill.** The kill's documented signature — every thread suspended with
one spinning — never appeared.

## What it does look like

The logcat at the moment of death:

```
W System.err: java.io.IOException: Failed to write data.
W System.err:   at com.winlator.xconnector.XConnectorEpoll.killConnection(SourceFile:263)
```

**The game's X11 connection to Winlator's X server drops**, repeatedly. That fits the symptom seen on the
device — a black frame with the AoE cursor — better than a protection response does: the process is alive
and rendering nothing useful, then its display connection dies and it exits.

The game's own log stops at `Loading step: [Property Bag Manager]` in this run (an earlier run reached
`MapGen`), so the load does not complete before the connection goes.

## Ruled out along the way

**The crash log is unrelated.** `pluvia_crash_2026-10-06_20-09-55.txt` is:

```
RuntimeException: Unable to stop service app.gamenative.service.epic.EpicService
Caused by: java.util.ConcurrentModificationException at EpicService.onDestroy
```

That is a GameNative bug when **shutting the app down**, triggered by my own `am force-stop` calls. It is
not the game dying.

## Consequence for the objective

The objective is framed around defeating Aegis so the game survives the 2–4.5 minute kill. **This run
suggests the current blocker is not Aegis at all.** If the game is losing its X connection during load,
then no amount of work on the protection will make it playable, and the SMC finding — while real and
verified — may not be what stands between here and a playable game.

**Next:** find out why the X connection drops. Candidates: the X server's request handling under FEX,
resource exhaustion, or the game's own window/display setup. Worth checking whether `killConnection`
correlates with a specific request, and whether it happens at the same point in the load every time.
