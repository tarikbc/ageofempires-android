# The network theory: the game's backend session and Wine's TLS

> **Archived.** Ruled out as a trigger (2026-10-06). Two statements below are wrong: the patched ntdll was never loaded ([NTDLL-NEVER-LOADED.md](NTDLL-NEVER-LOADED.md)), and `CertificateRevocation=0` is a mitigation, not a fix ([WINE-GAPS.md](WINE-GAPS.md)). Whether that registry value is still set on the Thor was not checked; the current setup does not need it. The story: [STORY.md](../../STORY.md).

Three write-ups from 2026-10-06, joined in the order they were written. The stops were later traced to the hook check and the watchdog ([HOOK-CHECK.md](../../how-it-works/HOOK-CHECK.md), [WATCHDOG.md](../../how-it-works/WATCHDOG.md)).

- [Part 1](#part-1-the-games-backend-session-is-broken): The game's backend session is broken
- [Part 2](#part-2-the-kill-is-downstream-of-losing-the-backend-session): The kill is downstream of losing the backend session
- [Part 3](#part-3-wines-tls-to-the-age-backend-costs-519-s-and-certificaterevocation0-fixes-it): Wine's TLS to the Age backend costs 5–19 s, and `CertificateRevocation=0` fixes it

## Part 1: The game's backend session is broken

Aegis's kill lands ~1 minute *after* `TlsConnection::Shutdown … errno=10038`
([KILL-ANALYSIS.md](KILL-ANALYSIS.md)). The network is therefore the most concrete unexplained
thing, and it has now been measured from several directions. The short version: **the network and
Wine's own networking are fine; the game's specific requests are not.**

### The network path itself is fine

`tools/research/netprobe.c`, in the live session, DNS up to TLS:

```
DNS  www.ageofempires.com         rc=0 (81 ms)  -> 150.171.110.36
TCP  www.ageofempires.com         rc=0 err=0 (20 ms)
HTTPS www.ageofempires.com       OK status=200 (9361 ms)
HTTPS www.microsoft.com          OK status=200 (227 ms)
```

The AoE host is ~40x slower than Microsoft's (9.4 s vs 0.23 s), but it works. (An earlier unbounded
attempt appeared to hang, which is why the probe sets `WinHttpSetTimeouts`.)

### Wine's WebSocket layer is fine

The game uses **WebSocket++/0.8.2** over Relic Link (`WebSocketManager.cpp`), and the request path is
`/wss/` — found as a string in `RelicCardinal.exe`, along with the protocol constants:

```
RL_MISSPOLL / RL_USE_WEBSOCKET_PERCENT / RL_KEEPALIVE
"wss/"
{"clientLibVersion":%d,"operation":%d,"sessionToken":"%s"}   <- matches the log line exactly
{"operation":%d,"ackCount":%d}
/game/login/readSession
```

Holding that WebSocket, native vs Wine:

| client | path | result |
|---|---|---|
| Native Python (`tools/research/ws_native.py`, `tools/research/ws_paths.py`) | `/wss/` | `101`, survived 45 s |
| `tools/research/wsprobe.c` inside Wine | `/wss/` | `101`, **still alive at 165 s** |
| **the game** | `/wss/` | `101`, **died at 39 s** |

Path check (native): `/wss/` → 101, `/wss/v1/` → 101, `/wss` → 302, `/game/wss/` → 404, `/` → 200.
An unauthenticated upgrade to `/` is refused with `200`, so only `/wss/`-style paths are real.

### Wine's winhttp is fine

`tools/research/apiprobe.c` against the real backend:

```
leaderboards title=age4   ok http=200 bytes=399
leaderboards title=age2   ok http=200 bytes=399
game host /wss/           ok http=405 (GET not allowed)
```

`getAvailableLeaderboards?title=age4` returns the correct JSON through Wine, and 33 KB natively. So
the same backend serves AoE IV exactly as the [aoe2-apis](https://github.com/ustacode/aoe2-apis)
catalogue describes for AoE2 — swapping `title` is the whole trick.

### So what is actually broken

The game's own HTTP calls fail with two codes, and the transition matters:

| phase | code | meaning |
|---|---|---|
| before the socket dies | `12152` | `ERROR_WINHTTP_INVALID_SERVER_RESPONSE` |
| **after** | `12157` | `ERROR_WINHTTP_SECURE_FAILURE` — TLS cannot establish at all |

The sequence in the game's log:

```
11:06:59.418  WebSocketConnection::OnConnect                 <- working
11:06:59.422  Sending session token                          <- authenticated
11:07:00.821  ProcessMessages: PresenceMessage               <- live data
11:07:37.980  TlsConnection::Shutdown: SSL shut down failed; errno=10038
11:07:38.139  OnConnectionClose; statusCode=1006             <- abnormal close
11:07:38.292  Creating a websocket connection               <- reconnect...
              (no further OnConnect -- the reconnect never succeeds)
11:08:51      kill, ~73 s later
```

Since a bare Wine WebSocket to the same path survives 165 s and bare Wine HTTP to the same backend
returns 200, neither "Wine cannot do WebSockets" nor "Wine cannot do TLS" explains this. The 399-byte
truncation in `apiprobe` is the probe's own 400-byte read buffer, not the server.

**Next step: capture the game's actual requests.** `WINEDEBUG=+winhttp` is the direct route, but
`WINEDEBUG` is not in `HKCU\Environment` or the container's Environment tab, so it is being set by the
launcher — that needs establishing before the channel can be enabled. Failing that, the container
exposes a proxy setting and the Mac has `mitmproxy`, which would show the requests, at the cost of
installing mitmproxy's CA into the prefix so Wine trusts it.

## Part 2: The kill is downstream of losing the backend session

This is the first end-to-end causal chain that fits every observation, including why the Mac lives
and the Thor dies. The game's own log tells the whole story with timestamps.

### The sequence, from the game's log

```
11:06:51.391  OnRequest app ticket returned N
11:06:51.392  SteamAuth received ticket at t=2030124228
11:06:54.887  WebSocketConnection::Process: Creating a websocket connection;
              host=dr-activerelease1-api.worldsedgelink.com port=443
11:06:59.418  WebSocketConnection::OnConnect; m_resumeState=0            <- connected
11:06:59.422  Sending session token; clientLibVersion=191 sessionToken=… <- authenticated
11:07:00.821  ProcessMessages: message=[0,"PresenceMessage",…]           <- live data

11:07:37.980  TlsConnection::Shutdown: socket 0/2032 SSL shut down failed;
              A system error occurred. errno=10038                        <- WSAENOTSOCK
11:07:38.135  WebSocketConnection::Process: Connection closed
11:07:38.139  WebSocketConnection::OnConnectionClose; statusCode=1006    <- abnormal closure
11:07:38.292  Creating a websocket connection (reconnect attempt)

11:07:39.780  WorldwideGetProfileByIDAsync - response was failure: -48
11:07:40.741  HTTPCLIENT [ID 10] dwError=12157                           <- SECURE_FAILURE
              … and every subsequent HTTPS request fails the same way
11:08:51      (last log line) kill, ~73 s after the socket closed
```

#### The error code changes, which is the tell

| phase | `dwError` | meaning |
|---|---|---|
| before the TLS failure | `12152` | `ERROR_WINHTTP_INVALID_SERVER_RESPONSE` — request/response content |
| **after** the TLS failure | `12157` | `ERROR_WINHTTP_SECURE_FAILURE` — **TLS cannot establish at all** |

The connection *worked* — it authenticated, sent a session token, and received a `PresenceMessage`.
Then the socket went invalid and **every later TLS connection failed**. The game never talks to its
backend again, and Aegis kills the process about a minute after the socket closes — the timing already
recorded in [KILL-ANALYSIS.md](KILL-ANALYSIS.md).

So the kill is **downstream of losing the backend session**, not a spontaneous timer.

### Why `errno=10038` points at Wine

`10038` is `WSAENOTSOCK` — "socket operation on non-socket". The socket handle was **invalid** at a
point where the game believed it was live. That is not what a server-side rejection looks like; a
server that wanted to drop the session would close the TCP connection cleanly, giving a normal
WebSocket close, not a handle that stopped being a socket.

And this repository already documents a Wine defect with exactly this signature — from
[`patches/experiments/proton-arm64ec-ntdll/apply.py`](../../../patches/experiments/proton-arm64ec-ntdll/apply.py):

> Patch 1 (always): `invoke_arm64ec_syscall.s` replaces the x64 stub that runs a direct `syscall` from
> emulated x64 code. **Wine's stub clobbered rdx/r8/r9/r10/rflags; the Windows kernel keeps them.**

A syscall stub that clobbers argument registers corrupts syscall parameters. A corrupted handle
passed to a socket call is `WSAENOTSOCK`. That is a mechanism, not a coincidence — and it matches the
error-code *transition* above: the first failure is one corrupted call, and from then on the socket
table state is wrong for that thread.

**Correction, checked by hash — that patch IS applied.** The deployed ntdll is not pristine:

| file | sha256 | meaning |
|---|---|---|
| `lib/wine/aarch64-windows/ntdll.dll` | `606d0a2fb197d37b…` | equals `apply.py`'s `PRISTINE` (unpatched) |
| `C:\windows\system32\ntdll.dll` (the one Wine loads) | `5325f69ecbce31f3…` | equals `apply.py`'s `EXPECTED[False]` — **patched** |

Both are 7,077,888 bytes and the stub pattern `4c89542408` sits at `0xEC050` in the pristine copy
and `0xEC057` in the deployed one — i.e. the 5-byte `jmp` to the code cave is present. So
`proton-11.0-99-arm64ec-1`'s ntdll is byte-identical to `proton-11.0-1-arm64ec`'s, and **patch 1 is
live**. (The deployed build is `EXPECTED[False]`, so the `--waitq` fix is *not* applied.)

That weakens the mechanism above: if the clobbering stub were the whole story, the fix would have
removed it. So either the fix is incomplete — other ARM64EC entry paths may clobber the same
registers — or the invalid handle has a different cause, such as a race in Wine's socket teardown or a
defect in Wine's schannel (the game drives TLS through schannel directly, and WinHTTP uses it too).

**The error-code transition is still the strongest evidence:** `12152` before the failure, `12157`
(`SECURE_FAILURE`) for *everything* afterwards. That is a TLS stack that stops working, not a server
saying no. Wine's schannel is the prime suspect until something else explains that transition.

### Why this explains the Mac/Thor split

The Mac runs a different Wine build (x86-64 under Rosetta) and never had this ARM64EC syscall stub.
Same game files, same servers, no register clobbering, no socket corruption, no kill.

### The first "reproduction" was a false positive — corrected

`tools/research/wsprobe.c` appeared to reproduce the drop outside the game:

```
--- phase 2: WebSocket, held open --- upgrade OK -- holding the socket open
                                      WebSocket receive failed after 30s: err=12152
```

**That was not a WebSocket drop, and the probe does not reproduce anything.** A native client
(`tools/research/ws_native.py`, plain Python `ssl` + hand-rolled WebSocket, no Wine in the path) shows what the
server actually answers for an unauthenticated upgrade at `/`:

```
TLS OK  TLSv1.2  cipher=ECDHE-RSA-AES256-GCM-SHA384
handshake: HTTP/1.1 200 OK                       <- NOT 101 Switching Protocols
Content-Type: application/json;charset=utf-8
Content-Length: 0
Set-Cookie: ApplicationGatewayAffinity=…
```

The backend **refuses** the upgrade, replying `200` with an empty JSON body. Wine's
`WinHttpWebSocketCompleteUpgrade` returned a handle anyway for that non-upgraded connection, and the
subsequent `WinHttpWebSocketReceive` then failed with `12152` (`INVALID_SERVER_RESPONSE`) — which is
exactly what receiving an ordinary HTTP response on a socket you believed was a WebSocket looks like.

So the 30 s timeout was an artifact of the probe, not a Wine defect. The game's WebSocket *does*
upgrade for real (`OnConnect`, session token sent, `PresenceMessage` received), and the game uses some
path or authentication the probe did not.

**The failure to reproduce is itself the finding:** the game's socket works and then dies at ~39 s,
while the same host answers a correct native client, so the drop is not simply "the server closes
after N seconds". Reproducing it needs the game's real upgrade request — which means capturing it
(the Mac has `mitmproxy` installed, and the container has a proxy setting) rather than guessing.

### Next steps

1. Measure the game's handle/socket count across a run. If it climbs to a limit, the `12157` storm is
   exhaustion, not TLS, and the fix is wherever the leak is.
2. Make the probe authenticate (send a session token) so its WebSocket lifetime is comparable to the
   game's — that separates "server drops unauthenticated sockets" from "Wine drops sockets".
3. Re-run the game and watch for `errno=10038` / status `1006` disappearing. **The success criterion is
   the log still growing past ~5 minutes**, not the process merely existing.

Related: [part 1](#part-1-the-games-backend-session-is-broken) measured the network path itself (DNS/TCP/TLS all
work from the session; the AoE host is ~40x slower than Microsoft's).

## Part 3: Wine's TLS to the Age backend costs 5–19 s, and `CertificateRevocation=0` fixes it

A real, measured Wine-side defect, with a fix that demonstrably changes the game's behaviour — even
though it does not, on its own, stop the kill.

### The measurement

`tools/research/reuseprobe.c`, one connection handle, real AoE IV backend endpoint, inside the session:

```
request #1  ok http=200  15448ms
request #2  ok http=200  19436ms   (after 20s idle)
request #3  ok http=200   5293ms   (after 45s idle)
request #4  ok http=200   5666ms   (after 75s idle)
```

The **same** request from the Mac takes **0.94 s**. Control in the same Wine session:
`www.microsoft.com` → **212 ms**. So it is not "Wine is slow", and not the network — it is this host's
certificate chain.

`tools/research/timingtest.c` isolates it:

```
aoe-api default                 total=17194 ms
aoe-api IGNORE_REVOCATION       total= 8998 ms     <- revocation is about half
aoe-api IGNORE_ALL              total=14321 ms
microsoft.com (control)         total=  212 ms
```

The server's chain is four certificates deep:

```
0 CN=*.worldsedgelink.com                     <- leaf
1 GoDaddy TLS Intermediate CA DV - R1v1
2 GoDaddy TLS Root CA - R1                    <- cross-signed
3 Go Daddy Root Certificate Authority - G2
```

Verifying that many RSA signatures, plus revocation lookups, under emulation is the cost.

### The fix

```
reg add "HKCU\Software\Microsoft\Windows\CurrentVersion\Internet Settings" \
        /v CertificateRevocation /t REG_DWORD /d 0 /f
```

Re-measured, **default path 17194 ms → 2656 ms** (6.5x). (The `IGNORE_*` rows stay slow because passing
`WINHTTP_OPTION_SECURITY_FLAGS` explicitly overrides the policy — so it only helps code that uses the
defaults, which the game does.)

### What it changed in the game

This is the interesting part. Same run, before vs after:

| | before | after |
|---|---|---|
| `errno=10038` (`TlsConnection::Shutdown`) | 1 | **0** |
| `12157` (`SECURE_FAILURE`) | many | **0** |
| WebSocket closed with `1006` | yes, at 39 s | **never — messages at 12:09:51 *and* 12:10:38** |
| `HTTPCLIENT` errors | 39 | 17, all `12152` |
| furthest point reached | around the menu | **loading a scenario** (`MemShrink`, `Tuning Variant`, `scenario tuning variant [campaign]`) |

So the whole session-loss chain described in [part 2](#part-2-the-kill-is-downstream-of-losing-the-backend-session) — the abnormal
WebSocket close, the failed reconnect, the `SECURE_FAILURE` storm — **disappears**. The backend session
stays healthy for the life of the run.

### But the kill still fires

`si` on that healthy-session run still shows the kill signature: 61 threads, nearly every one
`suspend=1`, one spinning at 100 % CPU, log frozen at 12:10:47.

**That disproves losing the backend session as the trigger.** The correlation in
[KILL-ANALYSIS.md](KILL-ANALYSIS.md) — kill ~1 min after the socket closes — was real but not causal:
remove the socket failure entirely and Aegis kills anyway, on roughly the same schedule.

What remains is something that fires on a timer and is *not* network, *not* CPUID (patched, see
[FEX-PATCH-LIVE.md](FEX-PATCH-LIVE.md)), and *not* debugger detection (see [AEGIS.md](../../how-it-works/AEGIS.md)). The
leading candidate is now a **code-integrity check over memory that ARM64EC legitimately rewrites** —
which would explain, for the first time, why the Mac (x86-64 under Rosetta) passes and the Thor fails.
An earlier experiment already showed Aegis notices a single modified byte, so the check exists.

This fix should be kept regardless: it is a genuine Wine/TLS problem, it is one `reg add`, and it moved
the game materially further.

### Reproducing

```
x86_64-w64-mingw32-gcc -O1 -static -o timingtest.exe timingtest.c -lwinhttp
x86_64-w64-mingw32-gcc -O1 -static -o reuseprobe.exe reuseprobe.c -lwinhttp
```

Note `WinHttpConnect` is asynchronous, so a naive "connect vs send" timing split reports 0 ms for the
connect and hides the cost inside `WinHttpSendRequest`.

### Testing the waitq fix next

This repo carries a second ntdll patch that is **not currently applied** — the deployed ntdll is
`apply.py`'s `EXPECTED[False]` (syscall-register fix only), not `EXPECTED[True]` (the `--waitq`
build). That patch makes `RtlWaitOnAddress` / `RtlWakeAddress*` spinlocks safe against
`NtSuspendThread`, and [KILL-ANALYSIS.md](KILL-ANALYSIS.md) records a Wine
`RtlWaitOnAddress`/`RtlWakeAddress` spinlock deadlock in the kill. It was tested before and "did not
stop the freeze" — but that was with a broken backend session, so the combination has never been
measured.

With the session now healthy, the kill still fires, so the deadlock is a live suspect again.

**A `.wcp` cannot be installed while a session is running** (`copy` into `C:\windows\system32`
returns *Sharing violation* for a mapped ntdll), and the Contents Manager installs a content's files
when that content is **selected**, not when it is imported. So `aoe4-fixes.wcp` (committed here until 2026-10-07, now in the git history)
bundles everything into one selection:

```json
"files": [
  { "source": "libarm64ecfex.dll", "target": "${system32}/libarm64ecfex.dll" },
  { "source": "libarm64ecfex.dll", "target": "${system32}/xtajit64.dll"      },
  { "source": "libwow64fex.dll",   "target": "${system32}/libwow64fex.dll"   },
  { "source": "ntdll.dll",         "target": "${system32}/ntdll.dll"         }
]
```

A bare ntdll bundle would also revert the FEX DLLs, because GameNative restores the contents of the
selected version — hence shipping all four files together. GameNative warns that `xtajit64.dll` and
`ntdll.dll` are outside its trusted set, and installs them anyway.

**Status at the end of this session: imported, but not yet selected.** The FEXCore Version dropdown
still lists only `2610-aoe-nofex2-3`, so the install has not taken effect and the waitq fix is
**not** applied. Next step is to select `aoe4-fixes-4` and confirm the swap by hash
(`ce925da602e6abfe…` = waitq build) before drawing any conclusion from a run.

### Correction: the timing is noisy, and that changes the reading

The numbers above were single samples. Repeating the native baseline six times:

```
run 1: http=200 time=1.075s connect=0.411s tls=0.805s
run 2: http=200 time=0.814s connect=0.131s tls=0.529s
run 3: http=200 time=0.814s connect=0.131s tls=0.528s
run 4: http=200 time=0.813s connect=0.133s tls=0.531s
run 5: http=200 time=0.813s connect=0.134s tls=0.532s
run 6: http=200 time=0.797s connect=0.132s tls=0.529s
```

The server is **rock stable at ~0.8 s**, with a ~0.53 s TLS handshake. Wine, by contrast, has been
measured at **1.8 s, 2.7 s, 5.3 s, 5.7 s, 9.0 s, 15.4 s, 17.2 s, 19.4 s and 23.1 s** for the same
request — and the 2.7 s figure quoted earlier was a single favourable sample. So the honest claim is:

- **The defect is real**: Wine is consistently slower than native, never faster, and sometimes 20x+.
- **The size is not fixed.** It varies wildly run to run, which is exactly what you'd expect if the
  cost is emulated crypto competing for CPU with the game's own emulation, rather than a fixed
  per-connection penalty.
- **`CertificateRevocation=0` still helps** (it removes a whole verification step) and the behavioural
  effect was real — the session-loss chain vanished in that run. But it is a mitigation, not a cure,
  and later runs showed `errno=10038`/`12157` returning occasionally.
- The setting **does persist** — re-queried and still `CertificateRevocation = 0x0`.

The practical consequence: the game's `12152` errors are best explained as its HTTP client timing out
on a handshake whose cost is unpredictable, which is consistent with them appearing and disappearing
across runs. Making that cost small and *stable* is the actual goal; trimming revocation was only part
of it.
