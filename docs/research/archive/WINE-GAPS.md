# Concrete gaps between Wine and Windows that Aegis could notice

Both were measured from inside the game's own session, and both are things a "stealth" anti-tamper
plausibly depends on.

## 1. `NtSetInformationThread(ThreadHideFromDebugger)` is not implemented

`tools/research/antidebug2.c`, in the live session:

```
--- thread hiding ---
  NtSetInformationThread(ThreadHideFromDebugger) 0xc0000002     <- STATUS_NOT_IMPLEMENTED
     (Windows: 0x00000000 STATUS_SUCCESS)
     TEB byte@0x1fb = 0x00                                      <- flag never set
```

Windows returns `STATUS_SUCCESS` and marks the thread so debuggers cannot see it. Wine returns
`STATUS_NOT_IMPLEMENTED` and the flag is never set.

**Why this is worth attention:** Aegis's own build log says `Pre-Main Stealth Startup: Enabled` and
`** Installed Stealth-Startup`
([AEGIS.md](../../how-it-works/AEGIS.md)). Hiding its threads from debuggers is exactly what that feature would do, and a
protection that hides its watchdog thread and then finds the hiding silently failed has a good reason
to treat the environment as hostile. It is also the kind of check that would fire on a *timer* rather
than immediately, which matches the kill's behaviour.

**Not yet proven that Aegis calls it.** The shipped import table names `IsDebuggerPresent` and
`OutputDebugStringA/W` but none of the Native API anti-debug functions, and `ThreadHideFromDebugger` is
a compile-time constant rather than a string, so a string search cannot settle it. Aegis's code is
encrypted, so its own imports are not visible either. The way to settle it is to implement the class in
Wine and see whether the kill changes.

### Where the fix has to go

`NtSetInformationThread` in the PE `ntdll.dll` is nothing but a syscall stub, so the class handling is
**not** in a file this repo has patched before:

```
NtSetInformationThread  rva=0x659a0  va=0x1800659a0
  0x1800659a0: a80180d2   mov  x8, #0xd          <- syscall 13
  0x1800659a4: e9031eaa   mov  x9, x30
  0x1800659a8: 90000058   ldr  x16, #0x1800659b8
  0x1800659ac: 100240f9   ldr  x16, [x16]
  0x1800659b0: 00023fd6   blr  x16
  0x1800659b4: c0035fd6   ret
```

The implementation is on the **Unix side** — `lib/wine/aarch64-unix/ntdll.so` in the Proton tree, an
ELF shared object inside the imagefs. `patches/experiments/proton-arm64ec-ntdll/apply.py` patches the PE DLL and
will not help here; this needs an ELF/ARM64 patch to `ntdll.so`, or a rebuilt Wine.

That is a genuine increase in cost, so it is worth confirming Aegis even calls the class before paying
it. The cheapest confirmation is a Wine debug channel: if the game is run with `WINEDEBUG=+thread` (or
whichever channel logs the unimplemented class) and the log shows the call, the hypothesis is settled
without touching binary code. `WINEDEBUG` is currently set by the launcher, not by
`HKCU\Environment` or the container's Environment tab, so that has to be established first.

### Everything else on that surface is correct

```
NtQuerySystemInformation(SystemKernelDebuggerInformation)  0x0  KdDebuggerEnabled=0 NotPresent=1  (correct)
SharedUserData@0x7ffe0000 KdDebuggerEnabled=0x00                                              (correct)
OutputDebugString + GetLastError = 0xdeadbeef                                                 (correct)
NtQueryObject(ObjectTypesInformation) size probe 0xc0000004                                   (correct)
```

So this gap stands out rather than being one of many.

## 2. The AoE backend is slow through Wine, and it is not revocation

Correcting the earlier claim: the native baseline used `curl`, which does **no** revocation checking,
so part of the "20x slower" gap was an unfair comparison. Measuring properly, repeated:

| run | default | IGNORE_REVOCATION | IGNORE_ALL | microsoft.com |
|---|---|---|---|---|
| 1 | 17194 ms | 8998 ms | 14321 ms | 212 ms |
| 2 | 2656 ms | 16710 ms | 12010 ms | 217 ms |
| 3 | 17151 ms | **18331 ms** | 8663 ms | 189 ms |

`IGNORE_REVOCATION` is slow in run 3, so **revocation is not the cause**. Neither is the chain:
`www.microsoft.com` has an equivalent shape and answers in ~0.2 s every time.

```
AoE chain:          leaf RSA-2048 → RSA-4096 → RSA-4096 → root RSA-2048   (4 certs)
microsoft.com:      leaf RSA-2048 → RSA-4096 → RSA-4096                   (3 certs)
```

The consistent facts are: **this host is always slow through Wine (2.6–23 s) and `microsoft.com` is
always fast (0.19–0.22 s), in the same session, with similar chains.** The most likely remaining
explanation is that repeat contacts to `microsoft.com` resume a cached TLS session while each AoE
request pays full handshake crypto on an emulated CPU — which would also explain the variance, since
that cost competes with the game for CPU.

`CertificateRevocation=0` in `HKCU\...\Internet Settings` persists and may still help WinINET callers,
but it clearly does **not** control WinHTTP's chain building, so it is not the lever it looked like.

## What would settle each

1. Implement `ThreadHideFromDebugger` in Wine's ntdll (set the flag, return `STATUS_SUCCESS`) and see
   whether the kill changes. This is the cheapest remaining test with a real hypothesis behind it, and
   the same binary-patch technique already in `patches/experiments/proton-arm64ec-ntdll/` applies.
2. Measure a genuinely cold handshake to a control host with a 4-cert RSA-4096 chain, to confirm the
   session-resumption explanation for the Wine/native gap.
