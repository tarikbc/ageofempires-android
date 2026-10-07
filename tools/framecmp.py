#!/usr/bin/env python3
"""Compare frame times before and after an event in a fpsgraph/frametimes CSV.

    framecmp.py CSV [--gap SECONDS]

The event is found as the longest frame gap over --gap seconds (default 1.5): GameNative's Quick Menu pauses the game,
so a setting changed there leaves a gap in the record. Frames within 5 s of the gap are skipped on both sides.
"""
import statistics
import sys


def stats(ft):
    q = sorted(ft)
    span = sum(ft) / 1000
    return (f"{len(ft) / span:5.1f} FPS over {span:5.0f} s; median {statistics.median(ft):5.1f} ms, "
            f"p99 {q[int(len(q) * 0.99) - 1]:6.1f} ms; >50 ms {sum(f > 50 for f in ft) / span:4.2f}/s, "
            f">100 ms {sum(f > 100 for f in ft) / span:4.2f}/s")


def main():
    path = sys.argv[1]
    gap = float(sys.argv[sys.argv.index("--gap") + 1]) if "--gap" in sys.argv else 1.5
    rows = [line.split(",") for line in open(path).read().splitlines()[1:] if line]
    ts = [int(r[0]) for r in rows]
    ft = [float(r[1]) for r in rows]
    gaps = [i for i, f in enumerate(ft) if f > gap * 1000]
    if not gaps:
        sys.exit("no gap found")
    g = max(gaps, key=lambda i: ft[i])
    before = [f for t, f in zip(ts, ft) if t < ts[g] - ft[g] * 1e6 - 5e9 and f <= gap * 1000]
    after = [f for t, f in zip(ts, ft) if t > ts[g] + 5e9 and f <= gap * 1000]
    print(f"event: {ft[g] / 1000:.1f} s gap")
    print("before:", stats(before))
    print("after: ", stats(after))


if __name__ == "__main__":
    main()
