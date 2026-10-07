#!/usr/bin/env python3
"""Package a FEX libarm64ecfex.dll as a GameNative FEXCore content bundle (.wcp).

    make_fex_wcp.py DLL OUT.wcp --name aoe-fastcontinue --code 10 [--description TEXT]

A .wcp is an xz-compressed tar with profile.json and the files it names. GameNative's Contents Manager imports it,
and the FEXCore Version selected in the container's Emulation tab is then copied to its targets at every container
start (docs/GAMENATIVE-UI.md). The dropdown shows it as <name>-<code>. Only libarm64ecfex.dll is shipped: that is the
one file the tested setup replaced.
"""
import argparse
import io
import json
import tarfile
import time

ap = argparse.ArgumentParser()
ap.add_argument("dll")
ap.add_argument("out")
ap.add_argument("--name", required=True)
ap.add_argument("--code", type=int, required=True)
ap.add_argument("--description", default="")
a = ap.parse_args()

profile = {
    "type": "FEXCore",
    "versionName": a.name,
    "versionCode": a.code,
    "description": a.description,
    "files": [{"source": "libarm64ecfex.dll", "target": "${system32}/libarm64ecfex.dll"}],
}
data = json.dumps(profile, indent=2).encode()
with tarfile.open(a.out, "w:xz") as tar:
    info = tarfile.TarInfo("profile.json")
    info.size, info.mtime, info.mode = len(data), int(time.time()), 0o644
    tar.addfile(info, io.BytesIO(data))
    tar.add(a.dll, arcname="libarm64ecfex.dll")
print(f"wrote {a.out}: {a.name}-{a.code}")
