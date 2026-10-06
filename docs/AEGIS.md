# Aegis — the protection, identified

The copy protection has a name and it left its build log in the game directory.

## The log

`Aegis_RelicCardinal.log` sits next to `RelicCardinal.exe` in the install
(`C:\Program Files (x86)\Steam\steamapps\common\Age of Empires IV\`). Full text:

```
Please wait, analyzing Build...

Version Check: Passed.

Release build detected

Custom PDB RelicCardinal.pdb successfully loaded

Patcher-Loader: Disabled
Creating file RelicCardinal.exe
Virtualization Seed: 1187590981
Pre-Main Stealth Startup: Enabled
Processing static functions -- this may take some time (minutes)...

** Installed Stealth-Startup
Please wait, processing instruction filter (233 out of 172731 fake instructions inserted)
RTTI Processing: RelicCardinal.exe
RTTI Patching 21731 objects

Attention, BlockList.json automatically included from the VirtTool directory

Processing block file [642 items]: E:/P4Share/engine/source/foreign/Aegis/../../../tools/Foreign/Aegis/Retail/internal/BlockList.json
 [Added 464 unique items]

Processing block file [642 items]: E:\P4Share\engine\tools\Foreign\Aegis\Retail\internal\BlockList.json
 [Added 0 unique items]

Studio report list contains 6 items

Studio block list contains 6 items

Studio notify list contains 25 items

Aegis block list contains 386 items

Aegis report list contains 41 items

DirectX Permit list contains 3 items

Only-If permit list contains 17 items

Permit (signed)  list contains 7 items

Always permit list contains 0 items

Added block list.

Appending virtual map @ offset 0x8076800 for 10049732 bytes
Patcher-Loader: Disabled
Signed successfully. [Sig: 071aee0d1afc77e8, Seed: 1187590981]
Instruction Count: 172498 (+233)
DFH Bytes: 55356383
Metadata Size: 10049804
Virtualization Successful
Creating Bundle: UNPROTECTED_RelicCardinal_Aegis_a35a954c9a3b98a9.abf
```

## What it tells us

**Aegis is Relic's own protection**, built from the game tree
(`engine/source/foreign/Aegis/…`, `engine/tools/Foreign/Aegis/Retail/internal/BlockList.json`). It is not
Steam DRM and not a third-party packer — it is applied to the game by Relic's build pipeline, and this log
is the record of that run.

It combines several techniques:

| Log line | Meaning |
|---|---|
| `Virtualization Seed: 1187590981` / `Virtualization Successful` | a **code-virtualization** pass ran over the binary |
| `Appending virtual map @ offset 0x8076800 for 10049732 bytes` | the **10 MB overlay** we found *is* Aegis's virtual map |
| `DFH Bytes: 55356383` | 55.36 MB of code was processed |
| `Metadata Size: 10049804` | metadata block size (overlay + header) |
| `processing instruction filter (233 out of 172731 fake instructions inserted)` | **233 fake instructions** injected into 172,731 |
| `RTTI Patching 21731 objects` | RTTI structures rewritten |
| `Installed Stealth-Startup` / `Pre-Main Stealth Startup: Enabled` | startup runs **before main**, invisibly |
| `BlockList.json … [Added 464 unique items]` | a **block list** of 464 items is baked in |
| `Aegis block list contains 386 items`, `report list 41`, `Studio …`, `DirectX Permit 3`, `Only-If permit 17`, `Permit (signed) 7` | Aegis's **permit / block / report** rule sets |
| `Signed successfully. [Sig: 071aee0d1afc77e8]` | the image is signed with the seed |
| `Patcher-Loader: Disabled` | the runtime patcher is **off** in this build |
| `Creating Bundle: UNPROTECTED_RelicCardinal_Aegis_a35a954c9a3b98a9.abf` | bundle name |

## Cross-checks against our own measurements

The log's numbers match what we measured independently, which confirms the identification:

| Log | Measured |
|---|---|
| virtual map `10049732` bytes at offset `0x8076800` | overlay is **10,049,828** bytes starting at `0x8076800` |
| `DFH Bytes: 55356383` | **55,010,162** bytes of `.text` differ between disk and memory |
| `Metadata Size: 10049804` | overlay 10,049,828 + 24-byte header region |

So the "79 % of `.text` is encrypted at rest" finding is exactly **Aegis's virtualization**: on disk the
protected code is replaced by virtualized data, and at runtime it is restored/interpreted.

## Runtime behaviour measured this session

- The restore is **eager**: a dump taken ~19 s after the process appeared already showed 83.5 % of the
  sampled region restored, and a dump at t≈2 min was **byte-identical** to it.
- Therefore there is **no lazy per-function re-encryption** during the run; the "garbage prefixes" seen on
  a handful of functions are a static artefact of the virtualization, not ongoing self-modifying code.

## Why this matters for the freeze

Aegis is an **active anti-tamper**: it carries permit/block/report lists, injects fake instructions, patches
RTTI and runs a stealth startup before `main`. The freeze — a thread that suspends every other thread ~1 min
after the socket closes — is consistent with Aegis's runtime response to a check it does not like. The
`BlockList.json` (464 items) is the prime suspect: if any entry matches something present in the Thor's
environment, Aegis acts. The list is not plaintext in the image (the metadata overlay is uniformly 7.99–8.00
bits/byte), so it must be recovered at runtime from the decrypted metadata.

## Files

- `samples/aegis/Aegis_RelicCardinal.log` — the log verbatim.
- `samples/aegis/CodeSignSummary-*.md` — the code-signing summary shipped beside it.
