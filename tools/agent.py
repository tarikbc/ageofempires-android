#!/usr/bin/env python3
"""Host side of tools/probes/aoeagent.c: start the agent once inside the Wine session, then send it commands.

    agent.py start                 push aoeagent.exe (if changed) and start it; no console window opens
    agent.py ping | threads | stop
    agent.py <any agent command>   e.g.  agent.py mod libarm64ecfex.dll
                                         agent.py pause 3e1b04c 8000
                                         agent.py peek libarm64ecfex.dll 3ee038 100

Each command is one `adb shell` call: it writes req.txt, waits on the Thor for rsp.txt, prints it and removes it.
The agent runs until `stop` or the end of the Wine session; `start` is safe to repeat (a second agent exits at once).
"""
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run_watch as rw  # noqa: E402

EXE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "probes", "aoeagent.exe")
REMOTE_EXE = "/sdcard/Download/aoeagent.exe"
DIR = "/sdcard/Download/aoe/agent"
_seq = [int(time.time() * 1000) % 100000000]


def call(command, timeout=15):
    """Send one command; return the response lines without the status line and END. Raises on err/timeout."""
    _seq[0] += 1
    rid = _seq[0]
    loops = int(timeout / 0.05)
    script = (f"cd {DIR} 2>/dev/null || exit 3; rm -f rsp.txt; printf '%s\\n' '{rid} {command}' > req.tmp && "
              f"mv req.tmp req.txt; i=0; while [ $i -lt {loops} ]; do "
              f"if grep -q '^END' rsp.txt 2>/dev/null; then cat rsp.txt; rm -f rsp.txt; exit 0; fi; "
              f"sleep 0.05; i=$((i+1)); done; echo TIMEOUT")
    out = rw.sh(script, timeout=timeout + 15).splitlines()
    if not out or out[-1].strip() == "TIMEOUT":
        raise TimeoutError(f"agent did not answer '{command}' (is it running? agent.py start)")
    status = out[0]
    if not status.startswith(f"id={rid} ok"):
        raise RuntimeError(status)
    return [status[len(f"id={rid} ok"):].strip()] + [line for line in out[1:] if line.strip() != "END"]


def running():
    return "aoeagent.exe" in rw.sh("ps -A -o NAME")


def start():
    if not os.path.exists(EXE):
        sys.exit(f"{EXE} is missing: run tools/probes/build.sh")
    local = os.path.getsize(EXE)
    remote = rw.sh(f"stat -c %s {REMOTE_EXE} 2>/dev/null").strip()
    if remote != str(local):
        rw.adb("push", EXE, REMOTE_EXE)
    rw.sh(f"mkdir -p {DIR}")
    if not running():
        rw.winexec("D:\\aoeagent.exe", "")
    for _ in range(40):
        time.sleep(0.5)
        try:
            return call("ping", timeout=2)[0]
        except (TimeoutError, RuntimeError):
            pass
    raise TimeoutError("agent did not start")


def peek(module, offset, size):
    """Bytes of the target's memory at module base + offset (module '0' = absolute address)."""
    data = bytearray()
    for line in call(f"peek {module} {offset:x} {size:x}", timeout=30)[1:]:
        data += bytes(int(x, 16) for x in line.split(":", 1)[1].split())
    return bytes(data)


def main():
    args = sys.argv[1:]
    if not args:
        sys.exit(__doc__)
    if args[0] == "start":
        print(start())
    elif args[0] == "stop":
        print("\n".join(call("quit")))
    else:
        print("\n".join(call(" ".join(args), timeout=60)))


if __name__ == "__main__":
    main()
