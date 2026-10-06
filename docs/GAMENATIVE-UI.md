# Driving GameNative on the Thor (learned the hard way)

Steps verified on the AYN Thor, GameNative 1.2.1, display 0 (1920×1080). Coordinates are for that
display; they are stable across launches.

## The one thing that matters most: dump **all** windows

```
adb shell uiautomator dump --windows /sdcard/gn_ui.xml
```

A plain `uiautomator dump` sees only the default window and **silently goes stale** here: it kept
returning the game detail page while the screen actually showed the AYN assistant panel, which cost a
lot of time chasing "taps don't register". GameNative's menus and the AYN assistant panel are separate
windows, so `--windows` is required. `tools/gn_nav.py` does this.

Taps themselves are fine (`input -d 0 tap X Y`); it was the *reading* that lied, not the input.

If a dump still looks wrong, ask the human what is on screen — it is faster than fighting it.

## Navigation

**Game list → game detail page**
Tap the game's card. AoE IV sits at roughly `(723, 297)` in the grid.

**Game detail → container config**
Tap the cog (top right of the detail page, ~`(1651, 536)`).
This opens the **AYN assistant panel**, not a GameNative screen:
`Options` / `Quick Actions` / `Edit container` / `Open container` / `Get AI help` /
`Create shortcut` / `Export for frontend` / `Copy launch link`.

**→ Edit container** (`~1381, 328`).

That opens the GameNative container config, titled `<game> Config`, with tabs:

`General` · `Graphics` · **`Emulation`** · `Controller` · `Wine` · `Win Components` · `Environment` · `Drives`

Note the tab strip scrolls; `Win Components` may be off-screen to the right until you scroll it.

On `General` you can read **Container Variant** (`bionic`) and **Wine Version**
(`proton-11.0-99-arm64ec-1`).

**`Emulation` tab** is where the emulators live:

| field | value seen |
|---|---|
| FEXCore Version | `2610-aoe-1` |
| 64-bit Emulator | `FEXCore` |
| 32-bit Emulator | `Box64` |
| Box64 Version | `0.4.2` |

The **FEXCore Version** value is a dropdown (~`121, 370`); it lists installed FEXCore contents only —
a `.wcp` that has not been imported will **not** appear there.

**Installing a build:** Settings → **Contents Manager** → **Import .wcp from device** → pick the file
(e.g. `/sdcard/Download/fexcore-2610-aoe-nofex2.wcp`) → then select it in
Edit container → Emulation → FEXCore Version.

A list at the bottom (`Mode` / `Task` / `Settings`, `60 FPS MODE`, `Top screen`, …) belongs to the AYN
device panel, not GameNative — ignore those entries when parsing.

## Verifying what actually got installed

Installing a FEXCore updates the files the manifest names. Do **not** assume that means the emulator in
use changed — copy the DLLs out and check the bytes:

```bat
:: dllnow.bat, run inside the session
copy /y "C:\windows\system32\xtajit64.dll" D:\aoe\xtajit_now.dll >nul
copy /y "C:\windows\system32\libarm64ecfex.dll" D:\aoe\libfex_now.dll >nul
```

then check offset `0x28644` (see [FEX-VENDOR-LEAK.md](FEX-VENDOR-LEAK.md)) and confirm which module the
game process actually loaded with `modchk`, or which one a fresh process loads via `dbgprobe`'s module
list.

**GameNative loads `xtajit64.dll`, not `libarm64ecfex.dll`.** The two files started out byte-identical,
which makes this easy to miss. A `.wcp` can install to either name because the manifest chooses the
target — `fexcore-2610-aoe-nofex2.wcp` installs the patched DLL under **both** names for that reason.

## Gotchas

- GameNative **re-installs the emulator DLLs on launch**. Patching `C:\windows\system32\*` by hand is
  reverted (observed: patched `libarm64ecfex.dll`, mtime 10:13, reverted to stock at 10:27 when the
  game next launched). The Contents Manager is the supported channel.
- The `Wow64\amd64` registry value is forced back to `xtajit64.dll`; editing it (or the `wine.inf` that
  defines it — there is no copy inside the prefix) does not survive a launch.
- Long waits are normal: launching to the AoE IV loading screen takes minutes, and "stuck" often means
  it is uploading saves rather than hung.
- The AYN assistant overlay is `NOT_FOCUSABLE NOT_TOUCHABLE` (a `MAGNIFICATION_OVERLAY`, type 2027) —
  it does **not** swallow taps.
