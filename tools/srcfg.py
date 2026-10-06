#!/usr/bin/env python3
"""Patch keys in the SOURCE container config. Usage: srcfg.py key=value ..."""
import sys, json, time
sys.path.insert(0, ".")
from run_experiment import winhandler_exec, adb

SRC = 'Z:\\home\\xuser-STEAM_1466860\\.container'
pairs = [a.split("=", 1) for a in sys.argv[1:]]
open("pullsrc.bat", "w").write('@echo off\r\ncopy /y "%s" D:\\aoe\\src_live.json >nul\r\n' % SRC)
adb(["push", "pullsrc.bat", "/sdcard/Download/pullsrc.bat"])
winhandler_exec("cmd", "/c D:\\pullsrc.bat"); time.sleep(8)
adb(["shell", "rm", "-f", "/sdcard/Download/aoe/src_live.json"])
winhandler_exec("cmd", "/c D:\\pullsrc.bat"); time.sleep(8)
adb(["pull", "/sdcard/Download/aoe/src_live.json", "src_live.json"])
o = json.loads(open("src_live.json").read())
for k, v in pairs:
    if "." in k:
        h, t = k.split(".", 1); o.setdefault(h, {})[t] = v
    else:
        o[k] = v
    print(f"set {k} = {v}")
json.dump(o, open("src_patch.json", "w"), separators=(",", ":"))
adb(["push", "src_patch.json", "/sdcard/Download/aoe/src_patch.json"])
open("pushsrc.bat", "w").write('@echo off\r\ncopy /y D:\\aoe\\src_patch.json "%s"\r\necho ok > D:\\aoe\\src_ok.txt\r\n' % SRC)
adb(["push", "pushsrc.bat", "/sdcard/Download/pushsrc.bat"])
winhandler_exec("cmd", "/c D:\\pushsrc.bat"); time.sleep(8)
print("pushed to source")
