# Why the container stalls: Box64 cannot find `wine`

GameNative keeps its own logs under `/sdcard/Android/data/app.gamenative/files/` — readable over adb,
unlike everything in `/data/user/0/app.gamenative/` (which is app-private and denied).

```
files/wine_logs/wine_debug.log                        2026-10-06 18:07   <- current, 2.5 KB
files/wine_logs/debug_run_STEAM_1466860.log           2026-09-21 22:52   <- last known good
files/wine_logs/debug_run_STEAM_1466860.logcat        2026-09-21 22:53
files/crash_logs/pluvia_crash_2026-10-05_21-30-38.txt
```

## The current failure, verbatim

`wine_debug.log` ends with:

```
[BOX64] Detected running wine with "explorer"
[BOX64] Binary search path: ./:bin/:/data/user/0/app.gamenative/files/imagefs/opt/wine/bin/:/data/user/0/app.gamenative/files/imagefs/usr/bin/
[BOX64] Looking for wine
[BOX64] argv[1]="explorer"
[BOX64] argv[2]="/desktop=shell,1280x720"
[BOX64] argv[3]="winhandler.exe"
[BOX64] argv[4]=""C:\Program Files (x86)\Steam\steamapps\common\Age of Empires IV\RelicCardinal.exe""
[BOX64] Error: File is not found. (wine)
```

**The launch is correct in every respect — right desktop, right `winhandler.exe`, right game path — and
then Box64 cannot find the `wine` binary.** It searches
`…/imagefs/opt/wine/bin/`, and the run dies there. Nothing starts, which is exactly what the outside
looked like: no `wineserver`, no `winhandler`, no game, and no error visible over adb.

This also explains the round-34 observation that the container reaches its X server
(`Creating XServerView and XServer` / `Starting up XServerScreen`) and then stalls: the X server comes up,
then the Wine launch fails at the binary lookup.

## Evidence that it used to work

`debug_run_STEAM_1466860.log` from **2026-09-21** contains a full Wine trace from a real run —
`MemoryLib: (INFO) : Pool Strings`, `Pool Default`, `expect_no_runtimes Process exited with a Mono
runtime loaded`, and a clean `NtTerminateProcess`. So the same container ran the game on that date.

**Something between 2026-09-21 and now left the container unable to resolve `wine`.** The candidates are
all this session's changes to the container: the FEXCore content selections, the Proton version switches
(including ones that failed to install), and the round-18 edit of `wineVersion` in the source config.

## Why this cannot be fixed from adb

Everything needed is app-private:

```
$ adb shell ls /data/user/0/app.gamenative/files/imagefs/opt/
ls: .../imagefs/opt/: Permission denied
```

So the Wine version cannot be corrected by editing the config the way `envVars` could be — this needs
the UI.

## The ask

In the container for AoE IV: **General tab → Wine version**, and select a version the Wine/Proton Manager
shows as actually installed — `proton-11.0-99-arm64ec-1` is the one that existed before
`.container`'s `wineVersion` also names it, and `Z:\opt\` showed it present. Then Save, and start the
container again.

If the UI offers a container **variant** too, note that `.container` reports `containerVariant: bionic`
while `appliedWineVersion` is a *Proton* build — a mismatch worth correcting in whichever direction the
UI allows.
