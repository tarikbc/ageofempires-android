#!/bin/sh
# Builds every probe as a static x86-64 Windows exe (needs Homebrew mingw-w64).
set -e
cd "$(dirname "$0")"
for src in *.c; do
    x86_64-w64-mingw32-gcc -O1 -static -o "${src%.c}.exe" "$src" -lpsapi -lws2_32 -liphlpapi -lbcrypt -lwinhttp -lsynchronization
    echo "built ${src%.c}.exe"
done
