# Building ARM64EC FEX on macOS

Needed to test any FEX change. This works, and produces `Bin/libarm64ecfex.dll`.

## Toolchain

```sh
brew install ninja nasm
# llvm-mingw 20260922 — the same release the earlier FEX build used
curl -sLO https://github.com/mstorsjo/llvm-mingw/releases/download/20260922/llvm-mingw-20260922-ucrt-macos-universal.tar.xz
tar -xf llvm-mingw-20260922-ucrt-macos-universal.tar.xz -C ~/toolchains
export PATH=~/toolchains/llvm-mingw-20260922-ucrt-macos-universal/bin:$PATH
arm64ec-w64-mingw32-clang --version      # must report target arm64ec-w64-windows-gnu
```

`nasm` is required because `unittests/ASM/` is added **outside** the `if (NOT MINGW)` guard, so
configuring fails without it.

## Three macOS problems that must be fixed first

1. **`Scripts/NeedDisabledSVE.py` reads `/proc/cpuinfo`, twice** — once in `GetCPUFeatures()` and once in
   `IsAffectedSnapdragon()`. Linux-only, so on macOS it raises and CMake aborts. Guard both; reporting
   "no features / not affected" is correct, since the check only exists to work around SVE-disabled
   Snapdragon SoCs when cross-compiling for a specific CPU.

2. **`CMakeLists.txt:525` has an unquoted `string(STRIP ${AARCH64_CPU} AARCH64_CPU)`.** This is a real
   bug, not just a macOS one: when the helper emits nothing, CMake sees `string(STRIP` with one argument
   and dies with *"string sub-command STRIP requires two arguments"*. Quote it:
   `string(STRIP "${AARCH64_CPU}" AARCH64_CPU)`.

3. **BSD `sed`** — the ASM test-data generator runs `sed -i -e '1s;^;BITS 64\n;' …`, and BSD `sed`
   requires an argument to `-i`, so it fails with *"sed: -e: No such file or directory"*. Avoid it by
   building the target directly rather than `ninja` everything.

## Configure and build

```sh
git clone --depth 1 https://github.com/FEX-Emu/FEX.git
cd FEX && git submodule update --init --recursive --depth 1
mkdir build && cd build
cmake -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=../Data/CMake/toolchain_mingw.cmake \
  -DMINGW_TRIPLE=arm64ec-w64-mingw32 \
  -DCMAKE_BUILD_TYPE=Release -DENABLE_LTO=False ..
ninja arm64ecfex          # NOT plain ninja -- see problem 3
```

Artifact: `Bin/libarm64ecfex.dll` (~5.6 MB, `PE32+ executable (DLL) (GUI) x86-64`).

The build prints its revision — confirm it matches what the device ships before deploying:

```
-- FEX commit: 7d3090f78237267b2adf2d32116f0105b8d665cb
```

## All local build-environment changes

Problems 1–3 are **local build fixes, deliberately kept out of
[`patches/fex/0001-hide-smc-trap-from-guest.patch`](../patches/fex/0001-hide-smc-trap-from-guest.patch)**.
That patch should contain only the SMC change. Anything needed to make a patch build is different from
the patch itself.

## Reminder for deployment

This base still contains the **CPUID hypervisor vendor leak**
(`FEXCore/Source/Interface/Core/CPUID.cpp:984`, `HypervisorID = "FEXIFEXIEMU"`). A `.wcp` built from a
plain main checkout therefore reintroduces a problem the repo already solved once — the CPUID patch has
to be carried too. See [FEX-VENDOR-LEAK.md](FEX-VENDOR-LEAK.md).
