# CV1000 GPU: Fast-Path Append to Newest Active Bin

Baseline `7d16c5d3`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

In `mmpork`, over 90% of Cave CV1000 draw commands are consecutive on-screen sprites drawn into the same spatial region. In previous multi-bin schedulers:
- The search loop always started from `b = 0` up to `b = EPIC12_GPU_BINS - 1`.
- When an on-screen sprite arrived, testing `b = 0..6` required scanning all queued commands in earlier bins looking for commutation hazards.
- Because older bins also contained on-screen sprites, false WAW destination overlaps (`c_dest = 107,287`) and false RAW cross-reads (`c_raw = 165,793`) repeatedly rejected the sprite from earlier bins until it finally reached the newest active bin (`bin[7]`).
- This resulted in millions of redundant inner-loop rectangle comparisons per frame, fragmented batches across multiple bins, and increased GPU batch counts (3,634 batches in 300 frames), forcing the worker thread to incur ~2.5 ms of D3D `LockRect` (`readback_wait`) per batch.

## What

In [`epic12_gpu.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_gpu.h):
- Added an $O(1)$ fast-path before the multi-bin search loop:
  - Find the highest-index active bin `last_active`.
  - If the incoming command fits geometrically and without intra-batch RAW hazards into `last_active` (`epic12_gpu_can_fit(last_active, ...) == 0`):
  - Append it directly to `last_active` without any commutation check!
- Rationale: Because `last_active` is the newest active bin in the pipeline, all subsequent bins are empty. No commands exist after `last_active` in execution order. Therefore, appending to `last_active` strictly maintains original FIFO program order and cannot invert execution order or cause WAW/RAW/WAR hazards.
- Only when a command cannot fit into `last_active` does the scheduler fall back to searching earlier disjoint bins or allocating a new bin.

## Actual Benchmark Results (300 frames `mmpork`, Xenia Canary)

| Metric | 8-Bin Baseline (`7d16c5d3`) | Fast-Tail Append (`c1fc9771`) | Delta / Improvement |
| :--- | :--- | :--- | :--- |
| **GPU Batches (300f)** | 3,633 | **2,973** | **-660 batches (-18.2%)** |
| **Draw Sync Wait Time** | 6.27 ms | **1.83 ms** | **-4.44 ms/frame (-70.8%)** |
| **GPU Busy Time (300f)** | 14,395 ms | **12,854 ms** | **-1,541 ms overall (-10.7%)** |
| **Worker Busy Time (300f)**| 15,686 ms | **14,033 ms** | **-1,653 ms overall (-10.5%)** |
| **Total Core Frame Time** | 73.74 ms | **69.56 ms** | **-4.18 ms/frame (-5.7%)** |
| **Active Loop Time** | 74.51 ms | **70.40 ms** | **-4.11 ms/frame (-5.5%)** |
| **`c_dest` Conflicts** | 107,287 | **3,437** | **-103,850 (-96.8%)** |
| **`c_raw` Conflicts** | 165,793 | **66,350** | **-99,443 (-60.0%)** |
| **CPU Replay Pixels** | 27,511,700 | **26,551,967** | **-959,733 pixels saved** |

## Verification & Artifacts

- **QEMU PPC Regression Test**:
  - `python3 libretro/FBNeo/tests/epic12_gpu/run.py --toolchain-root /home/humor/salvia-tests/toolchains/ppc/root`
  - PASS: All 14,400 commands, full VRAM and delay comparison bit-identical to reference CPU blitter.
- **Xbox 360 Diagnostics XEX**:
  - Tag: `fast-tail` (`c1fc9771`)
  - File: `xex-archive/fbneo-20261015-0659-fast-tail-diag.xex`
  - SHA256: `a0c81a9d197b22274180d8638c19ab1ece751012e32335dce1e0686eb2d1bcd9`
