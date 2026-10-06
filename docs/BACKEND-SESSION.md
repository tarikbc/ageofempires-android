# The game's backend session is broken

Aegis's kill lands ~1 minute *after* `TlsConnection::Shutdown … errno=10038`
([KILL-ANALYSIS.md](KILL-ANALYSIS.md)). The network is therefore the most concrete unexplained
thing, and it has now been measured from several directions. The short version: **the network and
Wine's own networking are fine; the game's specific requests are not.**

## The network path itself is fine

`tools/netprobe.c`, in the live session, DNS up to TLS:

```
DNS  www.ageofempires.com         rc=0 (81 ms)  -> 150.171.110.36
TCP  www.ageofempires.com         rc=0 err=0 (20 ms)
HTTPS www.ageofempires.com       OK status=200 (9361 ms)
HTTPS www.microsoft.com          OK status=200 (227 ms)
```

The AoE host is ~40x slower than Microsoft's (9.4 s vs 0.23 s), but it works. (An earlier unbounded
attempt appeared to hang, which is why the probe sets `WinHttpSetTimeouts`.)

## Wine's WebSocket layer is fine

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
| Native Python (`tools/ws_native.py`, `tools/ws_paths.py`) | `/wss/` | `101`, survived 45 s |
| `tools/wsprobe.c` inside Wine | `/wss/` | `101`, **still alive at 165 s** |
| **the game** | `/wss/` | `101`, **died at 39 s** |

Path check (native): `/wss/` → 101, `/wss/v1/` → 101, `/wss` → 302, `/game/wss/` → 404, `/` → 200.
An unauthenticated upgrade to `/` is refused with `200`, so only `/wss/`-style paths are real.

## Wine's winhttp is fine

`tools/apiprobe.c` against the real backend:

```
leaderboards title=age4   ok http=200 bytes=399
leaderboards title=age2   ok http=200 bytes=399
game host /wss/           ok http=405 (GET not allowed)
```

`getAvailableLeaderboards?title=age4` returns the correct JSON through Wine, and 33 KB natively. So
the same backend serves AoE IV exactly as the [aoe2-apis](https://github.com/ustacode/aoe2-apis)
catalogue describes for AoE2 — swapping `title` is the whole trick.

## So what is actually broken

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
