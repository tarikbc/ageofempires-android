#!/usr/bin/env python3
"""Native WebSocket client to the AoE backend, to compare against the Wine probe.

If this survives comfortably past ~30 s while wsprobe.exe inside Wine dies at 30 s,
the drop is Wine's, not the server's.
"""
import base64
import os
import socket
import ssl
import struct
import sys
import time

HOST = "dr-activerelease1-api.worldsedgelink.com"
PORT = 443
PATH = "/"
DURATION = int(sys.argv[1]) if len(sys.argv) > 1 else 75


def recv_exact(s, n):
    buf = b""
    while len(buf) < n:
        c = s.recv(n - len(buf))
        if not c:
            raise EOFError(f"connection closed after {len(buf)}/{n} bytes")
        buf += c
    return buf


def read_frame(s):
    hdr = recv_exact(s, 2)
    fin = hdr[0] & 0x80
    opcode = hdr[0] & 0x0F
    masked = hdr[1] & 0x80
    ln = hdr[1] & 0x7F
    if ln == 126:
        ln = struct.unpack(">H", recv_exact(s, 2))[0]
    elif ln == 127:
        ln = struct.unpack(">Q", recv_exact(s, 8))[0]
    mask = recv_exact(s, 4) if masked else None
    payload = recv_exact(s, ln) if ln else b""
    if mask:
        payload = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
    return opcode, fin, payload


def send_frame(s, opcode, payload=b""):
    mask = os.urandom(4)
    masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
    ln = len(payload)
    hdr = bytes([0x80 | opcode])
    if ln < 126:
        hdr += bytes([0x80 | ln])
    elif ln < 65536:
        hdr += bytes([0x80 | 126]) + struct.pack(">H", ln)
    else:
        hdr += bytes([0x80 | 127]) + struct.pack(">Q", ln)
    s.sendall(hdr + mask + masked)


def main():
    ctx = ssl.create_default_context()
    raw = socket.create_connection((HOST, PORT), timeout=15)
    s = ctx.wrap_socket(raw, server_hostname=HOST)
    print(f"TLS OK  {s.version()}  cipher={s.cipher()[0]}", flush=True)

    key = base64.b64encode(os.urandom(16)).decode()
    req = (
        f"GET {PATH} HTTP/1.1\r\n"
        f"Host: {HOST}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n"
    )
    s.sendall(req.encode())
    resp = b""
    while b"\r\n\r\n" not in resp:
        c = s.recv(4096)
        if not c:
            print("server closed during handshake", flush=True)
            return
        resp += c
    head = resp.split(b"\r\n\r\n")[0].decode("latin1")
    status = head.split("\r\n")[0]
    print(f"handshake: {status}", flush=True)
    if "101" not in status:
        print("  full headers:", head[:500], flush=True)
        return

    t0 = time.time()
    s.settimeout(10)
    last_ping = 0.0
    while time.time() - t0 < DURATION:
        el = time.time() - t0
        if el - last_ping >= 20:
            send_frame(s, 0x9, b"keepalive")     # PING
            print(f"  [+{el:5.1f}s] sent PING", flush=True)
            last_ping = el
        try:
            opcode, fin, payload = read_frame(s)
        except socket.timeout:
            continue
        except EOFError as e:
            print(f"  [+{el:5.1f}s] {e}", flush=True)
            return
        name = {0x0: "continuation", 0x1: "text", 0x2: "binary",
                0x8: "CLOSE", 0x9: "ping", 0xA: "pong"}.get(opcode, f"op{opcode}")
        print(f"  [+{el:5.1f}s] {name} len={len(payload)} {payload[:80]!r}", flush=True)
        if opcode == 0x9:
            send_frame(s, 0xA, payload)          # PONG
        if opcode == 0x8:
            print("  server sent CLOSE", flush=True)
            return
    print(f"survived {DURATION}s with no abnormal close", flush=True)


main()
