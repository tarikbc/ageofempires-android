# Trying to drive GameNative without tapping

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
