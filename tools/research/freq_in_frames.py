"""Time-weighted CPU clock inside long frames against normal frames, from a mono-clock atrace and a frames CSV.
usage: freq_in_frames.py FRAMES.csv TRACE.txt [THRESHOLD_MS]"""
import collections
import csv
import re
import sys

LINE = re.compile(r"^\s*.+?-\d+\s+(?:\(\s*[-\d]+\)\s+)?\[\d+\]\s+\S+\s+(?P<ts>\d+\.\d+):\s+cpu_frequency:\s+state=(?P<f>\d+) cpu_id=(?P<c>\d+)")
frames_path, trace_path = sys.argv[1], sys.argv[2]
thr = float(sys.argv[3]) if len(sys.argv) > 3 else 40
frames = [(int(r["present_ns"]) / 1e9, float(r["frame_ms"])) for r in csv.DictReader(open(frames_path))]
ev = collections.defaultdict(list)  # cpu -> [(ts, khz)]
t_lo = t_hi = None
for raw in open(trace_path, errors="replace"):
    if "cpu_frequency" not in raw:
        if t_lo is None:
            m = re.search(r"\s(\d+\.\d+):", raw)
            if m:
                t_lo = float(m.group(1))
        continue
    m = LINE.match(raw)
    if m:
        ts = float(m["ts"])
        ev[int(m["c"])].append((ts, int(m["f"])))
        t_lo = ts if t_lo is None else min(t_lo, ts)
        t_hi = ts if t_hi is None else max(t_hi, ts)
for c in ev:
    ev[c].sort()


def mean_freq(cpu, f0, f1):
    pts = ev[cpu]
    cur = None
    i = 0
    while i < len(pts) and pts[i][0] <= f0:
        cur = pts[i][1]
        i += 1
    acc, t = 0.0, f0
    while i < len(pts) and pts[i][0] < f1:
        if cur is not None:
            acc += cur * (pts[i][0] - t)
        t, cur = pts[i]
        i += 1
    if cur is not None:
        acc += cur * (f1 - t)
    return acc / (f1 - f0) / 1000 if cur is not None else None


def frame_classes():
    inside = [(t, ms) for t, ms in frames if t_lo <= t - ms / 1e3 and t <= t_hi]
    return ([(t, ms) for t, ms in inside if ms > thr], [(t, ms) for t, ms in inside if ms <= 20], [(t, ms) for t, ms in inside if 20 < ms <= thr])


long_f, norm_f, mid_f = frame_classes()
print(f"frames inside the trace: {len(long_f)} over {thr:.0f} ms, {len(mid_f)} between 20 and {thr:.0f} ms, {len(norm_f)} up to 20 ms")
for cpu in (0, 3, 7):
    row = []
    for label, fl in (("long", long_f), ("mid", mid_f), ("normal", norm_f)):
        vals = [v for v in (mean_freq(cpu, t - ms / 1e3, t) for t, ms in fl) if v is not None]
        if vals:
            vals.sort()
            row.append(f"{label}: mean {sum(vals) / len(vals):.0f} MHz, p10 {vals[len(vals) // 10]:.0f}, min {vals[0]:.0f} (n={len(vals)})")
    print(f"cpu{cpu}: " + " | ".join(row))
# how often each class saw the prime core below 2 GHz for part of the frame
for label, fl in (("long", long_f), ("normal", norm_f)):
    low = sum(1 for t, ms in fl if any(f0 <= ts <= t for ts, k in ev[7] if k < 2000000 for f0 in [t - ms / 1e3]))
    print(f"{label}: frames with a cpu7 clock change to below 2 GHz inside them: {low} of {len(fl)}")
