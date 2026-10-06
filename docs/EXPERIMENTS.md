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
| `SMCChecks`: `none` | Exits ~2 min |
| `SMCChecks`: `full` | Hangs at launch from Play |
| `SMCChecks`: `mtrack` (default) | The freeze described here |

## Void — the test never ran

| Tried | Why it proves nothing |
|---|---|
| "Original vs patched Wine `ntdll`" | The **mapped** ntdll is pristine; Wine loads its own tree's ntdll, not `system32`'s. Both patches have never executed. Any conclusion from this is worthless. |

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

## Fixed along the way

| Problem | Fix |
|---|---|
| Box64 decode: empty SSE/AVX store stubs | Patched `decopcode.c` (worth upstreaming). Box64 still 6× slower than Rosetta → moved to FEX. |
| D3D12: "No adapter found which supports Direct3D 12" | Use the VKD3D wrapper, not DXVK alone |
| Stale Steam ticket (GameNative bug) | Delete `<game>/.steam_coldclient_used` per launch, or enable Bionic Steam |
| `12152`/`12157` / `XAL_TELEMETRY` errors | **Noise.** They are Xbox Live calls, downstream of the asio failure. The endpoints answer fine from Wine. |
