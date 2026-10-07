"""Linear disassembly of an address range from the dumped blocks (first-seen bytes per address).
usage: blkmem.py DUMP START_HEX END_HEX [--tid TID_HEX]"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import blkparse  # noqa: E402

recs = blkparse.load(sys.argv[1])
start, end = int(sys.argv[2], 16), int(sys.argv[3], 16)
tid = int(sys.argv[sys.argv.index("--tid") + 1], 16) if "--tid" in sys.argv else None
mem = {}
for entry, size, t, ticks, code in recs:
    if tid is not None and t != tid:
        continue
    if entry + size < start or entry > end:
        continue
    for i, b in enumerate(code):
        mem.setdefault(entry + i, b)
a = start
while a < end:
    if a not in mem:
        b = a
        while b < end and b not in mem:
            b += 1
        print(f"   ... {a:#x}-{b:#x} not executed")
        a = b
        continue
    b = a
    while b < end and b in mem:
        b += 1
    code = bytes(mem[x] for x in range(a, b))
    off = 0
    for ins in blkparse.md.disasm(code, a):
        print(f"   {ins.address:#x}: {ins.mnemonic} {ins.op_str}")
        off = ins.address + ins.size - a
    if off < len(code):
        print(f"   {a + off:#x}: <{len(code) - off} undecoded bytes>")
    a = b
