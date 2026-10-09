# Why the container stalls: Box64 cannot find `wine`

> **Archived.** Resolved (2026-10-06): the container's Wine Version named a missing tree. The earlier sections are wrong diagnoses kept for the record. The how-to parts are in [GAMENATIVE.md](../../guides/GAMENATIVE.md). The story: [STORY.md](../../STORY.md).

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


## What the container used to point at

`files/debug_reports/STEAM_1466860_1789404977441/header.json` (2026-09-14, `appVersion` 1.2.1) is a
config dump from a working period:

```
wineVersion          : proton-11.0-1-arm64ec-1
appliedWineVersion   : proton-11.0-1-arm64ec-1
containerVariant     : bionic
```

Two things follow.

**1. `bionic` + a Proton wine is a combination that has worked.** So the variant is not obviously wrong
and does not need changing — that removes the risk of "fixing" the wrong half.

**2. The selected version has changed since.** `.container` now names `proton-11.0-99-arm64ec-1`, and
`Z:\opt\` did show that tree present. But Box64 still resolves `wine` through
`…/imagefs/opt/wine/bin/`, which suggests GameNative's own wine link — the thing that should point at the
selected tree — is stale or dangling. In other words the version names a tree that exists, while the
path actually used does not.

## Revised ask

**Re-select the Wine version in the UI and Save** — even if the correct version already appears
selected. That should make GameNative re-create its wine link for the container. Changing the variant is
*not* indicated by the evidence, so leave `containerVariant` alone unless the UI refuses to save.

Then start the container. `winhandler` appearing is the signal that the lookup succeeded; GameNative's
own `wine_logs/wine_debug.log` will say so directly either way, and it is readable over adb:

```sh
adb shell cat /sdcard/Android/data/app.gamenative/files/wine_logs/wine_debug.log
```

If it ends with `Error: File is not found. (wine)` again, the link did not get re-created and the next
step is the Wine/Proton Manager rather than the container's General tab.


## Fallback: a freshly installed Wine tree to select

Re-selecting the current version may not be enough if GameNative considers it already applied. So a clean
`Proton` bundle was built and pushed, giving the container a *distinct* version to switch to — which
forces the wine setup to run again from scratch.

```
/sdcard/Download/proton-11.0-99-clean.wcp     268 MB      (Proton, versionCode 5)
  versionName : 11.0-99-arm64ec-aoe-clean      -> appears as 11.0-99-arm64ec-aoe-clean-5
  ntdll       : PRISTINE, 606d0a2fb197d37b     no patches of any kind
  entries     : 2233, same structure as the original bundle
  wine        : { binPath: bin, libPath: lib, prefixPack: prefixPack.txz }
```

It carries **no** ntdll patch deliberately. The point is a Wine tree that installs cleanly and can be
selected, not to advance the SMC work — that comes later, via the FEXCore bundle.

Note what else is on the device: the *original* `proton-11.0-99-arm64ec.wcp` (364 MB, 2026-10-05) is
still there. The container's `wineVersion` of `proton-11.0-99-arm64ec-1` is that bundle — the `-1` is its
versionCode. **So re-importing it would change nothing; it is already the selected version.** The
problem is the link from `/opt/wine` to it, which is why a *different* version is the more reliable
lever.

## Order to try

1. Container → General → Wine version → select **`11.0-99-arm64ec-aoe-clean-5`** → Save → start.
   (Requires importing `proton-11.0-99-clean.wcp` first, via the Wine/Proton Manager.)
2. If that fails the same way, re-select the *existing* `proton-11.0-99-arm64ec-1` and Save.
3. Either way, read the verdict directly:
   `adb shell tail -3 /sdcard/Android/data/app.gamenative/files/wine_logs/wine_debug.log`
   `winhandler` appearing in `ps` is the success signal.


## Access routes into the imagefs, tested

Correcting the container's wine reference needs to write inside `imagefs/`, so every route was tried:

| Route | Result |
|---|---|
| `adb shell ls /data/user/0/app.gamenative/files/imagefs/opt/` | `Permission denied` |
| `adb shell run-as app.gamenative …` | `run-as: package not debuggable: app.gamenative` |
| `/sdcard/Android/data/app.gamenative/files/` | readable, but holds only `crash_logs`, `debug_reports`, `powercontrol`, `wine_logs` — no `imagefs` |

So there is **no adb-side route** into the imagefs. The only writer is GameNative itself.

## The route that might still work: start a *different* container

**The imagefs is shared between containers.** Its prefix lives at `imagefs/home/xuser`, and per-container
configs sit beside it (`home/xuser-STEAM_<appid>/.container`). So *any* container that starts
successfully yields a Wine session — and from inside that session the AoE IV container's config is
writable, including `envVars` (which is honoured, unlike `wineVersion`) and possibly the wine reference
itself.

The library shows **The Witcher 2 with 5h10m of play time**, i.e. a container that has worked. Launching
it is therefore a way to get a session without fixing the broken one first.

**It was not confirmed this round** — the Witcher's detail page opened, but no action button
(`Play`/`Install`/`Update`) appeared in the UI dump, so the launch could not be triggered. Worth
retrying; if that container starts, the AoE IV config becomes reachable without any UI changes to the
container itself.


## A field that was in front of me all along: `needsUnpacking: true`

The `.container` read in round 18 contains:

```json
"needsUnpacking": true
```

A Wine tree ships a `prefixPack.txz` — the bundle built in round 37 has one at its root — and that pack
has to be **unpacked** into the container before Wine can run. `.container` says that unpacking is still
pending.

That fits the failure exactly. Box64 is asked to run `wine explorer /desktop=… winhandler.exe …`, looks
for the `wine` binary at `…/imagefs/opt/wine/bin/`, and does not find it — because the tree it should be
finding has never been laid down. It is not that the binary is missing from a tree; it is that the setup
step which would put it there has not run.

It also matches the earlier symptom chain: the X server comes up (that is the Android side), and then the
Wine launch fails, which is the point where the unpacked tree is first needed.

`needsUnpacking: true` is also the kind of flag a re-selection should clear — which is why
**re-selecting the Wine version and saving** remains the right first move, and why doing it with a
*different* version (`11.0-99-arm64ec-aoe-clean-5`) is the more reliable version of that move.

Also noted: the launch goes through Box64 (`[BOX64] Wine64 detected`) even though the container's
`appliedWineVersion` is an ARM64EC Proton build. That is not necessarily wrong — Box64 is the launcher
here — but combined with the missing binary it suggests the container is being assembled for a tree that
is not present, rather than failing to find a file inside a present one.


---

# RESOLVED: the container was pointed at a Wine version with no tree

**This is what it actually was, and it is worth reading before the diagnosis above.**

The container's **Wine Version** was set to **`proton-11.0-1-arm64ec-aoefix-1`** — a bundle imported
earlier in the session that never worked. `Z:\opt\` showed only `proton-11.0-99-arm64ec-1`, so the
container was naming a Wine version **whose tree did not exist**. Box64 then looked for the `wine` binary
and found nothing:

```
[BOX64] Binary search path: ./:bin/:…/imagefs/opt/wine/bin/:…/usr/bin/
[BOX64] Looking for wine
[BOX64] Error: File is not found. (wine)
```

**The fix:** container → General → **Wine Version** → select **`proton-11.0-99-arm64ec-1`** → Save.

Immediately afterwards the container booted and everything downstream worked:

- `wineserver`, `services.exe`, `winedevice`, `plugplay`, `explorer`, `winhandler` all started
- the game launched and wrote a real `warnings.log`
- `smctest` could finally be run — which is what confirmed the SMC trap
  ([`SMC-TRAP.md`, part 2](../../how-it-works/SMC-TRAP.md#part-2-confirmed-fex-leaks-its-smc-write-trap-to-the-guest))

Selection is what matters, not importing: `proton-11.0-99-arm64ec-1` was **already installed** (it is the
original `proton-11.0-99-arm64ec.wcp` still on the device, the `-1` being its versionCode). Re-importing
it would have changed nothing.

## What this cost, and the lesson

Roughly ten rounds were spent on this, and two confident diagnoses along the way were **wrong**:

1. **"The container is wedged"** (rounds 19, 26) — the device was simply on its **secure lock screen**, so
   every tap went to the keyguard. `adb shell ls /sdcard/` failing is the tell: user storage is not
   decrypted until the first unlock after a boot.
2. **"needsUnpacking: true means it is unpacking"** — it was not. The app sat at 23% CPU with **zero**
   container threads; that was Java and telemetry work. Measuring the thread list rather than the CPU
   figure is what showed it.

The root cause was my own change: an earlier attempt to switch Proton versions left the container
pointing at a tree that was never installed.

## Also learned here: renaming beats overwriting

`C:\windows\system32\libarm64ecfex.dll` cannot be overwritten while the container runs — it is mapped,
and `copy` fails with **"Sharing violation"**. But a mapped file **can be renamed**:

```
move /y "C:\windows\system32\libarm64ecfex.dll" "C:\windows\system32\libarm64ecfex.stock.dll"
copy /y D:\libarm64ecfex.patched.dll "C:\windows\system32\libarm64ecfex.dll"
```

That is how the patched FEX was swapped in and out without stopping the container. Note the swap only
takes effect for **newly started** processes; already-running ones keep the old mapping, so a container
restart is needed for it to apply.
