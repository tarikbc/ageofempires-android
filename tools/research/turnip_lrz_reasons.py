"""Depth render passes grouped by their LRZ disable reasons, from the raw perfetto trace. usage: turnip_lrz_reasons.py TRACE.pftrace   (needs the perfetto Python package)"""
import collections
import sys

from perfetto.protos.perfetto.trace.perfetto_trace_pb2 import Trace

t = Trace()
t.ParseFromString(open(sys.argv[1], "rb").read())
names, ev = {}, []
tmin = tmax = None
for p in t.packet:
    if p.HasField("interned_data"):
        for s in p.interned_data.gpu_specifications:
            names[s.iid] = s.name
    if p.HasField("gpu_render_stage_event"):
        e = p.gpu_render_stage_event
        k = e.stage_iid if e.HasField("stage_iid") else e.stage_id
        tmin = p.timestamp if tmin is None else min(tmin, p.timestamp)
        tmax = p.timestamp if tmax is None else max(tmax, p.timestamp)
        if names.get(k) == "Render Pass":
            ev.append((e.duration, {x.name: x.value for x in e.extra_data}))
span = (tmax - tmin) / 1e9
agg = collections.defaultdict(lambda: [0, 0])
for d, x in ev:
    if x.get("hasDepth") == "true":
        key = (x.get("lrzDisableReason", ""), x.get("lrzDisabledAtDraw", ""), x.get("lrzWriteDisableReason", ""),
               x.get("lrzWriteDisabledAtDraw", ""), x.get("forceRenderModeReason", ""), x.get("drawCount", ""))
        agg[key][0] += d
        agg[key][1] += 1
print(f"depth render passes by LRZ reasons ({span:.1f} s):")
for k, (d, c) in sorted(agg.items(), key=lambda kv: -kv[1][0])[:12]:
    print(f"  {d / 1e6 / span:7.1f} ms/s {c / span:6.1f}/s draws={k[5]:>4} disable={k[0]!r} at={k[1]} writeDisable={k[2]!r} at={k[3]} force={k[4]!r}")
d, x = max(ev, key=lambda e: e[0])
print("\nlargest pass record:", {k: (v[:70] if isinstance(v, str) else v) for k, v in x.items() if k != "binInfo"})
