#!/usr/bin/env python3
"""Draw frame-time recordings (CSV from frametimes.py / bench.py / fpsgraph.py) as stacked bar charts, one bar per
frame, like tools/fpsgraph.py's live graph.

    frameplot.py OUT.png "TITLE=CSV" ["TITLE=CSV" ...] [--frames N] [--scale MS]

Each CSV gets one panel with its first N frames (default 900, about 30 s at 30 FPS).
"""
import statistics
import sys

from PIL import Image, ImageDraw, ImageFont

W, PANEL_H, PAD = 1800, 260, 16


def load(path, n):
    rows = [line.split(",") for line in open(path).read().splitlines()[1:] if line]
    return [float(r[1]) for r in rows][:n]


def font(size):
    for name in ("/System/Library/Fonts/Menlo.ttc", "/System/Library/Fonts/Monaco.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return ImageFont.load_default()


def main():
    args = sys.argv[1:]
    n = int(args[args.index("--frames") + 1]) if "--frames" in args else 900
    scale = float(args[args.index("--scale") + 1]) if "--scale" in args else 120.0
    panels = [a.split("=", 1) for a in args[1:] if "=" in a]
    img = Image.new("RGB", (W, PAD + len(panels) * (PANEL_H + PAD)), (16, 18, 20))
    d = ImageDraw.Draw(img)
    f_title, f_small = font(22), font(15)
    for k, (title, path) in enumerate(panels):
        ft = load(path, n)
        top = PAD + k * (PANEL_H + PAD)
        plot_top, plot_bot, left = top + 40, top + PANEL_H - 6, 70
        span = sum(ft) / 1000
        stats = (f"{len(ft) / span:.1f} FPS, median {statistics.median(ft):.1f} ms, frames > 50 ms: "
                 f"{sum(f > 50 for f in ft)}, > 100 ms: {sum(f > 100 for f in ft)}, worst {max(ft):.0f} ms")
        d.text((left, top + 4), title, fill=(232, 232, 232), font=f_title)
        d.text((left + 12 + d.textlength(title, font=f_title), top + 10), stats, fill=(138, 144, 153), font=f_small)
        y = lambda ms: plot_bot - min(ms, scale) / scale * (plot_bot - plot_top)  # noqa: E731
        for ms, lab in ((16.7, "60"), (33.3, "30"), (50, "20"), (100, "10")):
            if ms <= scale:
                d.line([(left, y(ms)), (W - PAD, y(ms))], fill=(42, 47, 54))
                d.text((8, y(ms) - 8), f"{lab} fps", fill=(138, 144, 153), font=f_small)
        bw = (W - PAD - left) / max(len(ft), 1)
        for i, ms in enumerate(ft):
            color = (63, 185, 80) if ms <= 34 else (210, 153, 34) if ms <= 50 else (248, 81, 73)
            x0 = left + i * bw
            d.rectangle([x0, y(ms), x0 + max(bw - 1, 1), plot_bot], fill=color)
    img.save(args[0])
    print(args[0])


if __name__ == "__main__":
    main()
