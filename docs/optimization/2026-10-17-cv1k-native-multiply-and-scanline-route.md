# CV1000: Native Hardware Multiply for Shadows & Scanline CPU Routing for Dialogue Mask

Baseline `52a8b63a`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

1. **Dialogue Dark Mask Root Cause**:
   - In commit `52a8b63a`, background scanlines (`dimy <= 4 && dimx >= 120`) were routed to the GPU to consolidate batches.
   - When loading savestates or entering dialogue scenes, the game draws 112-240 horizontal 1-pixel scanlines (`w=324..352, h=1, bl=512, sm=0, dm=0, sa=20, da=16`) to darken the dialogue box.
   - On the Xenos GPU, resolving 1-pixel high strips across EDRAM causes boundary distortion, producing a visible dark mask across the screen.
   - Once dialogue ends and normal gameplay resumes, the game stops drawing these scanlines, so the mask vanishes during gameplay.
   - Routing these scanlines back to the CPU blitter completely eliminates the dark mask without impacting gameplay performance.

2. **30 FPS Lock vs Occasional 60 FPS Root Cause**:
   - Hardware logs from the Xbox 360 showed `worker_busy = 24.16 ms/frame` (over the 16.67 ms 60 FPS deadline), with `draw_sync = 13.03 ms`.
   - Inspection of `cv1000-gpu.log` revealed **10,864 `cpu_draw` flushes across 3,638 frames (2.98 flushes per frame)**, fragmenting each frame into **3.83 GPU batches**.
   - Each batch flush takes ~2.38 ms on Xbox 360 (EDRAM resolve + LockRect stall + VMX untile). $3.83 \times 2.38\text{ ms} = 9.1\text{ ms}$ of pure batch flush overhead, which combined with 10.34 ms of GPU rendering pushed the worker time to 24.16 ms, forcing VSync down to 30 FPS.
   - The root cause of `cpu_draw` during gameplay was multiply shadows under characters, enemies, and bullets:
     - In *Muchi Muchi Pork!*, shadow sprites are drawn with `blend = 1, smode = 0, dmode = 1, sa = 0`.
     - In `epic12_gpu_submit`, all `norm_dm != 0` draws were rejected to CPU.
     - Every rejected shadow called `epic12_gpu_cpu_draw`, flushing the active GPU batch.
     - When very few elements were on screen (no enemies/shadows), no flushes occurred, so the frame rate jumped to 60 FPS!
     - When enemies appeared, shadows were drawn, causing 3-4 flushes per frame and dropping the frame rate to 30 FPS.

## What

1. **Restore Scanline CPU Filter (`epic12_gpu.h`)**:
   - Re-enabled the filter:
     ```c
     if (blend && ((dimy <= 4 && dimx >= 120) || (dimx <= 4 && dimy >= 120) || (dimx <= 32 && dimy >= 160) || (dimy <= 32 && dimx >= 160))) {
         return false;
     }
     ```
   - Routes dialogue/intro 1-pixel scanlines to the CPU blitter, permanently eliminating the dark mask.

2. **Native Hardware Multiply on Xenos ROP (`epic12_gpu.h`, `epic12_gpu_batch.h`, `epic12_gpu_xbox.h`)**:
   - For CV1000 `dmode == 1, sa == 0`, the color formula is $D_{out} = \lfloor (D_{in} \times S) / 31 \rfloor$.
   - In Direct3D 9, setting `D3DRS_SRCBLEND = D3DBLEND_ZERO` and `D3DRS_DESTBLEND = D3DBLEND_SRCCOLOR` evaluates $S \cdot 0 + D \cdot S = D \cdot S$, performing exact hardware multiply directly in EDRAM.
   - In `epic12_gpu.h`: allow `dmode == 1 && s_alpha == 0` draws on GPU.
   - In `epic12_gpu_batch.h`: exempt `dmode == 1 && s_alpha == 0` from feedback texture resolves, and classify as blend state 200.
   - In `epic12_gpu_xbox.h`: configure `D3DBLEND_ZERO` / `D3DBLEND_SRCCOLOR` for state 200, and update shaders (`shade_fast`, `shade`) to output unattenuated color $S$ when `mode.z > 0.5`.

## Verification & Results

1. **Host PPC Test Suites**:
   - `libretro/FBNeo/tests/epic12_gpu/run.py`: **100% PASS** (all 14,400 commands, 10.4M component cases).
   - `libretro/FBNeo/tests/epic12_blit/run.py`: **100% PASS** (all 65,536 rounding cases and 5,000 image cases).

2. **Xenia Canary Benchmark (120 frames `mmpork`)**:
   - `draw_sync`: **0.158 ms**.
   - `small_cpu`: **0**.
   - GPU pixels rendered: **48,374,457**.

3. **Artifacts**:
   - Diagnostics XEX: `Distro360/fbneo.xex`
     - SHA256: `583a70ecb8c70879da849131cbcf0e93ad4cbb4d17debf2dcb3765f461036f04`
   - Archive: `/home/humor/salvia-tests/xex-archive/fbneo-20261017-0949-52a8b63a-diag.xex`
