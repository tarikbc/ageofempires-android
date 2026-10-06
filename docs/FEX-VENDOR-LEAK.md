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

## Files

- `tools/dbgprobe.c` — the probe that reports CPUID, anti-debug and module state from inside the
  session.
