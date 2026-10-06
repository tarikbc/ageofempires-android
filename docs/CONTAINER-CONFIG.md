# You can edit the container config from inside Wine

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

And when `tools/xboxprobe.c` fetches those same endpoints from inside the session, **all of them work**:

```
title.mgt.xboxlive.com (XAL)       ok  err=0  http=200   1566 ms  {"EndPoints":[...]}
self.events.data.microsoft.com     ok  err=0  http=400   1474 ms
control: AoE backend               ok  err=0  http=200    881 ms
```

So `12152`/`12157` are **downstream of the asio/openssl failure** (`errno=10038`, the
[SESSION-LOSS.md](SESSION-LOSS.md) chain) and not an endpoint problem. Several rounds went into
"Wine cannot do the game's HTTP" — that was the wrong thread, and this closes it.

## Still true

The session-loss chain was already shown not to cause the kill
([KILL-STILL-OPEN.md](KILL-STILL-OPEN.md)), so this does not change the kill picture directly. What it
does change is leverage: **`envVars` is now an editable channel** for anything FEX or Wine reads from
the environment — `FEX_TSOENABLED`, `FEX_MULTIBLOCK`, `FEX_X87REDUCEDPRECISION`, `BOX64_*`, `WINEDEBUG` —
and `extraData` names the Wine version and FEXCore content the container uses.
