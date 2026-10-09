# The evening runs were slowed by leftover Wine debug channels

Measured on the AYN Thor, 2026-10-06, 20:47 to 20:57.

## What was found

The container config still carried the verbose channels that round 17 turned on to capture a trace
([CONTAINER-CONFIG.md](CONTAINER-CONFIG.md), "What the verbose channels showed"):

```
WINEDEBUG=+thread,+sync,+virtual,+timestamp,+tid
```

Read at 20:47 from both copies of the config, `Z:\home\xuser\.container` and
`Z:\home\xuser-STEAM_1466860\.container` (byte-identical), and confirmed live: `set` inside the session
printed the same value. GameNative's `files/wine_logs/wine_debug.log` had grown to 4.7 MB from starting
`explorer` and `winhandler` alone, with no game running.

Nothing on the device or in the repo records the value being changed back after round 17. The runs
judged in the evening (19:52 to 20:29, in [FIX-VERIFIED.md](FIX-VERIFIED.md) and
[DEATH-IS-NOT-THE-KILL.md](DEATH-IS-NOT-THE-KILL.md)) were launched from this config.

## The A/B

Same Wine (`proton-11.0-99-arm64ec-1`), same FEX DLL (SHA-1 `460568b8`, see below), same launch path
(Play after a clean app restart). Only `WINEDEBUG` differs.

| run | `WINEDEBUG` | last loading step in the game's log | detail |
|---|---|---|---|
| 20:29 | `+thread,+sync,+virtual,+timestamp,+tid` | `Property Bag Manager` (20:29:32) | the final `warnings.log`, read from the prefix at 20:47 (14,226 bytes), ends there |
| 20:51 | `-all` | `GEWorld` (20:54:20) | `PropertyBagManager Loaded in 22.261999s`; `App Init Complete` at 20:52:21 |

The 22.26 s matches run 16 from before round 17 (`PropertyBagManager Loaded in 22.650000s`,
2026-10-05 21:06). This is one run on each side.

## Earlier claims this corrects

- **"The wall is a MapGen asset error"** ([FIX-VERIFIED.md](FIX-VERIFIED.md)) is wrong. The line
  `MapGen - Failed to validate: !m_texturePath.empty()` appears in every run that got further (run 16,
  `warnings-quit.log`, `warnings-now.log`, and the 20:51 run), and loading continues after it. In the 20:51
  run it is logged at 20:51:53 and `PropertyBagManager Loaded` follows at 20:52:10. It is an info message,
  not a stop.
- **"Stock FEX has been restored"** ([FIX-VERIFIED.md](FIX-VERIFIED.md)) restored
  `libarm64ecfex.stock.dll`, which is SHA-1 `460568b82f174e23d6a19f6f0b4f171f4666f016`: the
  **CPUID-patched** build, identical to `docs/research/samples/aegis/libarm64ecfex.patched.dll` and to the DLL inside
  `aoe4-fixes.wcp`. The unpatched 2610-aoe build is `6f5f25f6d6f48501880614e77cfe4845e86f93fc`
  (`fexcore-2610-aoe.wcp`). The name came from a file that was already patched when it was renamed.
- **"The patched build regresses the game"** was measured with the channels on. It was re-tested clean
  in [KILL-REMEASURED.md](KILL-REMEASURED.md): the no-trap build still does not start the game, and
  `SMCChecks=full` with the trap armed stops the same way.
- The thread counts and the X-connection reading in [DEATH-IS-NOT-THE-KILL.md](DEATH-IS-NOT-THE-KILL.md)
  come from the 20:29 run, so they describe a run slowed by the channels.

## The fix, and the guard

Both config copies now say `WINEDEBUG=-all`. The edit replaced only that token and left the rest of the
file byte-identical (GameNative writes `\/` escapes, which a JSON library would not reproduce). A session
started from this config does not define `WINEDEBUG` at all (`set WINEDEBUG` at 21:10: "not defined"), so
no trace channels are on.

Check before any run that will be judged:

```bat
findstr /c:"WINEDEBUG" "Z:\home\xuser\.container"
```

## Current FEX setup, as measured

| item | value |
|---|---|
| FEXCore Version in the UI | `ntdll-waitq-fix-1` |
| what that bundle installs | only `${system32}/ntdll.dll` (`ntdll-waitq2.wcp`); Wine never loads it ([NTDLL-NEVER-LOADED.md](NTDLL-NEVER-LOADED.md)) |
| `C:\windows\system32\libarm64ecfex.dll` | SHA-1 `460568b8`, the CPUID-patched build, left there by earlier installs |
| module the game loads | `C:\windows\system32\libarm64ecfex.dll` (`modlist`, 20:57, 86 modules) |

Because the selected bundle carries no FEX DLL, the emulator in use is whatever file sits in
`system32`. Selecting a different FEXCore Version will replace it.
