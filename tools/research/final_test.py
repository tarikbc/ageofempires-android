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

adb(["shell", "rm", "-f", "/sdcard/Download/aoe/watch.txt",
     "/sdcard/Download/aoe/xtajit_now.dll", "/sdcard/Download/aoe/libfex_now.dll"])
gn = GN()
gn.tap_text("Play")
print("Play tapped", flush=True)
t0 = time.time(); pid = ""
while time.time() - t0 < 240:
    pid = game_pid()
    if pid: break
    time.sleep(3)
if not pid:
    print("NO PROCESS"); raise SystemExit
t_start = time.time()
print(f"pid={pid} t=0", flush=True)

time.sleep(45)
print("--- installing check: emulator DLL bytes ---", flush=True)
winhandler_exec("cmd", "/c D:\\dllnow.bat"); time.sleep(10)
print(adb(["shell","ls","-la","/sdcard/Download/aoe/xtajit_now.dll","/sdcard/Download/aoe/libfex_now.dll"],timeout=30).stdout, flush=True)
print("--- live CPUID in a fresh session process ---", flush=True)
winhandler_exec("cmd", "/c D:\\dbgrun.bat"); time.sleep(12)
out = adb(["shell","cat","/sdcard/Download/dbgprobe.txt"],timeout=30).stdout
for line in out.splitlines():
    if "hypervisor" in line or "libarm64ecfex" in line or "xtajit" in line:
        print("   ", line.strip(), flush=True)

print("--- survival: log growth is the criterion ---", flush=True)
last = -1
for i in range(48):
    time.sleep(10)
    el = int(time.time() - t_start)
    if el % 60 < 12:
        winhandler_exec("cmd", "/c D:\\cp.bat"); time.sleep(2)
        cur = logsize()
        print(f"t={el}s alive={bool(game_pid())} log={cur}{'   (STALLED)' if cur==last else ''}", flush=True)
        last = cur
    if not game_pid():
        print(f"t={el}s *** PROCESS GONE ***", flush=True); break
print(f"final t={int(time.time()-t_start)}s", flush=True)
