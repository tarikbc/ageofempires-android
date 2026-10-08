#!/usr/bin/env python3
"""How often the game's threads block and get preempted: voluntary and involuntary context switches per second, and
CPU use, from /proc over adb (nothing runs inside the game).

    threadwaits.py [SECONDS] [--top N]

A thread that sleeps or waits (an event, a fence, a futex) gives up the CPU voluntarily; one that is pushed off by
another thread is switched involuntarily. Rates are over the interval; threads by name as in threadcpu.py.
"""
import re
import subprocess
import sys
import time

SERIAL = "64ff2273"


def sh(cmd):
    return subprocess.run(["adb", "-s", SERIAL, "shell", cmd], capture_output=True, text=True).stdout


def game_pid():
    for line in sh("ps -A -o PID,NAME").splitlines():
        if line.strip().endswith("RelicCardinal.exe"):
            return int(line.split()[0])
    sys.exit("game not running")


def snapshot(pid):
    # one adb call: per thread "tid|comm|voluntary|nonvoluntary|utime+stime"
    script = (f"for t in /proc/{pid}/task/*; do "
              f"v=$(grep -E '^(voluntary|nonvoluntary)_ctxt_switches' $t/status | tr -s '\\t ' ' ' | cut -d' ' -f2 | tr '\\n' ' '); "
              f"s=$(cat $t/stat); echo \"${{t##*/}}|$v|$s\"; done")
    out = {}
    for line in sh(script).splitlines():
        tid, sw, stat = line.split("|", 2)
        vol, invol = (int(x) for x in sw.split())
        m = re.match(r"\d+ \((.*)\) (.*)", stat)
        fields = m.group(2).split()
        out[int(tid)] = (m.group(1), vol, invol, int(fields[11]) + int(fields[12]))
    return out


def main():
    args = sys.argv[1:]
    seconds = float(args[0]) if args and not args[0].startswith("--") else 10.0
    top = int(args[args.index("--top") + 1]) if "--top" in args else 12
    hz = 100  # USER_HZ on Android
    pid = game_pid()
    a = snapshot(pid)
    t0 = time.time()
    time.sleep(seconds)
    b = snapshot(pid)
    dt = time.time() - t0
    rows = []
    for tid, (name, vol, invol, ticks) in b.items():
        if tid not in a:
            continue
        _, vol0, invol0, ticks0 = a[tid]
        rows.append(((ticks - ticks0) / hz / dt * 100, (vol - vol0) / dt, (invol - invol0) / dt, tid, name))
    rows.sort(reverse=True)
    print(f"pid {pid}, {len(b)} threads, {dt:.1f} s")
    print(f"{'tid':>7} {'cpu %':>6} {'waits/s':>8} {'preempt/s':>9}  name")
    for cpu, vol, invol, tid, name in rows[:top]:
        print(f"{tid:>7} {cpu:6.1f} {vol:8.0f} {invol:9.0f}  {name}")


if __name__ == "__main__":
    main()
