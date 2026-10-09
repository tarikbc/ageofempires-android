#!/usr/bin/env python3
"""Temperatures, GPU load and CPU clocks of the Thor over adb (no root needed).

    thermals.py [SECONDS]        sample every 3 s and print a summary

As a library: Sampler().start() ... .stop() -> summary string, used by bench.py during each frame-time window.
Zones on the Thor (2026-10-07): cpu-0-* small cores, cpu-1-* big cores, cpuss-* CPU subsystem, gpuss-* GPU.
"""
import subprocess
import sys
import threading
import time

SERIAL = "64ff2273"
# Shell builtins only (read, echo): the earlier version ran two `cat` per thermal zone (95 zones, about 190 processes
# every 3 s), which took up to 30 ms of a big core inside measured frames. Zone names are read once.
SCRIPT = ("for z in /sys/class/thermal/thermal_zone*; do read t < $z/temp && echo T ${z##*zone} $t; done 2>/dev/null; "
          "read b < /sys/class/kgsl/kgsl-3d0/gpu_busy_percentage; read c < /sys/class/kgsl/kgsl-3d0/gpuclk; echo G $b $c; "
          "read m < /sys/devices/system/cpu/cpu3/cpufreq/scaling_cur_freq; "
          "read x < /sys/devices/system/cpu/cpu7/cpufreq/scaling_cur_freq; echo F $m $x")
TYPES_SCRIPT = "for z in /sys/class/thermal/thermal_zone*; do read n < $z/type && echo ${z##*zone} $n; done 2>/dev/null"
_types = {}


def zone_types():
    if not _types:
        out = subprocess.run(["adb", "-s", SERIAL, "shell", TYPES_SCRIPT], capture_output=True, text=True).stdout
        for line in out.splitlines():
            p = line.split()
            if len(p) == 2:
                _types[p[0]] = p[1]
    return _types


def sample():
    types = zone_types()
    out = subprocess.run(["adb", "-s", SERIAL, "shell", SCRIPT], capture_output=True, text=True).stdout
    cpu, gpu, res = [], [], {}
    for line in out.splitlines():
        p = line.split()
        if p[:1] == ["T"] and len(p) == 3 and p[2].lstrip("-").isdigit():
            p[1] = types.get(p[1], "")
            if p[1].startswith(("cpu-", "cpuss-")):
                cpu.append(int(p[2]) / 1000)
            elif p[1].startswith("gpuss-"):
                gpu.append(int(p[2]) / 1000)
        elif p[:1] == ["G"]:
            nums = [int(x.rstrip("%")) for x in p[1:] if x.rstrip("%").isdigit()]  # "71 % 615000000"
            if len(nums) == 2:
                res["gpu_busy"], res["gpu_mhz"] = nums[0], nums[1] // 1000000
        elif p[:1] == ["F"] and len(p) == 3:
            res["mid_mhz"], res["prime_mhz"] = int(p[1]) // 1000, int(p[2]) // 1000
    if cpu:
        res["cpu_max"], res["cpu_mean"] = max(cpu), sum(cpu) / len(cpu)
    if gpu:
        res["gpu_max"] = max(gpu)
    return res


class Sampler:
    def __init__(self, interval=3.0):
        self.interval, self.samples, self._stop = interval, [], threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)

    def _run(self):
        while not self._stop.is_set():
            self.samples.append(sample())
            self._stop.wait(self.interval)

    def start(self):
        self._thread.start()
        return self

    def stop(self):
        self._stop.set()
        self._thread.join()
        return summary(self.samples)


def summary(samples):
    def col(k):
        return [s[k] for s in samples if k in s]
    if not samples:
        return "no thermal samples"
    cmax, cmean, gmax = col("cpu_max"), col("cpu_mean"), col("gpu_max")
    busy, pmhz, gmhz = col("gpu_busy"), col("prime_mhz"), col("gpu_mhz")
    return (f"CPU max {max(cmax):.1f} C (mean of sensors {sum(cmean) / len(cmean):.1f} C), GPU max {max(gmax):.1f} C, "
            f"GPU busy {sum(busy) / len(busy):.0f} % at {sorted(set(gmhz))} MHz, prime core {min(pmhz)}-{max(pmhz)} MHz")


if __name__ == "__main__":
    s = Sampler().start()
    time.sleep(float(sys.argv[1]) if len(sys.argv) > 1 else 15)
    print(s.stop())
