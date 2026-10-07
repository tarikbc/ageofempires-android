#!/usr/bin/env python3
"""Live frame-time graph of the game on the Thor, in the Mac's browser (like Minecraft's F3 frame graph).

    fpsgraph.py [--port 8765] [--csv OUT]      then open http://127.0.0.1:8765

Nothing runs inside Wine or the game. One `adb shell` loop on the Thor prints `dumpsys SurfaceFlinger --latency` for
GameNative's SurfaceView once a second: the compositor already keeps the present time of the last 128 frames of every
surface, so the cost on the Thor is one small dumpsys per second. The Mac merges the windows and draws the graph.

Other scripts can put a labelled marker on the graph (for example "loop paused") with
    curl -s 'http://127.0.0.1:8765/mark?label=loop%20paused'
"""
import http.server
import json
import subprocess
import sys
import threading
import time
import urllib.parse

SERIAL = "64ff2273"
LAYER_HINT = "SurfaceView[app.gamenative/"

lock = threading.Lock()
frames = []  # present times in ns, ascending
marks = []  # (present-time-domain ns, label)
last_seen = [0]
csv_file = None


def adb_shell(cmd):
    return subprocess.run(["adb", "-s", SERIAL, "shell", cmd], capture_output=True, text=True).stdout


def game_layer():
    for line in adb_shell("dumpsys SurfaceFlinger --list").splitlines():
        if line.startswith(LAYER_HINT) and "(BLAST)" in line:
            return line.strip()
    return None


def device_now_ns():
    # SurfaceFlinger timestamps are CLOCK_MONOTONIC; /proc/uptime is close enough to place markers
    out = adb_shell("cat /proc/uptime").split()
    return int(float(out[0]) * 1e9) if out else 0


def add_window(text):
    vals = []
    for tok in text.split()[1:]:
        try:
            vals.append(int(tok))
        except ValueError:
            pass
    new = [vals[i + 1] for i in range(0, len(vals) - 2, 3) if 0 < vals[i + 1] < (1 << 62)]
    with lock:
        for t in new:
            if t > last_seen[0]:
                if csv_file and frames:
                    csv_file.write(f"{t},{(t - frames[-1]) / 1e6:.3f}\n")
                frames.append(t)
                last_seen[0] = t
        del frames[:-36000]  # keep about 10-20 minutes
    if csv_file:
        csv_file.flush()


def poll_loop():
    while True:
        layer = game_layer()
        if not layer:
            time.sleep(3)
            continue
        cmd = f"while true; do dumpsys SurfaceFlinger --latency '{layer}'; echo ==END==; sleep 1; done"
        proc = subprocess.Popen(["adb", "-s", SERIAL, "shell", cmd], stdout=subprocess.PIPE, text=True)
        buf = []
        empty = 0
        for line in proc.stdout:
            if line.startswith("==END=="):
                text = "".join(buf)
                buf = []
                if len(text.split()) <= 1:
                    empty += 1
                    if empty >= 3:  # the layer is gone (game restarted): find it again
                        break
                    continue
                empty = 0
                add_window(text)
            else:
                buf.append(line)
        proc.kill()
        time.sleep(1)


PAGE = r"""<!doctype html><html><head><meta charset="utf-8"><title>Thor frame graph</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
:root{--bg:#101214;--fg:#e8e8e8;--dim:#8a9099;--grid:#2a2f36}
body{margin:0;background:var(--bg);color:var(--fg);font:14px ui-monospace,Menlo,monospace}
#top{display:flex;flex-wrap:wrap;gap:18px;padding:10px 16px;align-items:baseline}
.v{font-size:22px;font-weight:600}.l{color:var(--dim);font-size:12px}
canvas{display:block;width:100%;height:calc(100vh - 120px)}
#bar{padding:4px 16px;color:var(--dim);font-size:12px}
button{background:#22272e;color:var(--fg);border:1px solid #3a414b;border-radius:4px;padding:3px 10px;font:inherit}
</style></head><body>
<div id="top">
 <div><div class="v" id="fps1">-</div><div class="l">FPS (1 s)</div></div>
 <div><div class="v" id="fps10">-</div><div class="l">FPS (10 s)</div></div>
 <div><div class="v" id="low">-</div><div class="l">1% low FPS (30 s)</div></div>
 <div><div class="v" id="med">-</div><div class="l">median ms (30 s)</div></div>
 <div><div class="v" id="s50">-</div><div class="l">frames &gt;50 ms (30 s)</div></div>
 <div><div class="v" id="s100">-</div><div class="l">frames &gt;100 ms (30 s)</div></div>
 <div><div class="v" id="mx">-</div><div class="l">worst ms (30 s)</div></div>
 <div><button id="pause">pause</button> <button id="zoom">scale 100 ms</button></div>
</div>
<canvas id="c"></canvas>
<div id="bar">bar = one frame, height = frame time. green &le; 34 ms, yellow &le; 50 ms, red &gt; 50 ms. Lines at 60/30/20/10 FPS.</div>
<script>
let frames=[],marks=[],paused=false,scale=100,since=0;
const c=document.getElementById('c'),g=c.getContext('2d');
document.getElementById('pause').onclick=e=>{paused=!paused;e.target.textContent=paused?'resume':'pause'};
document.getElementById('zoom').onclick=e=>{scale=scale==100?200:scale==200?500:100;e.target.textContent='scale '+scale+' ms'};
async function pull(){
 try{const r=await fetch('/data?since='+since);const j=await r.json();
  if(j.frames.length){frames=frames.concat(j.frames);since=j.frames[j.frames.length-1];if(frames.length>40000)frames=frames.slice(-40000)}
  marks=j.marks}catch(e){}
 setTimeout(pull,500)}
function stats(){
 const n=frames.length;if(n<3)return;const last=frames[n-1];
 const win=s=>{let i=n-1;while(i>0&&last-frames[i-1]<=s*1e9)i--;return i};
 const i1=win(1),i10=win(10),i30=win(30);
 const set=(id,v)=>document.getElementById(id).textContent=v;
 set('fps1',((n-1-i1)/((last-frames[i1])/1e9||1)).toFixed(1));
 set('fps10',((n-1-i10)/((last-frames[i10])/1e9||1)).toFixed(1));
 const ft=[];for(let i=i30+1;i<n;i++)ft.push((frames[i]-frames[i-1])/1e6);
 const q=ft.slice().sort((a,b)=>a-b);
 const p99=q[Math.max(0,Math.floor(q.length*0.99)-1)];
 set('low',(1000/p99).toFixed(1));set('med',q[Math.floor(q.length/2)].toFixed(1));
 set('s50',ft.filter(f=>f>50).length);set('s100',ft.filter(f=>f>100).length);set('mx',q[q.length-1].toFixed(0))}
function draw(){
 requestAnimationFrame(draw);if(paused)return;
 const W=c.clientWidth,H=c.clientHeight,dpr=window.devicePixelRatio||1;
 if(c.width!=W*dpr||c.height!=H*dpr){c.width=W*dpr;c.height=H*dpr}
 g.setTransform(dpr,0,0,dpr,0,0);g.clearRect(0,0,W,H);
 const bw=3,count=Math.floor((W-50)/bw),n=frames.length,y=ms=>H-6-Math.min(ms,scale)/scale*(H-12);
 g.font='11px ui-monospace,Menlo,monospace';
 for(const [ms,lab] of [[16.7,'60'],[33.3,'30'],[50,'20'],[100,'10'],[200,'5']]){if(ms>scale)continue;
  g.strokeStyle='#2a2f36';g.beginPath();g.moveTo(40,y(ms));g.lineTo(W,y(ms));g.stroke();
  g.fillStyle='#8a9099';g.fillText(lab+' fps',2,y(ms)+4)}
 const start=Math.max(1,n-count);
 for(let i=start;i<n;i++){const ms=(frames[i]-frames[i-1])/1e6,x=W-(n-i)*bw;
  g.fillStyle=ms<=34?'#3fb950':ms<=50?'#d29922':'#f85149';g.fillRect(x,y(ms),bw-1,H-6-y(ms))}
 if(n>1){for(const [t,label] of marks){let i=n-1;while(i>start&&frames[i]>t)i--;if(i<=start)continue;
  const x=W-(n-i)*bw;g.strokeStyle='#58a6ff';g.beginPath();g.moveTo(x,0);g.lineTo(x,H);g.stroke();
  g.fillStyle='#58a6ff';g.fillText(label,x+3,12)}}
 stats()}
pull();draw();
</script></body></html>"""


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def send(self, code, body, ctype):
        data = body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        url = urllib.parse.urlparse(self.path)
        q = urllib.parse.parse_qs(url.query)
        if url.path == "/":
            self.send(200, PAGE, "text/html; charset=utf-8")
        elif url.path == "/data":
            since = int(q.get("since", ["0"])[0])
            with lock:
                new = [t for t in frames[-4000:] if t > since]
                mk = list(marks[-50:])
            self.send(200, json.dumps({"frames": new, "marks": mk}), "application/json")
        elif url.path == "/mark":
            label = q.get("label", ["mark"])[0][:40]
            t = device_now_ns()
            with lock:
                marks.append((t, label))
            self.send(200, "ok\n", "text/plain")
        else:
            self.send(404, "not found\n", "text/plain")


def main():
    global csv_file
    args = sys.argv[1:]
    port = int(args[args.index("--port") + 1]) if "--port" in args else 8765
    if "--csv" in args:
        csv_file = open(args[args.index("--csv") + 1], "w")
        csv_file.write("present_ns,frame_ms\n")
    threading.Thread(target=poll_loop, daemon=True).start()
    server = http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler)
    print(f"frame graph at http://127.0.0.1:{port}")
    server.serve_forever()


if __name__ == "__main__":
    main()
