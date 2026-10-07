#!/usr/bin/env python3
"""Reusable GameNative UI automation for the AYN Thor (dual-screen).

Drives the GameNative app via `uiautomator dump` + `input tap` so we don't have to
screenshot-and-decide every run. The Thor has display 0 (main, 1920x1080) and display 4
(external, 1240x1080); GameNative runs on display 0.

Usage (import or CLI):
    from gn_nav import GN
    gn = GN()
    gn.launch_game("Age of Empires IV")   # navigate library -> detail -> Play

CLI:
    python3 gn_nav.py dump                 # print current clickable/text nodes
    python3 gn_nav.py tap-text Play
    python3 gn_nav.py tap-desc Play
    python3 gn_nav.py wait-text "Play" 60
    python3 gn_nav.py launch "Age of Empires IV"
"""
import argparse
import re
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

DISPLAY = "0"


def sh(*args, timeout=60):
    return subprocess.run(list(args), capture_output=True, text=True, timeout=timeout)


def dump_xml(serial=None):
    """Dump the current UI hierarchy as an ElementTree root."""
    adb = ["adb"] + (["-s", serial] if serial else [])
    # Use --windows: a plain `dump` only sees the default window and goes stale on the Thor.
    # GameNative's menus and the AYN assistant panel live in other windows.
    sh(*adb, "shell", "uiautomator", "dump", "--windows", "/sdcard/gn_ui.xml", timeout=30)
    out = sh(*adb, "shell", "cat", "/sdcard/gn_ui.xml", timeout=30)
    x = out.stdout
    m = re.search(r"<\?xml.*", x, re.S)
    if not m:
        raise RuntimeError("no xml in uiautomator dump: " + x[:200])
    return ET.fromstring(m.group(0))


def _nodes(root):
    for n in root.iter("node"):
        yield n


def find_nodes(root, text=None, desc=None, clickable=None):
    """Return nodes matching (text, content-desc, clickable). None means 'any'."""
    out = []
    for n in _nodes(root):
        if text is not None and n.get("text") != text:
            continue
        if desc is not None and n.get("content-desc") != desc:
            continue
        if clickable is not None and n.get("clickable") != clickable:
            continue
        out.append(n)
    return out


def bounds_center(n):
    b = n.get("bounds")
    m = re.match(r"\[(\d+),(\d+)\]\[(\d+),(\d+)\]", b)
    x1, y1, x2, y2 = map(int, m.groups())
    return (x1 + x2) // 2, (y1 + y2) // 2


class GN:
    def __init__(self, serial=None, display=DISPLAY):
        self.serial = serial
        self.display = display
        self.adb = ["adb"] + (["-s", serial] if serial else [])

    def tap(self, x, y):
        sh(*self.adb, "shell", "input", "-d", self.display, "tap", str(x), str(y))

    def keyevent(self, code):
        sh(*self.adb, "shell", "input", "-d", self.display, "keyevent", str(code))

    def back(self):
        self.keyevent(4)

    def dump(self):
        return dump_xml(self.serial)

    def _tap_node(self, n):
        # If there is a clickable node whose bounds contain this node, tap that instead.
        b = n.get("bounds")
        target = n
        root = self.dump()
        for c in _nodes(root):
            if c.get("clickable") == "true" and _contains(c.get("bounds"), b):
                target = c
                break
        x, y = bounds_center(target)
        self.tap(x, y)
        time.sleep(1.0)
        return (x, y)

    def tap_text(self, text, idx=0, retries=3):
        for _ in range(retries):
            root = self.dump()
            hits = find_nodes(root, text=text)
            if idx < len(hits):
                return self._tap_node(hits[idx])
            time.sleep(0.5)
        raise RuntimeError(f"text {text!r} not found")

    def tap_desc(self, desc, retries=3):
        for _ in range(retries):
            root = self.dump()
            hits = find_nodes(root, desc=desc)
            if hits:
                return self._tap_node(hits[0])
            time.sleep(0.5)
        raise RuntimeError(f"desc {desc!r} not found")

    def wait_text(self, text, timeout=60, interval=2):
        end = time.time() + timeout
        while time.time() < end:
            root = self.dump()
            if find_nodes(root, text=text):
                return True
            time.sleep(interval)
        return False

    def wait_gone_text(self, text, timeout=60, interval=2):
        end = time.time() + timeout
        while time.time() < end:
            root = self.dump()
            if not find_nodes(root, text=text):
                return True
            time.sleep(interval)
        return False

    def wait_process(self, pattern, timeout=180, interval=5):
        end = time.time() + timeout
        while time.time() < end:
            r = sh(*self.adb, "shell", "ps", "-A", timeout=30)
            if pattern.lower() in r.stdout.lower():
                return True
            time.sleep(interval)
        return False

    def is_game_page(self, title, root=None):
        """True when the detail page of the game called `title` shows (its title, Play and the Options cog)."""
        root = root if root is not None else self.dump()
        return bool(find_nodes(root, text=title) and find_nodes(root, text="Play") and find_nodes(root, desc="Options"))

    def open_game_page(self, title, settle_timeout=30, tries=4):
        """Library -> the detail page of `title`. GameNative asks its API for a suggested game after start; when the
        answer arrives, the suggestion takes the first card ("Recommended") and every other card moves one place.
        A tap that races that move opens the suggested game, so wait for the "Recommended" card first, tap from a
        single screen read, and check the page that opens."""
        for _ in range(tries):
            self.wait_text("Recommended", timeout=settle_timeout)
            time.sleep(1)
            root = self.dump()
            hits = find_nodes(root, text=title)
            if not hits:
                time.sleep(2)
                continue
            b = hits[0].get("bounds")
            card = next((c for c in _nodes(root) if c.get("clickable") == "true" and _contains(c.get("bounds"), b)), hits[0])
            self.tap(*bounds_center(card))
            for _ in range(5):
                time.sleep(1.5)
                root = self.dump()
                if self.is_game_page(title, root):
                    return True
            if find_nodes(root, text="Play"):  # another game's page opened
                self.back()
                time.sleep(2)
            settle_timeout = 5
        return False

    def launch_game(self, name):
        """Library -> game detail -> Play, waiting for each stage."""
        if not self.open_game_page(name):
            raise RuntimeError(f"detail page of {name!r} did not open")
        self.tap_text("Play")
        return True


def _contains(big, small):
    m1 = re.match(r"\[(\d+),(\d+)\]\[(\d+),(\d+)\]", big)
    m2 = re.match(r"\[(\d+),(\d+)\]\[(\d+),(\d+)\]", small)
    if not (m1 and m2):
        return False
    x1, y1, x2, y2 = map(int, m1.groups())
    a1, b1, a2, b2 = map(int, m2.groups())
    return x1 <= a1 and y1 <= b1 and x2 >= a2 and y2 >= b2


def _print_nodes(root):
    for n in _nodes(root):
        cl = n.get("clickable")
        t = n.get("text") or ""
        d = n.get("content-desc") or ""
        b = n.get("bounds") or ""
        if cl == "true" or t or d:
            print(f"[{cl}] {b:28s} text={t!r:42s} desc={d!r}")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--serial", default=None)
    p.add_argument("cmd")
    p.add_argument("arg", nargs="*")
    a = p.parse_args()
    gn = GN(a.serial)
    if a.cmd == "dump":
        _print_nodes(gn.dump())
    elif a.cmd == "tap-text":
        gn.tap_text(a.arg[0])
    elif a.cmd == "tap-desc":
        gn.tap_desc(a.arg[0])
    elif a.cmd == "wait-text":
        print("found" if gn.wait_text(a.arg[0], int(a.arg[1]) if len(a.arg) > 1 else 60) else "not-found")
    elif a.cmd == "wait-process":
        print("found" if gn.wait_process(a.arg[0], int(a.arg[1]) if len(a.arg) > 1 else 180) else "not-found")
    elif a.cmd == "launch":
        gn.launch_game(a.arg[0])
        print("launched")
    else:
        sys.exit("unknown cmd")


if __name__ == "__main__":
    main()
