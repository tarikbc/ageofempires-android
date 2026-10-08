"""Analyse an atrace text capture (`atrace -t 5 sched freq sync gfx -o FILE`) (sched + dma_fence) of the game: per thread running / runnable / sleeping time, who
wakes it, scheduling latency, and GPU fence lifetimes per fence context.
usage: at_analyze.py TRACE.txt [thread-name-prefix ...]   (default: the busy AoE IV threads)"""
import collections
import re
import statistics
import sys

LINE = re.compile(r"^\s*(?P<task>.+?)-(?P<pid>\d+)\s+\(\s*(?P<tgid>[-\d]+)\)\s+\[(?P<cpu>\d+)\]\s+\S+\s+"
                  r"(?P<ts>\d+\.\d+):\s+(?P<ev>\w+):\s+(?P<args>.*)$")
LINE2 = re.compile(r"^\s*(?P<task>.+?)-(?P<pid>\d+)\s+\[(?P<cpu>\d+)\]\s+\S+\s+(?P<ts>\d+\.\d+):\s+(?P<ev>\w+):\s+(?P<args>.*)$")
SW = re.compile(r"prev_comm=(?P<pc>.*?) prev_pid=(?P<pp>\d+) prev_prio=\d+ prev_state=(?P<ps>\S+) ==> "
                r"next_comm=(?P<nc>.*?) next_pid=(?P<np>\d+)")
WK = re.compile(r"comm=(?P<c>.*?) pid=(?P<p>\d+)")
FENCE = re.compile(r"driver=(?P<d>\S+) timeline=(?P<tl>.*?) context=(?P<ctx>\d+) seqno=(?P<seq>\d+)")

prefixes = sys.argv[2:] or ["Game/Main", "Game/Render", "rcss worker 0", "Simulation", "vkd3d_queue", "RelicCardinal",
                            "AK::EventMan"]
comm = {}
state = {}      # tid -> (state, since)
acc = collections.defaultdict(lambda: collections.Counter())
lat = collections.defaultdict(list)
wakers = collections.defaultdict(collections.Counter)
first_ts = last_ts = None
fences = {}
fence_life = collections.defaultdict(list)
fence_owner = collections.defaultdict(collections.Counter)
fence_signal_ts = collections.defaultdict(list)
fence_tl = {}

for raw in open(sys.argv[1], errors="replace"):
    m = LINE.match(raw) or LINE2.match(raw)
    if not m:
        continue
    ts = float(m["ts"])
    first_ts = ts if first_ts is None else first_ts
    last_ts = ts
    ev, args = m["ev"], m["args"]
    cur = int(m["pid"])
    comm.setdefault(cur, m["task"].strip())
    if ev == "sched_switch":
        s = SW.search(args)
        if not s:
            continue
        pp, np_ = int(s["pp"]), int(s["np"])
        comm[pp] = s["pc"]
        comm[np_] = s["nc"]
        if pp in state and state[pp][0] == "run":
            acc[pp]["run"] += ts - state[pp][1]
        if pp:
            st = "runnable" if s["ps"].startswith("R") else ("sleep_D" if s["ps"].startswith("D") else "sleep")
            state[pp] = (st, ts)
        if np_ in state and state[np_][0] in ("runnable", "sleep", "sleep_D"):
            if state[np_][0] == "runnable":
                acc[np_]["runnable"] += ts - state[np_][1]
                lat[np_].append(ts - state[np_][1])
            else:
                acc[np_][state[np_][0]] += ts - state[np_][1]
        if np_:
            state[np_] = ("run", ts)
    elif ev in ("sched_waking", "sched_wakeup"):
        w = WK.search(args)
        if not w:
            continue
        p = int(w["p"])
        comm[p] = w["c"]
        if ev == "sched_waking" and p in state and state[p][0] in ("sleep", "sleep_D"):
            acc[p][state[p][0]] += ts - state[p][1]
            state[p] = ("runnable", ts)
            wakers[p][comm.get(cur, str(cur))[:15]] += 1
    elif ev.startswith("dma_fence_"):
        f = FENCE.search(args)
        if not f:
            continue
        key = (f["ctx"], f["seq"])
        fence_tl.setdefault(f["ctx"], f["tl"][:40])
        if ev == "dma_fence_init":
            fences[key] = ts
            fence_owner[f["ctx"]][(f["d"], comm.get(cur, str(cur))[:15])] += 1
        elif ev == "dma_fence_signaled":
            fence_signal_ts[f["ctx"]].append(ts)
            if key in fences:
                fence_life[f["ctx"]].append(ts - fences.pop(key))

dur = last_ts - first_ts
print(f"trace {dur:.2f} s")
print(f"{'thread':16} {'tid':>6} {'run%':>6} {'runnable%':>9} {'sleep%':>7} {'D%':>5} {'lat med/p95 ms':>15}  top wakers")
for tid, name in sorted(comm.items(), key=lambda x: -acc[x[0]]["run"]):
    if not any(name.startswith(p) for p in prefixes) or not acc[tid]:
        continue
    a = acc[tid]
    l = sorted(lat[tid])
    lm = f"{1000 * statistics.median(l):.2f}/{1000 * l[int(0.95 * (len(l) - 1))]:.2f}" if l else "-"
    top = ", ".join(f"{k} {v}" for k, v in wakers[tid].most_common(4))
    print(f"{name[:16]:16} {tid:>6} {100 * a['run'] / dur:6.1f} {100 * a['runnable'] / dur:9.1f} "
          f"{100 * a['sleep'] / dur:7.1f} {100 * a['sleep_D'] / dur:5.1f} {lm:>15}  {top}")
print("\nGPU fence contexts (signaled/s, init->signaled median/p95 ms, who creates them):")
for ctx, ls in sorted(fence_signal_ts.items(), key=lambda x: -len(x[1]))[:8]:
    life = sorted(fence_life[ctx])
    lm = f"{1000 * statistics.median(life):.2f}/{1000 * life[int(0.95 * (len(life) - 1))]:.2f}" if life else "-"
    own = ", ".join(f"{d}/{t} {n}" for (d, t), n in fence_owner[ctx].most_common(3))
    print(f"  ctx {ctx:>8} {fence_tl.get(ctx, ''):40}: {len(ls) / dur:7.1f}/s  life {lm:>13} ms  {own}")
