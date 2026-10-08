# The protection runs code one instruction at a time (and why that was slow under FEX)

Measured on 2026-10-07 on the Thor, in a 1v1 skirmish with the camera turning (`tools/bench.py`), with analysis
builds of FEX that count SMC faults per page and per writing instruction (job-local, not in the repo).

## What the game does

A routine in the protection (around `RelicCardinal.exe+0x3e55a0f`) executes code by copying one instruction at a
time into a slot of a 16 MB buffer and jumping there:

```
lock xadd [counter], rbx                 ; slot = counter++
and ebx, 0x7ffff ; shl rbx, 5            ; 524,288 slots of 32 bytes
add rbx, [buffer]
mov al, [rdi+1] ; mov [rbx], al          ; exe+0x3e55a43: the write that faults under FEX
loop: mov al, [rdx+rcx+1] ; not al ; mov [rcx], al     ; the rest of the instruction, stored NOT-ed
```

- The buffer is its own allocation at `0x149c40000` (16 MB), just past the exe image (which ends at `0x148c3d000`),
  read live through `tools/agent.py` (pointer at `0x147af7ad0`, counter at `0x147af7ad8`).
- Each slot holds one instruction followed by `jmp` back to a fixed return stub in the protection. Slots read live:
  `xor al, r13b`, `cmp al, [rdx+rcx+8]`, `mov dl, [rbx]`, `add rcx, r15`, `mov rax, rcx`, `sub rax, [rsi]`,
  `cmp rax, r12`, `mov al, [rcx]`, each with its `jmp`.
- Conditional jumps are not copied: the routine evaluates them itself from a saved EFLAGS value (`0F 82` to `0F 8F`
  against ZF `0x40`, SF `0x80`, OF `0x800`).
- Both the game's main thread and the protection's loop thread use it.

## Why it was slow

Under FEX every step was new code at a new address:

1. The write into the slot faulted, because FEX had write-protected the page when it compiled the previous slot.
2. FEX invalidated that page in every thread (about 99) and made it writable again.
3. The jump to the slot missed every cache, so FEX compiled a two-instruction block and protected the page again.

Counts in an early match (analysis build `be6234d8`, 15 s): **15,993 write faults/s, 11,622/s of them from that one
`mov [rbx], al`**, 7,317/s on the loop thread and 4,116/s on the main thread. Per-thread FEX statistics (analysis
build, 30 s, at about match minute 13): the main thread spent **0.207 s/s compiling (8,261 blocks/s) and 0.233 s/s in
fault handling (6,807 faults/s)**, so 44 % of its time. Suspending the loop thread for 22 s raised the main thread's
fault rate to 12,140/s (more frames) and halved its cost per fault, from 54 to 26 us: the two threads were waiting on
each other in FEX's locks.

The faults were not the game's ordinary code changing: of 145,479 blocks the main thread compiled in
`exe+0x3800000..0x5000000` (patch 0008's dump), 45 had a second byte version.

There was a second cost. FEX keeps compiled code in one shared buffer, at most 128 MB, and starts a new, empty one
when it is full, so every thread compiles all of its code again. With these builds a new 128 MB buffer was started
about every 30 s, and in one 90 s window **every frame over 80 ms came 2.5 s before a buffer replacement** in the
counter's time base (a fixed offset between the two clocks). Those are the big stutters.

## The fix: patch 0012 (volatile code regions)

[`patches/fex/0012-volatile-code-regions.patch`](../../patches/fex/0012-volatile-code-regions.patch), on by
default (the code reads `FEX_EXP_VOLATILE=0` as off; that switch was not tested):

- **Detection:** when one page of a private allocation of at most 64 MB takes 16 write faults, the whole allocation
  becomes a volatile code region. FEX drops the code it compiled there and gives the guest its write permission
  back. In the tested runs exactly one region was found: `0x149c40000`, `0x1001000` bytes.
- **No address caching there:** blocks in the region are never added to FEX's lookup caches and no branch is linked
  to them, so every visit is decided again, and the region is never write-protected again.
- **Reuse by content:** a block of the form *[one instruction without RIP-relative operands][jmp rel8/rel32 to
  outside the region]* compiles to the same host code at any slot, because the JIT turns the jump into an absolute
  target. Such blocks are cached by (instruction bytes, absolute target), per code buffer and buffer generation.
  Anything else in the region is compiled for that one visit.
- **RIP for exceptions:** a thread records the slot it entered, and FEX's RIP reconstruction uses it inside a
  shared block.
- **Counters:** `VolatileCodeStats` (marker `VOLCODE1`) holds the region, cache hits, misses, blocks that did not
  fit the pattern, and the time and size of the last 32 code buffer allocations.

[`patches/fex/0013-larger-code-buffer.patch`](../../patches/fex/0013-larger-code-buffer.patch) raises FEX's code
buffer cap from 128 to 512 MB, so the full recompile comes less often.

## Results

90 s of compositor frame times each (`tools/bench.py record` / `run`), same skirmish and camera turn:

| Build | Match time | FPS | median | p99 | frames > 50 ms | frames > 100 ms | worst |
|---|---|---|---|---|---|---|---|
| analysis `be6234d8` (before) | ~13 min | 25.6 | 33.4 ms | 116.9 ms | 761 (7.9/s) | 65 (0.67/s) | 617 ms |
| analysis + 0012 (128 MB cap) | ~1 min | 43.4 | 16.7 ms | 33.4 ms | 37 (0.39/s) | 8 (0.08/s) | 300 ms |
| analysis + 0012 + 0013 | ~1 min | 42.6 | 16.7 ms | 50.0 ms | 60 | 1 | 334 ms |
| **previous release** `eca1e25b` (0002, 0004, 0006, 0007, 0009, 0010) | 1 / 5 / 10 min | 26.7 / 26.6 / 26.0 | 33.4 ms | 133.5 ms | 721 / 734 / 744 | 71 / 75 / 83 | 467 / 517 / 434 ms |
| **release** `20fdc47a` (the same + 0012 + 0013) | 1 / 5 / 10 / 20 min | 43.7 / 43.2 / 42.3 / 42.0 | 16.7 ms | 33.4 ms | 29 / 35 / 39 / 24 | 2 / 0 / 2 / 0 | 167 / 67 / 267 / 50 ms |

The last two rows are the same automated run (`tools/bench.py run`, 14:52 and 15:20), one after the other.

![Frame times before and after patches 0012 and 0013](../img/frametimes-before-after.png)

With 0012 (analysis build, early match): 29,454 slot blocks/s served from the content cache, 124 misses/s and 292
blocks/s that did not fit the pattern; write faults fell from about 16,000/s to **311/s**, and the protection loop
handled 37,931 exceptions/s instead of 17,643/s (it runs about twice as fast, which also keeps its watchdog far from
its limit).

## The protection also scrambles its own code: patch 0014

The write at `exe+0x3e557e3` fills code pages in `exe+0x3e1b000..0x3f57000` with pseudo-random qwords
(`rax *= 0xda942043da942043`), and the code is decrypted again later. These are real code changes: with 0012 and 0013
they were about 230 of the remaining 311 faults/s, each followed by a recompile of that page's code. FEX still produced
about 4.3 MB of code per second: the 512 MB buffer filled in 119 s (analysis build) and in 144 and 180 s (build
`20fdc47a`), the 128 MB one in about 30 s.

[`patches/fex/0014-reuse-translations.patch`](../../patches/fex/0014-reuse-translations.patch), on by default (the code
reads `FEX_EXP_REUSE=0` as off; that switch was not tested): after a compile of code in a writable executable range,
FEX keeps *entry address -> (hash of the decoded guest bytes, host code, code buffer)*. When that address has to be
compiled again in the same code buffer, it decodes it, and if the bytes hash the same it puts the old host code back
into the lookup cache (and write-protects the pages again) instead of compiling. The hash is taken right after the
decode that the compile used. Counters: `CodeReuseStats` (marker `REUSE001`).

Analysis build `ee366f55` (0012 + 0013 + 0014 + counters), same automated test:

- 664 reuses/s and **0 hash mismatches** (568,474 reuses in the run): the decrypted code is the same every time.
- Compiles: the loop thread 480/s -> 198/s (JIT time 0.166 -> 0.079 s/s), the main thread 234/s -> 120/s
  (0.077 -> 0.052 s/s), both early in the match, compared with the analysis build without 0014.
- Code growth: the 128 MB buffer lasted 154 s (about 30 s without 0014), and the 256 MB buffer had not filled after
  585 s (81 s without 0014). Every replacement is a full recompile, which was the remaining big stutter.
- 44.4 / 44.4 / 44.2 FPS at minutes 1 / 5 / 10, worst frame 250 / 50 / 50 ms.

Release build `6990a221` (0002, 0004, 0006, 0007, 0009, 0010, 0012, 0013, 0014), same automated test (15:59):

| Build | Match time | FPS | median | p99 | frames > 50 ms | frames > 100 ms | worst |
|---|---|---|---|---|---|---|---|
| `20fdc47a` (without 0014) | 1 / 5 / 10 / 20 min | 43.7 / 43.2 / 42.3 / 42.0 | 16.7 ms | 33.4 ms | 29 / 35 / 39 / 24 | 2 / 0 / 2 / 0 | 167 / 67 / 267 / 50 ms |
| `6990a221` (with 0014) | 1 / 5 / 10 / 20 min | 43.3 / 43.5 / 43.3 / 42.1 | 16.7 ms | 33.4 ms | 18 / 18 / 16 / 24 | 1 / 0 / 0 / 0 | 267 / 50 / 50 / 50 ms |

In the `6990a221` run the A.I. destroyed the idle player's town at game time 00:19:51, so its minute-20 window
includes the end screen. The FPS stays the same; what 0014 removes is the full recompiles. In that run the code buffer grew to 256 MB in the
first 4.7 minutes after the game started and was then not replaced for the 13.9 minutes until the check (759,256
reuses, 0 mismatches). Without 0014 the 512 MB buffer was replaced every 2.4 to 3 minutes.

## What is left

- 292 blocks/s in the slot buffer do not fit the 0012 pattern and are compiled on each visit.
- Code buffer replacements still happen at start-up while the buffer grows (16 MB doubling to 512 MB).
