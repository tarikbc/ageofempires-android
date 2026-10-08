#!/usr/bin/env python3
"""Relaunch AoE IV, attach bpguard to the SuspendThread call sites, collect the traces."""
import sys
import time

sys.path.insert(0, ".")
from run_experiment import adb, winhandler_exec  # noqa: E402
from gn_nav import GN  # noqa: E402

import os
BP1 = os.environ.get("BP1", "1449065a1")   # +0x49065a1  call [SuspendThread]  (site 1)
BP2 = os.environ.get("BP2", "14490699d")   # +0x490699d  call [SuspendThread]  (site 2)


def game_pid():
    r = adb(["shell", "ps", "-A"], timeout=30)
    for line in r.stdout.splitlines():
        if "RelicCardinal.exe" in line and "grep" not in line:
            p = line.split()
            if p:
                return p[1]
    return ""


def winh_up():
    return "winhandler.exe" in adb(["shell", "ps", "-A"], timeout=30).stdout


def main():
    adb(["shell", "rm", "-f", "/sdcard/Download/aoe/bp.txt"])
    adb(["shell", "am", "force-stop", "app.gamenative"])
    time.sleep(4)
    uniq = adb(["shell", "ps", "-A"], timeout=30).stdout.count("RelicCardinal")
    print(f"[reset] RelicCardinal processes now: {uniq}", flush=True)

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
        print("[launch] nav problem:", e, flush=True)
        return

    t0 = time.time()
    pid = ""
    while time.time() - t0 < 200:
        pid = game_pid()
        if pid:
            break
        time.sleep(3)
    if not pid:
        print("[launch] no process", flush=True)
        return
    print(f"[launch] pid={pid} after {time.time()-t0:.0f}s", flush=True)

    for _ in range(40):
        if winh_up():
            break
        time.sleep(2)

    # attach the breakpoint debugger as early as possible
    print("[bp] attaching bpguard", flush=True)
    try:
        winhandler_exec("cmd", f"/c D:\\bp.bat {BP1} {BP2}")
    except Exception as e:
        print("[bp] launch failed:", e, flush=True)
        return

    # watch for traces for up to ~6 minutes
    last = -1
    for i in range(36):
        time.sleep(10)
        sz = adb(["shell", "ls", "-la", "/sdcard/Download/aoe/bp.txt"], timeout=30).stdout
        try:
            cur = int(sz.split()[4])
        except Exception:
            cur = -1
        alive = bool(game_pid())
        print(f"[t={i*10}s] bp.txt={cur} bytes game_alive={alive}", flush=True)
        if cur == last and cur > 0 and not alive:
            break
        last = cur
        if not alive:
            break
    print("[done]", flush=True)


if __name__ == "__main__":
    main()
