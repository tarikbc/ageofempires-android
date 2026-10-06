import sys, time, socket, struct
sys.path.insert(0, ".")
from run_experiment import adb
from gn_nav import GN

UID = None
def app_uid():
    o = adb(["shell", "dumpsys", "package", "app.gamenative"], timeout=40).stdout
    for line in o.splitlines():
        if "userId=" in line:
            return line.split("userId=")[1].split()[0]
    return None

def hexip(h):
    # /proc/net/tcp uses little-endian hex for IPv4
    b = bytes.fromhex(h)
    return socket.inet_ntoa(bytes(reversed(b)))

def sample():
    ips = {}
    for f in ("/proc/net/tcp", "/proc/net/tcp6"):
        o = adb(["shell", "cat", f], timeout=30).stdout
        for line in o.splitlines()[1:]:
            p = line.split()
            if len(p) < 8: continue
            rem, st, uid = p[2], p[3], p[7]
            if UID and uid != UID: continue
            if st == "0A": continue          # LISTEN
            ip, port = rem.split(":")
            if ip in ("00000000", "0"*32): continue
            try:
                a = hexip(ip) if len(ip) == 8 else socket.inet_ntop(socket.AF_INET6, bytes.fromhex(ip))
            except Exception:
                continue
            if a.startswith("127.") or a == "::1": continue
            ips[(a, int(port, 16))] = ips.get((a, int(port, 16)), 0) + 1
    return ips

UID = app_uid()
print("app uid:", UID, flush=True)
adb(["shell", "am", "force-stop", "app.gamenative"]); time.sleep(5)
adb(["shell", "am", "start", "-n", "app.gamenative/.MainActivityAliasDefault"]); time.sleep(8)
gn = GN()
try:
    gn.tap_text("Age of Empires IV: Anniversary Edition"); time.sleep(3)
    gn.tap_text("Play"); print("launched", flush=True)
except Exception as e:
    print("nav:", e, flush=True)

all_ips = {}
for i in range(30):
    time.sleep(10)
    try:
        for k, v in sample().items():
            all_ips[k] = all_ips.get(k, 0) + v
    except Exception as e:
        pass
    if i % 3 == 0:
        print(f"  t={i*10}s distinct endpoints so far: {len(all_ips)}", flush=True)

print("\n=== remote endpoints contacted ===", flush=True)
for (a, p), c in sorted(all_ips.items(), key=lambda x: -x[1]):
    try:
        name = socket.gethostbyaddr(a)[0]
    except Exception:
        name = "?"
    print(f"  {a:40s} :{p:<6d} samples={c:<3d} {name}", flush=True)
