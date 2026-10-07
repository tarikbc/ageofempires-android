#!/usr/bin/env python3
"""Repeatable AoE IV benchmark on the Thor: a 1v1 skirmish with the camera spinning, measured by frame times.

    bench.py boot                       intro films / title screen -> main PLAY page (presses B, checks the screen)
    bench.py skirmish [--from-menu]     title screen ("PRESS ANY BUTTON") -> running Solo Battle vs A.I., camera spinning;
                                        --from-menu starts on the main PLAY page instead (B on an intro film can skip
                                        both the film and the title)
    bench.py spin on|off                hold the right stick right (one evdev write, it stays held) / centre it
    bench.py record SECONDS [--csv F]   frame times from the compositor (tools/frametimes.py), printed as a summary
    bench.py run LABEL [--at 1,5,10] [--out DIR] [--attach]
                                        all of it: launch the game (run_watch.py --launch --quiet), boot, skirmish, then
                                        90 s of frame times at each match minute in --at; one line per window is
                                        appended to DIR/results.tsv (default ./bench_out); --attach starts from a
                                        game that already shows its main PLAY page

The menu path was tested on 2026-10-07 with GameNative's A/B and X/Y swap (physical B = the game's A):
title B; main page RIGHT RIGHT B (Single Player); RIGHT B (Skirmish); RIGHT B (lobby "Solo Battle vs A.I.", 1v1,
Standard, Danube River); START (load screen); when the yellow Play button shows, B. The game then starts at 00:00.
Screenshots are taken only while waiting for the Play button, never while recording.
"""
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import frametimes  # noqa: E402
import thermals  # noqa: E402
import thor_pad  # noqa: E402

SERIAL = "64ff2273"
DISPLAY = "4630946441858561667"  # the Thor's main screen for screencap -d
PLAY_BUTTON = (160, 596, 240, 616)  # load screen "Play" button area at 1920x1080
CRUCIBLE_TILE = (120, 260, 380, 460)  # main PLAY page: the purple "The Crucible" tile


def press(dev, name, settle=1.2):
    thor_pad.press(dev, name)
    time.sleep(settle)


def screen():
    from io import BytesIO

    from PIL import Image
    png = subprocess.run(["adb", "-s", SERIAL, "exec-out", "screencap", "-p", "-d", DISPLAY],
                         capture_output=True).stdout
    return Image.open(BytesIO(png)).convert("RGB")


def mean_rgb(box):
    img = screen().crop(box)
    px = list(img.get_flattened_data() if hasattr(img, "get_flattened_data") else img.getdata())
    return [sum(p[i] for p in px) / len(px) for i in range(3)]


def play_button_ready():
    r, g, b = mean_rgb(PLAY_BUTTON)
    return r > 150 and g > 120 and b < r - 40


def main_menu_visible():
    r, g, b = mean_rgb(CRUCIBLE_TILE)
    return b > g + 30 and r > g + 10


def boot(timeout=300):
    """Press physical A (the game's B with the A/B swap) until the main PLAY page shows. It skips the intro films and
    the title screen, and closes notices shown over the menu (e.g. "Server Maintenance", whose game-A button opens a
    browser). The screen is checked before every press, so the main menu itself never gets a B (back)."""
    dev = thor_pad.node()
    end = time.time() + timeout
    while time.time() < end:
        if main_menu_visible():
            print("main menu")
            return
        press(dev, "A", settle=6)
    sys.exit("the main menu did not appear")


def skirmish(from_menu=False):
    dev = thor_pad.node()
    steps = [] if from_menu else [("B", 3)]
    steps += [("RIGHT", 0), ("RIGHT", 0), ("B", 3), ("RIGHT", 0), ("B", 4), ("RIGHT", 0), ("B", 4),
             ("START", 5)]
    for name, wait in steps:
        press(dev, name)
        time.sleep(wait)
    for _ in range(60):  # up to 5 minutes of loading
        if play_button_ready():
            break
        time.sleep(5)
    else:
        sys.exit("the Play button did not appear")
    press(dev, "B")
    time.sleep(12)
    spin(True)
    print("skirmish running, camera spinning")


def spin(on):
    thor_pad.send(thor_pad.node(), [(thor_pad.EV_ABS, 2, 32767 if on else 0), (thor_pad.EV_ABS, 5, 0),
                                    (thor_pad.EV_SYN, 0, 0)])


def run(label, at, out, attach=False):
    import run_watch
    os.makedirs(out, exist_ok=True)
    if attach:  # the game already runs and shows its main PLAY page
        watcher = None
        skirmish(from_menu=True)
        return measure(label, at, out, time.time() - 12)
    log = open(os.path.join(out, f"run_watch_{label}.log"), "w", buffering=1)
    watcher = subprocess.Popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), "run_watch.py"),
                                "--launch", "--quiet", "--minutes", str(max(at) + 15), "--out", os.path.join(out, f"rw_{label}")],
                               stdout=log, stderr=subprocess.STDOUT)
    try:
        # Wait for run_watch's own "pid=" line: a game process left from an earlier run is still there until
        # run_watch force-stops GameNative, and pressing buttons then would disturb the launch.
        end = time.time() + 600
        log_path = os.path.join(out, f"run_watch_{label}.log")
        while "pid=" not in open(log_path).read():
            if time.time() > end or watcher.poll() is not None:
                sys.exit("the game did not start (see run_watch log)")
            time.sleep(5)
        print(f"[{label}] {time.strftime('%H:%M:%S')} game process up", flush=True)
        boot()
        skirmish(from_menu=True)
        measure(label, at, out, time.time() - 12)  # skirmish() returns about 12 s after the match starts
    finally:
        watcher.terminate()


def measure(label, at, out, t0):
    import run_watch
    for minute in at:
        time.sleep(max(0, t0 + minute * 60 - time.time()))
        if not run_watch.game_pid():
            print(f"[{label}] game process gone before minute {minute}", flush=True)
            break
        sampler = thermals.Sampler().start()
        ts = frametimes.record(90)
        heat = sampler.stop()
        line = f"{label}\tminute {minute}\t{time.strftime('%Y-%m-%d %H:%M')}\t{frametimes.summary(ts)}\t{heat}"
        print(line, flush=True)
        with open(os.path.join(out, "results.tsv"), "a") as f:
            f.write(line + "\n")
        with open(os.path.join(out, f"frames_{label}_m{minute}.csv"), "w") as f:
            f.write("present_ns,frame_ms\n")
            for a, b in zip(ts, ts[1:]):
                f.write(f"{b},{(b - a) / 1e6:.3f}\n")


def main():
    args = sys.argv[1:]
    if not args:
        sys.exit(__doc__)
    if args[0] == "boot":
        boot()
    elif args[0] == "skirmish":
        skirmish(from_menu="--from-menu" in args)
    elif args[0] == "run":
        at = [int(x) for x in args[args.index("--at") + 1].split(",")] if "--at" in args else [1, 5, 10]
        out = args[args.index("--out") + 1] if "--out" in args else "bench_out"
        run(args[1], at, out, attach="--attach" in args)
    elif args[0] == "spin":
        spin(args[1] == "on")
    elif args[0] == "record":
        ts = frametimes.record(float(args[1]))
        print(frametimes.summary(ts))
        if "--csv" in args:
            with open(args[args.index("--csv") + 1], "w") as f:
                f.write("present_ns,frame_ms\n")
                for a, b in zip(ts, ts[1:]):
                    f.write(f"{b},{(b - a) / 1e6:.3f}\n")
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
