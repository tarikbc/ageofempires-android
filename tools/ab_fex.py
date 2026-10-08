#!/usr/bin/env python3
"""Alternate FEX builds across judged runs and summarise each run.

    ab_fex.py --build fix=PATH --build ctl=PATH [--rounds N] [--out DIR]

Each build's DLL is pushed to D:\\fex_<name>.dll. Before every run it is installed as
C:\\windows\\system32\\libarm64ecfex.dll by renaming the current file aside (a mapped DLL cannot be
overwritten, but it can be renamed), and the installed file's SHA-1 is checked against the local one.
Needs a live GameNative session (Open container, or the session a previous run left behind); when a
run's game exited and took the session with it, a new one is opened through the UI.

Each run is judged by tools/run_watch.py. The summary line per run gives: the session drop
(errno=10038), the last line the game wrote, the largest number of threads seen at suspend count 1,
the kill thread's CPU time, and whether the process exited.
"""
import argparse
import hashlib
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run_watch as rw  # noqa: E402

DL = "/sdcard/Download"


def ensure_session():
    """install() talks to winhandler. When the last run's game exited instead of freezing, GameNative closed the
    container with it, so open a plain container session first (detail page, cog, "Open container")."""
    if "winhandler.exe" in rw.sh("ps -A -o NAME"):
        return True
    from gn_nav import GN, bounds_center, find_nodes
    for attempt in range(1, 4):
        print(f"[{time.strftime('%H:%M:%S')}] no container session; opening one (attempt {attempt})", flush=True)
        rw.sh("am force-stop app.gamenative")
        time.sleep(4)
        rw.sh("input keyevent 3")
        time.sleep(1)
        rw.sh("am start -n app.gamenative/.MainActivityAliasDefault")
        time.sleep(12)
        g = GN(rw.SERIAL or None)
        if not g.open_game_page("Age of Empires IV: Anniversary Edition"):
            print("    AoE IV detail page did not open", flush=True)
            continue
        g.tap(1651, 536)  # the cog next to Play
        time.sleep(3)
        # Tap the item itself: its first clickable parent is the full-screen panel (tap_text would hit the middle).
        hits = find_nodes(g.dump(), text="Open container")
        if not hits:
            print("    'Open container' not shown after the cog tap", flush=True)
            continue
        g.tap(*bounds_center(hits[0]))
        for _ in range(30):
            time.sleep(3)
            rw.keep_local_on_save_conflict()  # the Save Conflict dialog holds the session until answered
            if "winhandler.exe" in rw.sh("ps -A -o NAME"):
                time.sleep(5)
                return True
    return False


def install(name):
    bat = (
        "@echo off\r\n"
        # A renamed-aside DLL can still be mapped by a frozen game process, so never reuse its name: delete the
        # old copies that are free, and move the current one to a fresh name.
        'del /f "C:\\windows\\system32\\libarm64ecfex.old*.dll" >nul 2>&1\r\n'
        'move /y "C:\\windows\\system32\\libarm64ecfex.dll" "C:\\windows\\system32\\libarm64ecfex.old%RANDOM%%RANDOM%.dll" >nul 2>&1\r\n'
        f'copy /y D:\\fex_{name}.dll "C:\\windows\\system32\\libarm64ecfex.dll" >nul 2>&1\r\n'
        'copy /y "C:\\windows\\system32\\libarm64ecfex.dll" D:\\aoe\\fex_installed.dll >nul 2>&1\r\n'
        "echo %time% > D:\\aoe\\use_done.txt\r\n"
    )
    local = os.path.join(rw.tempfile.gettempdir(), "usefex.bat")
    open(local, "w", newline="").write(bat)
    rw.adb("push", local, f"{DL}/usefex.bat")
    rw.sh(f"rm -f {DL}/aoe/fex_installed.dll {DL}/aoe/use_done.txt")
    rw.winexec("cmd", "/c D:\\usefex.bat")
    for _ in range(60):
        time.sleep(1)
        if rw.sh(f"cat {DL}/aoe/use_done.txt 2>/dev/null").strip():
            break
    return rw.sh(f"sha1sum {DL}/aoe/fex_installed.dll 2>/dev/null").split(" ")[0]


def summarise(outdir):
    log = open(os.path.join(outdir, "watch.txt"), errors="replace").read() if os.path.exists(os.path.join(outdir, "watch.txt")) else ""
    first = log.splitlines()[0] if log else ""
    drop = re.findall(r"\[(\d\d:\d\d:\d\d\.\d+)\] \[\d+\]: TlsConnection::Shutdown.*errno=10038", log)
    times = re.findall(r"^\([IEW]\) \[(\d\d:\d\d:\d\d\.\d+)\]", log, re.M)
    steps = re.findall(r"Loading step: \[([^\]]*)\]", log)
    max_s1, killer_user, gone = 0, [], ""
    tl = os.path.join(outdir, "timeline.tsv")
    if os.path.exists(tl):
        for row in open(tl).read().splitlines()[1:]:
            c = row.split("\t")
            m = re.search(r"s1:(\d+)", c[5] if len(c) > 5 else "")
            if m:
                max_s1 = max(max_s1, int(m.group(1)))
            k = re.search(r"user=(\d+)ms", c[6] if len(c) > 6 else "")
            if k and "susp=0" in c[6]:
                killer_user.append(int(k.group(1)))
    wo = os.path.join(outdir, "watch_out.txt")
    if os.path.exists(wo):
        m = re.search(r"t=\s*(\d+)s\s+process gone", open(wo).read())
        gone = f"exited by t={m.group(1)}s" if m else "still alive"
    ec = os.path.join(outdir, "exitcode.txt")
    code = re.search(r"exit code (\S+)", open(ec).read()) if os.path.exists(ec) else None
    return {
        "start": first.replace("RelicCardinal started at ", "")[:16],
        "drop": drop[0] if drop else "none",
        "last_line": times[-1] if times else "",
        "last_step": steps[-1] if steps else "",
        "max_suspended": max_s1,
        "kill_thread_ms": killer_user[-3:],
        "end": gone + (f" code {code.group(1)}" if code else ""),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", action="append", required=True, help="name=local/path.dll")
    ap.add_argument("--rounds", type=int, default=2)
    ap.add_argument("--out", default="ab_out")
    ap.add_argument("--minutes", type=float, default=6)
    a = ap.parse_args()
    builds = [b.split("=", 1) for b in a.build]
    sha = {}
    for name, path in builds:
        sha[name] = hashlib.sha1(open(path, "rb").read()).hexdigest()
        rw.adb("push", path, f"{DL}/fex_{name}.dll", timeout=120)
    os.makedirs(a.out, exist_ok=True)
    results = []
    for rnd in range(a.rounds):
        for name, _ in builds:
            if not ensure_session():
                sys.exit("could not open a container session")
            got = install(name)
            if got != sha[name]:
                sys.exit(f"install of {name} failed: device has {got!r}, expected {sha[name]}")
            outdir = os.path.join(a.out, f"{len(results) + 1:02d}-{name}")
            print(f"[{time.strftime('%H:%M:%S')}] run {len(results) + 1}: {name} ({sha[name][:8]}) -> {outdir}", flush=True)
            os.makedirs(outdir, exist_ok=True)
            with open(os.path.join(outdir, "watch_out.txt"), "w") as wf:
                subprocess.run([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), "run_watch.py"),
                                "--launch", "--minutes", str(a.minutes), "--stop-after-frozen", "40", "--out", outdir],
                               stdout=wf, stderr=subprocess.STDOUT)
            s = summarise(outdir)
            results.append((name, s))
            print(f"    {name}: {s}", flush=True)
    print("\nsummary")
    for i, (name, s) in enumerate(results, 1):
        print(f"{i:2d} {name:4s} start={s['start']} drop={s['drop']} last={s['last_line']} step={s['last_step']} "
              f"suspended={s['max_suspended']} killms={s['kill_thread_ms']} {s['end']}")


if __name__ == "__main__":
    main()
