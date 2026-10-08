"""Stacked frame-time panels, one per benchmark window, from tools/bench.py frame CSVs (present_ns,frame_ms).
usage: frametimes_graph.py OUT.png "LABEL=frames.csv" ["LABEL=frames.csv" ...]   (needs matplotlib)
Each panel shows one bar per frame over the window's first 60 s, the same 0 to 100 ms scale, and guide lines at
120, 60 and 30 FPS; the FPS in the title is the window's frame count over its length."""
import csv
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

out = sys.argv[1]
series = [a.rsplit("=", 1) for a in sys.argv[2:]]  # the label may itself contain "="
fig, axes = plt.subplots(len(series), 1, figsize=(11, 1.9 * len(series) + 0.6), sharex=True)
if len(series) == 1:
    axes = [axes]
for ax, (label, path) in zip(axes, series):
    rows = list(csv.DictReader(open(path)))
    t0 = int(rows[0]["present_ns"])
    ts = [(int(r["present_ns"]) - t0) / 1e9 for r in rows]
    ms = [float(r["frame_ms"]) for r in rows]
    fps = len(rows) / ((int(rows[-1]["present_ns"]) - t0) / 1e9)
    keep = [i for i, t in enumerate(ts) if t <= 60]
    colors = ["#2d7a3a" if m <= 17 else "#a0741a" if m <= 34 else "#c23a33" for m in ms]
    ax.bar([ts[i] for i in keep], [min(ms[i], 100) for i in keep], width=0.012, color=[colors[i] for i in keep], linewidth=0)
    for y, name in ((8.3, "120"), (16.7, "60"), (33.3, "30")):
        ax.axhline(y, color="#888", linewidth=0.5, linestyle=":")
        ax.text(60.3, y, f"{name} FPS", va="center", fontsize=7, color="#666")
    ax.set_ylim(0, 100)
    ax.set_xlim(0, 60)
    ax.set_ylabel("ms", fontsize=8)
    ax.tick_params(labelsize=7)
    ax.set_title(f"{label}: {fps:.1f} FPS, median {sorted(ms)[len(ms) // 2]:.1f} ms, {sum(m > 50 for m in ms)} frames over 50 ms",
                 fontsize=9, loc="left")
axes[-1].set_xlabel("seconds into the window", fontsize=8)
fig.tight_layout()
fig.savefig(out, dpi=110)
print("wrote", out)
