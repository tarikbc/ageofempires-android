# The game's module list is full of names Windows does not have

> **Archived.** Ruled out (2026-10-06). The rule for which emulator DLL name the game loads is in [GAMENATIVE.md](../../guides/GAMENATIVE.md). "The README" is the first README (git `7e7ae2a`). The story: [STORY.md](../../STORY.md).

> **Status: ruled out as the trigger.** The emulator DLL name was tested before this session
> (`xtajit64.dll`) and the game still died — see [LEDGER.md](../LEDGER.md). The FEX-branded name
> is a real cosmetic regression worth reverting, but it is **not** the cause, and no further rounds
> should be spent on it. Left here as a record of the module names, which remain interesting for the
> hash hypothesis in [KILL-ANALYSIS.md](KILL-ANALYSIS.md).

Aegis carries a **386-item blocklist** ([AEGIS.md](../../how-it-works/AEGIS.md)). Enumerating what the game actually has
loaded, 85 modules, turns up several names that cannot exist on a real Windows machine:

| module | why it is a giveaway |
|---|---|
| `C:\windows\system32\libarm64ecfex.dll` | **the FEX emulator**; Windows ARM64 ships `xtajit64.dll` |
| `c:\windows\system32\winex11.drv` | Wine display driver |
| `C:\windows\system32\winevulkan.dll` | Wine Vulkan |
| `C:\windows\system32\winepulse.drv`, `winealsa.drv` | Wine audio drivers |
| `C:\windows\system32\lsteamclient.dll` | Steam's Linux client shim |
| `…\winsxs\amd64_microsoft.windows.common-controls_…_none_deadbeef\COMCTL32.dll` | Wine's placeholder GUID |

Reproduce with `tools/research/modlist.c` (run inside the session, target `RelicCardinal.exe`).

## A regression of mine, found by doing this

The baseline this session inherited used **`xtajit64.dll`** — the name Microsoft's own x64 emulator has
on Windows ARM64, i.e. a module that is *supposed* to be there. It now uses `libarm64ecfex.dll`, which
advertises FEX. That is my doing, from round 1: I edited `wine.inf`'s

```
HKLM,Software\Microsoft\Wow64\amd64,,2,"xtajit64.dll"
```

to `libarm64ecfex.dll` while chasing the CPUID question, and verified the edit landed.

Reverting `wine.inf` and the registry did **not** change the loaded module — GameNative forces the name
at launch. The actual cause is the `.wcp` manifest: the previous session's bundle targets **only**
`libarm64ecfex.dll` + `libwow64fex.dll`, and GameNative copies `libarm64ecfex.dll` to `xtajit64.dll`
itself, then loads `xtajit64.dll`. My `fexcore-2610-aoe-nofex2.wcp` added an explicit
`${system32}/xtajit64.dll` target, which overrode that and made the FEX-branded name the live one.

So the giveaway name is a self-inflicted change, and it is fixable without losing the CPUID patch:
`fexcore-2610-aoe-namefix.wcp` (`versionCode` 5) ships the patched `libarm64ecfex.dll` and targets
**only** `libarm64ecfex.dll` + `libwow64fex.dll`. Selecting it should put the emulator back under the
legitimate `xtajit64.dll` name.

## Why this might matter more than it looks

If Aegis reports the loaded-module set — or hashes of it — to its backend and lets the **server** decide,
then:

- every purely local fix (CPUID, memory integrity, the waitq patch, TLS) would fail to stop the kill,
  which is exactly what has been observed across four attempts;
- the kill would arrive *after* a server round-trip, matching its ~4-minute delay;
- the game's `C06T13R-1X-*` server error would be a symptom of the same conversation.

That is a hypothesis, not a result, and it is the first one that explains the pattern of "nothing local
changes it". It also means the practical lever is module **names**, which is testable.

## Next

1. Import and select `fexcore-2610-aoe-namefix.wcp`; confirm with `modlist` that the emulator is
   `xtajit64.dll` and not `libarm64ecfex.dll`.
2. If the kill persists, attack the remaining Wine-only names — `winex11.drv`, `winevulkan.dll`,
   `winepulse.drv`, `winealsa.drv`, `lsteamclient.dll`, and the `none_deadbeef` COMCTL32 path. The audio
   drivers are the most removable; the display and Vulkan drivers are not, so those would need renaming
   in a rebuilt Wine.

## The offline control does not work

To test whether the kill needs a server round-trip, I took the device fully offline
(`svc wifi disable`; confirmed with `ping 8.8.8.8` -> "Network is unreachable") and launched the game.
It exits after **~35 s**, much earlier than the ~4-minute online kill, and its log stops at:

```
Loading step: [Locale System]
Loading step: [Compatible Architecture Check]
…
Loading step: [Parental Control]
Loading step: [NetworkGlobal]        <- last line
```

So offline the game never reaches the state where Aegis's kill happens — it dies during network
initialisation instead. **The test neither supports nor refutes the server hypothesis**, because the
process is gone before the question can be asked.

It does mean the README's *"the game freezes or exits 2 to 4.5 minutes after launch; this happens online
and offline"* is probably conflating two different failures: the characterized kill (online), and this
early network-init exit (offline). Worth not re-testing offline again for this purpose.

Network was restored afterwards (wifi back on, `ping 8.8.8.8` OK).

## Where the emulator's module name actually comes from

Dumping the session environment (`tools/research/envdump.c`) shows there is **no variable selecting the
emulator** — only tuning (`FEX_TSOENABLED`, `FEX_X87REDUCEDPRECISION`, `BOX64_DYNAREC*`, …), plus
`SteamAppId=1466860` and paths like `WINELOADER=…/proton-11.0-99-arm64ec/lib/wine/aarch64-unix/wine`
and `WINEDLLDIR0=…/lib/wine`. The name is the registry value:

```
HKLM\Software\Microsoft\Wow64\amd64 = libarm64ecfex.dll
```

**And GameNative re-applies that value at session start.** I set it to `xtajit64.dll`, reverted
`wine.inf` back to `xtajit64.dll`, verified both — and after the next launch it was `libarm64ecfex.dll`
again. So neither the registry nor `wine.inf` is the durable source: **the FEXCore content's manifest
is**, and the registry follows it.

That also means the `namefix` bundle I built earlier was wrong. It lists `libarm64ecfex.dll` (first) and
`xtajit64.dll` (second), so it would leave the FEX-branded name in place. My "first target wins"
inference rests on a single data point and the previous session's bundle contradicts it (it lists only
`libarm64ecfex.dll`, yet the game then loaded `xtajit64.dll` — because their `wine.inf` said so and
nothing in their manifest overrode it).

So the unambiguous version is one that never mentions `libarm64ecfex.dll` at all:

```
/sdcard/Download/fexcore-2610-aoe-xtajitonly.wcp     (FEXCore, 2610-aoe-xtajitonly, versionCode 7)
  files: xtajit64.dll       -> ${system32}/xtajit64.dll
         libwow64fex.dll    -> ${system32}/libwow64fex.dll
```

Whatever rule GameNative uses to pick the value, the answer is `xtajit64.dll`. **Import this one, not
`namefix` (code 5).** Verify afterwards with `tools/research/modlist.c`: the emulator entry should read
`C:\windows\system32\xtajit64.dll`.

## Round 14: two shortcuts tried, neither works

**Editing GameNative's stored manifest directly — not possible.** Wine can reach Unix paths through the
`\??\unix\` prefix, but the imagefs is a chroot and the app's private directory is outside it:

```
Z:\ root:  bin  etc  home  lib  opt  share  Steamless  storage  tmp  usr  var     (no `data`)
Z:\opt:    apps  mono-gecko-offline  proton-11.0-99-arm64ec-1  wine  winetricks
```

`contents/` is not mounted, so the `.wcp` contents GameNative stores cannot be edited from inside.
Useful side-fact though: **`Z:\opt\proton-11.0-99-arm64ec-1` *is* the live Proton tree** — GameNative
appends `-1` (the versionCode) to the versionName — so
`Z:\opt\proton-11.0-99-arm64ec-1\lib\wine\aarch64-windows\ntdll.dll` is the file Wine actually
loads. That confirms [NTDLL-NEVER-LOADED.md](NTDLL-NEVER-LOADED.md) with a concrete path.

**Changing the registry mid-session and relaunching — no good either.** The registry *can* be set while
a session is live (`reg add … /d xtajit64.dll`, verified), and GameNative's own UI then offered
"Resume Game" rather than "Play". Tapping it produced no game process, so nothing was tested.

Launching the game directly inside the session (`start "" RelicCardinal.exe` via winhandler, bypassing
GameNative) *does* start it — and it dies at:

```
Loading step: [NetworkGlobal]
```

the same place the offline run died in round 11. GameNative's launcher evidently sets up the network
environment (resolver shims, `LD_PRELOAD`, and so on) that a bare `start` does not, so this route cannot
carry a module-name test.

**Conclusion: the module-name change still requires importing
`fexcore-2610-aoe-xtajitonly.wcp` (versionCode 7).** No shortcut was found.
