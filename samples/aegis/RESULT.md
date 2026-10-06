# Breakpoint run — result (partial, paused mid-experiment)

`bpguard.exe` attached to RelicCardinal.exe with int3 breakpoints planted at both
SuspendThread call sites (`+0x49065a1`, `+0x490699d`; original byte `ff` = the
`call [rip+…]` opcode, so the addresses were correct and the pages were the live
restored code).

Observed:

* Attach succeeded: 71 modules, both breakpoints armed, `debugger attached`,
  `[event] create process`.
* **No breakpoint ever fired.**
* The game **stalled at 0% CPU** (18 s of CPU consumed, then flat) and never
  progressed past archive loading — it also **never reached the usual kill**.

So planting the breakpoints (a 1-byte code modification) correlated with the game
freezing early instead of being killed at the usual 2–4.5 minute mark.

**Not yet resolved:** whether that stall was caused by (a) the breakpoint byte-write
(i.e. Aegis reacting to a code modification) or (b) merely the debugger attachment.
The control run — attach with no breakpoints — was launched but the game failed to
start (UI navigation), so it did not produce a result before the pause.

Resume by running the control:
    BP1=0 BP2=0 python3 -u bp_run.py
If the control reaches the normal kill, then (a) holds: Aegis reacts to a modified
code byte, which is direct evidence that its kill is an integrity response.
