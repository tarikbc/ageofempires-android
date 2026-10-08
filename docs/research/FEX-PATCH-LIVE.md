# The FEX patch is live — and the game still dies

## What was fixed

For the first time the emulator the game actually loads carries the patch. A fresh process in the
game's own session reports:

```
hypervisor leaf 0x40000000 eax=0x0 vendor=''
modules: ... C:\windows\system32\xtajit64.dll
```

Before the patch it reported `eax=0x40000001 vendor='FEXIFEXIEMU'`. **FEX no longer identifies itself.**

Getting there needed two things:

1. Patching the ARM64 function that builds the vendor string (see
   [FEX-VENDOR-LEAK.md](../how-it-works/FEX-VENDOR-LEAK.md)).
2. Installing the patched DLL under the name GameNative actually loads. The `.wcp` manifest chooses
   the target path, and `xtajit64.dll` is **not** in GameNative's trusted set — it warns
   *"Untrusted Files Detected … includes files outside the trusted set"* and lists
   `- ${system32}/xtajit64.dll`. Installing anyway writes it.
   `fexcore-2610-aoe-nofex2.wcp` (`versionCode` 3) targets both names.

Selection is `Edit container → Emulation → FEXCore Version`, and the entries render as
`<versionName>-<versionCode>` (so `nofex2` + code 3 shows as `2610-aoe-nofex2-3`). The config screen
must be **saved** with the top-right button, otherwise Back raises *"Unsaved Changes"* and the
selection is lost.

## What it did not fix

The game still dies in the same 2–4.5 minute window, and the threads still end up suspended — the
same kill signature as before (`si` shows almost every thread at `suspend=1`, one spinning, 100 % CPU).

This run did differ in one visible way: the game got far enough to raise its own error dialog,

```
ERROR LOADING MATCH RESULTS: an unexpected error has occurred due to a possible de-synchronisation
of match participants. Match data may not be accurate and final match results may not be available.
ERROR CODE: C06T13R-1X-13 4D6174630419
```

**That error is a server-connectivity failure, not file integrity.** The same `C06T13R-1X-*` family
with the `4D617463` ("Matc") tag appears in
[this AoE forum thread](https://forums.ageofempires.com/t/age-of-empires-iv-erro-ao-carregar-resltados-da-partida-a-acao-falou-devido-a-um-erro-de-coneccao-com-o-servidor-tente-novamente/262225)
(`C06T13R-1X-15 4D6174630C5D`), where the replies attribute it to a firewall blocking the server
connection and state that it does not prevent playing — the game simply cannot fetch cloud progress
and match results. The game's own log agrees: it is full of `failure: -48`,
`WINHTTP_CALLBACK_STATUS_REQUEST_ERROR ... dwError=12157` and `XAL_TELEMETRY` failures, the same
network noise the Mac build shows.

Note the hex suffix encodes an ASCII tag: `4D617463` is `"Matc"`, and the other thread's
`52656C69` is `"Reli"`.

## Where that leaves the hypothesis

Hiding FEX's CPUID signature was necessary-looking but is **not sufficient**: the kill still fires.
So Aegis is reacting to something else as well — or to a combination that this alone does not cover.

Open questions worth attacking next:

- The game never reaches a *stable* online session here; the backend is partly unreachable
  (`-48` / `12157`). Whether a fully-connected session changes Aegis's behaviour is untested — note
  the README records the freeze happening offline too, so connectivity is probably not the trigger,
  but the combination has not been re-tested *with the patch live*.
- The two `SuspendThread` call sites (`+0x49065a1`, `+0x490699d`) and the kill thread entry
  (`+0x3e69304`) are still the sharpest lead; the earlier breakpoint attempt stalled because Aegis is
  anti-debug. With the emulator now not advertising itself, re-running that probe may behave
  differently.
