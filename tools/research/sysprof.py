#!/usr/bin/env python3
"""Name the Wine system calls in a RIP sample dump (`tools/agent.py sample ... FILE`).

    sysprof.py RIPS_FILE NTDLL_DLL SYSCALLS_TSV NTDLL_BASE_HEX [TOP]

A sample at the return of an ARM64EC syscall stub (movz x8,#id; mov x9,x30; adrp x16; ldr x16; blr x16; ret) is the
thread inside, or waiting in, that call. The id is decoded from the stub in the game's own ntdll copy
(`tools/wincopy.py`) and named with the table from `ntdll_syscall_table.py`. NTDLL_BASE_HEX comes from
`tools/agent.py mod ntdll.dll` in the same session: every stub has the same shape, so a wrong base still decodes,
to wrong names. Other ntdll samples are listed by 256-byte block.
"""
import collections
import struct
import sys

rips_file, ntdll_file, table_file, base_hex = sys.argv[1:5]
top = int(sys.argv[5]) if len(sys.argv) > 5 else 15
d = open(ntdll_file, "rb").read()
names = {int(a, 16): b for a, b in (l.rstrip("\n").split("\t") for l in open(table_file))}
rips = [int(x, 16) for x in open(rips_file).read().split()]
base = int(base_hex, 16)

pe = struct.unpack_from("<I", d, 0x3c)[0]
nsec = struct.unpack_from("<H", d, pe + 6)[0]
optsz = struct.unpack_from("<H", d, pe + 20)[0]
size_img = struct.unpack_from("<I", d, pe + 24 + 56)[0]
secs = []
for k in range(nsec):
    o = pe + 24 + optsz + 40 * k
    vsize, va, rawsz, rawptr = struct.unpack_from("<IIII", d, o + 8)
    secs.append((va, max(vsize, rawsz), rawptr))


def insn(rva):
    for va, sz, rawptr in secs:
        if va <= rva < va + sz - 3:
            return struct.unpack_from("<I", d, rawptr + rva - va)[0]
    return None


def syscall_at(rva):
    if insn(rva - 4) != 0xd63f0200:  # blr x16 just before the return address
        return None
    m = insn(rva - 0x14)
    if m is None or (m & 0xffe0001f) != 0xd2800008:  # movz x8, #id at the stub start
        return None
    return (m >> 5) & 0xffff


n = len(rips)
calls, other, outside = collections.Counter(), collections.Counter(), 0
for r in rips:
    rva = r - base
    if not 0 <= rva < size_img:
        outside += 1
        continue
    sid = syscall_at(rva)
    if sid is not None:
        calls[names.get(sid, f"syscall 0x{sid:x}")] += 1
    else:
        other[rva & ~0xff] += 1
print(f"{n} samples; outside ntdll {outside} ({100 * outside / n:.1f}%); in syscalls {sum(calls.values())} "
      f"({100 * sum(calls.values()) / n:.1f}%); other ntdll {sum(other.values())} ({100 * sum(other.values()) / n:.1f}%)")
for k, v in calls.most_common(top):
    print(f"  {100 * v / n:5.1f}%  {k}")
for k, v in other.most_common(5):
    print(f"  {100 * v / n:5.1f}%  ntdll+{k:x} (256-byte block)")
