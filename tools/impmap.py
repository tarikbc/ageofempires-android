#!/usr/bin/env python3
"""Map import names to IAT slots for the RelicCardinal.exe dump set.

Reads the import descriptors from a dump of .rdata and prints, for each imported
function, the DLL, the name, and the IAT slot VA (base 0x140000000 + FirstThunk + i*8).
"""
import struct
import sys

IMG_BASE = 0x140000000
RDATA_BASE = 0x1474F0000
RDATA = open("rdata_imp.bin", "rb").read()
IMP_DIR_RVA = 0x7535BA0


def rd(rva, n):
    off = rva - (RDATA_BASE - IMG_BASE)
    if off < 0 or off + n > len(RDATA):
        return None
    return RDATA[off:off + n]


def cstr(rva):
    off = rva - (RDATA_BASE - IMG_BASE)
    if off < 0 or off >= len(RDATA):
        return None
    end = RDATA.find(b"\0", off)
    return RDATA[off:end].decode("latin1", "replace")


def main():
    want = set(sys.argv[1:]) or None
    out = []
    desc = IMP_DIR_RVA
    while True:
        raw = rd(desc, 20)
        if raw is None:
            print("descriptor out of dump range", hex(desc))
            break
        oft, tds, fwd, name_rva, ft = struct.unpack("<IIIII", raw)
        if oft == 0 and ft == 0 and name_rva == 0:
            break
        dll = cstr(name_rva)
        int_rva = oft or ft
        i = 0
        while True:
            ent = rd(int_rva + i * 8, 8)
            if ent is None:
                break
            v = struct.unpack("<Q", ent)[0]
            if v == 0:
                break
            if v & (1 << 63):
                fname = "#ord%d" % (v & 0xFFFF)
            else:
                fname = cstr(v + 2)
            iat_va = IMG_BASE + ft + i * 8
            if fname and (want is None or fname in want):
                out.append((dll, fname, iat_va, int_rva + i * 8))
            i += 1
        desc += 20
    for dll, fname, iat_va, int_va in out:
        print(f"{dll:22s} {fname:28s} IAT 0x{iat_va:x}  INT 0x{int_va:x}")


if __name__ == "__main__":
    main()
