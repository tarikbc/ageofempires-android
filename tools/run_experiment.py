#!/usr/bin/env python3
"""Monitor an AoE IV run on the Thor and catch the protection kill.

Phase 1 (monitor): every ~6 s, refresh D:\\aoe\\watch.txt from the game's warnings.log via
winhandler, and scan it for the network trigger (errno=10038 / TlsConnection::Shutdown /
WebSocketConnection). Phase 2 (sample): once triggered, run si.exe + tctx.exe every ~2.5 s for
SAMPLE_SECONDS to capture thread suspend states and contexts in the kill window.

Requires the GameNative session to be running (winhandler.exe up), and cp.bat / snap.bat pushed
to D:\\ (=/sdcard/Download).
"""
import os
import struct
import subprocess
import sys
import tempfile
import time

PORT = 7946
WATCH_REMOTE = "/sdcard/Download/aoe/watch.txt"
TRIGGER_PATTERNS = ["errno=10038"]


def adb(args, timeout=60):
    return subprocess.run(["adb", *args], capture_output=True, text=True, timeout=timeout)


def winhandler_exec(program: str, params: str):
    if len(program) + len(params) > 51:
        raise ValueError("program + params too long")
    body = bytes([2]) + struct.pack("<iii", len(program) + len(params) + 8, len(program), len(params))
    pkt = (body + program.encode() + params.encode()).ljust(64, b"\0")
    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as f:
        f.write(pkt)
        f.flush()
        tmp = f.name
    try:
        adb(["push", tmp, "/data/local/tmp/winhandler_exec.bin"], timeout=30)
        adb(["shell", "timeout 3 nc -u 127.0.0.1 %d < /data/local/tmp/winhandler_exec.bin" % PORT], timeout=10)
    finally:
        os.unlink(tmp)


def refresh_watch():
    winhandler_exec("cmd", "/c D:\\cp.bat")
    time.sleep(1.5)


def read_watch():
    r = adb(["shell", "cat", WATCH_REMOTE], timeout=30)
    return r.stdout


def game_pid():
    r = adb(["shell", "ps", "-A"], timeout=30)
    for line in r.stdout.splitlines():
        if "RelicCardinal.exe" in line and "grep" not in line:
            parts = line.split()
            if parts:
                return parts[1]
    return ""


def main():
    sample_seconds = int(sys.argv[1]) if len(sys.argv) > 1 else 90
    print(f"[monitor] starting; sample window {sample_seconds}s; watching {TRIGGER_PATTERNS}")
    # reset watch.txt
    adb(["shell", "rm", "-f", WATCH_REMOTE])
    # clear old snaps
    adb(["shell", "rm", "-f", "/sdcard/Download/aoe/si_*.txt", "/sdcard/Download/aoe/tctx_*.txt"])

    triggered_at = None
    last_len = 0
    while True:
        pid = game_pid()
        if not pid:
            print(f"[monitor] {time.strftime('%H:%M:%S')} game exited; stopping")
            break
        try:
            refresh_watch()
        except Exception as e:
            print(f"[monitor] winhandler refresh failed: {e}")
            time.sleep(3)
            continue
        txt = read_watch()
        tail = txt[last_len:]
        last_len = len(txt)
        if tail.strip():
            print(f"[monitor] {time.strftime('%H:%M:%S')} watch delta:\n{tail.strip()[-2000:]}")
        if triggered_at is None and any(p in txt for p in TRIGGER_PATTERNS):
            triggered_at = time.time()
            print(f"[TRIGGER] {time.strftime('%H:%M:%S')} pattern hit; entering sample phase")
            break
        time.sleep(4)

    if triggered_at is None:
        print("[monitor] no trigger seen")
        return

    # sample phase
    end = triggered_at + sample_seconds
    i = 0
    while time.time() < end:
        pid = game_pid()
        if not pid:
            print(f"[sample] game exited at {time.strftime('%H:%M:%S')}")
            break
        i += 1
        try:
            winhandler_exec("cmd", f"/c D:\\snap.bat {i}")
        except Exception as e:
            print(f"[sample] snap {i} failed: {e}")
        print(f"[sample] {time.strftime('%H:%M:%S')} snap {i} (pid={pid})")
        time.sleep(2.5)
    print("[done] samples written to D:\\aoe\\si_*.txt and tctx_*.txt")


if __name__ == "__main__":
    main()
