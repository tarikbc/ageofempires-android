#!/usr/bin/env python3
"""Build the syscall number -> name table of a Wine ARM64EC ntdll.dll from its own code.

    ntdll_syscall_table.py NTDLL.dll [--out table.tsv]

Wine's ARM64EC syscall stubs start with `movz x8, #id` (mov x8, #id). The DLL keeps its COFF symbols, so each
`#NtXxx$hp_target` (plain stubs) and `#syscall_NtXxx` (the inner stub of wrapped calls such as
NtProtectVirtualMemory) is located by name and its first instruction decoded. Syscall numbers differ between
Wine builds, so a trace must be decoded with the table of the exact ntdll it ran with.
"""
import argparse
import os
import re
import shutil
import struct
import subprocess

# llvm-nm and llvm-objdump from llvm-mingw (docs/guides/BUILDING-FEX.md); set LLVM_MINGW to its directory if they are
# not on PATH.
NM = os.path.join(os.environ["LLVM_MINGW"], "bin", "llvm-nm") if os.environ.get("LLVM_MINGW") else shutil.which("llvm-nm") or "llvm-nm"


def sections(data):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    optsize = struct.unpack_from("<H", data, pe + 20)[0]
    base = struct.unpack_from("<Q", data, pe + 24 + 24)[0]
    out = []
    for i in range(nsec):
        off = pe + 24 + optsize + i * 40
        vsize, va, rawsize, rawptr = struct.unpack_from("<IIII", data, off + 8)
        out.append((va, max(vsize, rawsize), rawptr))
    return base, out


def read_u32(data, base, secs, addr):
    rva = addr - base
    for va, size, raw in secs:
        if va <= rva < va + size:
            return struct.unpack_from("<I", data, raw + rva - va)[0]
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ntdll")
    ap.add_argument("--out")
    a = ap.parse_args()
    data = open(a.ntdll, "rb").read()
    base, secs = sections(data)
    syms = subprocess.run([NM, a.ntdll], capture_output=True, text=True).stdout
    table = {}
    for line in syms.splitlines():
        m = re.match(r"([0-9a-f]+) [Tt] #(?:syscall_)?(Nt\w+?)(\$hp_target)?$", line)
        if not m or (not m.group(3) and "syscall_" not in line):
            continue
        insn = read_u32(data, base, secs, int(m.group(1), 16))
        if insn is not None and (insn & 0xFFE0001F) == 0xD2800008:
            table.setdefault((insn >> 5) & 0xFFFF, m.group(2))
    # Wrapped calls (NtProtectVirtualMemory, NtGetContextThread, ...) call an unexported stub that objdump still
    # labels <syscall_NtXxx>; read those from the disassembly.
    dis = subprocess.run([NM.replace("llvm-nm", "llvm-objdump"), "-d", "--no-show-raw-insn", a.ntdll],
                         capture_output=True, text=True).stdout.splitlines()
    for i, line in enumerate(dis):
        m = re.match(r"[0-9a-f]+ <syscall_(Nt\w+)>:", line)
        if m and i + 1 < len(dis):
            mm = re.search(r"mov\s+x8, #(0x[0-9a-f]+)", dis[i + 1])
            if mm:
                table.setdefault(int(mm.group(1), 16), m.group(1))
    lines = [f"0x{i:03x}\t{n}" for i, n in sorted(table.items())]
    if a.out:
        open(a.out, "w").write("\n".join(lines) + "\n")
    print(f"{len(table)} syscalls decoded")
    for i, n in sorted(table.items()):
        if n in ("NtQueryVirtualMemory", "NtProtectVirtualMemory", "NtSuspendThread", "NtGetContextThread",
                 "NtGetNextThread", "NtResumeThread", "NtTerminateProcess", "NtSetContextThread"):
            print(f"  0x{i:03x} {n}")


if __name__ == "__main__":
    main()
