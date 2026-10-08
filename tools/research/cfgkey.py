#!/usr/bin/env python3
"""Patch arbitrary keys in the live .container. Usage: cfgkey.py key=value [key=value ...]"""
import sys, json, time
sys.path.insert(0, ".")
from run_experiment import winhandler_exec, adb

pairs = [a.split("=", 1) for a in sys.argv[1:]]
adb(["shell", "rm", "-f", "/sdcard/Download/aoe/cfg_live.json"])
open("pullcfg.bat", "w").write('@echo off\r\ncopy /y "Z:\\home\\xuser\\.container" D:\\aoe\\cfg_live.json >nul\r\n')
adb(["push", "pullcfg.bat", "/sdcard/Download/pullcfg.bat"])
winhandler_exec("cmd", "/c D:\\pullcfg.bat"); time.sleep(8)
adb(["pull", "/sdcard/Download/aoe/cfg_live.json", "cfg_live.json"])
o = json.loads(open("cfg_live.json").read())
for k, v in pairs:
    if "." in k:
        head, tail = k.split(".", 1)
        o.setdefault(head, {})[tail] = v
    else:
        o[k] = v
    print(f"set {k} = {v}")
json.dump(o, open("cfg_patch.json", "w"), separators=(",", ":"))
adb(["push", "cfg_patch.json", "/sdcard/Download/aoe/cfg_patch.json"])
open("pushcfg.bat", "w").write('@echo off\r\ncopy /y D:\\aoe\\cfg_patch.json "Z:\\home\\xuser\\.container"\r\necho pushed > D:\\aoe\\cfg_ok.txt\r\n')
adb(["push", "pushcfg.bat", "/sdcard/Download/pushcfg.bat"])
winhandler_exec("cmd", "/c D:\\pushcfg.bat"); time.sleep(8)
print("pushed")
