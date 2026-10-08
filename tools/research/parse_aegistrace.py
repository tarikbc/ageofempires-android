#!/usr/bin/env python3
"""Decode a syscall trace dumped by tools/probes/aegistrace.c (FEX patch 0005).

    parse_aegistrace.py TRACE.bin [--names ntsyscalls.h] [--summary] [--last N] [--tid TID]

Pass --table with the table of the exact ntdll.dll the trace ran with (tools/research/ntdll_syscall_table.py): syscall
numbers differ between Wine builds (the device's proton-11.0-99-arm64ec matches Wine master only for the older,
Windows-numbered calls). Without it, names come from Wine master's dlls/ntdll/ntsyscalls.h and can be wrong.
"""
import argparse
import collections
import os
import re
import struct
import urllib.request

HEADER = struct.Struct("<17Q")
ENTRY = struct.Struct("<QQIHHQQ3Q")
FLAGS = {1: "watch", 2: "region", 4: "outside", 8: "direct"}


def load_names(path, table=None):
    if table:
        return {int(i, 16): n for i, n in (l.split("\t") for l in open(table).read().splitlines() if l.strip())}
    if not path:
        path = os.path.expanduser("~/.cache/wine-ntsyscalls.h")
        if not os.path.exists(path):
            os.makedirs(os.path.dirname(path), exist_ok=True)
            data = urllib.request.urlopen("https://gitlab.winehq.org/wine/wine/-/raw/master/dlls/ntdll/ntsyscalls.h",
                                          timeout=30).read()
            open(path, "wb").write(data)
    text = open(path).read()
    names = {}
    for m in re.finditer(r"#define ALL_SYSCALLS\b", text):
        prev = text[max(0, m.start() - 40):m.start()]
        block = text[m.start():text.find("\n\n", m.start())]
        if "_WIN64" in prev:
            names = {int(n, 16): nm for n, nm in re.findall(r"SYSCALL_ENTRY\(\s*(0x[0-9a-f]+),\s*(\w+)", block)}
    return names


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("trace")
    ap.add_argument("--names")
    ap.add_argument("--table", help="syscall table of the exact ntdll used (tools/research/ntdll_syscall_table.py); preferred")
    ap.add_argument("--summary", action="store_true")
    ap.add_argument("--last", type=int, default=0)
    ap.add_argument("--tid", type=lambda s: int(s, 0))
    a = ap.parse_args()
    raw = open(a.trace, "rb").read()
    h = HEADER.unpack_from(raw, 0)
    marker, buf, cap, written, exe_lo, exe_hi, reg_lo, reg_hi, tps = h[:9]
    watch = [w * 64 + i for w in range(8) for i in range(64) if (h[9 + w] >> i) & 1]
    names = load_names(a.names, a.table)
    entries = []
    for off in range(HEADER.size, len(raw) - ENTRY.size + 1, ENTRY.size):
        e = ENTRY.unpack_from(raw, off)
        if e[0]:
            entries.append(e)
    entries.sort()
    print(f"capacity={cap} written={written} kept={len(entries)} ticks/s={tps} exe=0x{exe_lo:x}..0x{exe_hi:x} "
          f"region=+0x{reg_lo - exe_lo:x}..+0x{reg_hi - exe_lo:x}")
    print("watch list (decoded on device):", ", ".join(f"{names.get(i, hex(i))}=0x{i:x}" for i in watch))
    if not entries:
        return
    t0 = entries[0][1]

    def where(addr):
        if exe_lo <= addr < exe_hi:
            return f"exe+0x{addr - exe_lo:x}"
        return f"0x{addr:x}" if addr else "-"

    if a.tid is not None:
        entries = [e for e in entries if e[2] == a.tid]
    if a.summary:
        c = collections.Counter((names.get(e[3], hex(e[3])), "|".join(v for k, v in FLAGS.items() if e[4] & k)) for e in entries)
        for (n, f), k in c.most_common(60):
            print(f"{k:8d}  {n:40s} {f}")
        return
    for e in entries[-a.last:] if a.last else entries:
        seq, ticks, tid, sid, flags, caller, rip, a0, a1, a2 = e
        f = "|".join(v for k, v in FLAGS.items() if flags & k)
        print(f"{(ticks - t0) / tps:10.3f}s tid={tid:5x} {names.get(sid, hex(sid)):32s} {f:20s} caller={where(caller):18s} "
              f"rip={where(rip):18s} args=0x{a0:x} 0x{a1:x} 0x{a2:x}")


if __name__ == "__main__":
    main()
