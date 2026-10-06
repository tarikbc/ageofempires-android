import sys, time
sys.path.insert(0, ".")
from run_experiment import adb, winhandler_exec
from gn_nav import GN

def game_pid():
    r = adb(["shell", "ps", "-A"], timeout=30)
    for line in r.stdout.splitlines():
        if "RelicCardinal.exe" in line and "grep" not in line:
            p = line.split()
            if p: return p[1]
    return ""

adb(["shell", "rm", "-f", "/sdcard/Download/aoe/memwatch.txt", "/sdcard/Download/aoe/watch.txt"])
adb(["shell", "am", "force-stop", "app.gamenative"]); time.sleep(5)
adb(["shell", "am", "start", "-n", "app.gamenative/.MainActivityAliasDefault"]); time.sleep(8)
gn = GN()
gn.tap_text("Age of Empires IV: Anniversary Edition"); time.sleep(3)
gn.tap_text("Play"); print("launched", flush=True)
t0 = time.time(); pid = ""
while time.time() - t0 < 240:
    pid = game_pid()
    if pid: break
    time.sleep(3)
print("pid:", pid, flush=True)
time.sleep(25)
winhandler_exec("cmd", "/c D:\\mwrun.bat")
print("memory watcher started", flush=True)
for i in range(50):
    time.sleep(10)
    if not game_pid():
        print(f"t={int(time.time()-t0)}s process gone", flush=True); break
print("done", flush=True)
