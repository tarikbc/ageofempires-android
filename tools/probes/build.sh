#!/bin/sh
# Builds every probe as a static x86-64 Windows exe (needs Homebrew mingw-w64).
set -e
cd "$(dirname "$0")"
for src in *.c; do
    # aoeagent runs for the whole session: build it as a GUI program so it opens no console window
    extra=""
    [ "$src" = "aoeagent.c" ] && extra="-mwindows"
    x86_64-w64-mingw32-gcc -O1 -static $extra -o "${src%.c}.exe" "$src" -lpsapi -lws2_32 -liphlpapi -lbcrypt -lwinhttp -lsynchronization -lxinput1_4
    echo "built ${src%.c}.exe"
done
