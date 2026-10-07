#!/usr/bin/env python3
"""Select a FEXCore Version for the AoE IV container in GameNative's editor and save (GameNative 1.2.1, Thor).

    gn_select_fex.py ENTRY        e.g. aoe-bloom-13

Restarts GameNative, opens the AoE IV page (waiting for the suggested-game card), cog, Edit container, Emulation tab,
opens the FEXCore Version list, scrolls it until ENTRY shows, taps it, checks the field and saves.
"""
import sys
import time

from gn_nav import GN, bounds_center, find_nodes
import run_watch as rw

entry = sys.argv[1]
rw.sh("am force-stop app.gamenative")
time.sleep(3)
rw.sh("input keyevent 3")
time.sleep(1)
rw.sh("am start -n app.gamenative/.MainActivityAliasDefault")
time.sleep(12)
g = GN(rw.SERIAL or None)
if not g.open_game_page("Age of Empires IV: Anniversary Edition"):
    sys.exit("AoE IV page did not open")
g.tap(1651, 536)  # cog
time.sleep(3)
hits = find_nodes(g.dump(), text="Edit container")
if not hits:
    sys.exit("'Edit container' not shown")
g.tap(*bounds_center(hits[0]))
time.sleep(4)
g.tap(535, 203)  # Emulation tab
time.sleep(2)
g.tap(960, 342)  # FEXCore Version field
time.sleep(2)
for _ in range(6):
    hits = find_nodes(g.dump(), text=entry)
    if hits and bounds_center(hits[0])[1] < 1040:
        break
    rw.sh("input -d 0 swipe 1680 900 1680 500 500")
    time.sleep(1.5)
else:
    sys.exit(f"{entry} not in the list")
g.tap(*bounds_center(hits[0]))
time.sleep(2)
field = find_nodes(g.dump(), text=entry)
if not field or bounds_center(field[0])[0] > 800:
    sys.exit("selection not shown in the field")
g.tap(1856, 74)  # save
time.sleep(3)
print("saved", entry, "play visible:", bool(find_nodes(g.dump(), text="Play")))
