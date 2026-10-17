# CV1000: Allow Standard Sprite Alpha Blending on GPU to Eliminate Batch Fragmentation

Baseline `3cab125e`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

During real-hardware testing of *Muchi Muchi Pork!* on Xbox 360:
1. **Visual Seam Fixed**: The user confirmed that Issue 2 (the flickering horizontal bar/seam) is completely resolved.
2. **FPS Bottleneck (Still 30 FPS)**: The frame rate remained locked at 30 FPS most of the time.
3. **Console Log Diagnostics**:
   - Analysis of `cv1000-gpu.log` from real console (3,555 frames):
     - `worker_busy = 100,877 ms` across 3,555 frames = **28.37 ms/frame**!
     - `gpu_batches = 35,768` = **10.06 GPU batches per frame**!
     - `cpu_draw = 47,311 flushes` = **13.3 flushes per frame**!
   - Root Cause: In `epic12_gpu_submit`, the feedback blend check:
     ```c
     if (blend && (norm_dm != 0 || (norm_da != 0 && norm_da != 31))) return false;
     ```
     rejected ANY sprite with non-zero dest alpha (`norm_da != 0 && norm_da != 31`).
   - In *Muchi Muchi Pork!*, all 152 bullets per frame have `bl=512, dm=0, sa=16..27, da=24..28`.
   - Because `da` was 24..28, all 152 bullets per frame were rejected to CPU!
   - Every CPU bullet draw triggered `epic12_gpu_cpu_draw`, flushing the active GPU batch.
   - With 152 bullets per frame interspersed among background and sprite draws, GPU batches were fragmented into 10+ batches per frame.
   - Each GPU batch incurs fixed hardware costs: D3D state setup, EDRAM resolve (`Resolve(D3DRESOLVE_RENDERTARGET1)`), `LockRect` cache invalidation, and VMX128 untile writeback (`epic12_gpu_untile_mask`).
   - 10 batches * 1.2~1.6 ms overhead = 12~16 ms of pure worker thread overhead, pushing frame time to 28.37 ms (> 16.67 ms VSync deadline) and forcing the console to lock to 30 FPS.

## What

1. **Selective CPU Routing for Scanlines and Multiply Blends Only**:
   - The dark visual mask in previous versions was caused exclusively by:
     - 240 full-width horizontal scanlines (`dimy <= 2, dimx >= 160` or `dimx <= 2, dimy >= 160`, `bl=512, sm=0, dm=0, sa=20, da=16`) spanning $y=0..239$.
     - Dialogue box multiply shadows (`norm_dm != 0`).
   - Normal gameplay sprites (bullets, sparks, explosions, player sprites) with `dm == 0` (normal alpha blend) never caused the mask and are natively supported by the GPU shader pipeline.
   - In `libretro/FBNeo/src/burn/devices/epic12_gpu.h`:
     ```c
     if (blend && norm_dm != 0) {
         return false;
     }
     if (blend && ((dimy <= 4 && dimx >= 120) || (dimx <= 4 && dimy >= 120) || (dimx <= 32 && dimy >= 160) || (dimy <= 32 && dimx >= 160))) {
         return false;
     }
     ```
   - Standard sprite alpha blending (`dm == 0`, `da = 24..28`) remains 100% on the GPU.

## Verification & Results

1. **Host PPC Test Suites**:
   - `libretro/FBNeo/tests/epic12_gpu/run.py`: **100% PASS** (all 14,400 commands, 10.4M component cases).
   - `libretro/FBNeo/tests/epic12_blit/run.py`: **100% PASS** (all 65,536 rounding cases and 5,000 image test cases).

2. **Xenia Canary Benchmark (300 frames `mmpork`)**:
   - `cpu_draw` flushes dropped from 1,095 down to 394 (-64%).
   - `small_cpu` fallbacks: dropped to **0**!
   - Normal sprite rendering and bullet patterns fully preserved on GPU.
