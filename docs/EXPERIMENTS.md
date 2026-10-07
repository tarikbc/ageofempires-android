# Experiment ledger

Everything tried, and what actually happened. **The binary outcome is always the same test:** does the
game's own log (`warnings.log`) keep growing past ~5 minutes? "Process still alive" is not success —
the kill *suspends* the threads and leaves the process hung, so `ps` still shows it.

## Ruled out — tested, game still died

| Tried | Result |
|---|---|
| Online vs offline | Both fail. Offline fails *earlier and differently*: exits ~35 s at `Loading step: [NetworkGlobal]`. Not the kill. |
| Stale Steam ticket / Steam emulator vs Bionic Steam | No change |
| Wine debug logs on/off | No change |
| FEXCore presets: Intermediate, Stability | No change |
| `HideHypervisorBit`, `SmallTSCScale=0` | No change |
| FEX 2609, FEX main, own 2610 build | No change |
| Emulator DLL name (`xtajit64.dll`) | No change — **and this is now a known-settled question** |
| CPUID `0x40000000` vendor leak → patched, verified `eax=0x0 vendor=''` | No change |
| TLS: `CertificateRevocation=0` (cut backend TLS from 5–19 s to ~0.8 s) | No change |
| Memory integrity: Aegis image byte-identical at t=128 s through the kill | Not tampering |
| Debugger signals (`KdDebuggerEnabled`, `OutputDebugString`, `NtQueryObject`) | All correct under Wine |
| Healthy backend session for the whole run | **Still died** — so session loss is not causal |
| Hide FEX's SMC trap from the guest (patch 0004: `NtQueryVirtualMemory` and `NtProtectVirtualMemory` report the guest's own protection; trap still armed, verified with `smctest2`) | **Killed 5 of 5.** In-game counters: 732,206 queries filtered, 0 touched a trapped page. [SMC-TRAP-HIDDEN.md](SMC-TRAP-HIDDEN.md) |
| No session drop (`errno=10038` absent in runs 4 and A1, 2026-10-06) | Killed on time anyway |
| `SMCChecks`: `none` | Exits ~2 min |
| `SMCChecks`: `full` | Hangs at launch from Play (config dated 00:12 on 2026-10-06, before the round-17 debug channels; reproduced on the clean baseline at 21:13). Note: full mode keeps the trap armed in this FEX revision. |
| `SMCChecks`: `mtrack` (default) | The freeze described here |

## Void — the test never ran

| Tried | Why it proves nothing |
|---|---|
| "Original vs patched Wine `ntdll`" | The **mapped** ntdll is pristine; Wine loads its own tree's ntdll, not `system32`'s. Both patches have never executed. Any conclusion from this is worthless. |
| Evening runs, 19:52 to 20:29 on 2026-10-06 ("patched FEX regresses", "the wall is MapGen", the X-connection reading) | Launched with leftover `WINEDEBUG=+thread,+sync,+virtual,+timestamp,+tid` from round 17, which slowed the game until it stalled in `Property Bag Manager`. See [WINEDEBUG-LEFTOVER.md](WINEDEBUG-LEFTOVER.md). |

## Confirmed working (infrastructure)

| Thing | Status |
|---|---|
| `winhandler.exe` UDP `127.0.0.1:7946` → run a Windows program in the live session | Works |
| mingw probes (thread/stack/lock state) against the game process | Work |
| `/proc/<pid>/task/*/stat` for thread state from adb | Works |
| **Open container** → `explorer` + `winhandler` in ~30 s, no game launch | Works — the fast path |
| Editing `Z:\home\xuser\.container` `envVars`, read back at launch | Works |
| Editing `wineVersion` / `fexcoreVersion` in the same file | **Ignored** — GameNative re-applies from its own store |
| Writing into `Z:\opt\<wine-tree>` | **Read-only** |
| Windows-on-ARM reference run | Impossible — no Windows machine available |
| Locked device → "container will not start" (rounds 19, 26) | **MISDIAGNOSED.** The device was on a secure lock screen, so taps went to the keyguard. The container was never broken. Check `dumpsys trust \| grep deviceLocked` first |
| Hardware tracing (ETM/CoreSight) on the Thor | Needs root: `enable_source` is root-owned, `perf_event_paranoid=3`, `simpleperf` rejects `task-clock`. Bootloader is unlocked, so rooting is possible but not free. |

## The one that unblocked everything

| Problem | Fix |
|---|---|
| **Container would not start.** Box64: `Error: File is not found. (wine)`, searching `…/imagefs/opt/wine/bin/`. Nothing else worked: no Wine process at all, for ~10 rounds. | The container's **Wine Version** was `proton-11.0-1-arm64ec-aoefix-1`, a bundle imported earlier that never worked, and `Z:\opt\` only ever showed `proton-11.0-99-arm64ec-1` — so it named a tree that did not exist. Set Wine Version to **`proton-11.0-99-arm64ec-1`**, Save. It booted immediately, and everything downstream (the game log, `smctest`, the whole SMC investigation) became possible. See [CONTAINER-WONT-START.md](CONTAINER-WONT-START.md). |

**Two diagnoses made along the way were wrong, and both looked convincing:**

- **"The container is wedged"** (rounds 19, 26) — the device was on its **secure lock screen** and every
  tap went to the keyguard. Tell: `adb shell ls /sdcard/` fails, because user storage is not decrypted
  until the first unlock after a boot.
- **"`needsUnpacking: true` means it is unpacking"** — it was not. The app sat at 23% CPU with **zero**
  container threads; that was Java and telemetry. Check the thread list, not the CPU figure.

**And the root cause was self-inflicted:** an earlier attempt to switch Proton versions left the container
pointing at a tree that was never installed.

## Measured on the clean baseline (2026-10-06, `WINEDEBUG=-all`)

| Run | FEX DLL | Result |
|---|---|---|
| 20:51 | `460568b8` (CPUID-patched, SMC trap present) | Loads to `GEWorld`. Session drops at 20:53:47 (`errno=10038`, `1006`). Kill between 20:54:28 and 20:54:34: 60 threads at suspend 1, kill thread `+0x3e69304` spinning. Log frozen at 89,477 bytes. |
| 20:58 | `b4dbf32d` (no-trap, patches 0001 + 0003) | Never starts: one Windows thread (the main thread), 0 % CPU, no log, for 3+ minutes. |
| 21:13 | `460568b8` with `SMCChecks=2` (full; trap still armed) | Same as 20:58: one Windows thread, 0 % CPU, no log. Removing the trap is not needed for this stop; full-SMC validation is the shared factor. |

Details in [KILL-REMEASURED.md](KILL-REMEASURED.md).

A failed launch is not a result: GameNative sometimes hangs on "Syncing cloud saves" after Play (run A4).
Force-stop the app and start again; `run_watch.py --launch` now does this after 90 s.

## Fixed along the way

| Problem | Fix |
|---|---|
| Box64 decode: empty SSE/AVX store stubs | Patched `decopcode.c` (worth upstreaming). Box64 still 6× slower than Rosetta → moved to FEX. |
| D3D12: "No adapter found which supports Direct3D 12" | Use the VKD3D wrapper, not DXVK alone |
| Stale Steam ticket (GameNative bug) | Delete `<game>/.steam_coldclient_used` per launch, or enable Bionic Steam |
| `12152`/`12157` / `XAL_TELEMETRY` errors | **Noise.** They are Xbox Live calls, downstream of the asio failure. The endpoints answer fine from Wine. |
| Runs stalling in `Property Bag Manager` (evening of 2026-10-06) | Leftover `WINEDEBUG` channels in `envVars`. Set both config copies to `WINEDEBUG=-all`; the next run took 22 s for that step and loaded to `GEWorld`. |


## Launch-path trap: a direct launch is not a real run

Recorded after it produced two false conclusions in a row.

`start "" "A:\RelicCardinal.exe"` from a `cmd` in the live session **starts a process that is not the
game**. It appears in `ps` as `A:\RelicCardinal.exe`, it stays alive indefinitely, and it does nothing:

| launched by | threads | CPU | writes to `warnings.log` |
|---|---|---|---|
| **Play** (GameNative's own path) | ~20 | active | yes — fresh `RelicCardinal started at …` line |
| `start` from the session | **5, all state `S`** | **0%** | **no — nothing at all** |

I twice treated the second kind as a successful run and as evidence the Aegis kill was gone. It was not;
it was a process that never initialised.

**Guards, both needed:**

1. **Check the log's own first line.** `cp.bat` copies whatever `warnings.log` currently holds, which may
   be a *previous* run's file. If `RelicCardinal started at …` does not match the run under test, the log
   is not about that run. I spent a full monitoring cycle reading a 19:35 log while the run under test
   began at 20:10 and had written nothing.
2. **Check the thread count.** A real run has ~20 threads; 5 threads at 0% CPU means it never started.

**Use Play for anything that will be judged.** Direct launches are useful only for probing the session
(`smctest`, file operations), never for measuring game behaviour.


### Refinement: Play can also produce a dud

The table above is a tendency, not a guarantee. In round 44 a **Play** launch produced a process with
**5 threads that vanished within 15 seconds and wrote nothing to the log** — the same dud signature. So
"launched by Play" is not sufficient either.

**The only reliable test is the log.** Before and after any run:

```sh
adb shell cat /sdcard/Download/aoe/watch.txt | head -1     # must name THIS run's start time
```

If the first line still names an older run, the run under test has written nothing and **no conclusion
can be drawn from the process at all** — not its existence, not its thread count, not its CPU.

Also noted: repeatedly force-stopping GameNative leaves the app/container in a state where launches
produce duds. A clean app restart before a run under test is worth the extra minute.
