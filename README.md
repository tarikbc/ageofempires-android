# Age of Empires IV on the AYN Thor (GameNative 1.2.1)

Running AoE IV on an Android handheld with [GameNative](https://github.com/utkarshdalal/GameNative).
Device: AYN Thor (Snapdragon 8 Gen 2, Adreno 740, 16 GB, Android 13). Game build 16.3.11308.

**Goal:** the game's copy protection, **Aegis** (Relic's in-house virtualization/anti-tamper), stops the
game 2–4.5 minutes in. The user owns the game, so the aim is to make the protection *accept* this
environment — not to strip it out.

**Where we are (2026-10-06, 22:30):**

- **The kill is measured on a clean baseline.** With `WINEDEBUG=-all` the game loads its menu world in under
  three minutes. Then, 2 min 3 s to 3 min 2 s after start, every thread but one goes to Windows suspend
  count 1 and the log never grows again. That happened in 8 of the 9 runs that got past start-up today; the
  ninth (a control build) exited instead. The thread that does it starts at `RelicCardinal.exe+0x3e69304`.
  See [KILL-REMEASURED.md](docs/KILL-REMEASURED.md) and [SMC-TRAP-HIDDEN.md](docs/SMC-TRAP-HIDDEN.md).
- **The evening runs (19:52 to 20:29) were slowed by leftover debug channels.** The container still had
  `WINEDEBUG=+thread,+sync,+virtual,+timestamp,+tid` from round 17. With it, a run stopped in
  `Property Bag Manager`; without it, the same step took 22 s. The "MapGen wall" was a misreading: that
  message appears in every run that gets further. See [WINEDEBUG-LEFTOVER.md](docs/WINEDEBUG-LEFTOVER.md).
- **The SMC trap is not the trigger.** FEX does leak its write trap to the guest
  ([SMC-CONFIRMED.md](docs/SMC-CONFIRMED.md)), but a FEX build that hides it (patch 0004, verified with
  `smctest2`) was still killed in 5 of 5 runs. Inside the game, 732,206 memory queries passed the filter
  and none touched a trapped page. See [SMC-TRAP-HIDDEN.md](docs/SMC-TRAP-HIDDEN.md).
- **The session drop is not the trigger either.** Two runs had no `errno=10038` and were killed on time.

Read [docs/EXPERIMENTS.md](docs/EXPERIMENTS.md) first: it is the ledger of what was tried and what
happened, including the traps that produced wrong conclusions.

## The only success criterion

> The game's own log (`warnings.log`) is still growing **after ~5 minutes.**

"The process is still alive" is **not** success. The kill *suspends* every thread and leaves the process
hung, so `ps` keeps showing it and the log goes silent. Verified repeatedly.

## Verified facts

1. **The protection is Aegis.** Its build log is in the game folder (`Aegis_RelicCardinal.log`),
   referencing `Foreign/Aegis/Retail/internal/BlockList.json`; it ships a **386-item blocklist**.
2. **The kill:** a launch-time thread, entry `RelicCardinal.exe+0x3e69304`, whose function spans
   `+0x3e6b53c..+0x3e6cd0c`. It suspends all other threads via two indirect `SuspendThread` sites
   (`+0x49065a1`, `+0x490699d`), then spins at 100 % CPU. Its region `0x3e40000..0x3f90000` holds 1561
   registered functions.
3. **FEX advertised itself** — CPUID leaf `0x40000000` returned vendor `FEXIFEXIEMU`. Patched and
   verified live (`eax=0x0 vendor=''`). **The kill still fires.**
4. **The ntdll patches have never run.** Wine loads ntdll from *its own tree*, not
   `C:\windows\system32`. The mapped ntdll is pristine. This voids the earlier "patched ntdll" result.
5. **The game's backend session dies** (`TlsConnection::Shutdown errno=10038` → status `1006`), and the
   kill lands ~73 s later — but a run with a **healthy session throughout still died**. Correlational.
6. **`12152`/`12157` are noise.** They are Xbox Live (XAL) calls, downstream of the asio failure; the
   endpoints answer `200` from Wine.
7. **The kill's log tail is an `RtlWakeAddressAll` storm** — the Wine spinlock pathology
   `patches/proton-arm64ec-ntdll/waitq_fix.s` exists to fix, which has never been loaded.
8. **Wine TLS to the AoE backend is slow** (5–19 s vs 0.94 s native). `CertificateRevocation=0` fixes
   the timing, not the kill.

## Open hypotheses, best first

1. **Aegis hashes a memory region and compares it to an expected value.** Most likely `ntdll.dll`, whose
   bytes differ between Wine's ARM64EC build (Thor) and its x86-64 build (Mac/Rosetta) — which is
   exactly where the behaviour differs. **Located:** Aegis carries its own xxHash implementation at RVA
   `0x3e42d34` (plus siblings `0x3e563cc`, `0x3e681cc`, …), each with exactly one caller — and
   `0x3e681cc` is called from `0x3e68e3f`, ~1.2 KB before the kill thread entry `0x3e69304`. The calls
   pass a fixed 192-byte high-entropy blob at RVA `0x56fbf40`. See
   [KILL-ANALYSIS.md](docs/KILL-ANALYSIS.md).
   *Test:* resolve the parameter flow (Ghidra) to recover the hashed range and the expected hash.
2. **~~FEX leaks its self-modifying-code trap to the guest, and Aegis checks for exactly that.~~ Ruled
   out 2026-10-06** ([SMC-TRAP-HIDDEN.md](docs/SMC-TRAP-HIDDEN.md)): with the trap hidden the kill still
   fires (5 of 5), and no in-game query ever touched a trapped page. Original text kept below.
   **FEX leaks its self-modifying-code trap to the guest, and Aegis checks for exactly that.**
   Under `SMCChecks=mtrack`, FEX re-protects the guest's RWX pages to `PAGE_EXECUTE_READ` to trap writes
   (`InvalidationTracker::GetTrapProt`), and it does **not** intercept the guest's
   `NtQueryVirtualMemory` — so the guest is told its own code page is read-only when it set it
   read-write. Aegis calls `NtQueryVirtualMemory` **35,248 times per run**. This explains the kill's
   indifference to everything environmental, the exact `SMCChecks` sensitivity (`none` → no trap but no
   invalidation → exits at 2 min; `full` → stops at start-up with the trap still armed, re-measured in
   [KILL-REMEASURED.md](docs/KILL-REMEASURED.md)), why the Mac passes, and why
   byte-comparing probes saw a stable image (it is a *protection* change). **Fix:** intercept
   `NtQueryVirtualMemory` and report the untrapped protection. See
   [SMC-HYPOTHESIS.md](docs/SMC-HYPOTHESIS.md).
3. **A Wine API returns something Windows would not.** One concrete instance:
   `NtSetInformationThread(ThreadHideFromDebugger)` returns `0xC0000002` where Windows returns success —
   a plausible dependency of Aegis's "Stealth-Startup". *Test:* fix it in Wine's unix-side `ntdll.so`
   (the PE export is a bare syscall stub).

## Next actions

1. **Try the game through x86-64 Wine under Box64** instead of ARM64EC Wine + FEX. It splits the problem: if
   Aegis accepts it, the trigger is specific to ARM64EC. The repo records an earlier Box64 attempt only as
   "6× slower than Rosetta" (EXPERIMENTS.md); whether the kill fired there is not recorded.
2. **Trace what Aegis asks the OS.** Patch 0004 already filters every syscall in the process; extend it to log
   syscalls whose x64 caller lies in Aegis's region (`+0x3e40000..+0x3f90000`), then compare with the same
   trace where the game works (the Mac, or Proton on x86-64). The first difference points at the check.
3. **Find what wakes the kill thread.** It sleeps (0 ms CPU) until 2 to 3 minutes in, then suspends every
   thread within about 50 ms. Two threads share its entry `+0x3e69304` from the start. Capture its stack and
   the code it runs at that moment (on the base binary it keeps spinning, so its code stays live).
4. **Ghidra the xxHash64 callers**: recover the hashed range and expected hash.
5. **Fix `ThreadHideFromDebugger`** in Wine's unix side, and **deploy the waitq ntdll properly** (needs a
   `Proton`-type `.wcp`).

## Traps (each cost real time)

- **Alive ≠ working.** See the criterion above.
- **`text.bin` is indexed by `RVA - 0x1000`**, not RVA. Use `RelicCardinal.unpacked.exe` (restored
  `.text`); the on-disk `.exe` is still packed.
- **`uiautomator dump` alone lies here** — use `uiautomator dump --windows`.
- **GameNative manages the Wine version and FEXCore content itself.** Editing `wineVersion` in the
  container config is silently ignored; `envVars` *is* honoured.
- **Anything relying on "the patched ntdll" is void** — it was never loaded.
- **`C06T13R-1X-*` is server connectivity**, not file integrity, and does not prevent playing.
- **Debug channels left in the container config slow every later run.** Check
  `findstr /c:"WINEDEBUG" "Z:\home\xuser\.container"` before any judged run
  ([WINEDEBUG-LEFTOVER.md](docs/WINEDEBUG-LEFTOVER.md)).
- **Find the game by process NAME.** `explorer.exe` and `winhandler.exe` carry the game's path in their
  arguments, and a match on arguments picks `explorer` first.
- **`tctx` suspends the thread it reads**, and it can hang there, leaving the thread at suspend count 1.
  Use `suspinfo` (no suspend) to judge a kill.

## Setup that works

| Part | Value |
|---|---|
| Wine | `proton-11.0-99-arm64ec` |
| CPU emulator | FEXCore; container variant `bionic` |
| FEX DLL in use | `C:\windows\system32\libarm64ecfex.dll`, SHA-1 `460568b8` (CPUID-patched). The selected FEXCore Version, `ntdll-waitq-fix-1`, installs only an `ntdll.dll`, so this file is a leftover from earlier installs |
| DX wrapper | VKD3D (vkd3d-proton 2.14.1 + DXVK 2.4.1-gplasync) |
| GPU driver | Turnip v26.2.0 R4 |
| Executable | `RelicCardinal.exe` (set by hand after import) |
| Wine debug | `WINEDEBUG=-all` in `envVars` (set 2026-10-06 20:49). The session started from it does not define `WINEDEBUG` at all (read with `set` at 21:10), so no trace channels are on |

**Debug output slows the game.** Turn it off in Settings → Debug, in the container Environment tab, and
note Bionic Steam copies Settings channels into `WINEDEBUG` even when the switch is off.

## Tools that work over ADB

- **Run a Windows program in the live session:** `winhandler.exe` on UDP `127.0.0.1:7946`.
  [`tools/winhandler_exec.py`](tools/winhandler_exec.py) builds the packet. `cmd` + `/c D:\x.bat`
  (program + params must stay ≤ 51 bytes, so wrap in a `.bat`).
- **Open container** (cog → assistant panel) starts `explorer` + `winhandler` in ~30 s **with no game**.
  This is the fast path for probes and config work.
- **Drives:** `D:` = `/sdcard/Download`. Wine trees are `Z:\opt\<wine-name>` (**read-only**).
- **Container config:** `Z:\home\xuser\.container` — editable from inside Wine; `envVars` is honoured.
- **Per-game FEX settings:** `Z:\home\xuser\.fex-emu\AppConfig\RelicCardinal.exe.json`.
- **Emulator DLL name:** set by `HKLM\Software\Microsoft\Wow64\amd64`.
- **Compare FEX builds:** [`tools/ab_fex.py`](tools/ab_fex.py) installs each build in turn (rename trick,
  hash checked) and judges a run on each. [`tools/smctest2.c`](tools/smctest2.c) shows whether the SMC trap
  is visible and still catching rewrites; [`tools/probes/fexstats.c`](tools/probes/fexstats.c) reads patch
  0004's counters from a live process.
- **Judge a run:** [`tools/run_watch.py`](tools/run_watch.py) `--launch` restarts GameNative, taps Play
  (and restarts the app if no game process appears within 90 s, for the "Syncing cloud saves" hang),
  copies `warnings.log` every 10 s and runs `suspinfo` every 20 s through
  [`tools/thor/mon.bat`](tools/thor/mon.bat) (push it to `D:\mon.bat`), and writes a timeline.
- **Probes:** [`tools/probes`](tools/probes) (`build.sh` builds all), each writing to `D:\` — `tctx`,
  `tstack`, `suspinfo`, `waitq`, `stk`, `stkscan`, `vq`, `vmmap`, `netprobe`, `selfchk`.

## Patches in this repo

| Path | What it is |
|---|---|
| [`patches/fex/`](patches/fex) | 0002 hides the CPUID vendor; 0004 hides the SMC trap from guest queries (works; does not stop the kill). 0001/0003 stop the game at start-up. |
| [`patches/box64/`](patches/box64) | Decode SSE/AVX stores so write faults reach Wine as writes. Worth upstreaming. |
| [`patches/proton-arm64ec-ntdll/`](patches/proton-arm64ec-ntdll) | Two binary patches for the ARM64EC `ntdll.dll` (`invoke_arm64ec_syscall` register fix; `--waitq` spinlock fix). |
| [`patches/gamenative/`](patches/gamenative) | Fresh Steam ticket per launch. Not built or tested. |

## Docs

Start with **[docs/EXPERIMENTS.md](docs/EXPERIMENTS.md)** — the ledger of what was tried, what worked
and what did not. Then:

| Doc | Covers |
|---|---|
| [`AEGIS.md`](docs/AEGIS.md) | The protection: identity, build log, blocklist, timing constants |
| [`KILL-ANALYSIS.md`](docs/KILL-ANALYSIS.md) | The captured kill and the hash hypothesis (with next step) |
| [`NTDLL-NEVER-LOADED.md`](docs/NTDLL-NEVER-LOADED.md) | Why both ntdll patches are void, and where Wine really loads ntdll from |
| [`ANALYSIS-GOTCHAS.md`](docs/ANALYSIS-GOTCHAS.md) | Read before any offline analysis (`text.bin` indexing, packed vs unpacked, Mac tooling) |
| [`CONTAINER-CONFIG.md`](docs/CONTAINER-CONFIG.md) | Editing the container config from Wine; `Open container` |
| [`WINE-GAPS.md`](docs/WINE-GAPS.md) | Wine behaviours Aegis could notice (`ThreadHideFromDebugger`) |
| [`SMC-HYPOTHESIS.md`](docs/SMC-HYPOTHESIS.md) | **Aegis self-modifies its code and FEX's SMC handling is the suspect** — the first explanation that accounts for the `SMCChecks` sensitivity, the Mac passing, and nothing environmental helping. |
| [`SMC-CONFIRMED.md`](docs/SMC-CONFIRMED.md) | **CONFIRMED on hardware:** FEX removes write permission from a guest page the moment it translates code in it — `RWX` becomes `RX` with no request from the guest. |
| [`CONTAINER-WONT-START.md`](docs/CONTAINER-WONT-START.md) | **How the container was fixed**, and the two things that were NOT the cause (a locked device, and the MapGen message). Also the rename-a-mapped-DLL trick. |
| [`KILL-STILL-OPEN.md`](docs/KILL-STILL-OPEN.md) | Historical: the pre-SMC state of the kill question. **Superseded** by SMC-CONFIRMED / FIX-VERIFIED. |
| [`SMC-TRAP-HIDDEN.md`](docs/SMC-TRAP-HIDDEN.md) | **The SMC trap is not the trigger.** Patch 0004 hides it (verified), and the kill still fires in 5 of 5 runs; in-game counters show no query ever touched a trapped page. Fix vs control table. |
| [`KILL-REMEASURED.md`](docs/KILL-REMEASURED.md) | **The kill on the clean baseline**, measured with `suspinfo`: timeline, suspend counts, timing after `errno=10038`. Also why the no-trap build and `SMCChecks=full` both stop at start-up (shared factor: full-SMC validation). |
| [`WINEDEBUG-LEFTOVER.md`](docs/WINEDEBUG-LEFTOVER.md) | **Why the evening runs stalled**: leftover debug channels. The A/B, the corrected claims (MapGen, "stock" FEX), and the FEX setup as measured. |
| [`FIX-VERIFIED.md`](docs/FIX-VERIFIED.md) | Historical. The `smctest` result for the no-trap build stands; its run results were measured with the debug channels on. Read WINEDEBUG-LEFTOVER.md first. |
| [`DEATH-IS-NOT-THE-KILL.md`](docs/DEATH-IS-NOT-THE-KILL.md) | **Retracted** — the thread-state method it is based on cannot detect Wine's `SuspendThread` at all, so it proves nothing either way. Kept for the correction and for the correct instrument (`tools/probes/suspinfo.c`). |
| [`BUILDING-FEX.md`](docs/BUILDING-FEX.md) | Building ARM64EC FEX on macOS: toolchain, the three macOS problems that abort configure, and the artifact. The patched `libarm64ecfex.dll` builds successfully. |
| [`UPSTREAM-FEX-ISSUE.md`](docs/UPSTREAM-FEX-ISSUE.md) | Draft FEX issue: the SMC write trap is observable by the guest through `NtQueryVirtualMemory`, with a game-independent reproducer and a suggested fix. |
| [`MODULE-LIST.md`](docs/MODULE-LIST.md) | The loaded module names that do not exist on Windows |
| [`FEX-VENDOR-LEAK.md`](docs/FEX-VENDOR-LEAK.md) | The CPUID `0x40000000` leak and its patch |
| [`FEX-PATCH-LIVE.md`](docs/FEX-PATCH-LIVE.md) | The patch verified live — and the kill surviving it |
| [`SESSION-LOSS.md`](docs/SESSION-LOSS.md) | The `errno=10038` → `1006` → kill-73 s-later chain |
| [`TLS-REVOCATION-FIX.md`](docs/TLS-REVOCATION-FIX.md) | Wine TLS timing to the backend |
| [`BACKEND-SESSION.md`](docs/BACKEND-SESSION.md) | End-to-end network measurements |
| [`GAMENATIVE-UI.md`](docs/GAMENATIVE-UI.md) | Driving the GameNative UI over adb |
| [`AUTOMATION-PATHS.md`](docs/AUTOMATION-PATHS.md) | GameNative's intents, and what did not work |

## License

Tools and scripts are MIT (see [LICENSE](LICENSE)). Patches follow their project: Box64 MIT, Wine
LGPL-2.1-or-later, GameNative GPL-3.0.

Age of Empires is a trademark of Microsoft. Not affiliated with Microsoft, Relic, AYN or GameNative.
