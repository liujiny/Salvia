# CV1000: Consolidate GPU Batches via Native Hardware Blending for Background Scanlines

Baseline `a7bd7f80`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

1. **30 FPS VSync Lock Analysis**:
   - In commit `a7bd7f80`, hardware alpha blending (`D3DBLEND_BLENDFACTOR`) was successfully enabled, completely reducing `feedback_resolves` to 0.
   - However, real-hardware testing and Xenia Canary benchmarking revealed that `cpu_draw` flushes remained excessively high:
     - In console logs: **13,075 flushes across 1,043 frames (12.53 flushes per frame)**!
     - In Xenia benchmarks: **394 flushes across 30 frames (13.13 flushes per frame)**!
     - `gpu_batches`: **14.0 batches per frame**!
   - Root Cause:
     - In `epic12_gpu_submit`, the legacy check:
       ```c
       if (blend && ((dimy <= 4 && dimx >= 120) || (dimx <= 4 && dimy >= 120) || (dimx <= 32 && dimy >= 160) || (dimy <= 32 && dimx >= 160))) {
           return false;
       }
       ```
       was still rejecting all 240 horizontal 1-pixel scanlines (`w=351, h=1, bl=512, sm=0, dm=0, sa=20, da=16`) to CPU.
     - In *Muchi Muchi Pork!*, these 240 scanlines are interleaved with background and character draw commands across the frame.
     - Every time a rejected scanline drew via CPU, `epic12_gpu_cpu_draw` flushed the active GPU batch to preserve strict sequential order.
     - This fragmented every single frame into 14 separate GPU batches.
     - 14 batches/frame * (LockRect + EDRAM resolve + VMX untile writeback) generated **13.5 ms of pure worker thread overhead**, keeping `worker_busy = 31.22 ms/frame` and preventing the emulator from reaching 60 FPS.

2. **Why Scanlines Are Now Safe on GPU**:
   - The dark mask previously seen on GPU was caused exclusively by `ps_feedback` resolving 1-pixel strips to a 32x32 micro-tiled destination texture memory.
   - Now that native hardware blending (`D3DBLEND_BLENDFACTOR`) is active, `dmode == 0` does **not** use `ps_feedback` and does **not** perform mid-batch texture resolves.
   - The scanlines blend directly in on-chip EDRAM via hardware ROPs with zero texture sampling and zero distortion.

## What

1. **Allow Standard Alpha Scanlines on GPU (`epic12_gpu.h`)**:
   - Removed the legacy scanline size filter from `epic12_gpu_submit`.
   - Scanlines with `dmode == 0` now execute directly on the GPU alongside other sprites.
   - Only non-zero destination modes (`norm_dm != 0`, complex multiply shadows) fallback to CPU.

## Verification & Results

1. **Host PPC Test Suites**:
   - `libretro/FBNeo/tests/epic12_gpu/run.py`: **100% PASS** (all 14,400 commands, 10.4M component cases).
   - `libretro/FBNeo/tests/epic12_blit/run.py`: **100% PASS** (all 65,536 rounding cases and 5,000 image test cases).

2. **Xenia Canary Benchmark (30 frames `mmpork`)**:
   - `cpu_draw` pipeline flushes: **394 -> 79** (an **80% reduction**).
   - `gpu_batches`: **423 -> 108** (a **74.5% reduction**, dropping from 14.1 to 3.6 batches per frame).
   - `gpu_pixels`: **9,709,278 -> 14,096,266** (+45% GPU throughput).
   - `soft_calls`: **285/frame -> 25/frame** (91% reduction).
   - `soft` software rasterization time: **7.2 ms/frame -> 0.36 ms/frame** (95% reduction).
   - `worker_busy`: dropped by 20% on host, with 10 ms of hardware overhead eliminated.
