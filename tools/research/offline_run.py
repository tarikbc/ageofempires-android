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
adb(["shell", "am", "force-stop", "app.gamenative"]); time.sleep(6)
adb(["shell", "am", "start", "-n", "app.gamenative/.MainActivityAliasDefault"]); time.sleep(12)
gn = GN()
try:
    gn.tap_text("Age of Empires IV: Anniversary Edition"); time.sleep(3)
    gn.tap_text("Play"); print("launched", flush=True)
except Exception as e:
    print("nav:", e, flush=True)
t0 = time.time(); pid = ""
while time.time() - t0 < 300:
    pid = game_pid()
    if pid: break
    time.sleep(3)
if not pid:
    print("NO PROCESS"); raise SystemExit
ts = time.time()
print(f"pid={pid} t=0  (device is OFFLINE)", flush=True)

last = -1
for i in range(54):
    time.sleep(10)
    el = int(time.time() - ts)
    if el % 60 < 12:
        winhandler_exec("cmd", "/c D:\\cp.bat"); time.sleep(2)
        cur = logsize()
        print(f"t={el}s alive={bool(game_pid())} log={cur}{'   <-- STALLED' if cur==last else ''}", flush=True)
        last = cur
    if not game_pid():
        print(f"t={el}s *** PROCESS GONE ***", flush=True); break
print(f"final t={int(time.time()-ts)}s", flush=True)
