# Building the package, the driver and the probes

Everything in a release can be rebuilt on a Mac: the FEX package (`.wcp`), the Turnip driver (`.zip`) and the
Windows probes used for testing.

## The FEX package

### Toolchain

```sh
brew install ninja nasm
# llvm-mingw 20260922, the same release the earlier FEX builds used
curl -sLO https://github.com/mstorsjo/llvm-mingw/releases/download/20260922/llvm-mingw-20260922-ucrt-macos-universal.tar.xz
tar -xf llvm-mingw-20260922-ucrt-macos-universal.tar.xz -C ~/toolchains
export PATH=~/toolchains/llvm-mingw-20260922-ucrt-macos-universal/bin:$PATH
arm64ec-w64-mingw32-clang --version      # must report target arm64ec-w64-windows-gnu
```

`nasm` is required because `unittests/ASM/` is added **outside** the `if (NOT MINGW)` guard, so
configuring fails without it.

### Three macOS problems that must be fixed first

1. **`Scripts/NeedDisabledSVE.py` reads `/proc/cpuinfo`, twice**: once in `GetCPUFeatures()` and once in
   `IsAffectedSnapdragon()`. Linux-only, so on macOS it raises and CMake aborts. Guard both; reporting
   "no features / not affected" is correct, since the check only exists to work around SVE-disabled
   Snapdragon SoCs when cross-compiling for a specific CPU.

2. **`CMakeLists.txt:525` has an unquoted `string(STRIP ${AARCH64_CPU} AARCH64_CPU)`.** This is a real
   bug, not just a macOS one: when the helper emits nothing, CMake sees `string(STRIP` with one argument
   and dies with *"string sub-command STRIP requires two arguments"*. Quote it:
   `string(STRIP "${AARCH64_CPU}" AARCH64_CPU)`.

3. **BSD `sed`**: the ASM test-data generator runs `sed -i -e '1s;^;BITS 64\n;' …`, and BSD `sed`
   requires an argument to `-i`, so it fails with *"sed: -e: No such file or directory"*. Avoid it by
   building the target directly rather than `ninja` everything.

These are local build fixes and are not part of the patch files in [patches/fex](../../patches/fex).

### Configure, patch and build

The package is built on FEX commit `7d3090f`, not on the newest main:

```sh
git clone https://github.com/FEX-Emu/FEX.git
cd FEX && git checkout 7d3090f78237267b2adf2d32116f0105b8d665cb
git submodule update --init --recursive --depth 1
# apply the macOS fixes above, then the package's patches in order:
for p in 0004 0006 0007 0009 0010 0012 0013 0014 0015 0016 0017; do
  git apply /path/to/this/repo/patches/fex/$p-*.patch
done
mkdir build && cd build
cmake -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=../Data/CMake/toolchain_mingw.cmake \
  -DMINGW_TRIPLE=arm64ec-w64-mingw32 \
  -DCMAKE_BUILD_TYPE=Release -DENABLE_LTO=False ..
ninja arm64ecfex          # NOT plain ninja, see problem 3
```

The build prints its revision (`-- FEX commit: 7d3090f78237267b2adf2d32116f0105b8d665cb`). Artifact:
`Bin/libarm64ecfex.dll` (about 5.6 MB, `PE32+ executable (DLL) (GUI) x86-64`). That patch list is the v1.4.0 set (`aoe4-perf6`, DLL SHA-1 `7e707379`); v1.2.0 and v1.3.0
stop at 0016 (`b5e6e357`). A clean build of a released set gives the released DLL except the 4 bytes of the build
time stamp (PE header and debug directory); [patches/fex](../../patches/fex) lists which sets were checked that way.

### Package it as a `.wcp`

```sh
# from this repo's folder
python3 tools/make_fex_wcp.py /path/to/FEX/build/Bin/libarm64ecfex.dll fexcore-aoe4-perf6.wcp --name aoe4-perf6 --code 30
```

A `.wcp` is an xz-compressed tar with `profile.json` and the files it names; it ships only `libarm64ecfex.dll`.
GameNative's Contents Manager imports it, and the dropdown shows it as `<name>-<code>`. GameNative drops a bundle whose
`versionName` is already installed, even with a new code, so each test build needs a new name. Installing and selecting it from adb:
[GAMENATIVE.md](GAMENATIVE.md).

## The Turnip driver

[`tools/build_turnip.sh MESA_SRC NAME`](../../tools/build_turnip.sh) on a Mac: Android NDK r27 (the `darwin-x86_64`
toolchain runs natively on arm64 Macs), `meson` and `mako` in a Python environment on `PATH`, Homebrew `bison` 3.8
and `flex` (the system bison 2.3 is too old for the ir3 parser), `glslang`, `ninja`. A shallow clone of
`https://gitlab.freedesktop.org/mesa/mesa.git` builds in about two minutes on an M-series Mac:

```sh
python3 -m venv ~/.venvs/mesa && ~/.venvs/mesa/bin/pip install meson mako pyyaml packaging
brew install bison flex glslang ninja
git clone --depth 1 https://gitlab.freedesktop.org/mesa/mesa.git
PATH=~/.venvs/mesa/bin:$PATH tools/build_turnip.sh mesa turnip-main-$(git -C mesa log --format=%h -1)
```

The released driver is Mesa main `c78a9e9` (2026-10-08). Meson options: release build (Mesa refuses LTO),
`platforms=android`, `platform-sdk-version=36`, `android-stub=true`, `vulkan-drivers=freedreno`,
`freedreno-kmds=kgsl`, `vulkan-beta=true`, API 33 clang. The zip holds `libvulkan_freedreno.so` (stripped, 14.4 MB)
and `meta.json`; GameNative reads `name`, `libraryName` and `driverVersion` from it and refuses a `name` that is
already installed, so each build needs a new name.

A tracing build for GPU profiles adds `-Dperfetto=true -Dallow-fallback-for=perfetto`; it costs about 15 % FPS
([PERFORMANCE.md](../research/PERFORMANCE.md), "GPU render-stage profile").

Install: **Menu → Settings → Driver Manager → Import ZIP from device**, then the container's Graphics tab, Graphics
Driver Version. From adb: `tools/gn_driver.py import turnip-main-c78a9e9.zip turnip` and
`tools/gn_driver.py select turnip-main`. GameNative's log shows the load:
`hook_android_dlopen_ext: loading custom driver: .../adrenotools/turnip-main-c78a9e9/libvulkan_freedreno.so`.

## The probes

`tools/probes/build.sh` builds every `tools/probes/*.c` as a static x86-64 Windows program with Homebrew's
`mingw-w64` (`x86_64-w64-mingw32-gcc`). The ones that run during the game (the agent, `dlgclick`, `mclick`,
`wakecost` and a few more) are built as GUI programs so they open no console window. What each probe does:
[tools/probes/README.md](../../tools/probes/README.md).

## History

On 2026-10-06, after an unpatched build (`2610-aoe-1`), a build with patches 0001 and 0002 was packaged as
`fexcore-aoe-smcfix.wcp` (`versionCode` 8, 1.39 MB, DLL 5,586,944 bytes; it targeted `libarm64ecfex.dll` and
`libwow64fex.dll`, not `xtajit64.dll`). When the notes were written it had not reached the device, which was locked
([GAMENATIVE.md](GAMENATIVE.md), "Before any scripted UI work"); patch 0001, built later with 0003, stops the game at
start-up. The package lineage
from then to v1.4.0 is in [STORY.md](../STORY.md#the-fex-packages).
