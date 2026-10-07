#!/usr/bin/env python3
"""CPU use of each thread of the running game, from /proc over adb (nothing runs inside Wine).

    threadcpu.py [SECONDS] [TOP]

Prints the busiest threads as % of one core over the window, with the Linux thread name (the game names its
threads, e.g. "Game/Main Threa", "Game/Render_thr"), the last CPU each ran on, and the GPU load from the kgsl
sysfs node over the same window.
"""
import subprocess
import sys
import time

SERIAL = "64ff2273"
HZ = 100  # USER_HZ on Android


def sh(cmd):
    return subprocess.run(["adb", "-s", SERIAL, "shell", cmd], capture_output=True, text=True).stdout


def game_pid():
    for line in sh("ps -A -o PID,NAME").splitlines():
        if line.rstrip().endswith("RelicCardinal.exe"):
            return line.split()[0]
    sys.exit("RelicCardinal.exe is not running")


def sample(pid):
    out = sh(f"cat /proc/{pid}/task/*/stat 2>/dev/null")
    res = {}
    for line in out.splitlines():
        if ")" not in line:
            continue
        head, rest = line.rsplit(")", 1)
        tid, name = head.split(" (", 1)
        f = rest.split()
        # after ')': state ppid pgrp session tty tpgid flags minflt cminflt majflt cmajflt utime stime ... processor(37)
        res[tid] = (name, int(f[11]) + int(f[12]), f[36] if len(f) > 36 else "?")
    return res


def main():
    secs = float(sys.argv[1]) if len(sys.argv) > 1 else 10
    top = int(sys.argv[2]) if len(sys.argv) > 2 else 12
    pid = game_pid()
    a, ta = sample(pid), time.time()
    gpu = []
    end = ta + secs
    while time.time() < end:
        v = sh("cat /sys/class/kgsl/kgsl-3d0/gpu_busy_percentage /sys/class/kgsl/kgsl-3d0/gpuclk").split()
        if len(v) >= 3:
            gpu.append((int(v[0]), int(v[2]) // 1000000))
        time.sleep(1)
    b, tb = sample(pid), time.time()
    dt = tb - ta
    rows = sorted(((b[t][1] - a[t][1]) / HZ / dt * 100, t, b[t][0], b[t][2]) for t in b if t in a)[::-1]
    total = sum(r[0] for r in rows)
    print(f"pid {pid}, {len(rows)} threads, {dt:.1f} s: all threads {total:.0f}% of one core")
    for pct, tid, name, cpu in rows[:top]:
        print(f"  {tid:>6} cpu{cpu:>2} {pct:5.1f}%  {name}")
    if gpu:
        print(f"GPU busy {min(g[0] for g in gpu)}-{max(g[0] for g in gpu)} % (mean {sum(g[0] for g in gpu) / len(gpu):.0f}), "
              f"clock {sorted(set(g[1] for g in gpu))} MHz")


if __name__ == "__main__":
    main()
