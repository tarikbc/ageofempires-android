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


## Correction: the X error is a consequence, not the cause

One round later, the full stack trace showed I had read it backwards. Read top-down:

```
WindowManager.destroyWindow
  -> WindowManager.removeAllSubwindowsAndWindow
    -> Window.sendEvent -> EventListener.sendEvent
      -> DestroyNotify.send
        -> XOutputStream.flush -> ClientSocket.write
          -> "Failed to write data"
```

and the frame below it:

```
XClient.freeResources
  -> XClientConnectionHandler.handleConnectionShutdown
    -> XConnectorEpoll.killConnection
      -> XConnectorEpoll.handleExistingConnection
        -> XConnectorEpollNative.doEpollIndefinitely
```

**The client disconnected first.** The server noticed (`handleExistingConnection`), began tearing the
client down (`killConnection` → `handleConnectionShutdown` → `freeResources`), destroyed its windows, and
then failed to *write* a `DestroyNotify` to a socket that had already gone — which is exactly what a
socket write to a closed connection does, and is harmless.

So "the X connection drops" is **not** a cause of the game's death. It is what the server does *after* the
client goes away. The plausible reading is the reverse of what was written above: **the game process exits
first, and the X cleanup follows.**

That also removes the tension with the measurement, which stands unchanged: the thread states show
ordinary activity right up to the exit, with no suspension and no spinner — so whatever ends the run is
neither the kill's signature nor a struggling display connection.

## Where that leaves the cause

Back to the load failing on its own. The two candidates still standing:

1. **The `MapGen` validation** — an info-level data-authoring message, but it is the last thing logged in
   the runs that get that far, and nothing is logged after it before the process exits.
2. **An incomplete install** — `!m_texturePath.empty()` is an empty texture path, which is what missing
   game data looks like. The install reports 45.31 GiB; a full AoE IV with expansions is larger.

**Both point at the same cheap test: Steam → verify integrity of game files.** That is a UI action and it
would settle whether data is missing, without any further emulation work.


## Retraction: the thread-state method cannot detect this kill at all

The measurement that this document is built on is **invalid**, and the title is wrong.

**Wine's `SuspendThread` does not put a thread into the Linux `T` state.** `T` is job-control stop
(`SIGSTOP`). A thread suspended through the Windows API stays in `S` as far as `/proc/<pid>/task/*/stat`
is concerned — it is parked inside the Wine server, not stopped by the kernel.

So "no mass suspension, no spinner" is exactly what a *successful* Aegis kill would also look like. The
sampling could not distinguish "healthy" from "every thread suspended". Both the table above and the
conclusion drawn from it must be disregarded.

The timing is consistent with the kill, which is what I should have weighted: the log goes silent at
`[Property Bag Manager]`, and the process dies about **2.5 minutes later**, ~4 minutes into the run —
inside the documented 2–4.5 minute window.

## The correct instrument

`tools/probes/suspinfo.c` reads the **Windows** suspend count of every thread, which is the actual
signature:

- **kill fired** — every thread but one has a suspend count of 1, and the remaining one is spinning
- **healthy** — suspend counts of 0

It is built and on the device as `/sdcard/Download/suspinfo.exe`, driven by `D:\susp.bat`, which appends
each sample to `D:\aoe\susp_hist.txt` so a whole run can be read back afterwards.

**What is still true:** the X11 messages are a consequence of the process exiting, not a cause — the stack
trace shows the server reacting to a client that had already gone.

**What is not established by this document:** anything about whether the kill fired in that run.


## Instrument validated

`suspinfo` was confirmed working against a live process before being relied on — worth doing, because
the first attempt failed for a silly reason:

```
suspinfo.exe [exename] [outfile] [hexaddr]     defaults: RelicCardinal.exe D:\si.txt
```

**It takes the output path as `argv[2]`**, so `suspinfo.exe RelicCardinal.exe` alone writes to
`D:\si.txt`, *not* to any path the caller might expect. The first driver batch read the wrong file and
reported "File not found", which looked like a broken probe rather than a broken invocation.

Corrected driver:

```bat
D:\suspinfo.exe RelicCardinal.exe D:\aoe\susp_now.txt
type D:\aoe\susp_now.txt >> D:\aoe\susp_hist.txt
echo ---- >> D:\aoe\susp_hist.txt
```

Verified against `winhandler.exe`, which reports what a healthy process should:

```
tid 00ec suspend=0 user=1000ms start=winhandler.exe+12fd name=[]
tid 0124 suspend=0 user=150ms start=winhandler.exe+1dd0 name=[]
```

`suspend=0` per thread is the healthy reading. **A successful Aegis kill should invert this** — every
thread but one at `suspend=1`, with the remaining one spinning.
