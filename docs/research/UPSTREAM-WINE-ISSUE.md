# Draft Wine bug: ARM64EC `-import` exports start with a bare x64 `jmp [rip+x]`

Not filed. This is the source-level cause of the hook-check failure in [HOOK-CHECK.md](../how-it-works/HOOK-CHECK.md),
written as a Wine bug report. FEX patch 0007 works around it inside the emulator; a fix in Wine would make that patch
unnecessary. Sources were read on 2026-10-07 at Wine master `63f62f7cd696` and llvm-project main `1a5b507c6c6f`.

## Summary

On ARM64EC, functions that a DLL re-exports from another DLL (`-import` in the `.spec` file; kernel32.spec has many)
are exported as lld's x64 import thunk, which is a bare `FF 25 00000000` (`jmp *__imp_X(%rip)`). x64 code that resolves
such an export (`GetProcAddress`) sees a function that starts with a 6-byte indirect jump. x64 hook detectors in
games treat that start as an inline hook. On x86_64 Wine the same export starts with winebuild's hot-patch prolog
instead.

## What the code does

- **winebuild, x86_64:** the end of `output_exports` in
  [`tools/winebuild/spec32.c`](https://github.com/wine-mirror/wine/blob/63f62f7cd696/tools/winebuild/spec32.c#L585-L622)
  emits, for every `-import` entry, eight `nop`s, the label `__wine_spec_imp_<name>`, the hot-patch prolog
  `48 8D A4 24 00 00 00 00` (`lea rsp,[rsp+0]`) and `jmp *__imp_<name>(%rip)`. The export points at that thunk.
- **winebuild, ARM64EC:** the start of the same function
  ([lines 414 to 439](https://github.com/wine-mirror/wine/blob/63f62f7cd696/tools/winebuild/spec32.c#L414-L439))
  writes only `.drectve` directives `-export:Name=Name,@ord` and returns. The plain `Name` of an import is lld's x64
  import thunk.
- **lld:** the x64 import thunk is `importThunkX86[] = {0xff, 0x25, 0x00, 0x00, 0x00, 0x00}`
  ([`lld/COFF/Chunks.h`](https://github.com/llvm/llvm-project/blob/1a5b507c6c6f/lld/COFF/Chunks.h#L545-L547)).
  For exports whose target is ARM64EC code, lld emits a fast-forward sequence (`ECExportThunkCode`, `48 8B C4 48 89
  58 20 55 5D E9 ...`, [same file](https://github.com/llvm/llvm-project/blob/1a5b507c6c6f/lld/COFF/Chunks.h#L839-L847)),
  as Microsoft's toolchain does for all DLL exports ([ARM64EC ABI, "Fast-forward
  sequences"](https://learn.microsoft.com/en-us/windows/arm/arm64ec-abi)); an import thunk gets none.

## How it shows

AoE IV (Relic's Aegis) checks 63 Windows API functions at start-up. Under GameNative's Proton 11.0 ARM64EC with FEX,
27 of them were flagged, all in kernel32's `.text`, all starting with `FF 25` (measured, see
[HOOK-CHECK.md](../how-it-works/HOOK-CHECK.md)). Presenting the same jumps as `48 FF 25` (FEX patch 0007) made the
check flag nothing.

## Possible fix (not tested)

The following was proposed by reading the source; none of it was built or run here.

1. In `output_exports`, for ARM64EC `-import` entries, emit the same x86_64 thunk as on x86_64 (nop pad, hot-patch
   prolog, `jmp *__imp_<name>(%rip)`) and point the `-export:` directive at it. Wine merge request
   [!11975](https://gitlab.winehq.org/wine/wine/-/merge_requests/11975) (winebuild assembling ARM64EC output as
   x86_64) looks like the base for this.
2. `arm64x_check_call` in
   [`dlls/ntdll/signal_arm64ec.c`](https://github.com/wine-mirror/wine/blob/63f62f7cd696/dlls/ntdll/signal_arm64ec.c#L1897-L1979)
   follows a bare `FF 25` to its target so that ARM64EC callers skip the emulator. It would need to skip the hot-patch
   prolog as well, or calls through these exports would run in the emulator (correct, but slower).

Not checked: what Microsoft's own ARM64X kernel32 or link.exe produce for a re-exported import, and whether the
game's detector accepts the `lea` prolog form. The game's start-up decision under Box64, which runs x86_64 Wine, took
the same branch as with patch 0007 ([HOOK-CHECK.md](../how-it-works/HOOK-CHECK.md)).
