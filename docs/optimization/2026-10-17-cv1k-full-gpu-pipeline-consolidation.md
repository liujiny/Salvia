# CV1000: Full GPU Pipeline Consolidation (Scanlines + Multiply Shadows) to Eliminate Pipeline Flushes

Baseline `1b8a00fd`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

1. **Root Cause of Persistent 30 FPS Lock**:
   - In commit `1b8a00fd`, native hardware multiply for shadows (`dmode == 1, sa == 0`) was enabled on GPU, eliminating shadow CPU blits.
   - However, a scanline rejection check was re-added in `epic12_gpu_submit`:
     ```c
     if (blend && ((dimy <= 4 && dimx >= 120) || (dimx <= 4 && dimy >= 120) || (dimx <= 32 && dimy >= 160) || (dimy <= 32 && dimx >= 160))) {
         return false;
     }
     ```
   - Profiling of `cv1000-gpu.log` on real Xbox 360 hardware revealed:
     - `cpu_draw = 11,991` flushes across 1,085 frames (**11.05 flushes per frame**).
     - `soft_calls = 285,992` (**263 CPU blit calls per frame**).
     - `gpu_batches = 13,034` (**12.0 batches per frame**).
     - `worker_busy = 30.58 ms/frame`, locking the frame rate to 30 FPS.
   - The 240 horizontal 1-pixel scanlines (`w=324..352, h=1, bl=512, sm=0, dm=0, sa=20, da=16`) drawn every frame for background darkening were rejected to the CPU blitter in ~11 interleaved chunks.
   - Each CPU blit triggered `epic12_gpu_cpu_draw`, forcing a synchronous GPU batch flush and readback stall (~1.2 ms each * 11 = ~13 ms flush overhead).

2. **Resolution**:
   - Remove the scanline rejection check from `epic12_gpu_submit`.
   - Now that native hardware blending (`D3DBLEND_BLENDFACTOR`) is active, scanlines do not use texture feedback resolves, blending directly in EDRAM with zero distortion.
   - Both scanlines (`dmode == 0`) and multiply shadows (`dmode == 1, sa == 0`) execute entirely on the GPU.
   - `cpu_draw` drops to 0, batch count drops from 12 batches/frame to < 1 batch/frame, and the worker thread overhead is eliminated.

## What

1. **Allow Standard Scanlines on GPU (`epic12_gpu.h`)**:
   - Removed the `(dimy <= 4 && dimx >= 120)` rejection filter from `epic12_gpu_submit`.
2. **Revert Temporary Debug Dump (`epic12.cpp`)**:
   - Cleaned up the frame 1 file dump.

## Verification & Results

1. **Host PPC Test Suites**:
   - `libretro/FBNeo/tests/epic12_gpu/run.py`: **100% PASS** (all 14,400 commands, 10.4M component cases).
   - `libretro/FBNeo/tests/epic12_blit/run.py`: **100% PASS** (all 65,536 rounding cases and 5,000 image cases).

2. **Xenia Canary Benchmark (120 frames `mmpork`)**:
   - `cpu_draw` pipeline flushes: **11.05 / frame -> 0** (100% reduction).
   - `gpu_batches`: **12.0 / frame -> 0.99 / frame** (92% reduction).
   - `gpu_pixels`: **66,090,759** (100% rendered by GPU).
   - `rejections`: `blend=0, size=0, oob=0, sy=0, intersect=0` (0 rejections).
   - `draw_sync`: **0.158 ms**.

3. **Artifacts**:
   - Diagnostics XEX: `Distro360/fbneo.xex`
     - SHA256: `c5fd58549fff97f197447905441afa893f98834c29082f5db7a532e61dfd28b8`
   - Archive: `/home/humor/salvia-tests/xex-archive/fbneo-20261017-1020-1b8a00fd-diag.xex`
