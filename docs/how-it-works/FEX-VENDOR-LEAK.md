# FEX leaks its identity to the guest: CPUID leaf 0x40000000

The environment hands the game an unambiguous "you are running under FEX" signature, and FEX's
`HideHypervisorBit` option does **not** hide it.

## Measurement

A probe run **inside the game's Wine session, with the game's own FEX config applied**
(`Z:\home\xuser\.fex-emu\AppConfig\RelicCardinal.exe.json` = `{"Config":{"HideHypervisorBit":"1"}}`)
reports:

```
=== CPUID (what the game's own checks would see) ===
  vendor='GenuineIntel' maxleaf=0x16
  leaf1 eax=0x000a0661 (family 6 model 166 stepping 1)
  leaf1 ecx=0x3ed8330f edx=0x278bfbff  hypervisor_bit=0
  hypervisor leaf 0x40000000 eax=0x40000001 vendor='FEXIFEXIEMU'
  leaf7 ebx=0x218827e9
```

Read that carefully:

- `hypervisor_bit=0` — `HideHypervisorBit` **works**: it clears leaf 1 `ECX[31]`.
- `hypervisor leaf 0x40000000 ... vendor='FEXIFEXIEMU'` — **leaf `0x40000000` still answers**, and the
  vendor string spells **FEX**.

So the one option people reach for removes the single *bit*, while leaving a far louder *name plate*
readable by any guest instruction sequence.

## Where it comes from

FEX hardcodes it — [`FEXCore/Source/Interface/Core/CPUID.cpp`](https://github.com/FEX-Emu/FEX),
`CPUIDEmu::Function_4000_0000h`:

```cpp
// Hypervisor CPUID information leaf
FEXCore::CPUID::FunctionResults CPUIDEmu::Function_4000_0000h(uint32_t Leaf) const {
  FEXCore::CPUID::FunctionResults Res {};
  ...
  Res.eax = 0x40000001;

  // EBX, EDX, ECX become the hypervisor ID signature
  constexpr static char HypervisorID[12] = "FEXIFEXIEMU";
  memcpy(&Res.ebx, HypervisorID, sizeof(HypervisorID));
  return Res;
}
```

FEX follows VMWare's "Hypervisor CPUID Interface proposal" here, so the leaf is deliberate — but it
also means every guest can name the emulator in one instruction.

## No config option covers it

Checked all CPUID/hypervisor-related FEX options present in the shipped DLL:

`FEX_HIDEHYBRID`, `FEX_HIDEHYPERVISORBIT`, `FEX_SMALLTSCSCALE`, `FEX_SMCCHECKS`, `FEX_MULTIBLOCK`,
`FEX_TSOENABLED`, `FEX_VECTORTSOENABLED`, `FEX_HALFBARRIERTSOENABLED`, `FEX_MEMCPYSETTSOENABLED`,
`FEX_THUNKCONFIG`, `FEX_THUNKGUESTLIBS`, `FEX_THUNKHOSTLIBS`, `FEX_CPUFEATUREREGISTERS`,
`FEX_HOSTFEATURES`.

- `HIDEHYPERVISORBIT` → only `Function_01h`'s `Hypervisor` bit.
- `CPUFEATUREREGISTERS` → overrides **ARM64** feature registers (`isar0`, `midr`, `pfr0`, …), not x86
  CPUID leaves.
- `HOSTFEATURES` → enable/disable individual x86 *features* (`AVX`, `SVE`, `RNG`, …), not the vendor
  string.

`Function_4000_0000h` consults no config at all — it is unconditional.

## Why this matters here

- On the Mac, the same game runs under **Rosetta**, which is not an x86 hypervisor and does not present
  an x86 hypervisor vendor leaf. On the Thor, guest code asking the same question gets
  `FEXIFEXIEMU`.
- Aegis is an active anti-tamper (see [AEGIS.md](AEGIS.md)) whose whole job is to notice that its
  environment is not the one it shipped for. An unmasked, self-identifying emulator signature is
  exactly the class of signal such a system acts on — and it is a *clean* difference between the two
  environments where the behaviour differs.
- **This is still a hypothesis, not a proven cause.** Proving it requires changing the answer and
  re-running the game.

## The fix

Patch `Function_4000_0000h` to stop advertising FEX, then rebuild FEX and install it:

- return an all-zero vendor, or
- return a benign vendor (e.g. `"Microsoft Hv"`), or
- drop the leaf entirely (`Res.eax = 0`, which tells the guest "no hypervisor leaf here").

This repo's README already documents the build recipe used before (llvm-mingw 20260922 + CMake,
`toolchain_mingw.cmake`, `MINGW_TRIPLE=arm64ec-w64-mingw32`, packaged as a `.wcp` for the Contents
Manager), so the toolchain path is known.

Note that a **hex patch of the shipped DLL is not straightforward**: `FEXIFEXIEMU` is *not* present as
a searchable literal anywhere in `xtajit64.dll` / `libarm64ecfex.dll` / `libwow64fex.dll` (the ARM64
code materialises it from instruction immediates). A source build is the clean route.

## The patch — built and verified working

The leaf is implemented in a small ARM64 function that materialises the string from `movz`/`movk`
immediates, so there is no literal to hex-edit. It was located by disassembling the `.text` of
`libarm64ecfex.dll` as ARM64 (capstone) and searching decoded instructions for the immediates
`#0x4546`/`#0x4958` (="FEXI"), `#0x4d45`/`#0x55` (="EMU\0") next to `mov w9, #0x40000000`:

```
file 0x28644: mov  x0, #1                  ; eax = 0x40000001
file 0x28648: mov  x1, #0x4546             ; 'FE'
file 0x2864c: movk x0, #0x4000, lsl #16
file 0x28650: movk x1, #0x4958, lsl #16    ; 'XI'
file 0x28654: movk x0, #0x4546, lsl #32    ; 'FE'
file 0x28658: movk x1, #0x4d45, lsl #32    ; 'EM'
file 0x2865c: movk x0, #0x4958, lsl #48    ; 'XI'   -> x0 = {eax, "FEXI"}
file 0x28660: movk x1, #0x55, lsl #48      ; 'U\0'  -> x1 = {"FEXI", "EMU\0"}
file 0x28664: ret
```

The 32 bytes at `0x28644` were replaced with `mov x0, #0` / `mov x1, #0` / 6×`nop`, leaving the
`ret`. Result: the leaf returns `eax=0` and an all-zero vendor — i.e. "no hypervisor here", which is
consistent with the (already working) `HideHypervisorBit`.

**Verified.** With the patched DLL installed and loaded:

```
leaf1 ecx=0x3ed8330f edx=0x278bfbff  hypervisor_bit=0
hypervisor leaf 0x40000000 eax=0x0 vendor=''
modules: ... C:\windows\system32\libarm64ecfex.dll
```

The signature is gone. (Before: `eax=0x40000001 vendor='FEXIFEXIEMU'`.)

## Installing it: GameNative restores these DLLs itself

Two dead ends worth recording, because they cost time:

1. **Copying a patched file into `C:\windows\system32` does not stick.** GameNative re-installs the
   emulator DLLs at launch. Observed directly: a patched `libarm64ecfex.dll` (mtime 10:13) was
   reverted to the stock bytes with a fresh mtime at 10:27, when the game next launched.
2. **The `Wow64\amd64` registry value is forced back to `xtajit64.dll`.** Editing
   `HKLM\Software\Microsoft\Wow64\amd64` (and the `wine.inf` that defines it — there is no copy of
   `wine.inf` inside the prefix) does not survive a launch either. `xtajit64.dll` is byte-identical
   to `libarm64ecfex.dll`, so GameNative installs one and uses it under both names.

The supported channel is the **Contents Manager**, which consumes `.wcp` bundles. The format is
`tar.xz` containing `profile.json` plus the DLLs:

```json
{ "type": "FEXCore", "versionName": "2610-aoe", "versionCode": 1, ...
  "files": [ { "source": "libarm64ecfex.dll", "target": "${system32}/libarm64ecfex.dll" }, ... ] }
```

A patched bundle has been built and pushed to the device as
**`/sdcard/Download/fexcore-2610-aoe-nofex.wcp`** (`versionName` `2610-aoe-nofex`, `versionCode` 2,
patched `libarm64ecfex.dll` + stock `libwow64fex.dll`).

**Status: imported and installed — but onto the wrong filename.** The bundle was imported through
Settings → Contents Manager and selected for AoE IV (Edit container → Emulation → FEXCore Version →
`2610-aoe-nofex`). Verified afterwards on disk:

| file | state |
|---|---|
| `C:\windows\system32\libarm64ecfex.dll` | **PATCHED** (the `.wcp` target wired up correctly) |
| `C:\windows\system32\xtajit64.dll` | **ORIGINAL** (mtime unchanged, still 00:32) |

That matters because `xtajit64.dll` is the file GameNative actually loads. Confirmed by both
`modchk` (reports `xtajit64.dll` in the game process) and `dbgprobe` (its own module list shows
`C:\windows\system32\xtajit64.dll`, and CPUID still answers `eax=0x40000001 vendor='FEXIFEXIEMU'`).
So the `.wcp` install updates `libarm64ecfex.dll` but GameNative does **not** sync the two names, and
the emulator in use is still stock. The patch is therefore installed but **not in effect**.

Also observed: after selecting the new FEXCore the game stopped launching at all — the container comes
up (`wineserver`, `services.exe`, `winedevice`, `explorer`, `winhandler`, a `start.exe`) but
`RelicCardinal.exe` never appears and the log stays silent, leaving a black screen with the AoE
cursor. Whether that is caused by the patched FEXCore or is unrelated container state was not
determined before stopping.

### Resolved: install it under the name that is actually loaded

The `.wcp` manifest chooses the target path, so the same patched DLL can be installed as *both*
names. `fexcore-2610-aoe-nofex2.wcp` (`versionCode` 3) does exactly that:

```json
"files": [
  { "source": "libarm64ecfex.dll", "target": "${system32}/libarm64ecfex.dll" },
  { "source": "libarm64ecfex.dll", "target": "${system32}/xtajit64.dll"      },
  { "source": "libwow64fex.dll",   "target": "${system32}/libwow64fex.dll"   }
]
```

**How to tell whether the patch is live**, without guessing: copy the two DLLs out of the session and
check offset `0x28644`. `000080d2010080d2` = patched; `200080d2c1a888d2` = stock. Then confirm the
emulator actually in use — `dbgprobe`'s module list and `hypervisor leaf 0x40000000` line are the
quickest read (`eax=0x0 vendor=''` = patched and loaded; `eax=0x40000001 vendor='FEXIFEXIEMU'` = the
signature is still there).

### Next steps

1. Restart the device (or GameNative) to clear the stuck container, and confirm the stock
   `2610-aoe-1` FEXCore still launches the game — that separates "patched DLL broke the launch" from
   "container wedged".
2. Work out how `xtajit64.dll` is meant to track `libarm64ecfex.dll`. Either GameNative syncs them at
   some point (container creation? app start?) or the previous session created `xtajit64.dll` by
   hand. Until the loaded filename is patched, the FEX signature remains.
3. If there is no sync, the remaining route is the one that failed before: replace
   `C:\windows\system32\xtajit64.dll` while no Wine process holds it. Under Wine a mapped DLL
   could not be moved aside, so this needs either an in-place write from a Windows helper opened with
   `FILE_SHARE_DELETE`, or a window where the session is fully down.

**Earlier status note (kept for context): not yet installed.** Installing it needs the GameNative Contents Manager UI, and the app
was stuck at 0 % CPU ignoring input at the time of writing (it needed a restart). Once installed,
re-verify with `modchk`/`dbgprobe` that the leaf reads `eax=0x0 vendor=''` *in the game's own
process*, then measure whether the kill still happens at 2–4.5 minutes.

## Files

- `tools/research/dbgprobe.c` — the probe that reports CPUID, anti-debug and module state from inside the
  session.
- `docs/research/samples/aegis/dbgprobe_cpuid_report.txt` — the before/after CPUID reports.

## Reproducing the patch

```sh
# 1. take the shipped DLL
#    (device: C:\windows\system32\libarm64ecfex.dll)
# 2. patch 32 bytes at file offset 0x28644:
printf '\x00\x00\x80\xd2\x01\x00\x80\xd2\x1f\x20\x03\xd5\x1f\x20\x03\xd5\x1f\x20\x03\xd5\x1f\x20\x03\xd5\x1f\x20\x03\xd5\x1f\x20\x03\xd5' \
  | dd of=libarm64ecfex.dll bs=1 seek=$((0x28644)) conv=notrunc
# 3. package it (profile.json + both DLLs) as tar.xz and import via Contents Manager
```

The ready-made bundle was committed here as `fexcore-2610-aoe-nofex.wcp` (until 2026-10-07; it is in the git history).
