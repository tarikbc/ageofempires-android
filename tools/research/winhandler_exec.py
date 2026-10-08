#!/usr/bin/env python3
"""Run a Windows program inside a running GameNative session, with no typing on the device.

    winhandler_exec.py <program> [params] [--serial SERIAL]

GameNative's winhandler.exe listens on UDP 127.0.0.1:7946. An EXEC packet is 64 bytes:
byte 2, then int32 LE (len(program) + len(params) + 8), int32 len(program), int32 len(params),
then the program and the params, zero-padded. Program + params must fit in 51 bytes.

Examples:
    winhandler_exec.py 'D:\\tctx.exe'            # D: is /sdcard/Download
    winhandler_exec.py cmd '/c D:\\x.bat'
"""
import struct
import subprocess
import sys
import tempfile

PORT = 7946


def packet(program: bytes, params: bytes) -> bytes:
    if len(program) + len(params) > 51:
        sys.exit("program + params must fit in 51 bytes")
    body = bytes([2]) + struct.pack("<iii", len(program) + len(params) + 8, len(program), len(params))
    return (body + program + params).ljust(64, b"\0")


def main():
    args = sys.argv[1:]
    serial = []
    if "--serial" in args:
        i = args.index("--serial")
        serial = ["-s", args[i + 1]]
        del args[i:i + 2]
    if not 1 <= len(args) <= 2:
        sys.exit(__doc__)
    pkt = packet(args[0].encode(), (args[1] if len(args) > 1 else "").encode())
    with tempfile.NamedTemporaryFile(suffix=".bin") as f:
        f.write(pkt)
        f.flush()
        subprocess.run(["adb", *serial, "push", f.name, "/data/local/tmp/winhandler_exec.bin"], check=True, capture_output=True)
    subprocess.run(["adb", *serial, "shell", f"timeout 3 nc -u 127.0.0.1 {PORT} < /data/local/tmp/winhandler_exec.bin"])
    print("sent")


if __name__ == "__main__":
    main()
