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

## Testing the waitq fix next

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
when that content is **selected**, not when it is imported. So `aoe4-fixes.wcp` (committed here)
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

## Correction: the timing is noisy, and that changes the reading

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
