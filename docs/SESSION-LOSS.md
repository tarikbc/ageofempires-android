# The kill is downstream of losing the backend session

This is the first end-to-end causal chain that fits every observation, including why the Mac lives
and the Thor dies. The game's own log tells the whole story with timestamps.

## The sequence, from the game's log

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

### The error code changes, which is the tell

| phase | `dwError` | meaning |
|---|---|---|
| before the TLS failure | `12152` | `ERROR_WINHTTP_INVALID_SERVER_RESPONSE` — request/response content |
| **after** the TLS failure | `12157` | `ERROR_WINHTTP_SECURE_FAILURE` — **TLS cannot establish at all** |

The connection *worked* — it authenticated, sent a session token, and received a `PresenceMessage`.
Then the socket went invalid and **every later TLS connection failed**. The game never talks to its
backend again, and Aegis kills the process about a minute after the socket closes — the timing already
recorded in [KILL-ANALYSIS.md](KILL-ANALYSIS.md).

So the kill is **downstream of losing the backend session**, not a spontaneous timer.

## Why `errno=10038` points at Wine

`10038` is `WSAENOTSOCK` — "socket operation on non-socket". The socket handle was **invalid** at a
point where the game believed it was live. That is not what a server-side rejection looks like; a
server that wanted to drop the session would close the TCP connection cleanly, giving a normal
WebSocket close, not a handle that stopped being a socket.

And this repository already documents a Wine defect with exactly this signature — from
[`patches/proton-arm64ec-ntdll/apply.py`](../patches/proton-arm64ec-ntdll/apply.py):

> Patch 1 (always): `invoke_arm64ec_syscall.s` replaces the x64 stub that runs a direct `syscall` from
> emulated x64 code. **Wine's stub clobbered rdx/r8/r9/r10/rflags; the Windows kernel keeps them.**

A syscall stub that clobbers argument registers corrupts syscall parameters. A corrupted handle
passed to a socket call is `WSAENOTSOCK`. That is a mechanism, not a coincidence — and it matches the
error-code *transition* above: the first failure is one corrupted call, and from then on the socket
table state is wrong for that thread.

**Important:** that patch was written for `proton-11.0-1-arm64ec`. The device now runs
`proton-11.0-99-arm64ec-1`, so its offsets do not apply — the patch is *not* currently in effect, and
the README's device-state notes list ntdll as original.

## Why this explains the Mac/Thor split

The Mac runs a different Wine build (x86-64 under Rosetta) and never had this ARM64EC syscall stub.
Same game files, same servers, no register clobbering, no socket corruption, no kill.

## Next steps

1. Verify the defect still exists in the current build: locate `invoke_arm64ec_syscall` in
   `proton-11.0-99-arm64ec-1`'s `aarch64-windows/ntdll.dll` and check whether the x64 stub still
   clobbers `rdx/r8/r9/r10/rflags`.
2. If it does, port `invoke_arm64ec_syscall.s` to that build (the apply script already refuses files
   whose hash it does not know, so it needs new expected hashes and offsets).
3. Re-run the game and watch for `errno=10038` / status `1006` disappearing. **The success criterion is
   the log still growing past ~5 minutes**, not the process merely existing.

Related: [`BACKEND-SESSION.md`](BACKEND-SESSION.md) measured the network path itself (DNS/TCP/TLS all
work from the session; the AoE host is ~40x slower than Microsoft's).
