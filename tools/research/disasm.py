#!/usr/bin/env python3
"""Disassemble x86-64 code at specific RVAs from a raw dump of a module image.

usage: disasm.py <dump.bin> <base_hex> <rva_hex> [rva_hex ...]
Prints ~48 instructions starting at each RVA (as RVA-offset labels), and also the bytes.
Useful to inspect the protection thread entry and its call-chain return addresses.
"""
import capstone
import sys

def main():
    dump = open(sys.argv[1], "rb").read()
    base = int(sys.argv[2], 16)
    rvas = [int(a, 16) for a in sys.argv[3:]]
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    for rva in rvas:
        off = rva - base
        print(f"\n===== RVA +0x{rva-base:x} (VA 0x{rva:x}) =====")
        if off < 0 or off >= len(dump):
            print("  (outside dump range)")
            continue
        code = dump[off:off + 0x200]
        n = 0
        for ins in md.disasm(code, rva):
            raw = code[ins.address - rva:ins.address - rva + ins.size].hex()
            print(f"  +0x{ins.address-base:x}: {raw:<28s} {ins.mnemonic} {ins.op_str}")
            n += 1
            if n >= 48:
                break

if __name__ == "__main__":
    main()
