# Four fixes applied, the kill survives — and two theories are now dead

> **Archived.** Superseded state of the kill question before the SMC work (2026-10-06). The "deployed ntdll" line is void: Wine never loaded that ntdll ([NTDLL-NEVER-LOADED.md](NTDLL-NEVER-LOADED.md)). The story: [STORY.md](../../STORY.md).

The state at the end of this session, recorded honestly. Everything below was verified by hash or by
measurement, not assumed.

## What is actually installed and working

| fix | how verified | effect on the kill |
|---|---|---|
| FEX CPUID `0x40000000` vendor neutralised | `xtajit64.dll` and `libarm64ecfex.dll` both contain `000080d2010080d2` at `0x28644` | **none** |
| ntdll `RtlWaitOnAddress`/`RtlWakeAddress` spinlock fix (`apply.py --waitq`) | deployed `ntdll.dll` sha256 = `ce925da602e6abfe…` = `EXPECTED[True]` | **none** |
| TLS: `CertificateRevocation = 0` | 17.2 s → 2.7 s on the default path; the session-loss chain disappears (see [`NETWORK.md`, part 3](NETWORK.md#part-3-wines-tls-to-the-age-backend-costs-519-s-and-certificaterevocation0-fixes-it)) | **none, but the game gets further** |
| — | — | kill still fires at ~4 min, 60 threads, nearly all `suspend=1` |

## Theory killed: "Aegis checksums memory that ARM64EC rewrites"

This was the leading candidate, because it was the first thing that would explain the Mac/Thor split.
`tools/research/memwatch.c` hashes the whole 147 MB image in 64 KiB blocks every 15 s and reports changed
blocks:

```
[   0s]   0 changed  (baseline)
[  15s]  57 changed      <- Aegis's own restore + startup
[ 112s]  33 changed
[ 128s]   0 changed  (stable)
   ...    every sample stable through the kill at ~240 s
[ 412s]   0 changed  (stable)
```

**The image is byte-stable for the ~2 minutes leading up to the kill.** Nothing is being modified, so
nothing is failing a periodic hash *because it changed*. (This does not exclude a static mismatch —
decrypted bytes that are stable but wrong — only the "something rewrites memory" mechanism. Comparing
the restored bytes against a known-good Mac restore would settle that; there is no Mac capture.)

## Theory killed: "Wine's HTTP can't handle the game's requests"

`tools/research/postprobe.c` reproduces libHttpClient's style of call against the game's own backend host:

```
GET  /wss/                             ok http=405   9672 ms
POST /game/login/readSession  (json)   ok http=401   6562 ms
POST /game/login/readSession  (no Expect) ok http=401 7422 ms
POST /game/Challenge/getChallenges     ok http=400   3171 ms
POST /game/account/getProfileProperty  ok http=400   1634 ms
```

Transport-level all fine — correct HTTP statuses, no `12152`. So Wine can send what the game sends.
What is *not* fine is the timing: 1.6–9.7 s per request where the native client takes 0.2–0.9 s.

## What is left

The `12152` errors persist in the game (22 in one run) while the game's own transport works in
isolation. The most economical explanation is a **client-side timeout on a handshake that is still
several times slower than native** — the game gives up, the retry storm degrades the session, the
WebSocket eventually closes with `1006`, and the kill follows.

So the next lever is the remaining TLS cost, not the protocol:

1. The chain is four certificates deep (leaf → GoDaddy DV R1v1 → GoDaddy TLS Root CA R1 → GoDaddy
   Root G2). Pre-installing the intermediates in the prefix's CA store should remove any AIA fetching
   Wine does, and is worth measuring with `timingtest.exe`.
2. `timingtest.exe` shows the delay is *not* purely revocation: with `IGNORE_ALL` it was still 12–14 s,
   so most of it is RSA signature verification of that chain under emulation.
3. If the cost is irreducible, the remaining option is to stop the game from making those calls at all
   — the `RL_USE_WEBSOCKET_PERCENT` / `RL_KEEPALIVE` / `RL_MISSPOLL` tunables found in
   `RelicCardinal.exe` are server-supplied, but a local override may exist.

One correction worth repeating: the "kill ~1 minute after the socket closes" correlation is real and
was reproduced again here (the log's last lines are the `1006` close and the reconnect), but last
session proved it is not causal — a run with a healthy session for its whole life still died on the
same schedule.
