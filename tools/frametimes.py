#!/usr/bin/env python3
"""Record the game's real frame times from Android's compositor, with no screenshots and nothing run inside Wine.

    frametimes.py [SECONDS] [--csv OUT]

`dumpsys SurfaceFlinger --latency <layer>` returns the present time of the last 128 frames of one surface. GameNative
draws the Wine X server into a SurfaceView, so that layer's frames are the game's frames. At 30 FPS, 128 frames cover
about 4 s, so this polls every 2 s and merges the windows by timestamp.

Prints FPS, the mean, median, 99th-percentile and worst frame time, and the number of stutters (frames over 50 ms and
over 100 ms) for the recorded period.
"""
import statistics
import subprocess
import sys
import time

SERIAL = "64ff2273"
LAYER_HINT = "SurfaceView[app.gamenative/"


def adb_shell(cmd):
    return subprocess.run(["adb", "-s", SERIAL, "shell", cmd], capture_output=True, text=True).stdout


def game_layer():
    for line in adb_shell("dumpsys SurfaceFlinger --list").splitlines():
        if line.startswith(LAYER_HINT) and "(BLAST)" in line:
            return line.strip()
    sys.exit("GameNative SurfaceView layer not found (is a game running?)")


def presents(layer):
    out = adb_shell(f"dumpsys SurfaceFlinger --latency '{layer}'").split()
    vals = [int(v) for v in out[1:]]
    # rows of (desired present, actual present, frame ready); pending frames show INT64_MAX
    return [vals[i + 1] for i in range(0, len(vals) - 2, 3) if 0 < vals[i + 1] < (1 << 62)]


def record(seconds, layer=None):
    layer = layer or game_layer()
    seen = set()
    end = time.time() + seconds
    while True:
        seen.update(presents(layer))
        if time.time() >= end:
            break
        time.sleep(2)
    return sorted(seen)


def summary(ts):
    if len(ts) < 3:
        return "not enough frames"
    ft = [(b - a) / 1e6 for a, b in zip(ts, ts[1:])]
    span = (ts[-1] - ts[0]) / 1e9
    q = sorted(ft)
    return (f"{len(ts)} frames in {span:.1f} s: {(len(ts) - 1) / span:.1f} FPS; frame ms mean {statistics.mean(ft):.1f} "
            f"median {statistics.median(ft):.1f} p99 {q[int(len(q) * 0.99) - 1]:.1f} max {q[-1]:.1f}; "
            f">50 ms {sum(f > 50 for f in ft)}, >100 ms {sum(f > 100 for f in ft)}")


def main():
    args = sys.argv[1:]
    out = args[args.index("--csv") + 1] if "--csv" in args else None
    nums = [a for a in args if a.replace(".", "", 1).isdigit()]
    ts = record(float(nums[0]) if nums else 20)
    print(summary(ts))
    if out:
        with open(out, "w") as f:
            f.write("present_ns,frame_ms\n")
            for a, b in zip(ts, ts[1:]):
                f.write(f"{b},{(b - a) / 1e6:.3f}\n")


if __name__ == "__main__":
    main()
