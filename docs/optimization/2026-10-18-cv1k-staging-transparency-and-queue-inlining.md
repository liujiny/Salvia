# CV1000: Staging Transparency Optimization and Batch Submission Queue Inlining

Baseline `d1f3a567`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

1. **Hardware Profiling Analysis (Session Analysis)**:
   - Console logs across 2,300 gameplay frames in `mmpork` revealed that the worker thread spent **11.017 ms per job** in `submit` (23,059 ms across 2,093 jobs).
   - This accounted for **66.2% of total worker busy time** (16.65 ms/job), keeping the main thread waiting in `draw_sync` for **5.472 ms**.
   - Analysis of `diag_stag_sample` showed that 33% of staging operations consist of `dst=(768,0,16,16) src=(1280,0) fl=0x0001 tint=(32,32,32)` (transparent blits with `tint_mode == 0`).
   - The previous `epic12_staging_write_trans4()` implementation relied on branchless arithmetic right-shifts `m = ((INT32)p << 2) >> 31; d[col] = (p & m) | (d[col] & ~m);`.
   - On the in-order Xenon CPU, this forced 4 memory loads from `d` and 4 memory stores to `d` even when pixels were transparent, causing massive write-combining and L1 cache line thrashing.
   - For solid rows or empty rows of 16x16 and 8x8 sprites, there were no whole-row skip or whole-row 64-bit copy paths for transparent blits.

2. **Submission Queue Construction Overhead**:
   - `epic12_gpu_can_fit()` and `epic12_gpu_add_to_bin()` were un-inlined static functions taking 12 and 22 parameters respectively, forcing 14 stack spills and reloads per sprite.
   - For every sprite submission, `can_fit()` performed an array search across `epic12_gpu_dest_pages` to detect source dependency hazards, even when source textures were at `py0 >= 4` (or snapshots at `EPIC12_SNAPSHOT_Y = 4096`, `py0 >= 32`), where destination pages (always $y < 240, dpy \le 1$) can never overlap.

## What

1. **`libretro/FBNeo/src/burn/devices/epic12_gpu.h`**:
   - **Optimized `epic12_staging_write_trans4()` and `epic12_staging_rasterize_row()`**:
     - Eliminated all memory loads from `d` and redundant memory stores for transparent pixels.
     - Added 64-bit double-word stores (`d64[0] = s64[0]; d64[1] = s64[1]`) when all 4 pixels are opaque and 64-bit aligned.
     - Added direct single-store paths `if (p & 0x20000000) d[col] = p;` for non-opaque pixels.
   - **Whole-Row Skip and 64-Bit Unrolled Copy**:
     - Added unrolled 64-bit fast paths for $dimx = 16$ and $dimx = 8$ for `tint_mode == 0`, `tint_mode == 1 (tint63)`, and `tint_mode == 2 (tint0)`.
     - When all 16 (or 8) pixels are opaque: copies the entire row in 8 (or 4) 64-bit stores with zero loads from `d`.
     - When all 16 (or 8) pixels are transparent: skips the entire row immediately with zero memory accesses.
   - **16-Pixel Repeated Source Staging Fast Paths**:
     - Added 64-bit unrolled repeated source paths for $dimx = 16$ under `tint_mode == 0` and `tint_mode == 1`.
   - **Inlining and Destination Overlap Pruning in `epic12_gpu_can_fit()` / `epic12_gpu_add_to_bin()`**:
     - Inlined `epic12_gpu_can_fit` and `epic12_gpu_add_to_bin`.
     - Added `if (py0 < 4)` boundary check for destination overlap checks: completely eliminates destination page lookups for all snapshots ($py0 \ge 32$) and ROM sprites ($py0 \ge 4$).
     - Optimized single-page source insertion when $px0 == px1$ and $py0 == py1$.

## Verification

1. **Standalone Bit-Identical Tests**:
   - Verified 4-pixel and 16-pixel row transparency optimizations across 2,600,000 randomized test patterns: **100% BIT-IDENTICAL**.

2. **Host Test Suites**:
   - `libretro/FBNeo/tests/sh3_dispatch/run_inline.py`: **100% PASS** (all 8 host suites, 442,729 test cases).
   - `libretro/FBNeo/tests/epic12_gpu/run.py` (Debian PowerPC GCC + QEMU-PPC): **100% PASS** (all 14,400 commands, 10.4M shader cases, full VRAM comparisons).
   - `salvia-tests/run_bench_state9.py 60`: **PASS** (60 frames benchmark on `mmpork.state9`).

3. **Build Identity**:
   - Commit: `be9c95a9`
   - Archive Image: `fbneo-20261018-1946-be9c95a9-diag.xex`
   - Image SHA256: `91b774c23926d0ae941917c1a02fd52540595edf454eaa97ea474b6b70c484f4`

