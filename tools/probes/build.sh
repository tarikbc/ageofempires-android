#!/bin/sh
# Builds every probe as a static x86-64 Windows exe (needs Homebrew mingw-w64).
set -e
cd "$(dirname "$0")"
for src in *.c; do
    # aoeagent, waitexit and dlgclick run during the game: build them as GUI programs so they open no console window
    extra=""
    case "$src" in aoeagent.c|waitexit.c|dlgclick.c) extra="-mwindows" ;; esac
    x86_64-w64-mingw32-gcc -O1 -static $extra -o "${src%.c}.exe" "$src" -lpsapi -lws2_32 -liphlpapi -lbcrypt -lwinhttp -lsynchronization -lxinput1_4
    echo "built ${src%.c}.exe"
done
