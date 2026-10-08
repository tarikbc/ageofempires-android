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

adb(["shell", "am", "force-stop", "app.gamenative"]); time.sleep(4)
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
print(f"pid={pid} at t=0 (process appeared)", flush=True)
# monitor for 8 minutes
for i in range(48):
    time.sleep(10)
    alive = game_pid()
    el = int(time.time() - t_start)
    if not alive:
        print(f"t={el}s  *** GAME GONE (killed) ***", flush=True)
        break
    if i % 3 == 0:
        print(f"t={el}s  alive pid={alive}", flush=True)
else:
    print(f"t={int(time.time()-t_start)}s  *** STILL ALIVE after 8 minutes ***", flush=True)
