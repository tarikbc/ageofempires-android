"""Frame pipeline inside long frames: the game's QueueSubmit marks, the 'GPU completion' counter, GameNative's
dequeue/queueBuffer and SurfaceFlinger's present, from the gfx marks of a mono-clock atrace.
usage: pipeline_timeline.py FRAMES.csv GFX.txt [THRESHOLD_MS] [SHOW]"""
import collections
import csv
import re
import statistics
import sys

LINE = re.compile(r"^\s*(?P<task>.+?)-(?P<tid>\d+)\s+(?:\(\s*[-\d]+\)\s+)?\[\d+\]\s+\S+\s+(?P<ts>\d+\.\d+):\s+tracing_mark_write:\s+(?P<mark>.*)$")
frames_path, gfx_path = sys.argv[1], sys.argv[2]
thr = float(sys.argv[3]) if len(sys.argv) > 3 else 40
show = int(sys.argv[4]) if len(sys.argv) > 4 else 6
frames = [(int(r["present_ns"]) / 1e9, float(r["frame_ms"])) for r in csv.DictReader(open(frames_path))]
ev = collections.defaultdict(list)  # kind -> [ts] or [(ts, value)]
for raw in open(gfx_path, errors="replace"):
    m = LINE.match(raw)
    if not m:
        continue
    ts, task, mark = float(m["ts"]), m["task"].strip(), m["mark"]
    if mark.startswith("B|") and mark.endswith("|QueueSubmit") and task.startswith("vkd3d_queue"):
        ev["submit"].append(ts)
    elif mark.startswith("C|") and "GPU completion" in mark:
        ev["gpu_done"].append((ts, int(mark.rsplit("|", 1)[1])))
    elif mark.endswith("|queueBuffer") and task.startswith("pool-"):
        ev["queue"].append(ts)
    elif mark.endswith("|dequeueBuffer") and task.startswith("pool-"):
        ev["dequeue"].append(ts)
    elif mark.endswith("|present") and not task.startswith("pool-"):
        ev["sf_present"].append(ts)
for k in ev:
    ev[k].sort()
t_lo = min(v[0] if isinstance(v[0], float) else v[0][0] for v in ev.values() if v)
t_hi = max(v[-1] if isinstance(v[-1], float) else v[-1][0] for v in ev.values() if v)
print(f"marks: " + ", ".join(f"{k} {len(v)}" for k, v in ev.items()) + f"; trace {t_lo:.3f}..{t_hi:.3f}")


def within(kind, a, b):
    xs = ev[kind]
    if xs and isinstance(xs[0], tuple):
        return [x for x in xs if a <= x[0] <= b]
    return [x for x in xs if a <= x <= b]


def gaps(xs):
    return [b - a for a, b in zip(xs, xs[1:])]


inside = [(t, ms) for t, ms in frames if t_lo <= t - ms / 1e3 and t <= t_hi]
long_f = [(t, ms) for t, ms in inside if ms > thr]
norm_f = [(t, ms) for t, ms in inside if ms <= 20]
print(f"{len(long_f)} long frames, {len(norm_f)} normal frames inside the trace\n")
for t, ms in long_f[:show]:
    f0, f1 = t - ms / 1e3, t
    print(f"== frame ending {t:.3f}: {ms:.0f} ms  (window {f0 - 0.005:.3f} .. {f1 + 0.003:.3f})")
    rows = []
    for kind, label in (("submit", "QueueSubmit(game)"), ("gpu_done", "GPU completion"), ("dequeue", "GN dequeueBuffer"), ("queue", "GN queueBuffer"), ("sf_present", "SF present")):
        for x in within(kind, f0 - 0.005, f1 + 0.003):
            ts = x[0] if isinstance(x, tuple) else x
            val = f"={x[1]}" if isinstance(x, tuple) else ""
            rows.append((ts, f"{label}{val}"))
    rows.sort()
    last = None
    for ts, label in rows:
        gap = f" (+{(ts - last) * 1e3:5.1f} ms)" if last is not None else ""
        print(f"   {(ts - f0) * 1e3:7.1f} ms  {label}{gap}")
        last = ts
# statistics: submits per frame and the largest submit gap, long against normal
for label, fl in (("long", long_f), ("normal", norm_f[:400])):
    n_sub = [len(within("submit", t - ms / 1e3, t)) for t, ms in fl]
    big_gap = [max(gaps(within("submit", t - ms / 1e3, t)), default=0) * 1e3 for t, ms in fl]
    n_q = [len(within("queue", t - ms / 1e3, t)) for t, ms in fl]
    print(f"\n{label:6}: game QueueSubmits per frame median {statistics.median(n_sub):.0f} (min {min(n_sub)}, max {max(n_sub)}); "
          f"largest gap between submits median {statistics.median(big_gap):.1f} ms, p90 {sorted(big_gap)[int(len(big_gap) * .9) - 1]:.1f}; "
          f"GameNative queueBuffers per frame median {statistics.median(n_q):.0f}")
