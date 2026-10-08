"""Where a long display frame comes from: the game's present cadence (its QueueSubmit pairs), GameNative's
queueBuffer cadence and the latency between them, from the gfx marks; dequeueBuffer durations from the compositor
thread's B/E marks when PRES.txt is given.
usage: present_chain.py FRAMES.csv GFX.txt [PRES.txt] [THRESHOLD_MS]"""
import collections
import csv
import re
import statistics
import sys

LINE = re.compile(r"^\s*(?P<task>.+?)-(?P<tid>\d+)\s+(?:\(\s*[-\d]+\)\s+)?\[\d+\]\s+\S+\s+(?P<ts>\d+\.\d+):\s+tracing_mark_write:\s+(?P<mark>.*)$")
frames_path, gfx_path = sys.argv[1], sys.argv[2]
pres_path = sys.argv[3] if len(sys.argv) > 3 and not sys.argv[3].replace(".", "").isdigit() else None
thr = float(sys.argv[-1]) if sys.argv[-1].replace(".", "").isdigit() else 40
frames = [(int(r["present_ns"]) / 1e9, float(r["frame_ms"])) for r in csv.DictReader(open(frames_path))]
submits, queues = [], []
for raw in open(gfx_path, errors="replace"):
    m = LINE.match(raw)
    if not m:
        continue
    ts, task, mark = float(m["ts"]), m["task"].strip(), m["mark"]
    if mark.endswith("|QueueSubmit") and task.startswith("vkd3d_queue"):
        submits.append(ts)
    elif mark.endswith("|queueBuffer") and task.startswith("pool-"):
        queues.append(ts)


def dedupe(xs, eps=0.002):
    out = []
    for x in sorted(xs):
        if not out or x - out[-1] > eps:
            out.append(x)
    return out


presents = dedupe(submits)
queues = dedupe(queues)
t_lo, t_hi = presents[0], presents[-1]
print(f"game presents {len(presents)} ({len(presents) / (t_hi - t_lo):.1f}/s), GameNative queueBuffers {len(queues)} ({len(queues) / (t_hi - t_lo):.1f}/s)")


def hist(gaps):
    c = collections.Counter(min(round(g * 1e3 / 8.333), 8) for g in gaps)
    return " ".join(f"{k}:{v}" for k, v in sorted(c.items()))


pg = [b - a for a, b in zip(presents, presents[1:])]
qg = [b - a for a, b in zip(queues, queues[1:])]
inside = [(t, ms) for t, ms in frames if t_lo <= t - ms / 1e3 and t <= t_hi]
print("gaps in refresh periods (8.33 ms), count per bucket:")
print(f"   game presents       : {hist(pg)}   (>40 ms: {sum(g > 0.040 for g in pg)})")
print(f"   GameNative queues   : {hist(qg)}   (>40 ms: {sum(g > 0.040 for g in qg)})")
print(f"   display frames (SF) : {hist([ms / 1e3 for _, ms in inside])}   (>40 ms: {sum(ms > 40 for _, ms in inside)})")
# latency: each GameNative queueBuffer against the latest game present before it
lat = []
j = 0
for q in queues:
    while j + 1 < len(presents) and presents[j + 1] <= q:
        j += 1
    if presents[j] <= q:
        lat.append((q - presents[j]) * 1e3)
lat_s = sorted(lat)
print(f"latency game present -> GameNative queueBuffer: median {lat_s[len(lat_s) // 2]:.1f} ms, p90 {lat_s[int(len(lat_s) * .9)]:.1f}, max {lat_s[-1]:.1f}")
# for every long display frame: the game's present gaps and GameNative's queue gaps that overlap it
print(f"\nlong display frames (> {thr:.0f} ms) and what the game and GameNative did inside them:")
for t, ms in inside:
    if ms <= thr:
        continue
    f0 = t - ms / 1e3
    p_in = [p for p in presents if f0 - 0.03 <= p <= t]
    q_in = [q for q in queues if f0 - 0.03 <= q <= t]
    pgaps = " ".join(f"{(b - a) * 1e3:.0f}" for a, b in zip(p_in, p_in[1:]))
    qgaps = " ".join(f"{(b - a) * 1e3:.0f}" for a, b in zip(q_in, q_in[1:]))
    print(f"   {ms:3.0f} ms frame: game present gaps [{pgaps}] ms, GameNative queue gaps [{qgaps}] ms")
if pres_path:
    # dequeueBuffer / queueBuffer durations on the compositor thread (B ... E on the same thread)
    stack = collections.defaultdict(list)
    durs = collections.defaultdict(list)
    for raw in open(pres_path, errors="replace"):
        m = LINE.match(raw)
        if not m or not m["task"].strip().startswith("pool-"):
            continue
        ts, mark = float(m["ts"]), m["mark"]
        tid = m["tid"]
        if mark.startswith("B|"):
            stack[tid].append((mark.rsplit("|", 1)[1], ts))
        elif mark.startswith("E|") and stack[tid]:
            name, t0 = stack[tid].pop()
            durs[name].append((ts - t0) * 1e3)
    print("\ncompositor thread mark durations (ms): median / p90 / max")
    for name, v in sorted(durs.items(), key=lambda kv: -len(kv[1]))[:8]:
        v.sort()
        print(f"   {name:22} n={len(v):5}  {v[len(v) // 2]:6.2f} / {v[int(len(v) * .9)]:6.2f} / {v[-1]:6.2f}")
