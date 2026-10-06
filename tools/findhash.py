import struct, sys, time
tb = open('text.bin','rb').read()
VA = 0x1000
n = len(tb)

# --- locate candidate hash entries by the prologue ---
prologue = bytes.fromhex('48895c2408488974241848897c2420')
entries = []
j = tb.find(prologue)
while j >= 0:
    entries.append(j + VA); j = tb.find(prologue, j+1)
print("entries found by prologue:", [hex(e) for e in entries])

# --- linear scan of every E8, one pass ---
t0 = time.time()
pos = []
i = tb.find(b'\xe8')
while i >= 0:
    pos.append(i); i = tb.find(b'\xe8', i+1)
print("E8 bytes:", len(pos), "scan built in %.1fs" % (time.time()-t0))

from collections import defaultdict
callers = defaultdict(list)
for i in pos:
    if i + 5 > n: continue
    rel = struct.unpack_from('<i', tb, i+1)[0]
    tgt = (i + VA) + 5 + rel
    if 0x140000000 == 0x140000000:
        pass
    callers[tgt].append(i + VA)

print()
for e in entries:
    c = callers.get(e, [])
    print(f"entry RVA 0x{e:x}: {len(c)} direct caller(s)")
    for a in c[:12]:
        print(f"    called from RVA 0x{a:x}")
    # 64-bit pointer references anywhere
    pat = struct.pack('<Q', 0x140000000 + e)
    p = tb.find(pat); refs = []
    while p >= 0:
        refs.append(p + VA); p = tb.find(pat, p+1)
    if refs:
        print(f"    pointer refs: {[hex(x) for x in refs[:8]]}")
