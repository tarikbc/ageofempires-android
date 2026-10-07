# The kill is a timed job: what `WINEDEBUG=+seh` shows

2026-10-07. Measured on the clean FEX baseline (`460568b8`, `proton-11.0-99-arm64ec-1`) with only
`WINEDEBUG=+seh` added, and compared with the Box64 runs in [BOX64-ROUTE.md](BOX64-ROUTE.md).

## Why `+seh` sees the protection's logic

Much of the protection runs as illegal instructions (`c000001d`) that its own vectored handler at
`RelicCardinal.exe+0x3e46ff8` resumes (`returned ffffffff`, same handler under FEX and Box64). Wine's `+seh`
channel logs every such exception with the thread and the full register context. So the sequence of exception
addresses per thread is a trace of that logic, with no change to the game's code. The cost: the log is large
(8.5 million lines between 01:39:52 and 01:45:30) and the game loads more slowly, but the kill still came.

## The run

Run `fexkill`: game process seen 01:39:58, judged by `tools/run_watch.py`. The log stopped growing between the
271 s and 285 s samples; at 285 s `suspinfo` showed 32 threads at suspend count 1 and thread `0174` with
81,930 ms of user time (spinning), as in [KILL-REMEASURED.md](KILL-REMEASURED.md).

## Three threads share the entry `+0x3e69304`; all three are timed jobs

Each starts with an illegal instruction at `+0x3e69304`, then raises nothing until it wakes. At the first
exception after waking, `rax = 0x102` (`STATUS_TIMEOUT`) for all three, and `rsi` equals the time it slept:

| thread | started | woke | slept | `rsi` at wake | path after `+0x3e8d15c` |
|---|---|---|---|---|---|
| `0168` | 01:39:56.946 | 01:40:05.863 | 8.9 s | `0x22c9` = 8,905 | `+0x3e8d352` … |
| `016c` | 01:39:56.954 | 01:41:35.119 | 98.2 s | `0x17f26` = 98,086 | `+0x3e8d57b` … |
| `0174` | 01:39:57.278 | 01:43:18.133 | 200.9 s | `0x3102d` = 200,749 | `+0x3e8d3e5` … `+0x3e6b53c` |

So nothing wakes the kill thread early: it sleeps for a set time and then runs its job. In earlier runs the
kill came 2 min 3 s to 3 min 32 s after start; that spread fits a delay chosen per run, but one sample does not
show how it is chosen.

`0174`'s job, step by step (its whole life is 63 exceptions):

- 01:43:18.133 to 01:43:18.408: `+0x3e8d878` … `+0x3e8daa4`, the same steps the other two threads run after
  waking, then `+0x3e8d15c`, where the three paths split.
- 01:43:18.410 to 01:43:18.431: `+0x3e8d3e5` … `+0x3e8d509`. Registers hold `.data` globals
  (`r12 = 0x14754a840`, `r9 = 0x14754a820`) and a job object (`0xca18580`).
- `+0x3e8d509` to `+0x3e8d51d`: 150 ms with no exception, same `rsp` before and after (a call that returned).
  After it, `rbx`, `rdi` and `r8` hold `0x0e00000000000000`.
- 01:43:18.587: `+0x3e6b53c`, the function that suspends every other thread ([KILL-ANALYSIS.md](KILL-ANALYSIS.md)),
  entered with that value in `rbx`, `rdi` and `r8`.

Not established: what the 150 ms call computes, what `0x0e00000000000000` encodes, and whether this job enters
`+0x3e6b53c` on a machine where the game works (the game does not stop there, so either the path or that
function's effect must differ).

## A hook check that runs all the time: thread `0178`

`0178` runs a loop of illegal instructions at `+0x3f551cd` … `+0x3f551e4` (324,038 exceptions in this run).
From the contexts: `rsi` walks a table in `.data` in 0x28-byte entries (all zero in the file, so filled at run
time; 45 entries from `0x1475420a0`, each holding a function address and that function's first 20 bytes XOR
`0x45`, read live with `tools/probes/memwatch.c`, see [BOX64-ROUTE.md](BOX64-ROUTE.md)); for each entry, `rcx` walks about 20 bytes from the start of a function in the
exe; `rax` = that code byte XOR `r13` (`0x45`, an 8-bit XOR: `0x83` gives `0xc6`); the step at `+0x3f551d2`
compares, and `+0x3f551d6` either continues (`+0x3f551d8`) or leaves to `+0x3f551eb`. The order of functions
differs between runs.

In this FEX run the loop never went to `+0x3f551eb` (0 times). So this check did not trigger the kill here.

Under Box64 (run 01:18, [BOX64-ROUTE.md](BOX64-ROUTE.md)) it did: at the function at `0x143e76964`, offset 1
(code byte `0x83`, `rax = 0xc6`), the flags after the compare were `0x286` (ZF clear), where FEX gives ZF set
for the same `rax` at other functions. `0x286` is what comparing `0xc6` with 0 gives (other operands can give the same flags). The run then went down a
path that ended at address 0 and in `__fastfail`.

## What this changes

- "Find what wakes the kill thread" is answered: a timeout. The question is now what the job decides in its
  150 ms call, and why the result here is `0x0e00000000000000`.
- The `+seh` trace is a usable, non-invasive instrument for the protection's control flow, under both FEX and
  Box64, as long as the logcat capture keeps up (a few single steps look missing, which logcat drops under load).
