# Wine's TLS to the Age backend costs 5–19 s, and `CertificateRevocation=0` fixes it

A real, measured Wine-side defect, with a fix that demonstrably changes the game's behaviour — even
though it does not, on its own, stop the kill.

## The measurement

`tools/reuseprobe.c`, one connection handle, real AoE IV backend endpoint, inside the session:

```
request #1  ok http=200  15448ms
request #2  ok http=200  19436ms   (after 20s idle)
request #3  ok http=200   5293ms   (after 45s idle)
request #4  ok http=200   5666ms   (after 75s idle)
```

The **same** request from the Mac takes **0.94 s**. Control in the same Wine session:
`www.microsoft.com` → **212 ms**. So it is not "Wine is slow", and not the network — it is this host's
certificate chain.

`tools/timingtest.c` isolates it:

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

## The fix

```
reg add "HKCU\Software\Microsoft\Windows\CurrentVersion\Internet Settings" \
        /v CertificateRevocation /t REG_DWORD /d 0 /f
```

Re-measured, **default path 17194 ms → 2656 ms** (6.5x). (The `IGNORE_*` rows stay slow because passing
`WINHTTP_OPTION_SECURITY_FLAGS` explicitly overrides the policy — so it only helps code that uses the
defaults, which the game does.)

## What it changed in the game

This is the interesting part. Same run, before vs after:

| | before | after |
|---|---|---|
| `errno=10038` (`TlsConnection::Shutdown`) | 1 | **0** |
| `12157` (`SECURE_FAILURE`) | many | **0** |
| WebSocket closed with `1006` | yes, at 39 s | **never — messages at 12:09:51 *and* 12:10:38** |
| `HTTPCLIENT` errors | 39 | 17, all `12152` |
| furthest point reached | around the menu | **loading a scenario** (`MemShrink`, `Tuning Variant`, `scenario tuning variant [campaign]`) |

So the whole session-loss chain described in [SESSION-LOSS.md](SESSION-LOSS.md) — the abnormal
WebSocket close, the failed reconnect, the `SECURE_FAILURE` storm — **disappears**. The backend session
stays healthy for the life of the run.

## But the kill still fires

`si` on that healthy-session run still shows the kill signature: 61 threads, nearly every one
`suspend=1`, one spinning at 100 % CPU, log frozen at 12:10:47.

**That disproves losing the backend session as the trigger.** The correlation in
[KILL-ANALYSIS.md](KILL-ANALYSIS.md) — kill ~1 min after the socket closes — was real but not causal:
remove the socket failure entirely and Aegis kills anyway, on roughly the same schedule.

What remains is something that fires on a timer and is *not* network, *not* CPUID (patched, see
[FEX-PATCH-LIVE.md](FEX-PATCH-LIVE.md)), and *not* debugger detection (see [AEGIS.md](AEGIS.md)). The
leading candidate is now a **code-integrity check over memory that ARM64EC legitimately rewrites** —
which would explain, for the first time, why the Mac (x86-64 under Rosetta) passes and the Thor fails.
An earlier experiment already showed Aegis notices a single modified byte, so the check exists.

This fix should be kept regardless: it is a genuine Wine/TLS problem, it is one `reg add`, and it moved
the game materially further.

## Reproducing

```
x86_64-w64-mingw32-gcc -O1 -static -o timingtest.exe timingtest.c -lwinhttp
x86_64-w64-mingw32-gcc -O1 -static -o reuseprobe.exe reuseprobe.c -lwinhttp
```

Note `WinHttpConnect` is asynchronous, so a naive "connect vs send" timing split reports 0 ms for the
connect and hides the cost inside `WinHttpSendRequest`.
