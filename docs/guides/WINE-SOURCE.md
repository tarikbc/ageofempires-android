# Which Wine the Thor runs, and what it lacks

2026-10-06/07. Sources: the device's own `ntdll.dll`, GameNative's GitHub releases, and a research pass over
Valve's and upstream Wine (marked "research" below where I did not re-check each commit myself).

## The device's Wine is GameNative's Proton 11.0-1 ARM64EC

- The container's Wine Version is shown as `proton-11.0-99-arm64ec-1`. Its `ntdll.dll` (SHA-256 `606d0a2f…`,
  the "pristine" ntdll in [NTDLL-NEVER-LOADED.md](../research/archive/NTDLL-NEVER-LOADED.md)) is byte-identical to the one in
  GameNative's official download `proton-11.0-1-arm64ec.wcp` (research). The "-99" name comes from an
  earlier manual import, not from a different build.
- The DLL carries the build path `/home/runner/work/proton-wine/proton-wine`: it was built by GitHub Actions in
  [GameNative/proton-wine](https://github.com/GameNative/proton-wine), a fork of Valve's Wine.
- Release [`build-p11-20260502-1-sdk35`](https://github.com/GameNative/proton-wine/releases/tag/build-p11-20260502-1-sdk35)
  says (checked): "Proton Version: 11.0-1. Built from commit: 7c98acd6eafe1b1bac00e20d108f77ba39f3ff06",
  based on Valve's `proton_11.0` branch.

## Syscall numbers differ from upstream Wine

Wine numbers syscalls like Windows, but only the older calls match upstream exactly. Decoded from the device's
own stubs with [`tools/research/ntdll_syscall_table.py`](../../tools/research/ntdll_syscall_table.py) (258 syscalls):

| call | device | Wine master table |
|---|---|---|
| `NtQueryVirtualMemory` | `0x23` | `0x23` |
| `NtProtectVirtualMemory` | `0x50` | `0x50` |
| `NtResumeThread` | `0x52` | `0x52` |
| `NtGetContextThread` | `0x97` | `0x9d` |
| `NtGetNextThread` | `0x9a` | `0xa0` |
| `NtSetContextThread` | `0xde` | `0xe5` |
| `NtSuspendThread` | `0xf5` | `0xfc` |

So anything that decodes syscall numbers (a trace, a log) must use the table of the exact ntdll it ran with.

## ARM64EC thread-suspension fixes it does not have (research)

Upstream Wine (11.9 to 11.17) and Valve's `experimental_11.0` / `bleeding-edge` fix how ARM64EC Wine suspends a
thread and reports its context: cooperative suspend with the emulator (b8d8f34f, 211e7a3d), the context of a
suspended thread showing the x64 game state rather than Wine's dispatcher (3b6b0ced, f12bd89a, d3b41a85), and
consistent state for a suspend that lands in the syscall dispatcher (fc2ba3ff, 6ddac454, f286074a, 9b0165a3),
among others. According to the research none of them is in `7c98acd6`. It does contain Proton's waiting
`NtSuspendThread` (7ad5d08a) and an early "WIP: ntdll: ARM64EC suspend support" patch (189b5e87), in which
`NtGetContextThread` on another thread suspends and resumes that thread internally.

Why this matters here: the Aegis kill is a mass `SuspendThread`, and in run 1 the suspended main thread
reported `rip=6578653414`, an address outside every module ([KILL-REMEASURED.md](../research/archive/KILL-REMEASURED.md) samples).
If Aegis inspects suspended threads, a wrong context could be what it reacts to. **That is a hypothesis.**

## A newer build exists

[`proton-11.0-2-20260928`](https://github.com/GameNative/proton-wine/releases/tag/proton-11.0-2-20260928)
(checked: "Proton Version: 11.0-2. Built from commit: 555aa70febb7d36e82d96697b554ff8f4fe0bb1a"). Per the
research it adds the cooperative-suspend fixes b8d8f34f, 211e7a3d and 6e8bf984, but not the context fixes. Its
profile says "Needs a fresh arm64ec container"; it also switches synchronisation to ntsync. GameNative's own
content list stops at 11.0-1 for ARM64EC, so it has to be imported as a `.wcp`
(`/sdcard/Download/proton-11.0-2-arm64ec.wcp`, SHA-256 `fffa467241bdae3e…`, pushed 2026-10-06). **Not tested
yet.**

## The device's FEX already has FEX's own suspend fixes (research)

FEX `7d3090f` is after d2714f3338 and 8212f4b7fb (FEX noticing a suspend request inside long loops).
