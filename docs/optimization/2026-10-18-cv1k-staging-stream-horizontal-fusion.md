# CV1000: Horizontal Strip Fusion and Unrolled Tint0 Staging Optimization

Baseline `1c9e65de`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

1. **Hardware Profiling Analysis (Session Analysis)**:
   - Analysis of console logs across 1,839 frames revealed that **68% of all staging blits** (170 of 250 samples) consist of $8 \times 240$ vertical strips with `tint=(0, 0, 0)`.
   - The game engine clears and prepares large off-screen canvas areas by dispatching dozens of contiguous 8-pixel vertical strips side by side ($x = 2816, 2824, 2832, 2840, \dots$).
   - On the Xenon CPU's 32 KB 2-way L1 data cache and 512 KB L2 cache:
     - With a row pitch of 8,192 words (32,768 bytes), every vertical row jump ($d\_row += 8192$) matches the cache aliasing boundary, causing severe thrashing across 240 rows.
     - With 40 consecutive 8-pixel strips, the processor was forced to execute **9,600 row jumps of 32 KB**, evicting and reloading the same destination cache lines from DDR3 RAM dozens of times.

2. **Loop Overhead and Unrolling in Tint0 Staging**:
   - For `tint_mode == 2 (tint0)`, the previous rasterizer iterated through a generic `count64 = dimx >> 1` loop without unrolled fast paths for $dimx = 8$ or $dimx = 16$.
   - For an 8-pixel strip across 240 rows, this introduced 960 branch instructions per strip.

## What

1. **`libretro/FBNeo/src/burn/devices/epic12_gpu.h`**:
   - Added unrolled, load-use pipelined 64-bit fast paths for $dimx = 8$ and $dimx = 16$ in `epic12_staging_rasterize_blit()` for `tint_mode == 2`.
   - Implemented lookahead horizontal strip fusion in `epic12_gpu_staging_stream()`:
     - Detects adjacent horizontal strips with identical $y, dimy$, attributes, and tint.
     - Supports **Case 1 (Contiguous Source)**: fuses adjacent strips into a single wide blit of width $total\_dimx$, rasterizing the entire span in a single row-wise pass.
     - Supports **Case 2 (Repeated Source Pattern)**: for repeated source patterns (e.g. 8-pixel fills from $sx = 2304$), loads the source double-words once per row and writes them consecutively across the destination row.
     - Consolidates WAR hazard checks, staging page tracking, and invalidation bounding boxes across the entire fused span.

## Verification

1. **Standalone Bit-Identical Tests (`scratch/test_stream_fuse.cpp`)**:
   - Verified that both contiguous source and repeated source fusion produce **100% BIT-IDENTICAL** results on both host x86_64 and PowerPC QEMU.

2. **Host PPC Test Suites**:
   - `libretro/FBNeo/tests/sh3_dispatch/run_inline.py`: **100% PASS** (all 8 host suites, 442,729 test cases).
   - `libretro/FBNeo/tests/epic12_gpu/run.py` (Debian PowerPC GCC + QEMU-PPC): **100% PASS** (all 14,400 commands, 10.4M shader cases, full VRAM comparisons).

3. **Build Identity**:
   - Image identity: `fbneo-20261018-1914-e82bd7ac-diag.xex`
   - SHA256: `7ee0292320fce15924d2186127f62bc4ed80871a8d609439fd5fe155c15bc06d`

