#!/usr/bin/env python3
"""Binary-patch the ARM64EC ntdll.dll of GameNative's proton-11.0-1-arm64ec.

    apply.py <ntdll.dll> <out.dll> [--waitq]

The input is lib/wine/aarch64-windows/ntdll.dll from
https://downloads.gamenative.app/proton-11.0-1-arm64ec.wcp (a zstd tar). All offsets below
belong to that exact build, so the script refuses any other file.

Patch 1 (always): invoke_arm64ec_syscall.s replaces the x64 stub that runs a direct
`syscall` from emulated x64 code. Wine's stub clobbered rdx/r8/r9/r10/rflags; the Windows
kernel keeps them.

Patch 2 (--waitq): waitq_fix.s makes the RtlWaitOnAddress / RtlWakeAddress* spinlock safe
against NtSuspendThread. It did not stop the AoE IV freeze, but the Wine bug is real.

Needs Homebrew mingw-w64 (x86_64-w64-mingw32-as/objcopy) and llvm (llvm-mc/objcopy/nm).
"""
import hashlib
import os
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
LLVM = "/opt/homebrew/opt/llvm/bin"
PRISTINE = "606d0a2fb197d37b4746fe4bcfed6ef1b5f4980b9fd27deccce45c0806623184"
EXPECTED = {
    False: "5325f69ecbce31f341018dcc78f1f7c14ae27629bcd56d9d13eeeb0b2538563f",
    True: "ce925da602e6abfe2c37c7bbd9ce213abf406c6748e7d6afa4d0b9c367823e00",
}
IMAGE_BASE = 0x180000000


def tool(name):
    path = shutil.which(name) or os.path.join(LLVM, name)
    if not os.path.exists(path):
        sys.exit(f"missing tool: {name}")
    return path


def assemble_x64(src, tmp):
    obj, raw = os.path.join(tmp, "invoke.o"), os.path.join(tmp, "invoke.bin")
    subprocess.run([tool("x86_64-w64-mingw32-as"), "-o", obj, src], check=True)
    subprocess.run([tool("x86_64-w64-mingw32-objcopy"), "-O", "binary", "-j", ".text", obj, raw], check=True)
    return open(raw, "rb").read()


def assemble_arm64(src, tmp):
    obj, raw = os.path.join(tmp, "waitq_fix.o"), os.path.join(tmp, "waitq_fix.bin")
    subprocess.run([tool("llvm-mc"), "--triple=aarch64-linux-gnu", "-filetype=obj", "-o", obj, src], check=True)
    subprocess.run([tool("llvm-objcopy"), "-O", "binary", "-j", ".text", obj, raw], check=True)
    syms = {}
    for line in subprocess.run([tool("llvm-nm"), obj], capture_output=True, text=True, check=True).stdout.splitlines():
        parts = line.split()
        if len(parts) == 3:
            syms[parts[2]] = int(parts[0], 16)
    return open(raw, "rb").read(), syms


def patch_invoke(d, code):
    # Cave in the zero padding after .text; the original stub at ORIG becomes a jmp to it.
    cave, table, orig = 0xEC100, 0x11FA88, 0xEC050
    code = bytearray(code[:0xC7])
    assert d[orig:orig + 5] == bytes.fromhex("4c89542408"), "unexpected invoke_arm64ec_syscall"
    assert all(b == 0 for b in d[cave:cave + 0x100]), "cave is not empty"
    assert code[0xA8:0xAB] == bytes.fromhex("4c8d1d"), "lea of the syscall table moved"
    struct.pack_into("<i", code, 0xAB, table - (cave + 0xAF))
    d[cave:cave + len(code)] = code
    d[orig] = 0xE9
    struct.pack_into("<i", d, orig + 1, cave - (orig + 5))
    # Grow .text's VirtualSize so the cave is mapped executable.
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    sec = pe + 24 + struct.unpack_from("<H", d, pe + 20)[0]
    assert bytes(d[sec:sec + 8]).rstrip(b"\0") == b".text"
    struct.pack_into("<I", d, sec + 8, 0xDC200)


def patch_waitq(d, cave_code, syms):
    # Cave over the EC copy of DbgUiConvertStateChangeStructure (712 bytes). Raw offset == RVA here.
    cave = 0x1800B491C

    def ldaxr(rt, rn):
        return 0x885FFC00 | (rn << 5) | rt

    sites = {
        "stub_L1": (0x1800CDA00, ldaxr(12, 8)),
        "stub_L2": (0x1800CE194, ldaxr(10, 26)),
        "stub_L3": (0x1800CE2AC, ldaxr(9, 26)),
        "stub_L4": (0x1800CE524, ldaxr(9, 20)),
        "stub_U1": (0x1800CDA74, ldaxr(31, 8)),
        "stub_U2": (0x1800CDA98, ldaxr(31, 8)),
        "stub_U3": (0x1800CE250, ldaxr(31, 26)),
        "stub_U4": (0x1800CE280, ldaxr(31, 26)),
        "stub_U5": (0x1800CE2F8, ldaxr(31, 26)),
        "stub_U6": (0x1800CE5D8, ldaxr(31, 20)),
        "stub_U7": (0x1800CE604, ldaxr(31, 20)),
    }
    for name, (va, expected) in sites.items():
        got = struct.unpack_from("<I", d, va - IMAGE_BASE)[0]
        assert got == expected, f"{name}: {got:#x} at {va:#x}, expected {expected:#x}"
    assert len(cave_code) <= 712
    d[cave - IMAGE_BASE:cave - IMAGE_BASE + len(cave_code)] = cave_code
    for name, (va, _) in sites.items():
        imm = (cave + syms[name] - va) // 4
        assert -(1 << 25) <= imm < (1 << 25)
        struct.pack_into("<I", d, va - IMAGE_BASE, 0x14000000 | (imm & 0x3FFFFFF))


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    waitq = "--waitq" in sys.argv
    if len(args) != 2:
        sys.exit(__doc__)
    d = bytearray(open(args[0], "rb").read())
    if hashlib.sha256(d).hexdigest() != PRISTINE:
        sys.exit("input is not the pristine proton-11.0-1-arm64ec ntdll.dll")
    with tempfile.TemporaryDirectory() as tmp:
        patch_invoke(d, assemble_x64(os.path.join(HERE, "invoke_arm64ec_syscall.s"), tmp))
        if waitq:
            patch_waitq(d, *assemble_arm64(os.path.join(HERE, "waitq_fix.s"), tmp))
    digest = hashlib.sha256(d).hexdigest()
    if digest != EXPECTED[waitq]:
        sys.exit(f"result {digest} does not match the tested build {EXPECTED[waitq]}")
    open(args[1], "wb").write(d)
    print(f"wrote {args[1]} ({digest})")


if __name__ == "__main__":
    main()
