#!/usr/bin/env python3
"""Watch one AoE IV run on the Thor and judge it by the only valid criterion: is warnings.log growing?

    run_watch.py [--launch] [--minutes N] [--out DIR]

--launch   force-stop GameNative, start it fresh, open AoE IV and tap Play (clean-restart rule).
Every 10 s: copy warnings.log out through winhandler (tools/thor/mon.bat -> D:\\aoe\\watch.txt).
Every 20 s: also run suspinfo, which reads each thread's *Windows* suspend count without suspending
anything. /proc thread states cannot see Wine's SuspendThread, so suspinfo is the kill detector.

Guards from docs/research/LEDGER.md: the log's first line must name this run's start time, and a run
with ~5 threads at 0 % CPU never initialised.
"""
import argparse
import os
import re
import struct
import subprocess
import sys
import tempfile
import time

# adb picks the device itself when one is connected; set ANDROID_SERIAL (read by adb) when several are.
SERIAL = os.environ.get("THOR_SERIAL", "")
DL = "/sdcard/Download"
KILL_ENTRY = "RelicCardinal.exe+3e69304"


def adb(*args, timeout=60):
    return subprocess.run(["adb", *(["-s", SERIAL] if SERIAL else []), *args], capture_output=True, text=True, timeout=timeout)


def sh(cmd, timeout=60):
    return adb("shell", cmd, timeout=timeout).stdout


def winexec(program, params):
    if len(program) + len(params) > 51:
        raise ValueError("program + params must fit in 51 bytes")
    body = bytes([2]) + struct.pack("<iii", len(program) + len(params) + 8, len(program), len(params))
    pkt = (body + program.encode() + params.encode()).ljust(64, b"\0")
    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as f:
        f.write(pkt)
        tmp = f.name
    try:
        adb("push", tmp, "/data/local/tmp/winhandler_exec.bin", timeout=30)
        sh("timeout 3 nc -u 127.0.0.1 7946 < /data/local/tmp/winhandler_exec.bin", timeout=15)
    finally:
        os.unlink(tmp)


def game_pid():
    # Match the process NAME, not ARGS: explorer.exe and winhandler.exe carry the game path in their
    # arguments, and the first of them is not the game.
    for line in sh("ps -A -o PID,NAME").splitlines():
        if line.rstrip().endswith("RelicCardinal.exe"):
            return line.split()[0]
    return ""


def proc_stats(pid):
    """(threads, cpu_ticks) from /proc/<pid>/stat. The process-wide utime+stime keeps the time of
    threads that have exited, so the delta cannot go negative when the thread count drops."""
    line = sh(f"cat /proc/{pid}/stat 2>/dev/null")
    if ")" not in line:
        return 0, 0
    f = line.rsplit(")", 1)[1].split()
    return int(f[17]), int(f[11]) + int(f[12])  # num_threads, utime + stime


def run_mon(with_susp):
    before = sh(f"cat {DL}/aoe/mon_done.txt 2>/dev/null")
    winexec("cmd", "/c D:\\mon.bat s" if with_susp else "/c D:\\mon.bat")
    for _ in range(30):
        time.sleep(1)
        if sh(f"cat {DL}/aoe/mon_done.txt 2>/dev/null") != before:
            return True
    return False


def log_state():
    head = sh(f"head -1 {DL}/aoe/watch.txt 2>/dev/null").strip()
    size = sh(f"stat -c %s {DL}/aoe/watch.txt 2>/dev/null").strip()
    step = sh(f"grep -o 'Loading step: \\[[^]]*\\]' {DL}/aoe/watch.txt 2>/dev/null | tail -1").strip()
    last = sh(f"tail -c 400 {DL}/aoe/watch.txt 2>/dev/null").strip().splitlines()
    return head, int(size or -1), step, (last[-1][:140] if last else "")


def susp_state():
    txt = sh(f"cat {DL}/aoe/susp_now.txt 2>/dev/null")
    counts = {}
    killer = ""
    for line in txt.splitlines():
        m = re.match(r"tid (\w+) suspend=(\d+) user=(\d+)ms start=(\S+)", line)
        if not m:
            continue
        counts[m.group(2)] = counts.get(m.group(2), 0) + 1
        if m.group(4) == KILL_ENTRY:
            killer = f"killer tid={m.group(1)} susp={m.group(2)} user={m.group(3)}ms"
    return counts, killer


def start_exit_watch():
    """Start probes/waitexit.exe in the container. If the game exits instead of freezing, GameNative closes the
    container at once, so only this probe can record the exit code and copy the end of the log."""
    local = os.path.join(os.path.dirname(os.path.abspath(__file__)), "probes", "waitexit.exe")
    if os.path.exists(local):
        sh(f"mkdir -p {DL}/aoe")
        adb("push", local, f"{DL}/aoe/waitexit.exe")
    if not sh(f"ls {DL}/aoe/waitexit.exe 2>/dev/null").strip():
        print("waitexit.exe missing (run tools/probes/build.sh); an exit will not be recorded", flush=True)
        return
    winexec("D:\\aoe\\waitexit.exe", "")


def start_dialog_click():
    """Start probes/dlgclick.exe: when the game's "unable to determine your video card's installed driver version"
    dialog appears (its one-day "Don't show" choice expired), click "Don't show this message"; loading waits on it."""
    local = os.path.join(os.path.dirname(os.path.abspath(__file__)), "probes", "dlgclick.exe")
    if not os.path.exists(local):
        print("dlgclick.exe missing (run tools/probes/build.sh); the driver dialog will not be dismissed", flush=True)
        return
    sh(f"mkdir -p {DL}/aoe")
    adb("push", local, f"{DL}/aoe/dlgclick.exe")
    with tempfile.NamedTemporaryFile("w", suffix=".bat", delete=False, newline="") as f:
        f.write('@echo off\r\nstart "" D:\\aoe\\dlgclick.exe "Age of Empires IV" "show this message" 600\r\n')
        tmp = f.name
    try:
        adb("push", tmp, f"{DL}/aoe/dlgclick.bat")
    finally:
        os.unlink(tmp)
    winexec("cmd", "/c D:\\aoe\\dlgclick.bat")


def keep_local_on_save_conflict():
    """GameNative's "Save Conflict" dialog (a new remote and a new local save) holds the launch until it is answered.
    Keep the local save: no other machine plays this game. Returns True if the dialog was answered."""
    from gn_nav import GN, find_nodes
    gn = GN(SERIAL or None)
    try:
        root = gn.dump()
    except (RuntimeError, subprocess.TimeoutExpired):
        return False
    if not find_nodes(root, text="Save Conflict"):
        return False
    hits = find_nodes(root, text="Keep local")
    if not hits:
        return False
    gn._tap_node(hits[0])
    print(f"[{time.strftime('%H:%M:%S')}] Save Conflict: kept the local save", flush=True)
    return True


def launch():
    sh("am force-stop app.gamenative")
    time.sleep(4)
    sh("input keyevent 3")  # HOME clears the touchable launcher overlays (AUTOMATION-PATHS.md)
    time.sleep(1)
    sh("am start -n app.gamenative/.MainActivityAliasDefault")
    time.sleep(12)
    # The library's first ("Recommended") card loads late and moves the others; open_game_page waits for it and
    # checks that the AoE IV page opened, so Play never starts the suggested game.
    from gn_nav import GN
    if not GN(SERIAL or None).open_game_page("Age of Empires IV: Anniversary Edition"):
        print(f"[{time.strftime('%H:%M:%S')}] AoE IV detail page did not open", flush=True)
        return False
    sh("input -d 0 tap 206 536")  # Play
    print(f"[{time.strftime('%H:%M:%S')}] Play tapped", flush=True)
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--launch", action="store_true")
    ap.add_argument("--minutes", type=float, default=10)
    ap.add_argument("--out", default="run_watch_out")
    ap.add_argument("--t0", type=float, default=0, help="epoch seconds the game process appeared (re-attach)")
    ap.add_argument("--stop-after-frozen", type=int, default=0,
                    help="stop this many seconds after the log stops growing while threads are suspended")
    ap.add_argument("--start", action="append", default=[],
                    help="a D:\\ batch file to run in the session as soon as the game process exists (repeatable)")
    ap.add_argument("--quiet", action="store_true",
                    help="for benchmarks: after the start-up helpers, only watch the process over adb (no mon.bat, so "
                         "no cmd window or extra process inside Wine every 10 s)")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    if "deviceLocked=1" in sh("dumpsys trust"):
        sys.exit("device is locked; unlock it first")
    if not a.t0:
        sh(f"rm -f {DL}/aoe/watch.txt {DL}/aoe/susp_now.txt {DL}/aoe/susp_hist.txt {DL}/aoe/exitcode.txt {DL}/aoe/final_log.txt")
    pid = ""
    if a.launch:
        # GameNative sometimes hangs on "Syncing cloud saves" after Play, with no way past it in the UI.
        # A normal launch shows the game process within ~30 s, so restart the app and try again after 90 s.
        for attempt in range(1, 4):
            if not launch():
                continue
            t_launch = time.time()
            while time.time() - t_launch < 90 and not pid:
                pid = game_pid()
                if not pid:
                    keep_local_on_save_conflict()
                time.sleep(3)
            if pid:
                break
            print(f"[{time.strftime('%H:%M:%S')}] no game process 90 s after Play (attempt {attempt}); restarting GameNative",
                  flush=True)
    else:
        t_launch = time.time()
        while time.time() - t_launch < 300 and not pid:
            pid = game_pid()
            time.sleep(3)
    if not pid:
        sys.exit("no RelicCardinal.exe process after the launch attempts")
    t0 = a.t0 or time.time()
    run_start = time.strftime("%Y-%m-%d %H:%M")
    print(f"[{time.strftime('%H:%M:%S')}] pid={pid}; run started ~{run_start}", flush=True)
    if not a.t0:
        start_exit_watch()
        time.sleep(2)
        start_dialog_click()
    for bat in a.start:
        # space the requests out: one sent while winhandler was busy was lost (2026-10-07)
        time.sleep(2)
        winexec("cmd", f"/c {bat}")
    if a.quiet:
        while time.time() - t0 < a.minutes * 60 and game_pid():
            time.sleep(10)
        print(f"[{time.strftime('%H:%M:%S')}] {'process gone' if not game_pid() else 'time up'} after "
              f"{int(time.time() - t0)}s", flush=True)
        return
    new = not os.path.exists(os.path.join(a.out, "timeline.tsv"))
    tl = open(os.path.join(a.out, "timeline.tsv"), "a")
    new and tl.write("t_s\tthreads\tcpu_pct\tlog_bytes\tgrowing\tsusp\tkiller\tstep\tlast\n")
    last_size, last_ticks, last_t, tick, gone = -1, None, t0, 0, 0
    frozen_since, suspended_seen = None, False
    while time.time() - t0 < a.minutes * 60:
        tick += 1
        if not game_pid():
            gone += 1
            print(f"t={int(time.time()-t0)}s  process gone", flush=True)
            if gone >= 2:
                break
            time.sleep(5)
            continue
        with_susp = tick % 2 == 0
        ok = run_mon(with_susp)
        now = time.time()
        threads, ticks = proc_stats(pid)
        cpu = "" if last_ticks is None else f"{100.0 * (ticks - last_ticks) / 100 / (now - last_t):.0f}"
        last_ticks, last_t = ticks, now
        head, size, step, last = log_state()
        counts, killer = susp_state() if with_susp else ({}, "")
        growing = "" if last_size < 0 else ("yes" if size > last_size else "NO")
        last_size = size
        if counts.get("1", 0) > 1:
            suspended_seen = True
        frozen_since = (frozen_since or now) if growing == "NO" else None
        susp = ",".join(f"s{k}:{v}" for k, v in sorted(counts.items())) if with_susp else ""
        t = int(now - t0)
        tl.write(f"{t}\t{threads}\t{cpu}\t{size}\t{growing}\t{susp}\t{killer}\t{step}\t{last}\n")
        tl.flush()
        print(f"t={t:4d}s thr={threads:3d} cpu={cpu:>4}% log={size:7d} grow={growing:3s} {susp:14s} {killer} | {step}"
              + ("" if ok else "  (mon.bat did not finish)"), flush=True)
        if tick == 1:
            print(f"   log first line: {head}", flush=True)
        if a.stop_after_frozen and suspended_seen and frozen_since and now - frozen_since >= a.stop_after_frozen:
            print(f"t={t}s  log frozen with threads suspended; stopping", flush=True)
            break
        time.sleep(max(0, 10 - (time.time() - now)))
    run_mon(False)  # the session usually outlives the game: copy the final log, not the last sample
    for f in ("watch.txt", "susp_hist.txt", "exitcode.txt", "final_log.txt"):
        adb("pull", f"{DL}/aoe/{f}", os.path.join(a.out, f))
    if os.path.exists(os.path.join(a.out, "exitcode.txt")):
        print(open(os.path.join(a.out, "exitcode.txt")).read().rstrip(), flush=True)
    final, watch = os.path.join(a.out, "final_log.txt"), os.path.join(a.out, "watch.txt")
    if os.path.exists(final) and (not os.path.exists(watch) or os.path.getsize(final) > os.path.getsize(watch)):
        os.replace(final, watch)  # the game exited: waitexit's copy is the complete log
    print(f"[{time.strftime('%H:%M:%S')}] done after {int(time.time()-t0)}s; files in {a.out}", flush=True)


if __name__ == "__main__":
    main()
