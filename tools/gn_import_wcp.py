#!/usr/bin/env python3
"""Import a .wcp from /sdcard/Download into GameNative's Contents Manager (GameNative 1.2.1 on the Thor).

    gn_import_wcp.py FILE_NAME [--search WORD]

Restarts GameNative, opens Menu > Settings > Contents Manager > Import .wcp from device, searches the Android file
picker for WORD (one word: the keyboard splits compound words) and picks FILE_NAME. Coordinates and labels are from
docs/guides/GAMENATIVE-UI.md.
"""
import argparse
import sys
import time

from gn_nav import GN, bounds_center, find_nodes
import run_watch as rw

ap = argparse.ArgumentParser()
ap.add_argument("file")
ap.add_argument("--search", default="fexcore")
a = ap.parse_args()

rw.sh("am force-stop app.gamenative")
time.sleep(3)
rw.sh("input keyevent 3")
time.sleep(1)
rw.sh("am start -n app.gamenative/.MainActivityAliasDefault")
time.sleep(14)
g = GN(rw.SERIAL or None)
g.tap(1841, 73)  # top-right Menu
time.sleep(2)
# Two nodes read "Settings": the menu entry (right-hand panel, upper half) and a hint in the bottom bar.
hits = [n for n in find_nodes(g.dump(), text="Settings") if bounds_center(n)[0] > 1000 and bounds_center(n)[1] < 700]
if not hits:
    sys.exit("menu did not open")
g.tap(*bounds_center(hits[0]))
time.sleep(3)
for _ in range(8):
    hits = find_nodes(g.dump(), text="Contents Manager")
    if hits:
        break
    rw.sh("input -d 0 swipe 960 900 960 600 400")
    time.sleep(1.5)
else:
    sys.exit("Contents Manager not found")
g.tap(*bounds_center(hits[0]))
time.sleep(3)
hits = find_nodes(g.dump(), text="Import .wcp from device")
if not hits:
    sys.exit("import button not found")
g.tap(*bounds_center(hits[0]))
time.sleep(4)
g.tap(1753, 110)  # picker search
time.sleep(2)
rw.sh(f"input -d 0 text {a.search}")
time.sleep(1)
rw.sh("input -d 0 keyevent 66")
time.sleep(4)
hits = find_nodes(g.dump(), text=a.file)
if not hits:
    sys.exit(f"{a.file} not found in the picker")
g.tap(*bounds_center(hits[0]))
time.sleep(6)
print("picked", a.file)
