"""Split GameNative's present latency into stages from a mono-clock atrace:
game present (QueueSubmit mark of the game's queue thread) -> RequestHandler wakes the compositor thread
-> compositor thread runs -> its queueBuffer mark. usage: gn_chain.py TRACE.txt GAME_PID COMPOSITOR_TID"""
import bisect
import re
import sys

path, game_pid, comp_tid = sys.argv[1], sys.argv[2], sys.argv[3]
LINE = re.compile(r"^\s*(?P<task>.+?)-(?P<tid>\d+)\s+(?:\(\s*[-\d]+\)\s+)?\[(?P<cpu>\d+)\]\s+\S+\s+(?P<ts>\d+\.\d+):\s+(?P<ev>\w+):\s+(?P<args>.*)$")
presents, wakes, runs, queues = [], [], [], []
rh_runs = []  # RequestHandler switch-ins
rh_tid = None
for raw in open(path, errors="replace"):
    if comp_tid not in raw and f"B|{game_pid}|QueueSubmit" not in raw and "RequestHandler" not in raw:
        continue
    m = LINE.match(raw)
    if not m:
        continue
    ts, ev, args, task = float(m["ts"]), m["ev"], m["args"], m["task"].strip()
    if ev == "tracing_mark_write":
        if args.startswith(f"B|{game_pid}|QueueSubmit") and task.startswith(("vkd3d_queue", "<...>")):
            presents.append(ts)
        elif m["tid"] == comp_tid and args.endswith("|queueBuffer") and args.startswith("B|"):
            queues.append(ts)
    elif ev == "sched_waking" and f"pid={comp_tid} " in args:
        wakes.append((ts, task))
    elif ev == "sched_switch" and f"next_pid={comp_tid}" in args:
        runs.append(ts)
    elif ev == "sched_switch" and "next_comm=RequestHandler" in args:
        rh_runs.append(ts)
    elif ev == "sched_waking" and "comm=RequestHandler" in args:
        wakes.append((ts, "->RH:" + task))


def dedupe(xs, eps=0.002):
    out = []
    for x in sorted(xs):
        if not out or x - out[-1] > eps:
            out.append(x)
    return out


presents = dedupe(presents)
queues = dedupe(queues)
wk_comp = sorted(t for t, w in wakes if not w.startswith("->RH:") and w.startswith("RequestHandler"))
wk_rh = sorted((t, w[5:]) for t, w in wakes if w.startswith("->RH:"))
runs.sort()
rh_runs.sort()
print(f"{len(presents)} game presents, {len(queues)} queueBuffers, {len(wk_comp)} RequestHandler->compositor wakes, "
      f"{len(wk_rh)} wakes of RequestHandler")
from collections import Counter
print("who wakes RequestHandler:", Counter(w for _, w in wk_rh).most_common(6))
rows = []
for q in queues:
    i = bisect.bisect_right(presents, q) - 1
    if i < 0:
        continue
    p = presents[i]
    if q - p > 0.1:
        continue
    j = bisect.bisect_left(wk_rh and [t for t, _ in wk_rh] or [], p)
    t_rh_wake = wk_rh[j][0] if j < len(wk_rh) and wk_rh[j][0] <= q else None
    k = bisect.bisect_left(rh_runs, t_rh_wake) if t_rh_wake else None
    t_rh_run = rh_runs[k] if k is not None and k < len(rh_runs) and rh_runs[k] <= q else None
    a = bisect.bisect_left(wk_comp, p)
    t_wake = wk_comp[a] if a < len(wk_comp) and wk_comp[a] <= q else None
    b = bisect.bisect_left(runs, t_wake) if t_wake else None
    t_run = runs[b] if b is not None and b < len(runs) and runs[b] <= q else None
    rows.append((p, t_rh_wake, t_rh_run, t_wake, t_run, q))


def stats(vals, label):
    v = sorted(x * 1e3 for x in vals if x is not None)
    if not v:
        print(f"  {label:42} n=0")
        return
    print(f"  {label:42} n={len(v):5}  median {v[len(v) // 2]:6.2f}  p90 {v[int(len(v) * .9)]:6.2f}  p99 {v[int(len(v) * .99)]:6.2f}  max {v[-1]:6.2f} ms")


print("stage latencies:")
stats([r[1] - r[0] if r[1] else None for r in rows], "present -> RequestHandler woken")
stats([r[2] - r[1] if r[1] and r[2] else None for r in rows], "RequestHandler woken -> runs")
stats([r[3] - r[0] if r[3] else None for r in rows], "present -> compositor woken (by RH)")
stats([r[4] - r[3] if r[3] and r[4] else None for r in rows], "compositor woken -> runs")
stats([r[5] - r[4] if r[4] else None for r in rows], "compositor runs -> queueBuffer")
stats([r[5] - r[0] for r in rows], "present -> queueBuffer (total)")
slow = [r for r in rows if r[5] - r[0] > 0.012]
print(f"\n{len(slow)} frames with present -> queueBuffer > 12 ms; their stage split (ms):")
for r in slow[:14]:
    p, a, b, c, d, q = r
    f = lambda x, y: f"{(y - x) * 1e3:5.1f}" if x is not None and y is not None else "  -  "
    print(f"   RHwake {f(p, a)}  RHrun {f(a, b)}  compWake {f(b if b else p, c)}  compRun {f(c, d)}  toQueue {f(d, q)}  total {(q - p) * 1e3:5.1f}")
