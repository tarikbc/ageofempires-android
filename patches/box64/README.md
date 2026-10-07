# Box64 patches

Against GameNative's Box64, [`Pipetto-crypto/box64`](https://github.com/Pipetto-crypto/box64) at `eb6fb21f`,
applied in order (checked with `git apply` on a clean checkout, 2026-10-07):

| Patch | What it does | Status |
|---|---|---|
| `0001` | Decode SSE/AVX memory stores, so a fault on them reaches Wine as a write | built and used since 2026-10-05 (`0.4.5-aoefix`) |
| `0002` | Keep the guest's `PROT_EXEC` when the host refuses it (files on Android's `noexec` shared storage) | verified: the game's own DLLs run ([BOX64-ROUTE.md](../../docs/BOX64-ROUTE.md)) |
| `0003` | Send raw Windows syscalls to Wine's dispatcher when Wine installed no seccomp `SIGSYS` handler (39-bit address space) | verified: the game passes its syscall gateway and writes its log |

## Build (macOS host, as done on 2026-10-07)

Same flags as the fork's release CI for the `ANDROID` platform:

```sh
NDK=~/Library/Android/sdk/ndk/26.1.10909125          # NDK r26b, as in the CI and the shipped binary
CC=$NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android31-clang
mkdir build && cd build
cmake .. -DCMAKE_C_COMPILER=$CC -DCMAKE_ASM_COMPILER=$CC \
  -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
  -DTERMUX=0 -DANDROID=1 -DARM_DYNAREC=1 -DBAD_SIGNAL=1 \
  -DCMAKE_BUILD_TYPE=Release -DHAVE_TRACE=0 -DZYDIS3=0 -DSTATICBUILD=0 -DBOX32=0
make -j10 box64
```

The result is a bionic executable (`interpreter /system/bin/linker64`, needs `libc.so`, `libm.so`, `libdl.so`),
like the one GameNative ships.

## Install

A `.wcp` is an xz tar with `profile.json` and `box64` at the root:

```json
{ "type": "Box64", "versionName": "0.4.5-aoefix4", "versionCode": 4, "description": "...",
  "files": [ { "source": "box64", "target": "${bindir}/box64" } ] }
```

Pack with `COPYFILE_DISABLE=1 tar -cJf box64-0.4.5-aoefix4.wcp profile.json box64`, push to `/sdcard/Download`,
then GameNative: menu, Settings, Contents Manager, Import .wcp from device. It then appears in the container's
Emulation tab as `0.4.5-aoefix4-4` (versionName-versionCode). The Box64 version only matters with an x86-64
Wine version; with an ARM64EC Wine, the 64-bit emulator is FEXCore.
