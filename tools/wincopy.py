#!/usr/bin/env python3
"""Copy files inside the game's Wine session on the Thor, with no console window (probes/gcopy.exe).

    wincopy.py SRC DST [SRC DST ...]

Windows paths; %VARIABLES% are expanded inside the session, and D:\\ is /sdcard/Download on the Thor. Examples:
    wincopy.py "%USERPROFILE%\\Documents\\My Games\\Age of Empires IV\\warnings.log" "D:\\aoe\\watch.txt"
    wincopy.py "Z:\\home\\xuser-STEAM_1466860\\.config\\.power-profile" "D:\\aoe\\pp.json"
The pairs go through D:\\aoe\\gcopy.txt, because a winhandler launch request holds only 51 bytes of program and
arguments. Needs probes/gcopy.exe in /sdcard/Download/aoe and a running session.
"""
import os
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run_watch as rw  # noqa: E402


def main():
    pairs = sys.argv[1:]
    if not pairs or len(pairs) % 2:
        sys.exit(__doc__)
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False, newline="") as f:
        f.write("".join(p + "\r\n" for p in pairs))
        job = f.name
    try:
        rw.adb("push", job, "/sdcard/Download/aoe/gcopy.txt")
        rw.winexec("D:\\aoe\\gcopy.exe", "")
        time.sleep(4)
        rw.sh("rm -f /sdcard/Download/aoe/gcopy.txt")
    finally:
        os.unlink(job)
    print("sent", len(pairs) // 2, "copies")


if __name__ == "__main__":
    main()
