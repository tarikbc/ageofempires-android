#!/usr/bin/env python3
"""callers.py RVA [RVA...] -- find enclosing function start (walk back to int3 pad) and its callers."""
import struct, sys, bisect
tb = open('text.bin','rb').read()
VA = 0x1000

# one pass: every E8 -> target
pos = []
i = tb.find(b'\xe8')
while i >= 0:
    pos.append(i); i = tb.find(b'\xe8', i+1)
from collections import defaultdict
callers = defaultdict(list)
for i in pos:
    if i + 5 > len(tb): continue
    rel = struct.unpack_from('<i', tb, i+1)[0]
    callers[(i + VA) + 5 + rel].append(i + VA)

def func_start(rva):
    j = rva - VA
    while j > 0x1000:
        if tb[j] == 0xCC and tb[j-1] == 0xCC:
            return j + 1 + VA
        j -= 1
    return None

for a in sys.argv[1:]:
    rva = int(a, 16)
    s = func_start(rva)
    print(f"RVA 0x{rva:x}: enclosing function starts at 0x{s:x}" if s else f"RVA 0x{rva:x}: no start found")
    if s:
        c = callers.get(s, [])
        print(f"   {len(c)} direct caller(s): {[hex(x) for x in c[:14]]}")
