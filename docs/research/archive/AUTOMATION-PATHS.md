# Trying to drive GameNative without tapping

> **Archived.** Driving GameNative without taps (2026-10-06). The lessons (lock check, HOME first, the `appid` intent) are in [GAMENATIVE.md](../../guides/GAMENATIVE.md); the "container will not start" story ended in [CONTAINER-WONT-START.md](CONTAINER-WONT-START.md) (a wrong Wine Version). The story: [STORY.md](../../STORY.md).

Repeated rounds were lost to `input tap` not registering in GameNative's dialogs. These are the
alternatives found, and how each turned out.

## GameNative's intents (found via `dumpsys package`)

```
Activity Resolver Table:
  Schemes:
    gamenative://run              -> app.gamenative/.MainActivity
    gamenative://discord-linked   -> app.gamenative/.MainActivity
    home://pluvia                 -> app.gamenative/.MainActivity
    nxm://                        -> app.gamenative/.MainActivity
    app.gamenative://oauth/callback -> .ui.screen.auth.NexusOAuthCallbackActivity
  Non-Data Actions:
    app.gamenative.LAUNCH_GAME    -> app.gamenative/.MainActivity
```

`gamenative://run` and `app.gamenative.LAUNCH_GAME` look like exactly what is needed to start a
container or a game from `adb` with no UI. **They did not work as tried.** The intent is delivered —
`am start` reports *"intent has been delivered to currently running top-most instance"* — but nothing
starts. Guessed extras `appId`, `gameId` and `id` (all `STEAM_1466860`) changed nothing, so the
parameter names or the expected URI shape are still unknown. Worth revisiting by reading the app's
manifest/handler rather than guessing.

## The container stopped starting at all

By the end of round 19 no Wine process came up from either **Open container** or Play, across several
taps, with the app itself running normally. Two candidate causes, neither confirmed:

- `PowerControl: Clean restore executed: failure (entries=11, extraFiles=11,
  script=/data/media/0/Android/data/app.gamenative/files/powercontrol/power_restore.sh)` — repeated on
  every attempt. This is an AYN device feature rather than GameNative, so it may be incidental.
- A genuinely wedged Wine container, which a reboot normally clears.

Rebooted the device to clear the second possibility.

## Still the reliable path

**Open container** (cog → assistant panel → Open container) worked repeatedly earlier in the session and
gives `explorer` + `winhandler` in ~30 s with no game launch — worth retrying before concluding the
container is broken.


## The real cause of "the container will not start": a locked device

Rounds 19 and 26 both concluded the container was wedged. **That was wrong.** The device was sitting on
its lock screen, so every `input tap` was being delivered to the keyguard and silently discarded.
GameNative never received a Play or Open-container request — which is exactly what the logs showed:
*no container-start attempt at all*, only the app's usual GPU-stat polling.

Diagnosis, in one command:

```sh
adb shell dumpsys trust | grep deviceLocked        # -> deviceLocked=1
adb shell dumpsys window | grep -i mCurrentFocus   # -> ...Keyguard...
```

and the distinguishing test:

```sh
adb shell wm dismiss-keyguard     # works for a swipe-only keyguard
adb shell input swipe 960 1600 960 600 200
```

Both had no effect, so the lock is **secure** and adb cannot bypass it. Only the owner can unlock.

### Consequence for the workflow

**Check the lock state before concluding anything about taps.** A locked screen produces symptoms that
look exactly like a broken container: taps that do nothing, no process started, no error anywhere, and
the app appearing healthy in `ps` because it was already running when the screen locked.

Cheap guard to put at the top of any scripted UI work:

```sh
adb shell dumpsys trust | grep -q "deviceLocked=1" && echo "UNLOCK THE DEVICE FIRST"
```

This also means the notes previously recorded here — "the container stopped starting", "a reboot did
not clear it" — describe a locked device, not a container fault. The container was never the problem.


## Round 32: the deep-link key is `appid`, and taps are not reaching the app

### The parameter name, read from the APK

Guessing was the wrong approach and it was avoidable. Pulling the installed APK
(`adb pull $(adb shell pm path app.gamenative | sed 's/package://')`) and searching the extracted
`classes*.dex` for the scheme found the exact literal:

```
gamenative://run?appid=
```

**The key is `appid`, all lowercase.** Every earlier attempt used `appId`, `gameId` or `id`, which is why
they were delivered and silently ignored. The app uses AndroidX Navigation deep links
(`androidx/navigation/NavDeepLink$Builder`), so the pattern is a string constant in the DEX.

**But it still does not launch a container.** `adb shell am start -a android.intent.action.VIEW -d
'gamenative://run?appid=STEAM_1466860' app.gamenative` is accepted, the app starts if it was stopped,
and nothing else happens — no session, nothing in logcat. Tried with the container id
(`STEAM_1466860`) and the bare number (`1466860`), warm and cold. So either the value format is wrong,
or that URI is only ever *generated* by the app (the assistant panel offers **Copy launch link** and
**Create shortcut**) and the receiving path needs something more.

### Taps are not reaching GameNative at all

Every input form was tried and all are no-ops — the page does not change:

| Form | Result |
|---|---|
| `input tap` (single, and 6 rapid taps) | nothing |
| `input -d 0 tap` | nothing |
| `input touchscreen tap` | nothing |
| `input swipe x y x+1 y+1 80` (a tap with motion) | nothing |
| tapping **Back** — which should visibly leave the page | nothing |

A card tap *did* work earlier in the session (it navigated library → detail page), so input is not
universally dead. Something about the current state is swallowing it.

### Display geometry, which may explain the taps

```
Built-in Screen (display 0): 1080 x 1920   <- native portrait
Screen-2       (display 4): 1080 x 1240
GameNative window frame:     [0,0][1920,1080]   <- landscape, i.e. the display is rotated
mCurrentFocus lists two windows:
  rip.moth.cocoonshell/ExternalDisplayActivity
  app.gamenative/app.gamenative.MainActivityAliasDefault
```

GameNative's window is on display 0 and `input -d 0` was tried explicitly. The AYN shell
(`rip.moth.cocoonshell`) holds focus alongside it, which on a dual-screen handheld is plausibly where
input is going. Worth knowing before blaming the app.


## Round 33: why the taps were dead, and the fix

`dumpsys window windows` showed **three windows above GameNative that are touchable** — none of them
carries `NOT_TOUCHABLE`:

```
#2  com.odin.gameassistant   fl=NOT_FOCUSABLE HARDWARE_ACCELERATED
#4  com.odin.gameassistant   fl=NOT_FOCUSABLE NOT_TOUCH_MODAL LAYOUT_NO_LIMITS HARDWARE_ACCELERATED
#10 ShellDropTarget          fl=NOT_FOCUSABLE HARDWARE_ACCELERATED
                             pfl=... INTERCEPT_GLOBAL_DRAG_AND_DROP      frame (0,0)(fillxfill)
#12 app.gamenative/app.gamenative.MainActivity    <- the app, underneath all of them
```

`ShellDropTarget` is the launcher's drag-and-drop target: **full-screen, touchable, and above the app.**
The two `com.odin.gameassistant` windows (the AYN overlay) are touchable too. Any of these would consume
taps before they reach GameNative, which is exactly the symptom — and it explains why a tap *sometimes*
worked and usually did not.

**Fix: press HOME, then bring GameNative back.**

```sh
adb shell input keyevent 3                       # HOME -- resets the launcher overlays
adb shell am start -n app.gamenative/.MainActivityAliasDefault
```

Verified: immediately before, a tap on **Back** did nothing at all; immediately after, the same style of
tap changed the page (it navigated the library). So this is a real state reset, not luck.

**Put `input keyevent 3` at the start of any scripted UI sequence on this device**, alongside the lock
check. The two failure modes look identical from outside — nothing happens, nothing is logged — and the
causes are unrelated.


## Round 34: the Play tap does register, and the container gets as far as its X server

With HOME first (see above), a tap on **Play** works, and logcat shows the container genuinely starting:

```
I app.gamenative: Remembering touchMouse as MutableState(value=null)
I app.gamenative: Creating XServerView and XServer
I app.gamenative: Starting up XServerScreen
D Winlator_Renderer: Surface: 1280x720 container: 1280x720
D ControlsProfile: Loading controllers for profile: Physical Controller Default (ID: 0) ...
```

**So the request reaches the app and the container begins.** What never appears is Wine:

| process | count after 2.5 minutes |
|---|---|
| `winhandler` | 0 |
| `wineserver` | 0 |
| `box64` | 0 |
| `RelicCardinal` | 0 |

**The failure is therefore between "X server up" and "Wine starts"** — not in the tap handling, not in
the container request, and not (this time) the lock screen. Earlier rounds concluded "no container-start
attempt at all" from an absence of Wine processes; that was wrong. There *is* an attempt, and it gets
partway.

Also seen, repeatedly, and unexplained: `W app.gamenative: No lastPICSChangeNumber, skipping`, alongside
periodic large GCs in the app.

This is a much narrower target than "the container is broken" — the container starts, then stalls before
Wine. Worth checking next: whether the prefix is being unpacked, whether `prefixPack.txz` extraction
completes, and whether the container's Wine tree is intact.
