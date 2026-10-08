"""GPU busy time and idle gaps from the raw Turnip render-stage events. usage: turnip_gpu_timeline.py TRACE.pftrace [STAGE]   (needs the perfetto Python package)
Merges the intervals of one stage (default "Command Buffer"), prints busy share per second, the gap histogram and
the longest gaps, which show whether the GPU waits for the CPU."""
import collections
import sys

from perfetto.protos.perfetto.trace.perfetto_trace_pb2 import Trace

stage_name = sys.argv[2] if len(sys.argv) > 2 else "Command Buffer"
t = Trace()
t.ParseFromString(open(sys.argv[1], "rb").read())
names, iv = {}, []
clocks = collections.Counter()
for p in t.packet:
    if p.HasField("interned_data"):
        for s in p.interned_data.gpu_specifications:
            names[s.iid] = s.name
    if p.HasField("clock_snapshot"):
        for c in p.clock_snapshot.clocks:
            clocks[c.clock_id] += 1
    if p.HasField("gpu_render_stage_event"):
        e = p.gpu_render_stage_event
        k = e.stage_iid if e.HasField("stage_iid") else e.stage_id
        if names.get(k) == stage_name:
            iv.append((p.timestamp, p.timestamp + e.duration, e.hw_queue_iid if e.HasField("hw_queue_iid") else e.hw_queue_id))
iv.sort()
print(f"{len(iv)} {stage_name!r} events, clock ids in snapshots: {dict(clocks)}")
print("hw queues:", collections.Counter(q for _, _, q in iv))
merged = []
for a, b, _ in iv:
    if merged and a <= merged[-1][1]:
        merged[-1][1] = max(merged[-1][1], b)
    else:
        merged.append([a, b])
t0, t1 = iv[0][0], max(b for _, b, _ in iv)
span = (t1 - t0) / 1e9
busy = sum(b - a for a, b in merged) / 1e9
print(f"span {span:.2f} s, GPU busy {busy / span * 100:.1f} % (union of intervals), {len(merged)} busy blocks")
sec = collections.defaultdict(float)
for a, b in merged:
    s = int((a - t0) / 1e9)
    sec[s] += (b - a) / 1e9
print("busy per second:", " ".join(f"{sec[s] * 100:.0f}%" for s in sorted(sec)))
gaps = [(merged[i + 1][0] - merged[i][1]) / 1e6 for i in range(len(merged) - 1)]
hist = collections.Counter()
for g in gaps:
    hist["<0.1ms" if g < 0.1 else "<0.5ms" if g < 0.5 else "<1ms" if g < 1 else "<2ms" if g < 2 else "<5ms" if g < 5 else "<10ms" if g < 10 else ">=10ms"] += 1
print("gap histogram:", dict(hist), f"total gap {sum(gaps):.0f} ms")
big = sorted(((g, i) for i, g in enumerate(gaps)), reverse=True)[:8]
print("longest gaps (ms at s):", [(round(g, 2), round((merged[i][1] - t0) / 1e9, 2)) for g, i in big])
blocks = sorted(((b - a) / 1e6 for a, b in merged), reverse=True)
print("busy block lengths ms: max", [round(x, 2) for x in blocks[:6]], "median", round(blocks[len(blocks) // 2], 3))
