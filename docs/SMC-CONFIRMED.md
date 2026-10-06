# CONFIRMED: FEX leaks its SMC write trap to the guest

Measured on the AYN Thor, in a live GameNative container, 2026-10-06.

`tools/smctest.c` runs **inside the guest** (emulated by FEX), allocates a page as `PAGE_EXECUTE_READWRITE`,
and asks `VirtualQuery` what protection it actually has.

## Result

```
VirtualAlloc(PAGE_EXECUTE_READWRITE) -> 0000000000BC0000 (err=0)

immediately after VirtualAlloc     Protect=RWX      (0x40)
after 100ms                        Protect=RWX      (0x40)

wrote 6 bytes of code into the page
after writing to the page          Protect=RWX      (0x40)
executed the page -> returned 42
after executing the page           Protect=RX       (0x20)   <-- write permission missing

VirtualProtect(..., PAGE_EXECUTE_READWRITE) ok, previous=0x20
after explicit VirtualProtect RWX  Protect=RWX      (0x40)
```

## What it means

**The page is `RWX` right up to the moment code in it is executed. Immediately afterwards it is `RX`** —
the guest's write permission has been removed, and the guest did not ask for that.

That is precisely the behaviour located in FEX source ([SMC-HYPOTHESIS.md](SMC-HYPOTHESIS.md)):

- `InvalidationTracker::GetTrapProt()` returns `PAGE_EXECUTE_READ`;
- `ProtectRWXIntervalsInternal` applies it via `NtProtectVirtualMemory` when the guest marks a range
  executable;
- FEX intercepts the guest's `NtAllocateVirtualMemory` and `NtProtectVirtualMemory` but **not
  `NtQueryVirtualMemory`**, so the guest sees the trap.

The transition point is itself informative: the trap is armed when FEX **translates** the page, not when
the guest writes to it. And `VirtualProtect` restores `RWX` (previous reported as `0x20`), so this is a
protection change rather than a mapping change — which is why the byte-comparing memory probe never saw
anything.

## Why it matters

Aegis, AoE IV's anti-tamper, calls `NtQueryVirtualMemory` **35,248 times per run** (measured separately
from a 1 GB logcat capture). A page whose write permission has been silently removed is exactly the kind
of thing such a check exists to notice.

## Status

- **Confirmed:** the trap exists and is visible to the guest.
- **Not yet shown:** that Aegis acts on it. That needs the game run against the patched FEX
  (`fexcore-aoe-smcfix.wcp`) and judged by the only valid criterion — `warnings.log` still growing after
  five minutes.

The patch replaces the re-protection with `ForceFullSMCDetection`, which validates translated
instructions against guest memory at run time and needs no protection change at all — see
[`patches/fex/0001-hide-smc-trap-from-guest.patch`](../patches/fex/0001-hide-smc-trap-from-guest.patch).
