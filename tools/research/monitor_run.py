import sys, time
sys.path.insert(0, ".")
from run_experiment import adb, winhandler_exec

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

t0 = time.time()
last = -1
print("monitoring current run (stock emulator baseline)", flush=True)
for i in range(60):          # up to 10 minutes
    time.sleep(10)
    el = int(time.time() - t0)
    pid = game_pid()
    if el % 60 < 12:
        winhandler_exec("cmd", "/c D:\\cp.bat"); time.sleep(2)
        cur = logsize()
        print(f"t={el}s pid={pid} log={cur}{'  (log stalled)' if cur == last else ''}", flush=True)
        last = cur
    if not pid:
        print(f"t={el}s *** PROCESS GONE ***", flush=True); break
print("monitor done", flush=True)
