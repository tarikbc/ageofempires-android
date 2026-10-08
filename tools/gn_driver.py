#!/usr/bin/env python3
"""Install and select a graphics driver for the AoE IV container in GameNative 1.2.1 (Thor).

    gn_driver.py import FILE.zip SEARCH_WORD     Settings > Driver Manager > Import ZIP from device, pick FILE.zip
    gn_driver.py select SEARCH_TEXT              Edit container > Graphics > Graphics Driver Version: search, pick the
                                                 installed (white) match, check the field, save

Both restart GameNative. The version list mixes installed drivers (white text) with online ones (grey) and names
installed drivers by their folder: a package whose meta.json name contains "/" was installed as "tmp" (2026-10-07,
Balemuni Apex v2).
"""
import sys
import time
from io import BytesIO

from gn_nav import GN, bounds_center, find_nodes, _nodes
import run_watch as rw

SERIAL = rw.SERIAL or None


def restart():
    rw.sh("am force-stop app.gamenative")
    time.sleep(3)
    rw.sh("input keyevent 3")
    time.sleep(1)
    rw.sh("am start -n app.gamenative/.MainActivityAliasDefault")
    time.sleep(14)
    return GN(SERIAL)


def open_settings_item(g, label):
    g.tap(1841, 73)  # top-right Menu
    time.sleep(2)
    hits = [n for n in find_nodes(g.dump(), text="Settings") if bounds_center(n)[0] > 1000 and bounds_center(n)[1] < 700]
    if not hits:
        sys.exit("menu did not open")
    g.tap(*bounds_center(hits[0]))
    time.sleep(3)
    for _ in range(8):
        hits = find_nodes(g.dump(), text=label)
        if hits:
            g.tap(*bounds_center(hits[0]))
            time.sleep(3)
            return
        rw.sh("input -d 0 swipe 960 900 960 600 400")
        time.sleep(1.5)
    sys.exit(f"{label} not found")


def import_zip(name, word):
    g = restart()
    open_settings_item(g, "Driver Manager")
    g.tap(*bounds_center(find_nodes(g.dump(), text="Import ZIP from device")[0]))
    time.sleep(4)
    g.tap(1753, 110)  # picker search
    time.sleep(2)
    rw.sh(f"input -d 0 text {word}")
    time.sleep(1)
    rw.sh("input -d 0 keyevent 66")
    time.sleep(4)
    hits = find_nodes(g.dump(), text=name)
    if not hits:
        sys.exit(f"{name} not in the picker")
    g.tap(*bounds_center(hits[0]))
    time.sleep(8)
    texts = [n.get("text") for n in _nodes(g.dump()) if n.get("text") and 640 < bounds_center(n)[0] < 1300]
    print("Driver Manager now shows:", texts[texts.index("Installed custom drivers") + 1:] if "Installed custom drivers" in texts else texts)


def is_white(g, node):
    from PIL import Image
    import subprocess
    png = subprocess.run(["adb", *(["-s", SERIAL] if SERIAL else []), "exec-out", "screencap", "-p", "-d", "4630946441858561667"],
                         capture_output=True).stdout
    img = Image.open(BytesIO(png)).convert("RGB")
    b = node.get("bounds").replace("][", ",").strip("[]").split(",")
    x0, y0, x1, y1 = map(int, b)
    px = [img.getpixel((x, y)) for x in range(x0, x1, 3) for y in range(y0, y1, 3)]
    return max(sum(p) for p in px) > 3 * 230  # installed entries are drawn white, online ones grey


def select(search):
    g = restart()
    if not g.open_game_page("Age of Empires IV: Anniversary Edition"):
        sys.exit("AoE IV page did not open")
    g.tap(1651, 536)  # cog
    time.sleep(3)
    g.tap(*bounds_center(find_nodes(g.dump(), text="Edit container")[0]))
    time.sleep(4)
    g.tap(314, 203)  # Graphics tab
    time.sleep(2)
    g.tap(960, 508)  # Graphics Driver Version
    time.sleep(2)
    s = find_nodes(g.dump(), text="Search")
    g.tap(*bounds_center(s[0]))
    time.sleep(1)
    rw.sh(f"input -d 0 text {search.replace(' ', '%s')}")
    time.sleep(1)
    g.tap(1800, 238)  # keyboard DONE
    time.sleep(1.5)
    # the search box itself (an EditText) also carries the typed text; an exact name wins over longer matches
    hits = [n for n in _nodes(g.dump()) if n.get("text") and search.lower() in n.get("text").lower()
            and "EditText" not in n.get("class", "") and bounds_center(n)[0] > 1150 and bounds_center(n)[1] > 340]
    hits.sort(key=lambda n: n.get("text").lower() != search.lower())
    white = [n for n in hits if is_white(g, n)]
    print("matches:", [n.get("text") for n in hits], "installed:", [n.get("text") for n in white])
    if not white:
        sys.exit("no installed match")
    g.tap(*bounds_center(white[0]))
    time.sleep(2)
    field = [n.get("text") for n in _nodes(g.dump()) if n.get("text") and bounds_center(n)[0] < 800 and 500 < bounds_center(n)[1] < 560]
    print("field:", field)
    if white[0].get("text") not in field:
        sys.exit("selection not shown in the field")
    g.tap(1856, 74)  # save
    time.sleep(3)
    print("saved; play visible:", bool(find_nodes(g.dump(), text="Play")))


if __name__ == "__main__":
    if len(sys.argv) >= 4 and sys.argv[1] == "import":
        import_zip(sys.argv[2], sys.argv[3])
    elif len(sys.argv) >= 3 and sys.argv[1] == "select":
        select(" ".join(sys.argv[2:]))
    else:
        sys.exit(__doc__)
