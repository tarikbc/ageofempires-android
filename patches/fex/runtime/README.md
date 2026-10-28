# Volatile-code runtime variant

This variant carries the reviewed runtime changes from PR #36 without optional diagnostic patches 0018 and 0019.
It keeps the cache, boundary, permission, and failed-decode fixes.
The base package's existing counters remain. The optional logger, observer hooks, and new sample counters are absent.

This is a test build. Game loading times, FPS, and sustained gameplay still need independent tests.
The stable release remains v1.6.0.

## Apply

Use FEX base `7d3090f78237267b2adf2d32116f0105b8d665cb`.
Apply the released patch set, then this directory's 0020:

```sh
for p in 0004 0006 0007 0009 0010 0012 0013 0014 0015 0016 0017; do
  git apply /path/to/ageofempires-android/patches/fex/$p-*.patch
done
git apply /path/to/ageofempires-android/patches/fex/runtime/0020-volatile-code-pages.patch
```

Do not apply root patches 0018, 0019, or 0020 to this variant.
The root 0020 remains the research variant and requires 0018 and 0019.

Build with the toolchain and macOS setup in [BUILDING.md](../../../docs/guides/BUILDING.md).
Build target `arm64ecfex`.

## Check

Run from the ageofempires-android repository:

```sh
python3 tools/tests/test_volatile_patches.py /path/to/runtime/FEX --runtime-only
```

The runtime checks cover decoder boundaries, reuse keys, permission changes, disk-cache rejection, and failed-decode validation.
The three optional diagnostic tests do not apply to this variant.
These host checks do not execute Windows/FEX or the game.

Use the [AoE II test protocol](../../../docs/guides/AOE2-VOLPAGE-TESTING.md) before a stable release.
