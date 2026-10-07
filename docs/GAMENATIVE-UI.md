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
Tap the game's card. Its position is **not** fixed. After a start, GameNative asks its API for a suggested
game. Until the answer arrives, AoE IV is the first card; when it arrives, the suggestion takes the first
card (labelled `Recommended`) and AoE IV moves to the second (`(723, 297)` on 2026-10-07). A tap that
races the move opens the suggested game: on 2026-10-07 three "Open container" attempts opened the
container of another game (`STEAM_503820` in logcat) instead of AoE IV.
`GN.open_game_page()` in `tools/gn_nav.py` waits for the `Recommended` card (30 s at most), taps the card
from the same screen read, and checks that the page that opens shows the game's title, `Play` and the
`Options` cog. `run_watch.py --launch` and `ab_fex.py` use it.

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

Path on 2026-10-07 (GameNative 1.2.1): library, top-right **Menu** (`(1841, 73)`), **Settings**, scroll down to
**Contents Manager** (under Emulation), **Import .wcp from device**. That opens Android's file picker in
Downloads; its search (`(1753, 110)`) finds the file by a one-word name such as `fexcore` (the keyboard split
`fastcontinue` into two words). After the import the bundle is listed under Installed contents, FEXCore, as
`<versionName> (<versionCode>)`. In the Emulation tab's FEXCore Version list it appears as
`<versionName>-<versionCode>` below the built-in versions; the list scrolls.

## The in-game Quick Menu

Opened during a game. Items used on 2026-10-07:

- **Controller** tab, **Edit Physical Controller**: per-game button bindings. Face buttons can be bound to other
  gamepad buttons (A to B, X to Y and back), which swapped A/B and X/Y for AoE IV on the Thor.
- **Performance HUD**: FPS, CPU and GPU load, temperatures on screen.
- **Power Control** (tab 7 in logcat): GameNative's own CPU and GPU clock control. While it was on, the CPU caps
  were 2.05 GHz (cores 3 to 6) and 1.98 GHz (core 7); turning it off restored 2.71 and 3.19 GHz (logcat
  `PowerControl: Clean restore executed`) and raised AoE IV from 13.7 to about 25 FPS. Its default is in
  Settings, Performance, "Enable in-game power control by default".

**The menu pauses the game.** A 23.9 s stay in it was followed by AoE IV exiting with code 1 about 1 s later; a
9.3 s stay was not ([README](../README.md), known limits).

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

## A FEX build that stops every container start

On 2026-10-07 the job build `hot2` (`a29bf0a8`; hot-page SMC experiment plus a `thread_local` in
`InvalidationTracker.cpp`) was installed by hand as `C:\windows\system32\libarm64ecfex.dll`. After
that, every container start ended about 13 s after the Wine processes appeared, also with
`FEX_EXP_HOT_SMC` removed from the environment. Which of the two changes causes it is not isolated.
With no running container, the file cannot be replaced from Wine, and the app data is private
(`run-as: package not debuggable`, no `su`). This restored it:

1. Edit container → Emulation → FEXCore Version → `2610-aoe-nofex2-3` → Save. That content names
   `libarm64ecfex.dll`, so the next start writes `460568b8`.
2. Open container. It starts.
3. Install the wanted build in that session (`ab_fex.install(name)`, rename-aside and copy).
4. Edit container → Emulation → FEXCore Version → `ntdll-waitq-fix-1` → Save, so the next start does
   not write `460568b8` again.

In the FEXCore Version list, `ntdll-waitq-fix-1` is the last item and sits at the screen edge
(`(1680, 1055)`).

## Gotchas

- GameNative **re-installs the files of the selected FEXCore Version on every launch** (Emulation tab).
  A hand-patched DLL that the selected content names is reverted (observed: patched `libarm64ecfex.dll`,
  mtime 10:13, reverted to stock at 10:27 when the game next launched). A DLL that the content does
  not name stays: `ntdll-waitq-fix-1` ships only `ntdll.dll`, and with it selected a hand-installed
  `libarm64ecfex.dll` (`d20e07a7`) was still in place after a fresh container start (2026-10-07 08:53).
- The `Wow64\amd64` registry value is forced back to `xtajit64.dll`; editing it (or the `wine.inf` that
  defines it — there is no copy inside the prefix) does not survive a launch.
- Long waits are normal: launching to the AoE IV loading screen takes minutes, and "stuck" often means
  it is uploading saves rather than hung.
- The AYN assistant overlay is `NOT_FOCUSABLE NOT_TOUCHABLE` (a `MAGNIFICATION_OVERLAY`, type 2027) —
  it does **not** swallow taps.
