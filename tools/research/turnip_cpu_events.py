"""Mesa track events (CPU side) per thread from a raw perfetto trace. usage: turnip_cpu_events.py TRACE.pftrace [THREAD_FILTER]   (needs the perfetto Python package)
Interned names and default tracks are scoped by trusted_packet_sequence_id; only events of threads matching FILTER
(default: the whole trace) are counted."""
import collections
import sys

from perfetto.protos.perfetto.trace.perfetto_trace_pb2 import Trace, TrackEvent

flt = sys.argv[2] if len(sys.argv) > 2 else ""
t = Trace()
t.ParseFromString(open(sys.argv[1], "rb").read())
tracks, names, defaults = {}, {}, {}
count = collections.Counter()
dur = collections.Counter()
stack = collections.defaultdict(list)
tmin = tmax = None
for p in t.packet:
    seq = p.trusted_packet_sequence_id
    if p.HasField("track_descriptor"):
        d = p.track_descriptor
        if d.HasField("thread"):
            tracks[d.uuid] = f"{d.thread.thread_name or '?'}[{d.thread.tid}]"
        elif d.name:
            tracks[d.uuid] = d.name
    if p.HasField("trace_packet_defaults") and p.trace_packet_defaults.HasField("track_event_defaults"):
        defaults[seq] = p.trace_packet_defaults.track_event_defaults.track_uuid
    if p.HasField("interned_data"):
        for e in p.interned_data.event_names:
            names[(seq, e.iid)] = e.name
    if p.HasField("track_event"):
        e = p.track_event
        uuid = e.track_uuid if e.HasField("track_uuid") else defaults.get(seq, 0)
        tr = tracks.get(uuid, f"uuid{uuid}")
        if flt and flt not in tr:
            continue
        tmin = p.timestamp if tmin is None else min(tmin, p.timestamp)
        tmax = p.timestamp if tmax is None else max(tmax, p.timestamp)
        nm = names.get((seq, e.name_iid), e.name or "?")
        if e.type == TrackEvent.TYPE_SLICE_BEGIN:
            stack[uuid].append((nm, p.timestamp))
            count[(tr, nm)] += 1
        elif e.type == TrackEvent.TYPE_SLICE_END:
            if stack[uuid]:
                n0, t0 = stack[uuid].pop()
                dur[(tr, n0)] += p.timestamp - t0
        elif e.type == TrackEvent.TYPE_INSTANT:
            count[(tr, nm)] += 1
span = (tmax - tmin) / 1e9
print(f"span {span:.1f} s")
for (tr, nm), c in sorted(count.items(), key=lambda kv: -kv[1])[:30]:
    print(f"  {c / span:9.0f}/s  {dur[(tr, nm)] / 1e6 / span:8.1f} ms/s  {tr:28s} {nm}")
