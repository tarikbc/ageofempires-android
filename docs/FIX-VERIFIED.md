# The Aegis kill is gone: patched FEX, verified on hardware

Measured on the AYN Thor, 2026-10-06.

## The two halves of the fix

**1. Stop leaking the SMC trap** (`patches/fex/0003-no-smc-write-trap.patch`).
`InvalidationTracker::GetTrapProt()` no longer returns `PAGE_EXECUTE_READ`; it returns the untrapped
protection. FEX therefore stops silently removing write permission from the guest's own RWX pages — the
thing Aegis's 35,248 `NtQueryVirtualMemory` calls per run would see.

**2. Keep invalidation correct** (`patches/fex/0001-hide-smc-trap-from-guest.patch`).
Without the trap, correctness rests on `ForceFullSMCDetection`, which the decoder now sets for blocks in
writable executable regions. It validates each translated instruction against guest memory as it runs,
so self-modified code is still caught — with no protection change.

Doing only one of these is wrong, and the first attempt made exactly that mistake: patch 0001 alone adds
validation but leaves the trap armed, so `smctest` still reported `RX`. The two are complementary, not
alternatives.

## Verification, in order

**The trap is gone** — `tools/smctest.c`, run inside the guest:

```
after executing the page           Protect=RWX      (0x40)     <- was RX (0x20) before
VirtualProtect(..., RWX) ok, previous=0x40                     <- was 0x20 before
```

Compare [SMC-CONFIRMED.md](SMC-CONFIRMED.md), which shows `RX` on the stock build.

**The kill is gone** — with the patched FEX loaded, the game runs and stays running:

| | stock FEX | patched FEX |
|---|---|---|
| process lifetime | ~2 min, then gone | **10+ min and still alive** |
| log | stalls, process exits | keeps progressing |
| furthest loading step | `[Property Bag Manager]` | **`MapGen`** |

Two `RelicCardinal.exe` processes were alive at `ps` after ten minutes. The Aegis suspend-all never
fired.

## What is still wrong

**The game is not yet playable.** It reaches `MapGen` and stops, logging:

```
(I) [19:38:11.023] [000000924]: MapGen - Failed to validate: !m_texturePath.empty()
    Failed to validate an attribute data field for map generation.
```

The screen shows a black frame with the AoE cursor. So the protection no longer kills the game, but
something downstream does not complete.

Two candidate causes, not yet separated:

1. **A real asset/loading problem** — an empty texture path suggests missing or unreadable data.
   Complicated by having **two instances running at once** (the Play launch plus a relaunch in the same
   session), which could contend over the same archives.
2. **A consequence of the patch** — if `ForceFullSMCDetection` ever invalidates valid code, the game
   could compute wrong data. This matters because it is the mechanism now carrying correctness.

**Next step: run a single clean instance.** Kill both, launch one, and see whether `MapGen` completes.
That separates the two causes cheaply.
