#!/usr/bin/env python3
"""Drive the AYN Thor's built-in controller from adb by writing evdev events into its device node.

    thor_pad.py press A|B|X|Y|LB|RB|START|SELECT|UP|DOWN|LEFT|RIGHT [--hold SECONDS]
    thor_pad.py stick L|R X Y            (each -1.0..1.0; 0 0 recentres)
    thor_pad.py trigger LT|RT VALUE      (0.0..1.0)

The Thor's controller (Xbox style in Thor settings) is "Xbox Wireless Controller", /dev/input/event9 on the tested
unit; the node is found by name. adb's shell user is in the `input` group, and the node is writable, so the events
reach Android (and GameNative) as if the real controller sent them. Names here are the physical buttons: with
GameNative's Edit Physical Controller A/B and X/Y swap, physical B acts as the game's A.
"""
import subprocess
import sys
import time

SERIAL = "64ff2273"
EV_SYN, EV_KEY, EV_ABS = 0, 1, 3
BUTTONS = {"A": 304, "B": 305, "X": 307, "Y": 308, "LB": 310, "RB": 311, "SELECT": 314, "START": 315, "MODE": 316,
           "LS": 317, "RS": 318}
HAT = {"UP": (17, -1), "DOWN": (17, 1), "LEFT": (16, -1), "RIGHT": (16, 1)}
STICKS = {"L": (0, 1), "R": (2, 5)}  # ABS_X/ABS_Y, ABS_Z/ABS_RZ
TRIGGERS = {"LT": 10, "RT": 9}  # ABS_BRAKE, ABS_GAS


def node():
    out = subprocess.run(["adb", "-s", SERIAL, "shell", "getevent -lp"], capture_output=True, text=True).stdout
    dev = None
    for line in out.splitlines():
        if line.startswith("add device"):
            dev = line.split(":", 1)[1].strip()
        if "Xbox Wireless Controller" in line and dev:
            return dev
        if "Odin Controller" in line and dev:
            sys.exit("the Thor's controller is in standard mode; set it to Xbox style in the Thor's settings "
                     "(these tools were tested only in Xbox style, see docs/guides/TESTING.md)")
    sys.exit("controller node not found")


def send(dev, events):
    cmds = "; ".join(f"sendevent {dev} {t} {c} {v}" for t, c, v in events)
    subprocess.run(["adb", "-s", SERIAL, "shell", cmds], check=True)


def press(dev, name, hold=0.08):
    name = name.upper()
    if name in HAT:
        code, value = HAT[name]
        send(dev, [(EV_ABS, code, value), (EV_SYN, 0, 0)])
        time.sleep(hold)
        send(dev, [(EV_ABS, code, 0), (EV_SYN, 0, 0)])
    else:
        code = BUTTONS[name]
        send(dev, [(EV_KEY, code, 1), (EV_SYN, 0, 0)])
        time.sleep(hold)
        send(dev, [(EV_KEY, code, 0), (EV_SYN, 0, 0)])


def main():
    dev = node()
    cmd = sys.argv[1]
    if cmd == "press":
        hold = float(sys.argv[sys.argv.index("--hold") + 1]) if "--hold" in sys.argv else 0.08
        press(dev, sys.argv[2], hold)
    elif cmd == "stick":
        xc, yc = STICKS[sys.argv[2].upper()]
        x, y = float(sys.argv[3]), float(sys.argv[4])
        send(dev, [(EV_ABS, xc, int(x * 32767)), (EV_ABS, yc, int(y * 32767)), (EV_SYN, 0, 0)])
    elif cmd == "trigger":
        send(dev, [(EV_ABS, TRIGGERS[sys.argv[2].upper()], int(float(sys.argv[3]) * 32767)), (EV_SYN, 0, 0)])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
