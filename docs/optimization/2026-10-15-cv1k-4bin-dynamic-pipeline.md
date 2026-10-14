# CV1000 GPU: 4-Bin Dynamic Queue Pipeline to Eliminate Triple-Buffer Ping-Pong Flushes

Baseline `33f09a9a`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

In `mmpork`, previous profiling revealed that Cave CV1000 games rapidly alternate draw commands across three distinct spatial zones:
- Zone 0 (Visible Screen): $X \in [0..384]$
- Zone 1 (Off-screen staging buffer A): $X \in [768..896]$
- Zone 2 (Off-screen staging buffer B): $X \in [1280..1344]$

With our initial 2-bin disjoint batching implementation (`faa4c0c1`):
1. 18,532 commands were rejected from `bin 1` due to `c0_fail`. Detailed probe diagnostics (`e6597daf`) showed that 98.6% of these rejections were false RAW hazard flags (`c_raw = 27,986`), because incoming commands were reading offscreen buffers that `bin 0` had drawn into. Since `bin 0` is strictly rendered and retired to VRAM before `bin 1` runs, `bin 1` executes in strict FIFO program order relative to `bin 0` and does not need to commute with `bin 0`'s past writes.
2. Furthermore, with only 2 bins, whenever the blitter touched Zone 2, neither `bin 0` nor `bin 1` could accept it without exceeding the 512-pixel hardware render target span ($1344 - 0 \ge 512$ and $1344 - 768 \ge 512$), triggering 23,403 premature flushes (`fit1_fail` span $\ge 512$) and dumping thousands of sub-512-pixel batches into CPU software replay (`small_cpu`).

## What

In [`epic12_gpu.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_gpu.h) and [`epic12_gpu_xbox.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_gpu_xbox.h):
- Extended batch management to a generalized 4-bin dynamic queue pipeline (`EPIC12_GPU_BINS 4`).
- Implemented full Bernstein condition verification (`epic12_gpu_commutes_ahead`):
  - When an incoming command is considered for bin $b$, it verifies that it commutes ahead of all active later bins $j > b$:
    1. WAW: incoming destination does not intersect bin $j$'s destination.
    2. RAW: incoming source does not intersect bin $j$'s destination.
    3. WAR: incoming destination does not intersect bin $j$'s source.
- Bins execute in strict monotonic sequential order ($0 \to 1 \to 2 \to 3$), ensuring that any command placed into bin $b$ executes strictly after bins $0 \dots b-1$ and concurrently/ahead of bins $b+1 \dots 3$ only when fully disjoint.
- When no bin can accommodate the draw, `epic12_gpu_retire_bin0` flushes `bin[0]` to VRAM, shifts all active bins down by one slot ($O(N)$ with $N=4$), and retries.
- Maintained the existing 128-source-page batch limit and 512x512 render target boundaries per batch.

## Actual Benchmark Results (300 frames `mmpork`, Xenia Canary)

| Metric | Milestone 1 (Precise Dep) | Milestone 3 (Dual Bin) | Milestone 3 (4-Bin Pipe) | Change vs Baseline |
| :--- | :--- | :--- | :--- | :--- |
| **Total Batched Commands** | 276,579 | 276,579 | **301,482** | **+24,903 commands** |
| Bin 0 hits | 276,579 | 253,451 | 10,050 | Multi-bin flow |
| Bin 1 hits | 0 | 23,128 | 7,237 | Multi-bin flow |
| Bin 2 hits | 0 | 0 | 121,069 | Multi-bin flow |
| Bin 3 hits | 0 | 0 | 163,126 | Multi-bin flow |
| **Region Flushes** | 23,981 | 31,057 | **26,591** | **-4,466 flushes** |
| **Small CPU Batches** | 28,842 | 28,240 | **28,160** | **-682 batches** |
| **CPU Replay Pixels** | 27,373,383 | 27,205,034 | **26,996,012** | **-377,371 pixels** |
| Replay CPU Time (300f) | 219.25 ms | 208.85 ms | 211.05 ms | Low & steady |

## Verification & Artifacts

- **QEMU PPC Regression Test**:
  - `python3 libretro/FBNeo/tests/epic12_gpu/run.py --toolchain-root /home/humor/salvia-tests/toolchains/ppc/root`
  - PASS: All 14,400 commands, full VRAM and delay comparison bit-identical to reference CPU blitter.
- **Xbox 360 Diagnostics XEX**:
  - Tag: `bin4-pipe` (`34f18ac2`)
  - File: `xex-archive/fbneo-20261015-0630-bin4-pipe-diag.xex`
  - SHA256: `7f6843b401c02f6b7bb59ac151e7a1b4d1cb70066d9ebeb1e55ce9f952051be7`
