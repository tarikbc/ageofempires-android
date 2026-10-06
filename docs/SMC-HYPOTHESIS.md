# Aegis uses self-modifying code, and FEX's SMC handling is the suspect

This ties together three things that were already known separately, and it is the first explanation
that accounts for the *whole* pattern.

## The three facts

**1. `SMCChecks` is a FEX setting, and it changes the failure.** From the repo's own notes, the per-game
FEX config (`Z:\home\xuser\.fex-emu\AppConfig\RelicCardinal.exe.json`) takes raw values such as
`"SMCChecks":"2"`. SMC = **self-modifying code**. The observed behaviour:

| `SMCChecks` | Result |
|---|---|
| `none` | exits after ~2 minutes |
| `full` | hangs at launch from Play |
| `mtrack` (default) | the freeze described throughout this repo |

Every value changes the failure. That is not what an *environment-detection* check would do — it is what
a **code-translation** problem looks like.

**2. Aegis demonstrably self-modifies.** Round 22 found a `call` whose target holds high-entropy data
rather than code, with the preceding function ending cleanly at `pop rsi; ret`:

```
RVA 0x3ddea50: ... 5e c3 c2 00 00 cc          <- end of the previous function
RVA 0x3ddea6c: d5 d8 be e7 9e d9 5f 9d ...    <- data where code is expected
```

That is only possible if the bytes were written at runtime.

**3. Aegis encrypts its own metadata.** The descriptor table at RVA `0x7af8204` pairs function RVAs with
pointers into `.data` holding high-entropy blobs (`326c20a594dae3fd263b6da5cf5605fa`), so even function
names are unreadable statically.

## The hypothesis

> Aegis writes code at runtime and executes it. Under ARM64EC + FEX that self-modified code is
> **mistranslated or incompletely invalidated**, so Aegis's own integrity computation produces a wrong
> result and it responds by killing the game.

This explains, in one stroke:

- **why nothing environmental fixes it** — the CPUID patch, TLS revocation, module names, memory
  stability, debugger signals, session health. None of them touch translation correctness.
- **why the Mac passes** — Rosetta is a different translator with different SMC semantics.
- **why `SMCChecks` moves the failure around** rather than removing it.
- **why the image looks byte-stable before the kill** (round: "Memory integrity: Aegis image
  byte-identical at t=128 s through the kill"). Aegis's *own* writes happen early, at init; the game's
  memory is then quiet while the mistranslated check runs to its conclusion.

## How to test it

1. **Read FEX's SMC implementation for ARM64EC.** The repo already builds FEX from source
   (llvm-mingw + CMake, ~12 s), so a patch is cheap. Look for whether ARM64EC-mode block invalidation
   covers *all* the ways code can be rewritten — especially writes that land in a region FEX has already
   translated, and writes that cross a block boundary.
2. **Runtime confirmation.** Instrument the hash routine (`0x3e42d34`, single call site `0x3e439a7`) to
   record `(rcx, rdx)` — the pointer and length — and see whether the hashed range covers a
   self-modified region.
3. **Differential.** Run the same build with `SMCChecks=mtrack` and `none`, and compare what the hash
   routine is asked to digest.

## Caveat

This is a hypothesis with strong circumstantial support, not a proven cause. Its main virtue is that it
is the only one so far that predicts the observed `SMCChecks` sensitivity.
