import base64, os, socket, ssl, struct, sys
HOST="dr-activerelease1-api.worldsedgelink.com"
def try_path(path):
    try:
        ctx=ssl.create_default_context()
        raw=socket.create_connection((HOST,443),timeout=12)
        s=ctx.wrap_socket(raw,server_hostname=HOST)
        key=base64.b64encode(os.urandom(16)).decode()
        s.sendall((f"GET {path} HTTP/1.1\r\nHost: {HOST}\r\nUpgrade: websocket\r\n"
                   f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\n"
                   "Sec-WebSocket-Version: 13\r\n\r\n").encode())
        resp=b""
        while b"\r\n\r\n" not in resp:
            c=s.recv(4096)
            if not c: break
            resp+=c
        head=resp.split(b"\r\n\r\n")[0].decode("latin1")
        first=head.split("\r\n")[0]
        hdrs={}
        for l in head.split("\r\n")[1:]:
            if ":" in l:
                k,v=l.split(":",1); hdrs[k.strip().lower()]=v.strip()
        print(f"  {path:28s} -> {first}")
        if "101" in first:
            print(f"      accept={hdrs.get('sec-websocket-accept','?')} upgrade={hdrs.get('upgrade')}")
            # hold it briefly to see if it survives
            s.settimeout(45)
            t0=__import__('time').time()
            try:
                while __import__('time').time()-t0 < 45:
                    d=s.recv(4096)
                    if not d:
                        print(f"      closed by server after {__import__('time').time()-t0:.1f}s"); break
                    op=d[0]&0xF
                    print(f"      [+{__import__('time').time()-t0:.1f}s] frame op={op} len={len(d)} {d[:60]!r}")
            except socket.timeout:
                print("      survived 45s")
        s.close()
    except Exception as e:
        print(f"  {path:28s} -> ERROR {e}")
for p in ["/wss/", "/wss", "/game/wss/", "/wss/v1/", "/"]:
    try_path(p)
