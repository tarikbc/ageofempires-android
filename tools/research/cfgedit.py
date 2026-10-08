#!/usr/bin/env python3
"""edit .container: pull -> patch envVars -> push. Usage: cfgedit.py OLD NEW"""
import subprocess, sys, json, time, os
sys.path.insert(0, ".")
from run_experiment import winhandler_exec, adb

old, new = sys.argv[1], sys.argv[2]
# pull the live file
adb(["shell", "rm", "-f", "/sdcard/Download/aoe/cfg_live.json"])
open("pullcfg.bat", "w").write('@echo off\r\ncopy /y "Z:\\home\\xuser\\.container" D:\\aoe\\cfg_live.json >nul\r\n')
adb(["push", "pullcfg.bat", "/sdcard/Download/pullcfg.bat"])
winhandler_exec("cmd", "/c D:\\pullcfg.bat"); time.sleep(8)
adb(["pull", "/sdcard/Download/aoe/cfg_live.json", "cfg_live.json"])
raw = open("cfg_live.json").read()
o = json.loads(raw)
ev = o.get("envVars", "")
if old not in ev:
    print("PATTERN NOT FOUND. envVars tail:", ev[-100:]); sys.exit(1)
o["envVars"] = ev.replace(old, new)
json.dump(o, open("cfg_patch.json", "w"), separators=(",", ":"))
adb(["push", "cfg_patch.json", "/sdcard/Download/aoe/cfg_patch.json"])
open("pushcfg.bat", "w").write('@echo off\r\ncopy /y D:\\aoe\\cfg_patch.json "Z:\\home\\xuser\\.container"\r\nfindstr /c:"WINEDEBUG" "Z:\\home\\xuser\\.container" > D:\\aoe\\cfg_ok.txt 2>&1\r\n')
adb(["push", "pushcfg.bat", "/sdcard/Download/pushcfg.bat"])
winhandler_exec("cmd", "/c D:\\pushcfg.bat"); time.sleep(8)
t = adb(["shell", "cat", "/sdcard/Download/aoe/cfg_ok.txt"]).stdout
i = t.find("WINEDEBUG")
print("applied:", t[max(0,i):i+70] if i >= 0 else t[:200])
