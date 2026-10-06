# Two things to check before trusting an offline analysis

Both of these cost real time this session. Neither is obvious from the files themselves.

## 1. `text.bin` is indexed by `RVA - 0x1000`, not by RVA

`text.bin` is 91,082,752 bytes = `0x56dd000`, which is **exactly** `.text`'s `RawSize`:

```
.text  VA=0x1000  VSize=0x56dcfdc  RawPtr=0x600  RawSize=0x56dd000
```

So it is the section's *contents*, with no PE headers in front of it, and reading "RVA X" means
indexing `text.bin[X - 0x1000]`. Indexing `text.bin[X]` is off by one page.

Verifying against the file is confusing rather than clarifying, because the two legitimately differ:
`text.bin` is the **restored** `.text` (recovered from the live process), while `RelicCardinal.exe` on
disk is still packed. At the kill function:

```
rva 0x3e6b53c   RelicCardinal.exe  : 069437e6f4 5805d23c...
                text.bin[rva-0x1000]: 069437e6f4 48897424...   <- same 5 bytes, then diverges
```

The 5-byte agreement is what identifies it: same location, different (restored) content.

**Consequence:** the *content* findings from the offline analysis stand, but any address quoted from it
was one page too low. The delay constants and the `"SDC"` decrypt loop in
[AEGIS.md](AEGIS.md) are really at RVA + 0x1000. The region sweeps are unaffected in substance, since
the Aegis region is 1.4 MB wide.

## 2. `RelicCardinal.unpacked.exe` is the analysable binary

`RelicCardinal.exe` on disk is packed, so static analysis of it is analysis of the packer.
`RelicCardinal.unpacked.exe` has the restored `.text` and matches `text.bin` at every offset sampled
(0, 0x1000, 0x100000, 0x2000000, 0x3e5b53c, 0x56dc000). **Use the unpacked one** — the image base is
`0x140000000`, so VA = `0x140000000 + RVA`.

## 3. The Mac already had the tooling, and it is the right place for this work

[KILL-ANALYSIS.md](KILL-ANALYSIS.md) already sets the next step: *"trace the callers of the
`+0x563cc` / `+0x56bc0` xxHash64 routines to learn the hashed address range and the expected hash."*
Doing that by hand with a disassembler is the wrong tool, and the Mac has the right one:

| | |
|---|---|
| Ghidra 11.3.1 | `~/Applications/ghidra_11.3.1_PUBLIC`, with `support/analyzeHeadless` |
| JDK 17.0.16 | Homebrew, plus JDK 23 |
| RAM | 64 GB |

64 GB is ample for a 145 MB binary. `ghidra_scripts/DecompileTargets.py` decompiles the kill path and
both xxHash64 routines into `ghidra_decomp.txt`.

**Also present and previously overlooked:** Ghidra, CrossOver (no bottles configured), Porting Kit, UTM,
mitmproxy, and a Wineskin wrapper for AoE2 DE. That wrapper's engine is currently broken —
`wineserver` fails with `Library not loaded: @rpath/libinotify.0.dylib`, and the library is absent from
the engine — so it cannot start a prefix as it stands.
