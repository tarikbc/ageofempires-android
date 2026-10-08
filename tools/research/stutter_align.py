"""Line the long frames of a bench window up with an atrace capture taken on the mono clock.
usage: stutter_align.py FRAMES.csv TRACE.txt [THRESHOLD_MS] [OFFSET_S]
For every frame longer than THRESHOLD_MS (default 40) inside the trace: per game thread the run / runnable / sleep
time in that frame, the longest sleep of the main and render threads with the thread that ended it, the other tasks
that ran on the big cores (4 to 7) meanwhile, and CPU frequency changes. OFFSET_S is added to trace timestamps when
the clocks differ (default 0)."""
import collections
import csv
import re
import sys

LINE = re.compile(r"^\s*(?P<task>.+?)-(?P<pid>\d+)\s+(?:\(\s*[-\d]+\)\s+)?\[(?P<cpu>\d+)\]\s+\S+\s+(?P<ts>\d+\.\d+):\s+(?P<ev>\w+):\s+(?P<args>.*)$")
SW = re.compile(r"prev_comm=(?P<pc>.*?) prev_pid=(?P<pp>\d+) prev_prio=\d+ prev_state=(?P<ps>\S+) ==> next_comm=(?P<nc>.*?) next_pid=(?P<np>\d+)")
WK = re.compile(r"comm=(?P<c>.*?) pid=(?P<p>\d+)")
FQ = re.compile(r"state=(?P<f>\d+) cpu_id=(?P<c>\d+)")

frames_path, trace_path = sys.argv[1], sys.argv[2]
thr = float(sys.argv[3]) if len(sys.argv) > 3 else 40
off = float(sys.argv[4]) if len(sys.argv) > 4 else 0.0

rows = list(csv.DictReader(open(frames_path)))
frames = [(int(r["present_ns"]) / 1e9, float(r["frame_ms"])) for r in rows]

# parse the trace into per-cpu run segments, per-thread state segments, wakeups and freq events
events = []  # (ts, kind, data)
comm = {}
for raw in open(trace_path, errors="replace"):
    m = LINE.match(raw)
    if not m:
        continue
    ts = float(m["ts"]) + off
    ev, args, cpu = m["ev"], m["args"], int(m["cpu"])
    if ev == "sched_switch":
        s = SW.search(args)
        if s:
            comm[int(s["pp"])] = s["pc"]
            comm[int(s["np"])] = s["nc"]
            events.append((ts, "sw", (cpu, int(s["pp"]), s["ps"], int(s["np"]))))
    elif ev in ("sched_waking", "sched_wakeup"):
        w = WK.search(args)
        if w:
            events.append((ts, "wk", (int(m["pid"]), int(w["p"]))))  # waker tid, woken tid
            comm.setdefault(int(w["p"]), w["c"])
    elif ev == "cpu_frequency":
        f = FQ.search(args)
        if f:
            events.append((ts, "fq", (int(f["c"]), int(f["f"]))))
events.sort(key=lambda e: e[0])
if not events:
    sys.exit("no events parsed")
t_lo, t_hi = events[0][0], events[-1][0]
print(f"trace {t_lo:.3f} .. {t_hi:.3f} ({t_hi - t_lo:.1f} s), frames {frames[0][0]:.3f} .. {frames[-1][0]:.3f}")

game_tids = {tid for tid, name in comm.items() if any(k in name for k in ("Game/", "vkd3d", "rcss", "RelicCardinal", "Simulation", "wine", "AK::"))}
# the game's threads are in the same process; find the tgid-free way: collect by names only (above)

# build per-thread state timeline: list of (start, end, state, cpu, waker)
state = {}
segs = collections.defaultdict(list)  # tid -> [(start, end, state, cpu)]
last_waker = {}
cpu_run = collections.defaultdict(list)  # cpu -> [(start, end, tid)]
cpu_cur = {}
freq = []  # (ts, cpu, khz)
for ts, kind, d in events:
    if kind == "sw":
        cpu, prev, ps, nxt = d
        if prev in state:
            st, since, c0 = state[prev]
            segs[prev].append((since, ts, st, c0, last_waker.get(prev)))
        if prev:
            state[prev] = ("runnable" if ps.startswith("R") else "sleep", ts, cpu)
        if nxt in state:
            st, since, c0 = state[nxt]
            segs[nxt].append((since, ts, st, c0, last_waker.get(nxt)))
        state[nxt] = ("run", ts, cpu)
        if cpu in cpu_cur:
            s0, t0 = cpu_cur[cpu]
            cpu_run[cpu].append((s0, ts, t0))
        cpu_cur[cpu] = (ts, nxt)
    elif kind == "wk":
        last_waker[d[1]] = d[0]
    elif kind == "fq":
        freq.append((ts, d[0], d[1]))


def overlap(a0, a1, b0, b1):
    return max(0.0, min(a1, b1) - max(a0, b0))


def thread_summary(tid, f0, f1):
    acc = collections.Counter()
    longest = (0, None, None)
    cpus = collections.Counter()
    for s0, s1, st, c, wk in segs[tid]:
        o = overlap(s0, s1, f0, f1)
        if o <= 0:
            continue
        acc[st] += o
        if st == "run":
            cpus[c] += o
        if st == "sleep" and o > longest[0]:
            longest = (o, wk, s1)
    acc["cpus"] = " ".join(f"cpu{c}:{v * 1e3:.0f}" for c, v in cpus.most_common(3))
    return acc, longest


def freq_at(ts, cpu):
    f = None
    for t, c, k in freq:
        if t > ts:
            break
        if c == cpu:
            f = k
    return f


def name(tid):
    return comm.get(tid, "?")


watch = {}
for tid, nm in comm.items():
    if nm.startswith("Game/Main"):
        watch.setdefault("main", tid)
    elif nm.startswith("Game/Render"):
        watch.setdefault("render", tid)
    elif nm.startswith("vkd3d_queue"):
        watch.setdefault("vkd3d_q", tid)
    elif nm.startswith("vkd3d_fence"):
        watch.setdefault("vkd3d_f", tid)
    elif nm.startswith("vkd3d-swapchain"):
        watch.setdefault("swapchn", tid)
    elif nm.startswith("WSI swapchain"):
        watch.setdefault("wsi_q", tid)
    elif nm == "surfaceflinger":
        watch.setdefault("sflinger", tid)
print("watched threads:", {k: f"{name(v)}[{v}]" for k, v in watch.items()})

long_frames = [(t, ms) for t, ms in frames if ms > thr and t_lo <= t - ms / 1e3 and t <= t_hi]
print(f"{len(long_frames)} frames over {thr:.0f} ms inside the trace\n")
for t, ms in long_frames:
    f0, f1 = t - ms / 1e3, t
    print(f"== frame ending {t:.3f}: {ms:.0f} ms")
    for key, tid in watch.items():
        acc, longest = thread_summary(tid, f0, f1)
        line = f"   {key:8} run {acc['run'] * 1e3:5.1f}  runnable {acc['runnable'] * 1e3:5.1f}  sleep {acc['sleep'] * 1e3:5.1f} ms"
        if longest[0] > 0.004:
            line += f"   longest sleep {longest[0] * 1e3:.1f} ms, woken by {name(longest[1]) if longest[1] else '?'}"
        print(line + f"   on {acc['cpus']}")
    fs = {c: freq_at(f0, c) for c in (3, 7)}
    print("   clocks at frame start: " + ", ".join(f"cpu{c} {v // 1000 if v else '?'} MHz" for c, v in fs.items()))
    others = collections.Counter()
    for cpu in range(4, 8):
        for s0, s1, tid in cpu_run[cpu]:
            o = overlap(s0, s1, f0, f1)
            if o > 0 and tid not in game_tids and tid != 0:
                others[name(tid)] += o
    if others:
        print("   other tasks on big cores:", ", ".join(f"{n} {v * 1e3:.1f} ms" for n, v in others.most_common(5)))
    fq = [(ts, c, k) for ts, c, k in freq if f0 <= ts <= f1]
    if fq:
        print("   freq changes:", ", ".join(f"cpu{c} {k // 1000} MHz" for _, c, k in fq[:8]))
# which threads of the game add work in the long frames: run ms per frame, long against normal
norm = [(t, ms) for t, ms in frames if ms <= 20 and t_lo <= t - ms / 1e3 and t <= t_hi]
if long_frames and norm:
    def run_per_frame(frame_list):
        acc = collections.Counter()
        for t, ms in frame_list:
            f0, f1 = t - ms / 1e3, t
            for tid in game_tids:
                for s0, s1, st, c, wk in segs[tid]:
                    if st == "run":
                        o = overlap(s0, s1, f0, f1)
                        if o > 0:
                            acc[name(tid)] += o
        return {k: v * 1e3 / len(frame_list) for k, v in acc.items()}
    long_run = run_per_frame(long_frames)
    norm_run = run_per_frame(norm[:300])
    print("\ngame threads by run ms per frame, long frames against normal frames (sorted by the difference):")
    for nm, v in sorted(long_run.items(), key=lambda kv: -(kv[1] - norm_run.get(kv[0], 0)))[:14]:
        print(f"   {nm:18} long {v:5.1f} ms   normal {norm_run.get(nm, 0):5.1f} ms   +{v - norm_run.get(nm, 0):5.1f}")

if norm:
    tot = collections.defaultdict(collections.Counter)
    span = 0
    for t, ms in norm[:300]:
        span += ms / 1e3
        for key, tid in watch.items():
            acc, _ = thread_summary(tid, t - ms / 1e3, t)
            tot[key].update(acc)
    print(f"\nfor comparison, {min(300, len(norm))} normal frames (<= 20 ms), per-thread share of the frame:")
    for key in watch:
        a = tot[key]
        print(f"   {key:8} run {100 * a['run'] / span:5.1f} %  runnable {100 * a['runnable'] / span:5.1f} %  sleep {100 * a['sleep'] / span:5.1f} %")
