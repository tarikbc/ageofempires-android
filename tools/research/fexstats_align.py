"""Compare FEX JIT activity in sample bins that contain a long frame with the other bins.
usage: stats_align.py PREFIX [THRESHOLD_MS]   (PREFIX.stats.csv and PREFIX.frames.csv from stats_sample.py)"""
import collections
import csv
import statistics
import sys

prefix = sys.argv[1]
thr = float(sys.argv[2]) if len(sys.argv) > 2 else 40
stats = list(csv.DictReader(open(prefix + ".stats.csv")))
frames = [(int(r["present_ns"]), float(r["frame_ms"])) for r in csv.DictReader(open(prefix + ".frames.csv"))]
times = sorted({int(r["mono_ns"]) for r in stats})
print(f"{len(times)} sample points over {(times[-1] - times[0]) / 1e9:.1f} s, {len(frames)} frames")
# per bin (between consecutive sample points): JIT ns per thread, and the longest frame ending inside the bin
per_bin = collections.defaultdict(lambda: collections.Counter())
for r in stats:
    per_bin[int(r["mono_ns"])][int(r["tid"])] += int(r["dJIT_ns"])
bins = []
for a, b in zip(times, times[1:]):
    longest = max((ms for t, ms in frames if a < t <= b), default=0)
    jit = per_bin[b]
    bins.append(((b - a) / 1e9, longest, sum(jit.values()), jit))
# the threads with the most JIT time overall
tot = collections.Counter()
for _, _, _, jit in bins:
    tot.update(jit)
top = [tid for tid, _ in tot.most_common(6)]
span = sum(d for d, _, _, _ in bins)
print("JIT ms per second, whole run:", ", ".join(f"tid {tid:#x} {v / 1e6 / span:.1f}" for tid, v in tot.most_common(6)),
      f"| all threads {sum(tot.values()) / 1e6 / span:.1f}")
hit = [b for b in bins if b[1] > thr]
miss = [b for b in bins if b[1] <= thr and b[1] > 0]
print(f"bins with a frame over {thr:.0f} ms: {len(hit)}, other bins: {len(miss)}")
for name, group in (("with a long frame", hit), ("without", miss)):
    if not group:
        continue
    rate = [b[2] / 1e6 / b[0] for b in group]
    print(f"  {name:18} JIT ms per second: median {statistics.median(rate):.1f}, mean {statistics.mean(rate):.1f}, p90 {sorted(rate)[int(len(rate) * .9) - 1]:.1f}")
    for tid in top[:4]:
        r = [b[3][tid] / 1e6 / b[0] for b in group]
        print(f"      tid {tid:#x}: median {statistics.median(r):.1f} ms/s")
# the bins with the most JIT: what frames did they have?
print("bins with the most JIT (ms in bin, longest frame ms):", [(round(b[2] / 1e6, 1), round(b[1])) for b in sorted(bins, key=lambda b: -b[2])[:10]])
