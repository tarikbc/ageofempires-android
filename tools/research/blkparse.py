"""Parse a blkread dump. Library + CLI.
usage: blkparse.py DUMP stats
       blkparse.py DUMP xref TARGET_HEX          blocks whose direct call/jmp/jcc goes to TARGET
       blkparse.py DUMP dis ADDR_HEX [N]          disassemble the block(s) starting at ADDR (all versions)
       blkparse.py DUMP find ADDR_HEX             blocks that contain ADDR
       blkparse.py DUMP tid TID_HEX [N]           first N block entries compiled on a thread, in order
"""
import struct
import sys

import capstone

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = False


def load(path):
    data = open(path, "rb").read()
    hdr = struct.unpack_from("<6Q", data, 0)
    used = hdr[3]
    off = 48
    recs = []
    while off + 24 <= 48 + used:
        entry, size, tid, ticks = struct.unpack_from("<QIIQ", data, off)
        if size == 0:
            break
        code = data[off + 24: off + 24 + size]
        recs.append((entry, size, tid, ticks, code))
        off += (24 + size + 7) & ~7
    return recs


def insns(entry, code):
    return list(md.disasm(code, entry))


def branch_target(ins):
    if ins.mnemonic.startswith(("call", "j")) and ins.op_str.startswith("0x"):
        try:
            return int(ins.op_str, 16)
        except ValueError:
            return None
    return None


def show(rec, maxn=400):
    entry, size, tid, ticks, code = rec
    print(f"-- block {entry:#x} size {size} tid {tid:#x} ticks {ticks}")
    for i, ins in enumerate(insns(entry, code)):
        if i >= maxn:
            break
        print(f"   {ins.address:#x}: {ins.mnemonic} {ins.op_str}")


def main():
    recs = load(sys.argv[1])
    cmd = sys.argv[2]
    if cmd == "stats":
        tids = {}
        for r in recs:
            tids[r[2]] = tids.get(r[2], 0) + 1
        print(len(recs), "records;", "threads:", {f"{k:#x}": v for k, v in sorted(tids.items())})
    elif cmd == "xref":
        t = int(sys.argv[3], 16)
        for r in recs:
            for ins in insns(r[0], r[4]):
                if branch_target(ins) == t:
                    print(f"{ins.address:#x}: {ins.mnemonic} {ins.op_str}   (block {r[0]:#x}, tid {r[2]:#x}, ticks {r[3]})")
    elif cmd == "dis":
        a = int(sys.argv[3], 16)
        n = int(sys.argv[4]) if len(sys.argv) > 4 else 400
        for r in recs:
            if r[0] == a:
                show(r, n)
    elif cmd == "find":
        a = int(sys.argv[3], 16)
        for r in recs:
            if r[0] <= a < r[0] + r[1]:
                print(f"block {r[0]:#x} size {r[1]} tid {r[2]:#x} ticks {r[3]}")
    elif cmd == "tid":
        t = int(sys.argv[3], 16)
        n = int(sys.argv[4]) if len(sys.argv) > 4 else 100
        k = 0
        for r in recs:
            if r[2] == t:
                print(f"{r[0]:#x} size {r[1]} ticks {r[3]}")
                k += 1
                if k >= n:
                    break


if __name__ == "__main__":
    main()
