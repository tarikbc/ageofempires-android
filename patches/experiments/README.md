# Patch experiments

Earlier patches from the investigation. None of them is needed to run the game; the patches that are needed are in
[`patches/fex`](../fex).

| Folder | What it is |
|---|---|
| [`box64/`](box64) | Box64 patches for running x86-64 Wine under Box64. That route was blocked before the kill window ([BOX64-ROUTE.md](../../docs/research/archive/BOX64-ROUTE.md)) |
| [`proton-arm64ec-ntdll/`](proton-arm64ec-ntdll) | Patches to Wine's ARM64EC `ntdll.dll`. They were never loaded ([NTDLL-NEVER-LOADED.md](../../docs/research/archive/NTDLL-NEVER-LOADED.md)) |
| [`gamenative/`](gamenative) | A GameNative change to refresh the Steam ticket on every ColdClient launch. Not built or tested (see the patch header) |
