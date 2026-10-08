#!/usr/bin/env python3
"""Late-game benchmark from a replay on the Thor (docs/guides/TESTING.md, "Late-game benchmark from a replay").

    replay.py clock                      crop the replay's game clock and speed into replay_clock.png (to read)
    replay.py speed up|down [N]          press RIGHT (faster) or LEFT (slower) N times; steps 1/2X 1X 2X 4X 8X
    replay.py window LABEL [--out DIR]   90 s of compositor frame times with temperatures, then 20 s of per-thread
                                         CPU; one line appended to DIR/replay_results.tsv (default ./bench_out)

Start the replay in the game first: profile (LS), Match History, the match, X "View Replay"; X again in the replay
locks the camera to the player's recorded view. Nothing runs inside the game for the measurement.
"""
import os
import subprocess
import sys
import time
from io import BytesIO

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import frametimes  # noqa: E402
import thermals  # noqa: E402
import thor_pad  # noqa: E402

SERIAL = "64ff2273"
DISPLAY = "4630946441858561667"  # the Thor's main screen for screencap -d


def clock(path="replay_clock.png"):
    from PIL import Image
    png = subprocess.run(["adb", "-s", SERIAL, "exec-out", "screencap", "-p", "-d", DISPLAY], capture_output=True).stdout
    im = Image.open(BytesIO(png)).convert("RGB")
    w, h = im.size
    parts = [im.crop((int(w * 0.88), int(h * 0.19), int(w * 0.99), int(h * 0.25))),  # game clock
             im.crop((int(w * 0.49), int(h * 0.71), int(w * 0.54), int(h * 0.76)))]  # speed above the play button
    out = Image.new("RGB", (sum(p.width for p in parts) + 10, max(p.height for p in parts)))
    out.paste(parts[0], (0, 0))
    out.paste(parts[1], (parts[0].width + 10, 0))
    out.resize((out.width * 2, out.height * 2)).save(path)
    print(path)


def speed(direction, n=1):
    dev = thor_pad.node()
    for _ in range(n):
        thor_pad.press(dev, "RIGHT" if direction == "up" else "LEFT")
        time.sleep(0.8)


def window(label, out):
    os.makedirs(out, exist_ok=True)
    sampler = thermals.Sampler().start()
    ts = frametimes.record(90)
    heat = sampler.stop()
    line = f"{label}\t{time.strftime('%Y-%m-%d %H:%M')}\t{frametimes.summary(ts)}\t{heat}"
    print(line, flush=True)
    with open(os.path.join(out, "replay_results.tsv"), "a") as f:
        f.write(line + "\n")
    with open(os.path.join(out, f"replay_frames_{label}.csv"), "w") as f:
        f.write("present_ns,frame_ms\n")
        for a, b in zip(ts, ts[1:]):
            f.write(f"{b},{(b - a) / 1e6:.3f}\n")
    here = os.path.dirname(os.path.abspath(__file__))
    r = subprocess.run([sys.executable, os.path.join(here, "threadcpu.py"), "20"], capture_output=True, text=True)
    open(os.path.join(out, f"replay_threads_{label}.txt"), "w").write(r.stdout + r.stderr)
    print("\n".join(r.stdout.splitlines()[:8]))


def main():
    args = sys.argv[1:]
    if not args:
        sys.exit(__doc__)
    if args[0] == "clock":
        clock()
    elif args[0] == "speed":
        speed(args[1], int(args[2]) if len(args) > 2 else 1)
    elif args[0] == "window":
        window(args[1], args[args.index("--out") + 1] if "--out" in args else "bench_out")
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
