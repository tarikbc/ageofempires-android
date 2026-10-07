"""Decode a peek dump of the detection table (0x38-byte records).
usage: dettable.py PEEKFILE"""
import struct
import sys

data = bytearray()
base = None
for line in open(sys.argv[1]):
    if ":" not in line or line.startswith("pid"):
        continue
    addr, rest = line.split(":", 1)
    if base is None:
        base = int(addr, 16)
    data += bytes(int(x, 16) for x in rest.split())
for i in range(len(data) // 0x38):
    func, cb, arg, ident = struct.unpack_from("<QQQQ", data, i * 0x38)
    f20, f21 = data[i * 0x38 + 0x20], data[i * 0x38 + 0x21]
    tail = data[i * 0x38 + 0x22: i * 0x38 + 0x38].hex()
    print(f"{i:2d} func {func:#014x} cb {cb:#012x} arg {arg:#010x} id {ident:#5x} reported {f20} skip {f21} tail {tail}")
