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

def logsize():
    o = adb(["shell", "ls", "-la", "/sdcard/Download/aoe/watch.txt"], timeout=30).stdout
    try: return int(o.split()[4])
    except Exception: return -1

adb(["shell", "rm", "-f", "/sdcard/Download/aoe/watch.txt"])
adb(["shell", "am", "force-stop", "app.gamenative"]); time.sleep(5)
adb(["shell", "input", "keyevent", "4"])
adb(["shell", "am", "start", "-n", "app.gamenative/.MainActivityAliasDefault"]); time.sleep(3)
gn = GN()
gn.tap_text("Age of Empires IV: Anniversary Edition"); time.sleep(2)
gn.tap_text("Play")
print("launched", flush=True)
t0 = time.time(); pid = ""
while time.time() - t0 < 240:
    pid = game_pid()
    if pid: break
    time.sleep(3)
if not pid:
    print("NO PROCESS"); raise SystemExit
t_start = time.time()
print(f"pid={pid} t=0", flush=True)

# one early check: which emulator did the game load?
time.sleep(40)
winhandler_exec("cmd", "/c D:\\modchkrun.bat")
time.sleep(6)
mk = adb(["shell", "cat", "/sdcard/Download/modchk.txt"], timeout=30).stdout.strip()
print("EMULATOR CHECK:", mk.replace("\r", "").replace("\n", " | "), flush=True)

# monitor: process alive AND log still growing
prev = -1
for i in range(48):
    time.sleep(10)
    alive = game_pid()
    el = int(time.time() - t_start)
    if el % 60 < 12:
        winhandler_exec("cmd", "/c D:\\cp.bat")
        time.sleep(2)
        cur = logsize()
        print(f"t={el}s alive={bool(alive)} log={cur}", flush=True)
        prev = cur
    if not alive:
        print(f"t={el}s *** PROCESS GONE ***", flush=True)
        break
print(f"final t={int(time.time()-t_start)}s", flush=True)
