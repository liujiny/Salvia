# CV1000: Hoist Staging Blit Invariant Dispatch & Unroll 8-Pixel Rasterizer

Baseline `f6077643`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

1. **Root Cause Analysis of `draw_sync` Bottleneck in Heavy Scenes**:
   - Console hardware logs (`cv1000-gpu.log`) from Muchi Muchi Pork! Slot 9 heavy scenes with CPU Clock at 50% revealed:
     - `core_slow_frame_ms threshold_ms=25.000 samples=11 prep=0.004 cpu_io=11.778 audio_tail_misc=0.129 draw_sync=21.149 total=33.060`
     - `dominant_draw_sync=11 (100% of slow frames!)`
   - While lowering CPU clock from 100% to 50% successfully cut SH3 CPU emulation time from 23.1 ms down to 11.8 ms, the overall frame time stayed at ~33 ms (~30-40 FPS) because `draw_sync` surged to **21.15 ms**, dominating 100% of the slow frame stalls.
   - The worker thread executes **12,138,296 staging blits** (`stag_ident=12138296`), averaging **~945 staging blits per frame** (greater than the 650 on-screen draws).
   - In all 10 diagnostic samples, staging blits are vertical stripes with dimensions `w=8, h=240, flipx=0, flipy=0, trans=0, is_identity_tint=1`.
   - Previously, for every 8x240 blit, `epic12_staging_rasterize_row` was called 240 times inside the row loop. Inside each row call, 7 invariant conditions (`transparent`, `is_identity_tint`, `!flipx`, pointer alignment, `w==16`, column loop, `w&1`) were evaluated for every single row, generating over 1.5 million branch evaluations per frame on the in-order PowerPC Xenon CPU.
   - Furthermore, `w=8` lacked an unrolled double-word copy path and fell back to a 4-iteration scalar copy loop on every row.

2. **Loop Invariant Hoisting & Direct Quadword Copy**:
   - Because `transparent`, `is_identity_tint`, `flipx`, `flipy`, `dimx`, and 8-byte pointer alignment are invariant across all rows of a blit, dispatch is hoisted out of the row loop into `epic12_staging_rasterize_blit()`.
   - For the dominant `dimx=8` aligned opaque identity blit (100% of samples), the row loop compiles to 4 double-word loads and 4 double-word stores with zero function calls, zero branches, and zero column loops.
   - For `dimx=16` and `dimx=4`, matching unrolled quadword paths are provided.
   - An unrolled branchless 2x4-pixel block path is also added for `transparent && is_identity_tint && dimx=8`.

## What

1. **`libretro/FBNeo/src/burn/devices/epic12_gpu.h`**:
   - In `epic12_staging_rasterize_row()`: added unrolled `w == 8` (4 double-words) and `w == 4` (2 double-words) paths for aligned opaque identity copies.
   - Introduced `epic12_staging_rasterize_blit()` to hoist dispatch and unroll entire `(dimx, dimy)` rectangles.
   - Updated both `epic12_gpu_staging_blit()` and `epic12_gpu_staging_stream()` to call `epic12_staging_rasterize_blit()`.

## Verification

1. **Host PPC Test Suites**:
   - `libretro/FBNeo/tests/sh3_dispatch/run_inline.py`: **100% PASS** (all 8 host suites, ASan/UBSan clean, 442,729 test cases).
   - `libretro/FBNeo/tests/epic12_gpu/run.py` (Debian PowerPC GCC + QEMU-PPC): **100% PASS** (all 14,400 commands, 10.4M shader cases, full VRAM comparisons, and simulated GPU batches identical).
