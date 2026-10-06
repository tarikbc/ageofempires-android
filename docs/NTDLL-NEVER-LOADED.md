# The ntdll patches were never loaded

This is the most important finding of the investigation so far, and it invalidates conclusions drawn
across multiple earlier sessions.

## The measurement

`tools/ntdllcheck.c` reads the patch sites out of its **own mapped** `ntdll.dll` and compares them with
the file on disk:

```
=== mapped ntdll build ===
base=0000006FFF9A0000

rva 0xEC050 (invoke patch site): 0x4c  -> PRISTINE
   bytes: 4c 89 54 24 08
rva 0xCDA74 (waitq site stub_U1): 0x885ffd1f  -> PRISTINE
rva 0xCDA00 (waitq site stub_L1): 0x885ffd0c  -> PRISTINE

system32 file @0xEC050: e9 ab 00 00 00     <- the FILE is patched (0xE9 = jmp)
```

**The file is patched. The mapped image is pristine.** So the patches in
`patches/proton-arm64ec-ntdll/apply.py` are not in effect, no matter how carefully their output was
installed into `C:\windows\system32`.

## Why

Wine does not load `C:\windows\system32\ntdll.dll`. logcat shows the loader opening its own copy:

```
avc: granted { execute } for
  path="/data/user/0/app.gamenative/files/imagefs_shared/proton/
        proton-11.0-99-arm64ec/lib/wine/aarch64-windows/ntdll.dll"
```

That tree's `ntdll.dll` hashes to `606d0a2fb197d37b…` — `apply.py`'s `PRISTINE`. The prefix's
`system32` copy is a **separate file** (it is not a symlink: writing the patch there did not change the
tree's copy, and the two hashes differ).

So:
- **`invoke_arm64ec_syscall` has never run.** Wine's x64 syscall stub still clobbers
  `rdx/r8/r9/r10/rflags`, which is exactly the defect that patch was written to fix.
- **The `--waitq` spinlock fix has never run either.** The earlier note that it "did not stop the AoE IV
  freeze" therefore means nothing — it was never exercised.

## Why this matters

The clobbering stub corrupts syscall arguments. A corrupted socket handle is `WSAENOTSOCK` — and the
game's log says `TlsConnection::Shutdown … errno=10038`, i.e. exactly that, followed by the WebSocket
closing `1006` and every later request failing `12157`. That chain was traced in
[SESSION-LOSS.md](SESSION-LOSS.md) and is the best-supported cause of the session degradation. **The fix
for it exists and has simply never been loaded.**

It also means several "applied and verified" claims in this repo were verified *against the wrong file*.
Verifying a patch needs `ntdllcheck.exe`-style evidence from a **mapped** image, not a hash of a file
on disk.

## Getting the fix in

The previous session built a custom Proton containing the fix and it is still installed:

```
proton-11.0-1-arm64ec-aoefix-1
  description: "Proton 11.0-1 arm64ec + ntdll fix: direct x64 syscalls keep
                rdx/r8/r9/r10/rflags like Windows (AoE IV Aegis)"
```

Selecting it hangs: GameNative stalls at `Uploading configuration_user` at 0% CPU, and after a forced
restart the game reports "Does Not Open". The container was reverted to `proton-11.0-99-arm64ec-1`.

Two ways forward, in order of preference:

1. **Repack 11.0-99 with the patched ntdll** — the supported route. The `.wcp` is `type: "Proton"` with
   `files: []` and a `wine` block (`binPath`, `libPath`, `prefixPack`), so it carries a whole tree
   (365 MB); the current tree's `ntdll.dll` would be replaced by the `invoke` + `--waitq` build, then
   imported through the Wine/Proton Manager.
2. **Write the tree's `ntdll.dll` directly** while no session holds it. It lives under
   `imagefs_shared/proton/...`, which is app-private, so this needs a write path that does not go
   through Wine itself.

Either way, confirm success with `ntdllcheck.exe` **reading the mapped image** — `rva 0xEC050 == 0xE9`
and the `waitq` sites branching — before drawing any conclusion from a game run.

## Also worth knowing

`C:\windows\system32` in this setup is not the source of Wine's builtin DLLs, so any future patch aimed
at Wine internals must target the Proton tree. The FEX patch is unaffected: `xtajit64.dll` *is* loaded
from `system32` and that one is genuinely live, which is why the CPUID change was measurable.

## Why it was never loaded — and what happened when I tried

The local Proton bundle **does contain the fix**:

```
proton-11.0-99-arm64ec.wcp  ->  lib/wine/aarch64-windows/ntdll.dll
    sha256 5325f69ecbce31f3…  = apply.py's EXPECTED[False]   (invoke patch present)
    rva 0xEC050 = 0xE9                                       (jmp to cave)
  profile.json description: "Proton 11.0-1 arm64ec + ntdll fix: direct x64 syscalls
                             keep rdx/r8/r9/r10/rflags like Windows (AoE IV Aegis)"
```

But the **deployed** tree's copy hashes to `606d0a2fb197d37b…` (`PRISTINE`). So the running tree did
**not** come from that local bundle — it came from GameNative's **online** version
(`Available online versions:` in the Wine/Proton Manager), which ships a stock ntdll.

**Conclusion: the ntdll fix was never deployed, so it was never tested.** The earlier note that
`--waitq` "did not stop the AoE IV freeze" is void — it never ran. And `invoke_arm64ec_syscall` has
never run either, which means Wine's x64 syscall stub is still clobbering `rdx/r8/r9/r10/rflags` — the
leading explanation for `errno=10038` (`WSAENOTSOCK`) and the HTTP failures.

## Deploying it: the manager's rules, and a negative result

The Wine/Proton Manager states its requirements plainly:

> "Filename must begin with 'wine' or 'proton' (case-insensitive). **Packages must include bin/, lib/,
> and prefixPack.txz.** All imports are bionic-compatible only."

So a minimal bundle carrying just `lib/wine/aarch64-windows/ntdll.dll` is rejected — a full repack is
required. Built and imported one:

```
proton-11.0-99-ntdlfix.wcp   267 MB, 2233 entries (same count as the original)
  versionName 11.0-99-arm64ec-ntdlfix, versionCode 3
  ntdll sha256 ce925da602e6abfe…  = EXPECTED[True]   (invoke AND waitq)
  rva 0xEC050 = 0xE9,  rva 0xCDA74 = 0x17ff9c0b (branch)
```

The import succeeded — *"Proton proton-11.0-99-arm64ec-ntdlfix installed successfully"* — and it appears
in the Wine Version dropdown as `proton-11.0-99-arm64ec-ntdlfix-3`.

**But selecting it breaks the Wine session.** The game does not open: no `wineserver`, no game process,
no `warnings.log`, and no probe output at all — the session never started. GameNative shows its
"Does Not Open" feedback dialog. The container was reverted to `proton-11.0-99-arm64ec-1`.

Note this is the *second* time an ntdll-patched Proton has failed to start here: the previous
session's `proton-11.0-1-arm64ec-aoefix-1` hangs GameNative at `Uploading configuration_user`.

## Next

Isolate which patch breaks it. `EXPECTED[False]` (invoke only — the previous session's exact build) is
available as `lib/wine/aarch64-windows/ntdll.dll` inside `proton-11.0-1-arm64ec-aoefix.wcp`, and can be
packed the same way. If invoke-only starts and both-patches does not, the `--waitq` cave is at fault; if
neither starts, the `invoke` trampoline is, and the fix has to be re-derived rather than deployed.

Either way, `ntdllcheck.exe` decides it — the mapped image, not a disk hash.

## Round 8: the failure is isolated to the patched ntdll

Two things were cleared up.

**1. The prefix is healthy.** The earlier `aoefix` attempt hung mid prefix-migration, which could have
poisoned the container and confounded the repack test. It did not: selecting the stock
`proton-11.0-99-arm64ec-1` launches the game normally (confirmed with a live `wineserver` and a
`RelicCardinal.exe` pid). So the failure of `proton-11.0-99-ntdlfix-3` was caused by the patched
ntdll, not by a damaged prefix.

**2. The baseline is re-confirmed.** With the stock Proton, `ntdllcheck.exe` on its own mapped image:

```
rva 0xEC050 (invoke patch site): 0x4c          -> PRISTINE
rva 0xCDA74 (waitq site):        0x885ffd1f    -> PRISTINE
rva 0xCDA00 (waitq site):        0x885ffd0c    -> PRISTINE
system32 file @0xEC050:           e9 ab 00 00 00   <- patched file, ignored by the loader
```

**3. A hypothesis of mine that turned out wrong**, recorded so it is not re-tried: I suspected
`apply.py` placed the invoke code cave outside the mapped section (cave at file offset `0xEC100`,
`VirtualSize` set to `0xDC200`). It does not:

```
.text   VA=0x10000  VSize=0xdc0b5  RawSize=0xe0000  Raw=0x10000
        applies patch VSize=0xdc200  -> maps VA 0x10000..0xec200
        invoke cave VA 0xec100       -> inside; raw bytes to 0xf0000 also cover it
        waitq cave rva 0xb491c       -> inside
```

The cave is mapped and file-backed, so the patch's layout is sound and the failure lies in the
generated code or in deploying it, not in the section headers.

## Status

| build | ntdll | result |
|---|---|---|
| `proton-11.0-99-arm64ec-1` | pristine | launches normally |
| `proton-11.0-99-ntdlfix-3` | invoke + waitq | **session never starts** (no `wineserver`, no log) |
| `proton-11.0-99-invokeonly-4` | invoke only | built and pushed — **not yet imported** |

Next is the third row: importing the invoke-only bundle isolates whether the `--waitq` cave or the
`invoke` trampoline is responsible. If invoke-only starts, the waitq cave is at fault; if neither
starts, the `invoke` trampoline itself needs re-deriving.
