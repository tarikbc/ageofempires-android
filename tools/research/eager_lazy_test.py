#!/usr/bin/env python3
"""Eager-vs-lazy decryption test.

Relaunch the game, then dump the same .text region as soon as the process appears
(early) and again after the game has loaded (late). If the early dump has more
still-encrypted bytes (i.e. matching the on-disk packed image) than the late one,
the packer decrypts lazily at runtime.
"""
import sys
import time

sys.path.insert(0, ".")
from run_experiment import adb, winhandler_exec  # noqa: E402
from gn_nav import GN  # noqa: E402


def game_pid():
    r = adb(["shell", "ps", "-A"], timeout=30)
    for line in r.stdout.splitlines():
        if "RelicCardinal.exe" in line and "grep" not in line:
            p = line.split()
            if p:
                return p[1]
    return ""


def winh_up():
    r = adb(["shell", "ps", "-A"], timeout=30)
    return "winhandler.exe" in r.stdout


def main():
    adb(["shell", "rm", "-f", "/sdcard/Download/aoe/probe_early.bin", "/sdcard/Download/aoe/probe_late.bin"])
    # bring GameNative forward cleanly (BACK + am start worked reliably before)
    adb(["shell", "input", "keyevent", "4"])
    adb(["shell", "am", "start", "-n", "app.gamenative/.MainActivityAliasDefault"])
    time.sleep(3)
    gn = GN()
    try:
        gn.tap_text("Age of Empires IV: Anniversary Edition")
        time.sleep(2)
        gn.tap_text("Play")
        print("[launch] Play tapped", flush=True)
    except Exception as e:
        print("[launch] navigation problem:", e, flush=True)
        return

    t0 = time.time()
    pid = ""
    while time.time() - t0 < 200:
        pid = game_pid()
        if pid:
            break
        time.sleep(3)
    if not pid:
        print("[launch] game process never appeared", flush=True)
        return
    print(f"[launch] RelicCardinal pid={pid} after {time.time()-t0:.0f}s", flush=True)

    # wait for winhandler to be usable
    for _ in range(30):
        if winh_up():
            break
        time.sleep(2)

    print("[dump] EARLY dump", flush=True)
    for attempt in range(5):
        try:
            winhandler_exec("cmd", "/c D:\\dump_probe.bat early")
            break
        except Exception as e:
            print("  retry:", e, flush=True)
            time.sleep(3)
    time.sleep(8)
    print(adb(["shell", "ls", "-la", "/sdcard/Download/aoe/probe_early.bin"]).stdout, flush=True)

    time.sleep(100)

    print("[dump] LATE dump", flush=True)
    for attempt in range(5):
        try:
            winhandler_exec("cmd", "/c D:\\dump_probe.bat late")
            break
        except Exception as e:
            print("  retry:", e, flush=True)
            time.sleep(3)
    time.sleep(8)
    print(adb(["shell", "ls", "-la", "/sdcard/Download/aoe/probe_late.bin"]).stdout, flush=True)
    print("[done]", flush=True)


if __name__ == "__main__":
    main()
