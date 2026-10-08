"""CPU submit to GPU execution latency per submission, from a raw Turnip perfetto trace. usage: turnip_submit_latency.py TRACE.pftrace   (needs the perfetto Python package)
Converts the GPU clock to boottime with the trace's clock snapshots, then for each vk_queue_submit (CPU, with its
submission_id) finds the GPU 'Command Buffer' stages of the same submission_id."""
import collections
import statistics
import sys

from perfetto.protos.perfetto.trace.perfetto_trace_pb2 import Trace

BOOTTIME = 6
t = Trace()
t.ParseFromString(open(sys.argv[1], "rb").read())
names = {}
snaps = []
cpu = {}  # submission_id -> (ts_begin, ts_end, tid)
gpu = collections.defaultdict(list)  # submission_id -> [(start, end)] in GPU clock
for p in t.packet:
    if p.HasField("clock_snapshot"):
        d = {c.clock_id: c.timestamp for c in p.clock_snapshot.clocks}
        g = [v for k, v in d.items() if k >= 64]
        if BOOTTIME in d and g:
            snaps.append((g[0], d[BOOTTIME]))
    if p.HasField("interned_data"):
        for s in p.interned_data.gpu_specifications:
            names[s.iid] = s.name
    if p.HasField("vulkan_api_event"):
        v = p.vulkan_api_event
        if v.HasField("vk_queue_submit"):
            q = v.vk_queue_submit
            cpu[q.submission_id] = (p.timestamp, p.timestamp + q.duration_ns, q.tid)
    if p.HasField("gpu_render_stage_event"):
        e = p.gpu_render_stage_event
        k = e.stage_iid if e.HasField("stage_iid") else e.stage_id
        if names.get(k) == "Command Buffer":
            gpu[e.submission_id].append((p.timestamp, p.timestamp + e.duration))
snaps.sort()
print(f"{len(snaps)} clock snapshots (unused: the KGSL driver reports the GPU clock equal to boottime), {len(cpu)} CPU submits, {len(gpu)} GPU submissions")
# Anchor: the GPU cannot start a submission before vkQueueSubmit returned, so the offset that makes the tightest
# submission start exactly at its submit end is a lower bound of the real offset; latencies are then lower bounds too.
pairs = [(cpu[sid][1], min(a for a, _ in gpu[sid])) for sid in cpu if sid in gpu]
offset = max(ce - gs for ce, gs in pairs)
print(f"offset gpu->boottime {offset / 1e9:.3f} s (anchor: tightest submission)")


def conv(x):
    return x + offset


rows = []
for sid, (cb, ce, tid) in sorted(cpu.items()):
    if sid not in gpu:
        continue
    gs = min(conv(a) for a, _ in gpu[sid])
    ge = max(conv(b) for _, b in gpu[sid])
    rows.append((sid, tid, cb, ce, gs, ge))
rows.sort(key=lambda r: r[2])
print(f"{len(rows)} matched submissions over {(rows[-1][2] - rows[0][2]) / 1e9:.1f} s")
by_tid = collections.Counter(r[1] for r in rows)
print("submits per tid:", dict(by_tid))
lat = [(r[4] - r[3]) / 1e6 for r in rows]  # CPU submit end -> GPU start
dur = [(r[5] - r[4]) / 1e6 for r in rows]
sub = [(r[3] - r[2]) / 1e6 for r in rows]


def q(v):
    v = sorted(v)
    return f"median {v[len(v) // 2]:.2f}  p90 {v[int(len(v) * .9)]:.2f}  max {v[-1]:.2f}  mean {statistics.mean(v):.2f} ms"


print("CPU time inside vkQueueSubmit:", q(sub))
print("submit end -> GPU start      :", q(lat))
print("GPU duration of a submission :", q(dur))
neg = sum(1 for x in lat if x < 0)
print(f"submissions whose GPU start is before the CPU submit end (clock skew or overlap): {neg}")
# gaps between consecutive submissions on the GPU and what the CPU was doing then
gaps = []
for a, b in zip(rows, rows[1:]):
    gap = (b[4] - a[5]) / 1e6
    if gap > 0.3:
        # was the next submission already submitted when the GPU went idle?
        gaps.append((gap, (b[3] - a[5]) / 1e6))
gaps.sort(reverse=True)
late = sum(1 for g, s in gaps if s > 0)
print(f"{len(gaps)} GPU gaps > 0.3 ms between consecutive submissions, total {sum(g for g, _ in gaps):.0f} ms; "
      f"in {late} of them the next submit was issued AFTER the GPU went idle (CPU-bound gap)")
print("largest gaps (gap ms, next submit issued ms after GPU idle):", [(round(g, 2), round(s, 2)) for g, s in gaps[:10]])
