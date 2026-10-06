# The game's backend session is broken (new lead)

Aegis's kill lands ~1 minute *after* `TlsConnection::Shutdown … errno=10038`
([KILL-ANALYSIS.md](KILL-ANALYSIS.md)). That correlation was always suggestive, and the network is now
the most concrete unexplained thing left, so it got measured directly.

## The network path itself is fine

`tools/netprobe.c`, run inside the live session, from DNS up to TLS:

```
WSAStartup rc=0

DNS  www.ageofempires.com         rc=0 (81 ms)  -> 150.171.110.36
DNS  api.ageofempires.com         rc=0 (32 ms)  -> 52.238.248.160
DNS  www.microsoft.com            rc=0 (0 ms)  -> 2.20.130.2

TCP  www.ageofempires.com         rc=0 err=0 (20 ms)
TCP  www.microsoft.com            rc=0 err=0 (24 ms)

HTTPS www.ageofempires.com       OK status=200 (9361 ms)
HTTPS www.microsoft.com          OK status=200 (227 ms)
```

Everything works. Note the last two lines though: the Age of Empires host answers in **9.4 s** where
Microsoft's answers in **0.23 s** — ~40× slower. (The first attempt with no timeouts appeared to hang
outright, which is why `netprobe` sets `WinHttpSetTimeouts`.)

## But the game's own requests fail

The game's log carries 39 HTTP failures, and the two error codes are specific:

| count | `dwError` | meaning |
|---|---|---|
| 1 | `12157` | `ERROR_WINHTTP_SECURE_FAILURE` |
| many | `12152` | `ERROR_WINHTTP_INVALID_SERVER_RESPONSE` |

`12152` dominates — Wine's winhttp is receiving responses it cannot parse. This is **not** a dead
network: a plain `WinHttpSendRequest` to the same host from the same session returns `200`. So
something about the game's requests — HTTP version, transfer encoding, redirect handling, or a TLS
feature Wine's winhttp lacks — breaks them.

The log only exposes request IDs (`[ID 2]`, `[ID 3]`, …), not URLs, so the failing endpoints are not
yet known. The single URL that does appear, `https://www.catcert.net/verarrel`, comes from a
certificate-chain diagnostic, not the game's own traffic.

## Why this could be the trigger

- Aegis is a *server-aware* protection, and the kill fires ~1 min after the game's socket closes.
- The Mac runs the same game files without the freeze — and would plausibly have a working winhttp.
- The freeze happens **offline too**, which fits "no working backend session → kill" rather than
  contradicting it.

That is a hypothesis, not a result. It is also the first lead that explains the Mac/Thor difference
with a mechanism other than CPU emulation.

## Next experiments

1. Find the failing URLs — the request IDs need mapping to hosts. Options: run the game with
   `WINEDEBUG=+winhttp`, or capture traffic, or look at `libHttpClient` callers.
2. Determine whether the breakage is HTTP/2, chunked encoding, or TLS-related. A `winhttp` override or
   a Wine-side fix would follow from which one it is.
3. Re-test offline-vs-online *with the FEX patch live*, since that combination has not been measured.

`tools/netprobe.c` is the instrument; it builds with
`x86_64-w64-mingw32-gcc -O1 -static -o netprobe.exe netprobe.c -lwinhttp -lws2_32`.
(Note: `wininet.h` and `winhttp.h` collide in mingw 14 — including both fails to compile, so the probe
uses WinHTTP only.)
