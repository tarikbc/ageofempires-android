# You can edit the container config from inside Wine

> **Archived.** How to edit the container config from inside Wine (2026-10-06). The how-to parts are in [GAMENATIVE.md](../../guides/GAMENATIVE.md). The winhttp and verbose-channel findings belong to the network and ntdll leads, both closed; the lead called "now the priority" below was void. The story: [STORY.md](../../STORY.md).

The single most useful thing found in this session. GameNative's live container configuration is a
plain JSON file **inside the imagefs**, so it can be read and written with nothing but `cmd` in a
running session — no UI, no tapping, no `.wcp` import.

```
Z:\home\xuser\.container                    <- the config (and, per launch, xuser-STEAM_<appid>)
Z:\home\xuser\applied_config.json           <- a stale copy from an earlier session (an output, not an input)
```

Structure (abridged):

```json
{ "id": "STEAM_1466860",
  "envVars": "... WINEDEBUG=+winsock,+timestamp,+tid",
  "dxwrapper": "vkd3d",
  "drives": "D:/storage/emulated/0/Download E:/data/data/app.gamenative/storage
             A:/storage/emulated/0/GameNative/Steam/steamapps/common/Age of Empires IV",
  "fexcorePreset": "INTERMEDIATE",
  "extraData": { "appliedContainerVariant": "bionic",
                 "appliedWineVersion": "proton-11.0-99-arm64ec-1",
                 "fexcoreVersion": "ntdll-waitq-fix-1",
                 "box64Version": "0.4.5-aoefix-1", ... } }
```

**Verified working:** editing `envVars` to `WINEDEBUG=+winhttp,+timestamp,+tid`, copying the file back
with `copy /y`, and relaunching. The setting took effect and **GameNative did not overwrite the file**
(re-read after launch and it still said `+winhttp`).

This closes a question that had been open since round 3 — where `WINEDEBUG` comes from. Answer: this
file's `envVars`, which is why neither `HKCU\Environment` nor the container's Environment tab in the UI
ever showed it.

## What the winhttp channel revealed

With `+winhttp` on, the game's HTTP traffic is visible. It is **Xbox Live and telemetry**, not the AoE
backend:

```
https://title.mgt.xboxlive.com/titles/default/endpoints?type=1
https://self.events.data.microsoft.com/OneCollector/1.0/
```

That matters because it reclassifies a long-standing symptom. The game's log is full of
`HTTPCLIENT: dwError=12152/12157` and `XAL_TELEMETRY` failures, and `XAL` is the Xbox Authentication
Library — so those are **Xbox Live calls**, while the AoE backend session runs over the game's own
asio/websocketpp TLS (`WebSocketConnection`), which never touches winhttp.

And when `tools/research/xboxprobe.c` fetches those same endpoints from inside the session, **all of them work**:

```
title.mgt.xboxlive.com (XAL)       ok  err=0  http=200   1566 ms  {"EndPoints":[...]}
self.events.data.microsoft.com     ok  err=0  http=400   1474 ms
control: AoE backend               ok  err=0  http=200    881 ms
```

So `12152`/`12157` are **downstream of the asio/openssl failure** (`errno=10038`, the
[`NETWORK.md`, part 2](NETWORK.md#part-2-the-kill-is-downstream-of-losing-the-backend-session) chain) and not an endpoint problem. Several rounds went into
"Wine cannot do the game's HTTP" — that was the wrong thread, and this closes it.

## Still true

The session-loss chain was already shown not to cause the kill
([KILL-STILL-OPEN.md](KILL-STILL-OPEN.md)), so this does not change the kill picture directly. What it
does change is leverage: **`envVars` is now an editable channel** for anything FEX or Wine reads from
the environment — `FEX_TSOENABLED`, `FEX_MULTIBLOCK`, `FEX_X87REDUCEDPRECISION`, `BOX64_*`, `WINEDEBUG` —
and `extraData` names the Wine version and FEXCore content the container uses.

## Editing workflow, and what the tree looks like

Re-pulling before editing matters: an earlier attempt failed because I patched a stale local copy while
the live file had already moved on. `tools/research/cfgedit.py OLD NEW` now does pull → patch → push in one step
and prints what actually landed.

```
python3 tools/research/cfgedit.py "WINEDEBUG=+winhttp,+timestamp,+tid" "WINEDEBUG=+thread,+sync,+virtual,+timestamp,+tid"
```

The Wine versions are plain directories, and they are **read-only**:

```
Z:\opt\  apps  mono-gecko-offline  proton-11.0-99-arm64ec-1  wine  winetricks
Z:\opt\proton-11.0-99-arm64ec-1\  bin/  lib/  share/  prefixPack.txz  profile.json
  copy out the ntdll          -> works
  echo test > writetest.txt   -> fails (file not found)
```

So the Proton tree cannot be patched in place from Wine. `extraData.appliedWineVersion` names one of
these directories and GameNative builds `WINELOADER` from it (`…/lib/wine/aarch64-unix/wine`), so
arbitrary paths will not work either — changing the loaded ntdll still requires a `Proton`-type `.wcp`
and therefore an import.

## What the verbose channels showed (round 17)

With `WINEDEBUG=+thread,+sync,+virtual,+timestamp,+tid` and `adb logcat` streaming to a file (the buffer
rolls immediately at these volumes — the capture reached **1.0 GB**):

| pattern | count |
|---|---|
| `NtQueryVirtualMemory` | 35,248 |
| `NtSetInformationThread` | 39 |
| `SuspendThread` / `NtSuspendThread` | **0** |
| `ThreadHideFromDebugger` / `HideFromDebugger` | **0** |

The kill itself is invisible in these channels — no suspend lines at all — and nothing named
`HideFromDebugger`, so this does not settle round 5's question about
`NtSetInformationThread(ThreadHideFromDebugger)`.

**But the tail of the log is the interesting part.** The last lines before the process died are a storm
of:

```
2041932.587:00f0:trace:sync:RtlWakeAddressAll
2041932.587:00f0:trace:sync:RtlWakeAddressAll      (repeated many times per millisecond)
2041932.587:00f0:trace:virtual:NtMapViewOfSection
2041932.588:00f0:trace:virtual:NtQueryVirtualMemory
```

That is the Wine `RtlWaitOnAddress` / `RtlWakeAddress` pathology this repo has a patch for
(`patches/experiments/proton-arm64ec-ntdll/waitq_fix.s`), and **that patch has never been loaded** — the deployed
ntdll is pristine ([NTDLL-NEVER-LOADED.md](NTDLL-NEVER-LOADED.md)). The earlier note that it "did not
stop the AoE IV freeze" is therefore worthless, and the kill correlating with this storm means the
waitq fix is genuinely untested rather than disproved.

**That is a better lead than the module-name question, and it is now the priority.**

## Which fields you can actually change (round 18)

There are two files, and they are not equivalent:

| file | shape |
|---|---|
| `Z:\home\xuser\.container` | the **applied** copy — `extraData.appliedWineVersion`, `extraData.appliedContainerVariant`, … |
| `Z:\home\xuser-STEAM_<appid>\.container` | the **source** copy — top-level `wineVersion`, `containerVariant`, `emulator`, `fexcoreVersion`, `envVars`, `drives` |

Only some of it is honoured. Established by experiment:

| field | edited in | took effect? |
|---|---|---|
| `envVars` (`WINEDEBUG=…`) | applied copy | **yes** — the round-17 capture really was on `+thread,+sync,+virtual` |
| `wineVersion` / `extraData.appliedWineVersion` | both, separately | **no** — `WINELOADER` stayed `…/proton-11.0-99-arm64ec/lib/wine/aarch64-unix/wine` |

So GameNative treats `envVars` as user configuration and reads it back, while the Wine version and
FEXCore content are re-applied from its own store. **Environment changes are ours to make; a Wine-version
or content change still needs the UI.**

Practical consequence: the waitq ntdll cannot be deployed this way — it has to come from a
`Proton`-type `.wcp`, i.e. an import — but anything FEX or Wine reads from the environment
(`FEX_TSOENABLED`, `FEX_MULTIBLOCK`, `FEX_X87REDUCEDPRECISION`, `WINEDLLOVERRIDES`, `WINEDEBUG`,
`BOX64_*`) can be changed freely.

## `Open container` is the fast path

GameNative can start the container as Windows, independently of any game — the AYN assistant panel's
**Open container** entry (cog → panel → Open container). It brings up `explorer` plus `winhandler` in
about 30 s, with no game launch, no save conflict, and no `/wcp` selection. Everything in this document
was done through that session. It is the right way to run a probe or edit a config.
