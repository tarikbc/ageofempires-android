# Driving GameNative over adb

How the tests in this repo read and drive GameNative on the AYN Thor from a Mac. Verified with GameNative 1.2.1 on
2026-10-06 to 2026-10-08, display 0 (1920 × 1080); coordinates are for that display and were stable across launches.
`tools/gn_nav.py`, `tools/gn_driver.py`, `tools/gn_import_wcp.py`, `tools/gn_select_fex.py` and `tools/run_watch.py`
wrap the steps below. On 2026-10-09 the Thor moved to the GameNative 1.3.0 test release for GTA V; its library, the
Settings list (Driver Manager, Contents Manager) and the game page's Options cog at `(1651, 536)` looked the same, the
rest was not re-checked ([LOG.md](../research/LOG.md), "2026-10-09").

## Before any scripted UI work

1. **Check that the device is unlocked.** A locked screen looks exactly like a broken app: taps do nothing, no
   process starts, nothing is logged. User storage is only mounted after the first unlock following a boot:

   ```
   adb shell dumpsys trust | grep -q "deviceLocked=1" && echo "UNLOCK THE DEVICE FIRST"
   ls /sdcard/        -> No such file or directory     the user storage is not decrypted
   sm list-volumes    -> private mounted null          no emulated volume
   dumpsys user       -> State: RUNNING_LOCKED
   ```

   A screen that was locked again after the first unlock shows only in `dumpsys trust`. While the device is locked,
   `adb push` to `/sdcard` fails with "remote couldn't create file".

2. **Press HOME, then bring GameNative back.** The launcher's drag-and-drop target (`ShellDropTarget`) and the AYN
   assistant can sit above the app as full-screen touchable windows and eat taps:

   ```sh
   adb shell input keyevent 3                       # HOME, resets the launcher overlays
   adb shell am start -n app.gamenative/.MainActivityAliasDefault
   ```

3. **Read the screen with all windows.** A plain `uiautomator dump` sees only the default window and silently goes
   stale here (it kept returning the game page while the AYN assistant panel was showing):

   ```
   adb shell uiautomator dump --windows /sdcard/gn_ui.xml
   ```

   Taps themselves are fine (`input -d 0 tap X Y`). A list at the bottom (`Mode` / `Task` / `Settings`,
   `60 FPS MODE`, `Top screen`, …) belongs to the AYN device panel on the second screen; ignore it when parsing. If a
   dump still looks wrong, a screenshot (`adb exec-out screencap -p`) settles it.

## Navigation

**Game list → game page.** Tap the game's card. Its position is **not** fixed: after a start, GameNative asks its
API for a suggested game, and when the answer arrives the suggestion takes the first card (labelled `Recommended`)
and AoE IV moves to the second (`(723, 297)` on 2026-10-07). A tap that races the move opens the suggested game: on
2026-10-07 three "Open container" attempts opened the container of another game (`STEAM_503820` in logcat).
`GN.open_game_page()` in `tools/gn_nav.py` waits for the `Recommended` card (30 s at most), taps the card from the same
screen read, and checks that the page shows the game's title, `Play` and the `Options` cog.

**Game page → container settings.** Tap the cog (`~(1651, 536)`). It opens the AYN assistant panel, not a
GameNative screen: `Options` / `Quick Actions` / `Edit container` / `Open container` / `Get AI help` /
`Create shortcut` / `Export for frontend` / `Copy launch link`. **Do not tap right of the cog** on an installed
game: `(1790, 536)` is the uninstall button (GameNative 1.3.0 asks for a confirmation first; [LOG.md](../research/LOG.md), "2026-10-09").

**→ Edit container** (`~(1381, 328)`) opens `<game> Config` with the tabs `General` · `Graphics` · `Emulation` ·
`Controller` · `Wine` · `Win Components` · `Environment` · `Drives` (the tab strip scrolls). Save is at the top
right.

## Installing a FEX package or a driver

**FEX package (`.wcp`):** library, top-right **Menu** (`(1841, 73)`), **Settings**, scroll down to **Contents
Manager** (under Emulation), **Import .wcp from device**. Android's file picker opens in Downloads; its search
(`(1753, 110)`) finds the file by a one-word name. The bundle is then listed under Installed contents, FEXCore, as
`<versionName> (<versionCode>)`, and in the container's Emulation tab, FEXCore Version, as
`<versionName>-<versionCode>` below the built-in versions (the list scrolls). The FEXCore Version dropdown sits at about `(121, 370)`; only imported contents appear there.
**A bundle whose `versionName` is already installed is unpacked and then dropped without a message**, even with a new
code (2026-10-07: `aoe-fastcontinue` code 11 did not appear next to code 10); give every new build a new
`versionName`. From adb: `tools/gn_import_wcp.py` and `tools/gn_select_fex.py <name>-<code>`.

**Driver (`.zip`):** **Settings → Driver Manager → Import ZIP from device**, then the container's Graphics tab,
Graphics Driver Version. From adb: `tools/gn_driver.py import FILE.zip SEARCH` and `tools/gn_driver.py select NAME`.

**What the game then loads.** GameNative writes the files of the selected FEXCore content into `system32` at every
container start, and sets `HKLM\Software\Microsoft\Wow64\amd64` from the content's manifest. With this repo's
packages, which ship only `libarm64ecfex.dll`, the game loads `C:\windows\system32\libarm64ecfex.dll`; `tools/agent.py
peek libarm64ecfex.dll ...` reads the patch counters from that module in the game. (On 2026-10-06 the then-current
contents named `xtajit64.dll`, and the game loaded that name; several archived notes say so. Editing the registry value
or `wine.inf` by hand does not survive a launch: [archive/MODULE-LIST.md](../research/archive/MODULE-LIST.md).)

## The container's config from inside Wine

GameNative's live container configuration is a JSON file inside the imagefs, readable and writable with `cmd` in a
running session:

| File | What it is |
|---|---|
| `Z:\home\xuser-STEAM_<appid>\.container` | the **source** copy: top-level `wineVersion`, `containerVariant`, `emulator`, `fexcoreVersion`, `envVars`, `drives` |
| `Z:\home\xuser\.container` | the **applied** copy: `extraData.appliedWineVersion`, `extraData.appliedContainerVariant`, … |
| `Z:\home\xuser\applied_config.json` | a stale copy from an earlier session (an output, not an input) |

Only some fields are honoured when edited there: **`envVars` yes** (GameNative reads it back and does not overwrite it),
`wineVersion` and the FEXCore content **no** (re-applied from GameNative's own store; change them in the UI).
`tools/research/cfgedit.py OLD NEW` pulls, patches and pushes the applied copy in one step. A container saved from
GameNative's UI gets GameNative's own copy of `envVars` back, so make UI changes first and environment edits after.

The Wine versions are read-only directories under `Z:\opt\` (for example `Z:\opt\proton-11.0-99-arm64ec-1\` with
`bin/`, `lib/`, `share/`, `prefixPack.txz`, `profile.json`): files can be copied out but not written.

**`Open container` is the fast path** for probes and config edits: the assistant panel's **Open container** starts
`explorer` plus `winhandler` in about 30 s, with no game launch and no save sync. `tools/run_watch.py` sends commands
to that session through `winhandler` (UDP 7946; a command's program and parameters together are limited to 51 bytes, so longer commands go
into a `.bat` file on the `D:` drive, which is `/sdcard/Download`).

## Starting a game without taps

GameNative registers `gamenative://run` (key `appid`, all lowercase, read from the APK's DEX) and the action
`app.gamenative.LAUNCH_GAME`. Neither started a game or a container as tried on 2026-10-06 (`am start -a
android.intent.action.VIEW -d 'gamenative://run?appid=STEAM_1466860' app.gamenative` with the container id and the
bare number, warm and cold): the app comes to the front and nothing else happens. The tests tap **Play** instead.

## The in-game Quick Menu

Opened during a game. Items used on 2026-10-07:

- **Controller** tab, **Edit Physical Controller**: per-game button bindings (the tested setup needs none; the Thor's
  controller runs in its standard mode).
- **Performance HUD**: FPS, CPU and GPU load, temperatures on screen.
- **Power Control**: GameNative's own CPU and GPU clock control. While it was on with its defaults, the CPU caps were
  2.05 GHz (cores 3 to 6) and 1.98 GHz (core 7); turning it off restored 2.71 and 3.19 GHz and raised AoE IV from
  13.7 to about 25 FPS (2026-10-07; logcat `PowerControl: Clean restore executed`, tab 7 in logcat). Its default is
  in Settings, Performance, "Enable in-game power control by default". The values in use since then:
  [TUNING.md](TUNING.md), "Power profile".

**The menu pauses the game.** A 23.9 s stay in it was followed by AoE IV exiting with code 1 about 1 s later; a
9.3 s stay was not ([LOG.md](../research/LOG.md), known limits).

## Recovering from a FEX build that stops every start

GameNative writes the selected content's DLL at every start, so after a bad build, selecting a good content in the
Emulation tab (`tools/gn_select_fex.py aoe4-perf6-30`) is enough; it takes effect at the next start. On 2026-10-08
`aoe4-perf4-22`, a first build of patch 0016 that froze the game at start, was removed that way and then deleted in
Contents Manager (FEXCore type, trash icon, Remove).

A DLL installed by hand that the selected content does not name stays in place. On 2026-10-07 the job build `hot2`
(`a29bf0a8`, a hot-page SMC experiment plus a `thread_local` in `InvalidationTracker.cpp`), installed by hand as
`libarm64ecfex.dll`, ended every container start about 13 s in, also with `FEX_EXP_HOT_SMC` removed from the
environment. Which of the two changes causes it is not isolated. With no running container the file cannot be replaced from Wine, and the app data is
private (`run-as: package not debuggable`, no `su`). Selecting a content that names `libarm64ecfex.dll` overwrote it
at the next start.

## Gotchas

- GameNative **re-installs the files of the selected FEXCore Version on every launch**. A hand-patched DLL that the
  selected content names is reverted at the next launch (a patched `libarm64ecfex.dll` of 10:13 was stock again at
  10:27 on 2026-10-07). A DLL the content does not name stays: with `ntdll-waitq-fix-1` selected (it ships only
  `ntdll.dll`) a hand-installed `libarm64ecfex.dll` (`d20e07a7`) was still in place after a fresh start (08:53).
- The `Wow64\amd64` registry value follows the selected content's manifest; editing it or `wine.inf` (there is no
  copy of it inside the prefix) does not survive a launch.
- On 2026-10-06 the Emulation tab read: FEXCore Version `2610-aoe-1`, 64-bit Emulator `FEXCore`, 32-bit Emulator
  `Box64`, Box64 Version `0.4.2` (a snapshot of that day).
- Long waits are normal: launching to the AoE IV loading screen takes minutes, and "stuck" often means it is syncing
  saves rather than hung. A "Save Conflict" dialog can appear at a start; the tests always answer **Keep local**.
- The AYN magnification overlay (`MAGNIFICATION_OVERLAY`, type 2027) is `NOT_FOCUSABLE NOT_TOUCHABLE` and does not
  swallow taps. The assistant's other windows and the launcher's `ShellDropTarget` are touchable and can (step 2
  above).
- About 20 game starts in one day made Steam log the account off right after logon on 2026-10-08, and GameNative then
  crashed on every game page ([LOG.md](../research/LOG.md), "2026-10-09"). Space test starts out, and wait minutes, not seconds, after a
  crashed start.
