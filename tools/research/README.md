# Investigation scripts

One-off scripts and Windows probes from the investigation, kept as a record. Most were written for one experiment
and expect the Thor and the game in the state of that time; the [research log](../../docs/research/LOG.md)
and the write-ups in [docs/research](../../docs/research) say what each experiment found.

The Python scripts find their helpers through the working directory: run them from `tools/`, for example
`python3 research/cfgedit.py`. The C probes build with `x86_64-w64-mingw32-gcc`, like the ones in
[`tools/probes`](../probes).

## Index

**Analysis tools still in use** (the speed work, 2026-10-08 and 09; listed in [tools/README.md](../README.md)):
`frametimes_graph.py` (the README graph), `stutter_align.py`, `present_chain.py`, `pipeline_timeline.py`,
`freq_in_frames.py`, `fexstats_align.py`, `gn_chain.py`, `at_analyze.py`, `sysprof.py`, `ntdll_syscall_table.py`, and
the GPU-trace readers `turnip_stages.py`, `turnip_lrz_reasons.py`, `turnip_renderstages.cfg`, `turnip_gpu_timeline.py`,
`turnip_submit_latency.py`, `turnip_cpu_events.py`. `winhandler_exec.py` is imported by `tools/run_watch.py`.

**From the investigation of 2026-10-06 and 07**, named in the docs ([archive](../../docs/research/archive)):
`antidebug2.c`, `apiprobe.c`, `ws_native.py`, `ws_paths.py`, `wsprobe.c`, `reuseprobe.c`, `timingtest.c`,
`netprobe.c`, `postprobe.c`, `memwatch.c`, `xboxprobe.c`, `cfgedit.py`, `dbgprobe.c`, `envdump.c`, `modlist.c`,
`ntdllcheck.c`, `memscan.c`, `bpguard.c`, `bp_run.py`, `dumprange.c`, `disasm.py`, `impmap.py`, `callers.py`,
`run_experiment.py`, `smctest.c`, `smctest2.c`, `parse_aegistrace.py`, `blkparse.py`, `blkmem.py`, `dettable.py`.

**Not named in any doc** (kept as the record of that time): `cfgkey.py` and `srcfg.py` (read keys in the live and the
source container config), `conns.py`, `eager_lazy_test.py` (the eager-versus-lazy decryption test behind AEGIS.md's
"eager restore" finding), `filescan.c` (a byte pattern in files), `findhash.py` (reads `text.bin`), `hcount.c` (handle
counts over time), `protmon.c` (the protection map over time), `timing3.bat` (runs `timingtest.exe` three times), and
11 run drivers built on `run_experiment.py`: `final_test.py`, `memwatch_run.py`, `mon2.py`, `monitor_run.py`,
`offline_run.py`, `patched_test.py`, `revtest_run.py`, `survivetest.py`, `verify_test.py`, `waitq_run.py`,
`xtajit_run.py`. `verify_test.py` calls a `modchk` probe that is not in the repo.

`memwatch.c` and `netprobe.c` here are different programs from the ones with the same names in
[`tools/probes`](../probes).
