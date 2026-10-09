# Turnip patches

Patches against Mesa main `c78a9e9` (2026-10-08), the base of the repo's Turnip driver. Build: apply them in order to
a clone at that commit, then `tools/build_turnip.sh MESA_SRC NAME` ([BUILDING.md](../../docs/guides/BUILDING.md),
"The Turnip driver").

| Patch | Status |
|---|---|
| `0001` (2026-10-09) | Opt-in UBWC for vkd3d-proton's typeless render targets: `TU_UBWC_RGBA8_IGNORE_R32=1` and `TU_UBWC_RGBA16F_INT=1` in the container's environment. Without the variables the driver behaves like the unpatched `c78a9e9` build. With them, AoE IV gained 1.5 FPS (2.6 %) in alternating skirmish runs, and the driver warns in logcat (`UBWC opt-in: ... is in another UBWC class`) if the game ever views a compressed image in an incompatible format; it did not in the logged runs. In release v1.5.0 as `turnip-main-c78a9e9-ubwc`. [UBWC.md](../../docs/how-it-works/UBWC.md) |
