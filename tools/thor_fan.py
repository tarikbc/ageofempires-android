#!/usr/bin/env python3
"""Set or read the AYN Thor's fan mode from adb, through the Fan tile in Android's quick settings.

    thor_fan.py status          the current mode (Android setting system/fan_mode)
    thor_fan.py smart|custom    choose that row in the Fan dialog and check the result

Rule for testing: Custom (the user's 88 %) only while the game is being measured, Smart as soon as it is not, to save
the fan from needless wear. `fan_mode` values seen on the tested unit: 4 = Smart, 6 = Custom ("Customize"). The tile
and the dialog rows are tapped at fixed spots of the main screen (1920 x 1080, quick settings first page: Fan is the
third tile of the first row); if the check fails, look at the screen.
"""
import subprocess
import sys
import time

SERIAL = "64ff2273"
MODES = {"4": "smart", "6": "custom"}
FAN_TILE = (1193, 398)
ROWS = {"smart": (721, 502), "custom": (754, 724)}


def sh(cmd):
    return subprocess.run(["adb", "-s", SERIAL, "shell", cmd], capture_output=True, text=True).stdout.strip()


def status():
    raw = sh("settings get system fan_mode")
    return MODES.get(raw, f"unknown ({raw})")


def choose(mode):
    sh("cmd statusbar expand-settings")
    time.sleep(1.5)
    sh(f"input -d 0 tap {FAN_TILE[0]} {FAN_TILE[1]}")
    time.sleep(1.5)
    sh(f"input -d 0 tap {ROWS[mode][0]} {ROWS[mode][1]}")
    time.sleep(1.5)
    sh("input keyevent 4")  # closes the dialog; the game does not see this key while the shade is open
    time.sleep(0.5)
    sh("cmd statusbar collapse")
    now = status()
    if now != mode:
        sys.exit(f"fan is {now}, not {mode}: check the screen")
    print("fan", now)


def main():
    if len(sys.argv) < 2 or sys.argv[1] not in ("status", "smart", "custom"):
        sys.exit(__doc__)
    if sys.argv[1] == "status":
        print("fan", status())
    elif status() == sys.argv[1]:
        print("fan already", sys.argv[1])
    else:
        choose(sys.argv[1])


if __name__ == "__main__":
    main()
