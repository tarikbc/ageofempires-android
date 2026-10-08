#!/usr/bin/env python3
"""Handled-exception rate of the running game, from FEX patch 0010's counters (FastContinueStats, marker "FSTCONT1")
read through the in-game agent; nothing is added to the game.

    excrate.py [SECONDS] [--rva HEX]

Every exception the game handles and resumes ends in an NtContinue to x64 code; 0010 counts those it takes on its fast
path ("fast") and the rest ("slow"). The counters are process-wide. --rva is their offset in libarm64ecfex.dll: 3ef310
in the v1.2.0 build b5e6e357, 3ee2e8 in v1.1.0 (bc82c565); search another build's DLL file for the marker.
"""
import os
import struct
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import agent  # noqa: E402


def read(rva):
    lines = agent.call(f"peek libarm64ecfex.dll {rva} 18")
    data = bytes.fromhex("".join(l.split(":", 1)[1].replace(" ", "") for l in lines if ":" in l and not l.startswith("addr")))
    marker, fast, slow = struct.unpack_from("<QQQ", data)
    if marker != 0x31544e4f43545346:
        sys.exit(f"marker not found at libarm64ecfex.dll+{rva} (wrong build? use --rva)")
    return fast, slow


def main():
    args = sys.argv[1:]
    seconds = float(args[0]) if args and not args[0].startswith("--") else 10.0
    rva = args[args.index("--rva") + 1] if "--rva" in args else "3ef310"
    f0, s0 = read(rva)
    t0 = time.time()
    time.sleep(seconds)
    f1, s1 = read(rva)
    dt = time.time() - t0
    print(f"{dt:.1f} s: {(f1 - f0) / dt:,.0f} fast continues/s, {(s1 - s0) / dt:,.0f} slow/s "
          f"(totals {f1:,} / {s1:,})")


if __name__ == "__main__":
    main()
