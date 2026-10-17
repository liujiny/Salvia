# CV1000: Fix Dark Mask via Targeted Black Darkening Route & Restore GPU Acceleration

Baseline `b13e77ed`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

1. **Root Cause of "CPU Fallback gpu加速failed" in Previous Build**:
   - In commit `35cde267`, modifying `D3DRS_DESTBLENDALPHA` in `epic12_gpu_xbox.h` caused the D3D render target alpha output to diverge from the reference expected by `epic12_xenos_selftest()`:
     `self-test mismatch x=0 y=0 GPU=0048f800 CPU=2048f800`
   - Because the self-test detected an alpha difference `da = 0x20 > 0`, it immediately failed GPU validation at boot and aborted to pure CPU software replay (`epic12_gpu_replay`).
   - Reverting `epic12_gpu_xbox.h` to baseline restores exact pixel self-test equivalence (`batches = 360`, `gpu_pixels = 56,208,057`), eliminating the CPU fallback prompt completely.

2. **Root Cause of Flickering Horizontal Stripe**:
   - In *Muchi Muchi Pork!*, the arcade ROM draws full-width darkening strips (`w=24, h=240` in raw rotated VRAM) with:
     `cmd 64: dmode=4, sa=16, da=16, tint=(0,0,0)`
     `cmd 237: dmode=4, sa=16, da=16, tint=(0,0,0)`
   - When executed through the GPU hardware batch pipeline, sampling and resolving full-screen darkening bands across EDRAM produced boundary artifacts and flickering corrupted stripes across the screen.
   - Previous coarse rejection rules (`dimx <= 32 && dimy >= 160`) caught this darkening strip, but also accidentally caught player weapons and vertical missile streams, causing 10.5 `cpu_draw` flushes per frame and dropping FPS to 30.
   - Analysis of `draw_dump.txt` proved that player weapons and glowing bullets have non-zero tint, while **only the darkening strips have pitch-black tint (`tint.r == 0 && tint.g == 0 && tint.b == 0`) with `dmode == 4`**.
   - Targeting `dmode == 4 && tint_clr->r == 0 && tint_clr->g == 0 && tint_clr->b == 0` specifically routes the darkening strip to the CPU blitter without touching player weapons or missiles.
   - Dialogue 1–2 pixel scanlines (`(dimy <= 2 && dimx >= 120) || (dimx <= 2 && dimy >= 120)`) are also routed to CPU, eliminating dialogue mask distortion.

## What

1. **`libretro/FBNeo/src/burn/devices/epic12_gpu.h`**:
   - Configured targeted CPU routing in `epic12_gpu_submit`:
     ```cpp
     if (blend && ((dimy <= 2 && dimx >= 120) || (dimx <= 2 && dimy >= 120) ||
                  (dmode == 4 && tint_clr->r == 0 && tint_clr->g == 0 && tint_clr->b == 0))) {
         return false;
     }
     ```
   - Routes dialogue scanlines and pitch-black darkening strips (`cmd 64`/`cmd 237`) to the CPU blitter, completely eliminating the dark mask and flickering stripes while keeping 100% of player weapons and game sprites on the GPU.

2. **`libretro/FBNeo/src/burn/devices/epic12_gpu_xbox.h`**:
   - Reverted `D3DRS_DESTBLENDALPHA` modifications, restoring full `epic12_xenos_selftest()` pass and native GPU acceleration on Xbox 360.

## Verification & Results

1. **Host PPC Test Suites**:
   - `libretro/FBNeo/tests/epic12_gpu/run.py`: **100% PASS** (all 14,400 commands, 10.4M component cases).
   - `libretro/FBNeo/tests/epic12_blit/run.py`: **100% PASS** (all 65,536 rounding cases and 5,000 image cases).

2. **Xenia Canary Benchmark (120 frames `mmpork.state`)**:
   - GPU acceleration: **100% active** (`gpu_batches = 359`, `gpu_pixels = 56,208,057`).
   - `cpu_draw` flushes: dropped from 1,260 to **240** (only necessary darkening passes).
   - `worker_busy`: **3,173.4 ms** (down from 3,929.0 ms).
   - `draw_sync`: **0.134 ms**.

3. **Artifacts**:
   - Diagnostics XEX: `Distro360/fbneo.xex`
     - SHA256: `d6a76cdc4b6cd30a4ff571c47c61debc23c53dfbdab69cc89f5b09a45764b4b6`
   - Archive: `/home/humor/salvia-tests/xex-archive/fbneo-20261017-1251-fix-dark-mask-diag.xex`
