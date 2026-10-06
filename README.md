# Age of Empires IV on the AYN Thor (GameNative 1.2.1)

Notes, patches and debug tools from running Age of Empires IV on an Android handheld with [GameNative](https://github.com/utkarshdalal/GameNative).

Device: AYN Thor (Snapdragon 8 Gen 2, Adreno 740, 16 GB, Android 13). Game build 24231237 (16.3.11308).
Sessions: 5–6 Oct 2026.

## Status

- The game starts, renders, reaches the menu and logs in online.
- The game freezes or exits 2 to 4.5 minutes after launch. This happens online and offline.
- The freeze comes from the copy protection, not from a setting. It is still open. The user owns the game so the goal is to implement the proper changes to make that protection understand the game is legit.
- The protection's kill thread is now identified: a launch-time thread with entry `RelicCardinal.exe+0x3e69304` that suspends every other thread ~1 min after the network socket closes, then spins at 100 % CPU. See [`docs/KILL-ANALYSIS.md`](docs/KILL-ANALYSIS.md).
- **FEX was advertising itself to the game** (`CPUID 0x40000000` → `FEXIFEXIEMU`). That is patched and the patch is live in the game's own session (`eax=0x0 vendor=''`, under `xtajit64.dll`) — and **the kill still fires**. See [`docs/FEX-PATCH-LIVE.md`](docs/FEX-PATCH-LIVE.md).

### Traps that cost time (read before re-testing)

- **"The process is still alive" is not success.** The kill *suspends* the threads and leaves the
  process hung in place, so `ps` keeps showing it. The only valid criterion is the game's own log
  (`warnings.log`) still growing after ~5 minutes, or `si` showing threads not suspended.
- **GameNative re-installs the emulator DLLs on every launch.** Patching `C:\windows\system32\*` by
  hand is silently reverted (observed: patched at 10:13, back to stock at 10:27). Use the Contents
  Manager `.wcp` path.
- **GameNative loads `xtajit64.dll`, not `libarm64ecfex.dll`.** A `.wcp` installs to whatever path its
  manifest names, so it has to name `xtajit64.dll` too — GameNative warns that this is outside its
  trusted set, but installs it.
- **`uiautomator dump` alone lies here.** It sees one window and goes stale; use
  `uiautomator dump --windows`. Taps were fine all along — the *reading* was wrong.
- The container config must be **saved** (top-right button) or Back raises "Unsaved Changes" and the
  selection is lost.
- The game's `C06T13R-1X-*` error is **server connectivity**, not file integrity, and does not prevent
  playing.

## What is in this repo

| Path | What it is |
|---|---|
| [`patches/box64/`](patches/box64) | Box64: decode SSE/AVX stores, so write faults reach Wine as writes. |
| [`patches/proton-arm64ec-ntdll/`](patches/proton-arm64ec-ntdll) | Two binary patches for the ARM64EC `ntdll.dll` of GameNative's `proton-11.0-1-arm64ec`, with an apply script. |
| [`patches/gamenative/`](patches/gamenative) | GameNative: write a fresh Steam ticket on every launch. Not built or tested. |
| [`tools/probes/`](tools/probes) | Small Windows programs that read thread, stack and lock state of a running process. |
| [`tools/winhandler_exec.py`](tools/winhandler_exec.py) | Starts a Windows program in a running GameNative session over adb. |
| [`tools/gn_nav.py`](tools/gn_nav.py) | Drives the GameNative UI over adb (launch a game without touching the screen). |
| [`tools/run_experiment.py`](tools/run_experiment.py) + [`tools/thor/`](tools/thor) | Watches the game for the network trigger and samples thread state in the kill window. |
| [`docs/KILL-ANALYSIS.md`](docs/KILL-ANALYSIS.md) | The captured kill: which thread suspends the others, and where the protection code lives. |
| [`docs/AEGIS.md`](docs/AEGIS.md) | **The protection identified: Aegis**, Relic's in-house virtualization/anti-tamper, with its build log. |
| [`docs/FEX-VENDOR-LEAK.md`](docs/FEX-VENDOR-LEAK.md) | **FEX advertises itself to the guest**: CPUID leaf `0x40000000` returns vendor `FEXIFEXIEMU`, and `HideHypervisorBit` does not hide it. |
| [`docs/GAMENATIVE-UI.md`](docs/GAMENATIVE-UI.md) | **Driving the GameNative UI**: the container-config path, the emulator/FEXCore selector, and the `uiautomator dump --windows` gotcha. |
| [`docs/FEX-PATCH-LIVE.md`](docs/FEX-PATCH-LIVE.md) | **The FEX patch is live** (`eax=0x0 vendor=''` in the game's session) — and the game still dies in the same window. Includes the `C06T13R` error-code finding. |
| [`docs/BACKEND-SESSION.md`](docs/BACKEND-SESSION.md) | **The game's backend session is broken**: DNS/TCP/TLS all work in-session, but the game's own requests fail with `12152`/`12157`. New lead for the ~1 min-after-socket-close kill. |
| [`docs/TLS-REVOCATION-FIX.md`](docs/TLS-REVOCATION-FIX.md) | **Wine TLS to the AoE backend costs 5–19 s** (native: 0.94 s); `CertificateRevocation=0` cuts it 6.5x, removes the session-loss chain entirely — **and Aegis still kills**, disproving that theory. |
| [`docs/KILL-STILL-OPEN.md`](docs/KILL-STILL-OPEN.md) | **Four fixes applied, kill survives.** CPUID + waitq + TLS all verified installed and all insufficient; the image is byte-stable before the kill, and Wine's HTTP handles the game's own request shapes. |
| [`docs/SESSION-LOSS.md`](docs/SESSION-LOSS.md) | **The kill is downstream of losing the backend session**: the WebSocket dies with `errno=10038` (`WSAENOTSOCK`) then status `1006`, and Aegis kills ~73 s later. Also corrects a false-positive reproduction: the backend refuses unauthenticated upgrades with `200`, not `101`. |
| [`docs/BACKEND-SESSION.md`](docs/BACKEND-SESSION.md) | **Measured the network end to end**: DNS/TCP/TLS fine, Wine's WebSocket holds 165 s, Wine's winhttp returns 200 from the real backend — only the game's own requests fail. |

## Setup that works best

| Part | Value |
|---|---|
| Wine | `proton-11.0-99-arm64ec` (own repack) |
| CPU emulator | FEXCore 2609, or own build of FEX main `7d3090f` (`fexcore-2610-aoe.wcp`) |
| FEXCore preset | Intermediate (Stability works too, but it is very slow) |
| DX wrapper | VKD3D (vkd3d-proton 2.14.1 + DXVK 2.4.1-gplasync) |
| GPU driver | Turnip v26.2.0 R4 |
| Executable | `RelicCardinal.exe` (set it by hand after the import) |
| Wine debug | Off. See "Debug output" below. |

## What we fixed

1. **Box64 path (dropped).** Box64's AVX/SSE store decoder had empty stubs, so write faults reached Wine as reads. We patched `src/libtools/decopcode.c` ([patch](patches/box64/0001-decode-sse-avx-stores.patch)). The patch is worth sending upstream. Box64 was then about 6× slower than Rosetta, so we moved to ARM64EC + FEX.
2. **Direct3D 12.** DXVK alone fails with "No adapter found which supports Direct3D 12". Use the VKD3D wrapper.
3. **Stale Steam ticket (GameNative bug).** GameNative writes `ticket=` into `steam_settings/configs.user.ini` only when the marker `.steam_coldclient_used` in the game folder is missing. Later launches keep an old ticket, and the login fails with `C00T01R04X-01` / "Found 0 profiles".
   - Workaround: delete `<game folder>/.steam_coldclient_used` before each launch.
   - Real fix: refresh the ticket on every launch in `SteamUtils.replaceSteamclientDll` ([patch](patches/gamenative/0001-refresh-steam-ticket-every-launch.patch)).
4. **Bionic Steam mode** (container → General → "Enable Experimental Bionic Steam") logs in natively and needs no ticket workaround.
5. **WinHTTP 12152 / XAL errors are noise.** The Mac shows the same errors and works.

## Wine ARM64EC ntdll patches

Both patches are for `lib/wine/aarch64-windows/ntdll.dll` from [`proton-11.0-1-arm64ec.wcp`](https://downloads.gamenative.app/proton-11.0-1-arm64ec.wcp). The `.wcp` is a zstd tar. The script checks the input hash and the output hash, so it only works on that build.

```sh
cd patches/proton-arm64ec-ntdll
./apply.py ntdll.dll ntdll.patched.dll           # syscall register fix
./apply.py ntdll.dll ntdll.waitqfix.dll --waitq  # + spinlock fix
```

It needs Homebrew `mingw-w64` and `llvm`.

- **`invoke_arm64ec_syscall.s`:** x64 code that runs `syscall` directly goes through Wine's `invoke_arm64ec_syscall` stub. That stub changed `rdx`, `r8`, `r9`, `r10` and `rflags`. The Windows kernel keeps them. The new stub keeps them too.
- **`waitq_fix.s`:** Wine keeps the `RtlWaitOnAddress` / `RtlWakeAddress*` wait queues in user space, behind 256 spinlocks. A thread suspended while it holds one blocks every other thread on that bucket. If the suspender needs the same bucket, it spins forever. The patch marks the thread as "in a syscall callback" while it holds the lock, so Wine's suspend waits until the lock is free. This did not stop the AoE IV freeze, but it is a real Wine bug.

## Debug output slows the game

Turn debug output off in three places:

- Settings → Debug → "Enable Wine Debug Logs".
- A `WINEDEBUG` variable in the container's Environment tab.
- Bionic Steam mode copies the Settings debug channels into `WINEDEBUG`, even with the switch off. We saw `+syscall`. Set `WINEDEBUG=-all` in the container to stop it.

## The open blocker: the copy protection stops the game

Sequence in every run:

1. Login and menu work.
2. The game's network socket closes under it (`TlsConnection::Shutdown … errno=10038`).
3. About one minute later, one protection thread suspends all other threads and removes their stack memory.
4. Wine then hangs (100 % CPU on one thread), or the process exits.

This is a deliberate response by the protection. The trigger happens in the first one to two minutes.

**Ruled out as the trigger** (the game still stopped): online vs offline, stale ticket, Steam emulator vs Bionic Steam, debug logs, FEX presets Intermediate and Stability, `HideHypervisorBit`, FEX 2609 and FEX main, emulator DLL name (`xtajit64.dll`), `SmallTSCScale=0`, original vs patched Wine `ntdll`.

**`SMCChecks`:** `none` exits after about 2 minutes, `full` hangs at start from the Play button, and `mtrack` (the default) freezes as above.

**Comparison with the Mac:** the same game files run on the Mac (Rosetta + x86-64 Wine). With GameNative's Steam emulator files, the Mac fails the game's product check that the Thor passes. So the protected code acts differently under FEX/ARM64EC and under Rosetta.

## Debug tools that worked (over ADB)

- **Run a Windows program in the running session:** `winhandler.exe` listens on UDP `127.0.0.1:7946`. Send a 64-byte packet: byte `2`, then int32 sizes (name + params + 8, name, params), then the name and the params. Example: `cmd` with `/c D:\x.bat`. [`tools/winhandler_exec.py`](tools/winhandler_exec.py) builds and sends it.
- **Drives:** `D:` = `/sdcard/Download`. Wine's own files are at `Z:\opt\<wine-name>`.
- **Thread state from adb:** `/proc/<pid>/task/*/stat` and `status` are readable, and `comm` holds the Wine thread name. ptrace and simpleperf are blocked on a user build.
- **Probes:** small mingw-built programs (thread contexts, suspend counts, stack dumps, memory maps) work against the game process. See [`tools/probes`](tools/probes); `build.sh` builds them all. Each one writes its report to `D:\`.

  | Probe | Shows |
  |---|---|
  | `tctx` | x64 context of every thread, to find a spinning one |
  | `tstack` | code pointers on the busiest thread's stack |
  | `suspinfo` | suspend count of every thread, and the context of suspended ones |
  | `waitq` | the 256 `RtlWaitOnAddress` buckets and their spinlocks |
  | `stk` | state of each thread's top stack pages |
  | `stkscan` | return addresses into an ntdll range on every stack |
  | `vq` | memory state around one address, and which thread's stack it is |
  | `vmmap` | address space summary |
  | `netprobe` | DNS, sockets, adapters, BCrypt and WinHTTP, each with a time limit |
  | `selfchk` | confirms the `waitq_fix` patch is loaded and drives the wait/wake paths |

- **Per-game FEX settings:** `Z:\home\xuser\.fex-emu\AppConfig\RelicCardinal.exe.json` takes raw values, for example `"SMCChecks":"2"`.
- **Emulator DLL name:** set by `HKLM\Software\Microsoft\Wow64\amd64`. Proton resets it from `wine.inf` line 416 on each start.
- **Own FEX build:** llvm-mingw 20260922 + CMake (`toolchain_mingw.cmake`, `MINGW_TRIPLE=arm64ec-w64-mingw32`). It builds in about 12 s. Package it as a `.wcp` (tar.xz with `profile.json` + DLLs) and import it in the Contents Manager.

## Current Thor state (to restore stock)

- FEXCore version `2610-aoe-1` (own build). Stock: `2609-0`.
- `Z:\opt\proton-11.0-99-arm64ec-1\share\wine\wine.inf` loads `xtajit64.dll`. Original: `wine.inf.orig`.
- `ntdll.dll` is the original. Patched copies: `ntdll.dll.waitqfix`, `ntdll.dll.pre-waitq`.
- Container environment: `WINEDEBUG` was last set to `+winsock,+timestamp,+tid` for a test. Set it to `-all`.
- Bionic Steam is on.
- FEX per-game file: `HideHypervisorBit=1`.

## License

The tools and scripts are MIT (see [LICENSE](LICENSE)). Each patch follows the license of the project it changes: Box64 is MIT, Wine is LGPL-2.1-or-later, GameNative is GPL-3.0.

Age of Empires is a trademark of Microsoft. This project is not affiliated with Microsoft, Relic, AYN or GameNative.
