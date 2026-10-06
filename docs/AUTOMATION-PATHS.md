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
