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

adb(["shell", "rm", "-f", "/sdcard/Download/aoe/watch.txt"])
ts = time.time(); last = -1
print("monitoring (CertificateRevocation=0)", flush=True)
for i in range(60):                       # ~10 minutes
    time.sleep(10)
    el = int(time.time() - ts)
    if el % 60 < 12:
        winhandler_exec("cmd", "/c D:\\cp.bat"); time.sleep(2)
        cur = logsize()
        flag = "   <-- LOG STALLED" if cur == last else ""
        print(f"t={el}s alive={bool(game_pid())} log={cur}{flag}", flush=True)
        last = cur
    if not game_pid():
        print(f"t={el}s *** PROCESS GONE ***", flush=True); break
print(f"final t={int(time.time()-ts)}s", flush=True)
