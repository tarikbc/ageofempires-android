"""Decode Turnip GPU render-stage events straight from a perfetto trace file (no clock sync needed): GPU time per
stage name, per frame, and the per-render-pass extra data (size, draws, tiled, lrz reasons).
usage: turnip_stages.py TRACE.pftrace [TOP]   (needs the perfetto Python package)"""
import collections
import sys

from perfetto.protos.perfetto.trace.perfetto_trace_pb2 import Trace

path = sys.argv[1]
top = int(sys.argv[2]) if len(sys.argv) > 2 else 15
t = Trace()
t.ParseFromString(open(path, "rb").read())

stage_names = {}      # iid -> name (from interned_data.gpu_specifications)
events = []
ts_min = ts_max = None
for p in t.packet:
    if p.HasField("interned_data"):
        for spec in p.interned_data.gpu_specifications:
            stage_names[spec.iid] = spec.name
    if p.HasField("gpu_render_stage_event"):
        e = p.gpu_render_stage_event
        if e.specifications.stage:
            for i, s in enumerate(e.specifications.stage):
                stage_names.setdefault(i, s.name)
        key = e.stage_iid if e.HasField("stage_iid") else e.stage_id
        extra = {x.name: x.value for x in e.extra_data}
        events.append((e.event_id, key, e.duration, p.timestamp, extra))
        ts_min = p.timestamp if ts_min is None else min(ts_min, p.timestamp)
        ts_max = p.timestamp + e.duration if ts_max is None else max(ts_max, p.timestamp + e.duration)
span = (ts_max - ts_min) / 1e9 if events else 0
print(f"{len(events)} stage events over {span:.2f} s; {len(stage_names)} stage names")
tot = collections.Counter()
cnt = collections.Counter()
for _, k, d, _, _ in events:
    tot[k] += d
    cnt[k] += 1
print(f"\n{'stage':34} {'gpu ms/s':>9} {'count/s':>8}")
for k, v in tot.most_common(top):
    print(f"  {stage_names.get(k, str(k)):32} {v / 1e6 / span:9.1f} {cnt[k] / span:8.1f}")

# render passes: group by their extra data
rp = [(d, x) for _, k, d, _, x in events if "Render Pass" in stage_names.get(k, "")]
if rp:
    agg = collections.defaultdict(lambda: [0, 0])
    for d, x in rp:
        key = (x.get("width", "?") + "x" + x.get("height", "?"), x.get("attachment_count", "?"), x.get("tiledRender", "?"),
               x.get("lrz", "?"), x.get("lrzDisableReason", "")[:36], x.get("drawCount", "?"))
        agg[key][0] += d
        agg[key][1] += 1
    print(f"\nrender passes: {len(rp) / span:.0f}/s; by (size, attachments, tiled, lrz, lrz disable reason, draws):")
    print(f"  {'size':11} {'att':>3} {'tiled':>5} {'lrz':>5} {'draws':>5} {'gpu ms/s':>9} {'n/s':>6}  lrz disable reason")
    for k, (d, c) in sorted(agg.items(), key=lambda kv: -kv[1][0])[:top]:
        print(f"  {k[0]:11} {k[1]:>3} {k[2]:>5} {k[3]:>5} {k[5]:>5} {d / 1e6 / span:9.1f} {c / span:6.1f}  {k[4]}")
    keys = collections.Counter()
    for _, x in rp:
        keys.update(x.keys())
    print("\nextra data keys seen on render passes:", ", ".join(sorted(keys)))
