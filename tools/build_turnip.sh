#!/bin/sh
# Build Mesa's Turnip (freedreno Vulkan, KGSL) for Android arm64 on a Mac and package it as a GameNative /
# adrenotools driver zip (meta.json + libvulkan_freedreno.so). See docs/guides/BUILDING.md.
#
#   tools/build_turnip.sh MESA_SRC NAME [OUT_DIR]      -> OUT_DIR/NAME.zip (default OUT_DIR = ./turnip_out)
#
# Needs: Android NDK (ANDROID_NDK, default ~/Library/Android/sdk/ndk/27.1.12297006), meson + mako in a Python env
# on PATH (python3 -m venv; pip install meson mako pyyaml packaging), Homebrew bison 3.8 and flex (macOS's bison 2.3
# is too old), glslang (brew install glslang), ninja. Mesa refuses LTO, so the build is a plain release build.
set -e
SRC="$1"
NAME="$2"
OUT="${3:-$PWD/turnip_out}"
NDK="${ANDROID_NDK:-$HOME/Library/Android/sdk/ndk/27.1.12297006}"
TC="$NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin"
API="${ANDROID_API:-33}"
export PATH="/opt/homebrew/opt/bison/bin:/opt/homebrew/opt/flex/bin:$PATH"
mkdir -p "$OUT"
CROSS="$OUT/android-aarch64.ini"
cat > "$CROSS" <<EOF
[binaries]
ar = '$TC/llvm-ar'
c = ['$TC/aarch64-linux-android$API-clang']
cpp = ['$TC/aarch64-linux-android$API-clang++', '-fno-exceptions', '-fno-unwind-tables', '-fno-asynchronous-unwind-tables', '--start-no-unused-arguments', '-static-libstdc++', '--end-no-unused-arguments']
c_ld = 'lld'
cpp_ld = 'lld'
strip = '$TC/llvm-strip'
pkg-config = ['env', 'PKG_CONFIG_LIBDIR=/nonexistent', 'pkg-config']
[host_machine]
system = 'android'
cpu_family = 'aarch64'
cpu = 'armv8'
endian = 'little'
EOF
cd "$SRC"
rm -rf build-android
meson setup build-android --cross-file "$CROSS" -Dbuildtype=release \
  -Dplatforms=android -Dplatform-sdk-version=36 -Dandroid-stub=true -Dandroid-libbacktrace=disabled \
  -Dgallium-drivers= -Dvulkan-drivers=freedreno -Dfreedreno-kmds=kgsl -Dvulkan-beta=true \
  -Degl=disabled -Dglx=disabled -Dvideo-codecs= -Dzstd=disabled -Dxmlconfig=disabled -Dwerror=false
ninja -C build-android src/freedreno/vulkan/libvulkan_freedreno.so
mkdir -p "$OUT/$NAME"
"$TC/llvm-strip" --strip-unneeded -o "$OUT/$NAME/libvulkan_freedreno.so" build-android/src/freedreno/vulkan/libvulkan_freedreno.so
GIT=$(git log --format=%h -1)
VER=$(cat VERSION)
cat > "$OUT/$NAME/meta.json" <<EOF
{
  "schemaVersion": 1,
  "name": "$NAME",
  "description": "Mesa $VER $GIT Turnip (KGSL), built with tools/build_turnip.sh from github.com/tarikbc/ageofempires-android",
  "author": "tarikbc",
  "packageVersion": "1",
  "vendor": "Mesa",
  "driverVersion": "Vulkan 1.4",
  "minApi": 28,
  "libraryName": "libvulkan_freedreno.so"
}
EOF
cd "$OUT/$NAME" && rm -f "../$NAME.zip" && zip -9 -j "../$NAME.zip" libvulkan_freedreno.so meta.json
ls -la "$OUT/$NAME.zip"
echo BUILD-OK
