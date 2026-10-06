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

## Why this explains the Mac/Thor split

The Mac runs a different Wine build (x86-64 under Rosetta) and never had this ARM64EC syscall stub.
Same game files, same servers, no register clobbering, no socket corruption, no kill.

## Next steps

1. Reproduce the TLS breakdown outside the game: hammer HTTPS and then a long-lived WebSocket to
   `dr-activerelease1-api.worldsedgelink.com` from a probe, and see whether TLS stops working after a
   connection is torn down. If a simple probe reproduces `12157`, the culprit is Wine, not the game.
2. Establish which schannel/secur32 DLLs are in use and whether an override changes it.
3. Re-run the game and watch for `errno=10038` / status `1006` disappearing. **The success criterion is
   the log still growing past ~5 minutes**, not the process merely existing.

Related: [`BACKEND-SESSION.md`](BACKEND-SESSION.md) measured the network path itself (DNS/TCP/TLS all
work from the session; the AoE host is ~40x slower than Microsoft's).
