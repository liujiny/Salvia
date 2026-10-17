# CV1000: Fix Square Opaque Blocks, Restore Boss Layer Integrity & Filter CPU Upload Flushes

Baseline `adb48719`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

1. **User Observation & Reported Issues**:
   - The user reported: *"boss出场的时候图层依然不正确，这次甚至多了一些正方形不透明方块。密集的场景仍然下降到20帧，你看一下日志，继续研究修复"* (When Boss 1 appeared, layer ordering was still incorrect, and this time there were even some square opaque blocks added. Dense scenes still dropped to ~20 FPS. Inspect the logs and investigate fixes).

2. **Root Cause Analysis of Square Opaque Blocks & Layer Corruption**:
   - In commit `7ae6b1c2`, the CPU blitter routing check for dialogue scanlines and pitch-black `dmode == 4` darkening strips was removed:
     ```cpp
     if (blend && ((dimy <= 2 && dimx >= 120) || (dimx <= 2 && dimy >= 120) ||
                  (dmode == 4 && tint_clr->r == 0 && tint_clr->g == 0 && tint_clr->b == 0))) {
         return false;
     }
     ```
   - In *Muchi Muchi Pork!*, full-width darkening strips (`w=24, h=240, dmode=4, sa=16, da=16, tint=(0,0,0)`) are executed during dialogue, scene transitions, and Boss 1 entrance.
   - When executed on the Xenos GPU, Direct3D separate alpha blending (`D3DRS_SEPARATEALPHABLENDENABLE = TRUE`) was configured with `D3DRS_SRCBLENDALPHA = D3DBLEND_ONE` and `D3DRS_DESTBLENDALPHA = D3DBLEND_ZERO`.
   - Because the source pixel for `cmd 64` / `cmd 237` has zero alpha, the GPU wrote `0` into the render target's Alpha channel across the 24x240 band in EDRAM.
   - When resolved and untiled back to CPU VRAM (`m_bitmaps`) with `& 0x20f8f8f8`, bit 29 (the CV1000 transparency flag) was erased to `0` across that region.
   - When Boss 1's multi-part sprites (armor plates, wheels, rotating drills, guns) rendered over or sampled this region:
     - `clip(raw.a - 0.0625)` evaluated transparent background borders as opaque, or opaque pieces as transparent.
     - `epic12_gpu_alpha_trim` inspected bit 29 in `alpha[slot]` to calculate sprite bounding boxes. The erased alpha caused the bounding boxes to be severely miscalculated and truncated, shifting and misplacing boss sprite components.
     - Because VRAM atlas pages and untile blocks are divided into **32x32 pixel tiles**, the corrupted tile boundaries manifested on the TV screen as **sharp 32x32 square opaque blocks ("正方形不透明方块")** and layer corruption!
   - Restoring targeted CPU routing for pitch-black `dmode == 4` darkening strips and thin scanlines ensures they are rendered by the CPU software rasterizer with bit-exact 5-bit color math and full bit-29 transparency preservation (`clr_to_pen | (pen & 0x20000000)`), completely eliminating the opaque square blocks.

3. **Root Cause Analysis of 20 FPS Stalls in Dense Scenes (114 ms Worker Stalls)**:
   - Console hardware logs (`cv1000-gpu.log`, 10,473 batches across 1,973 frames up to Boss 1) revealed:
     - Peak frame 1835: `core_sampled_peak_ms interval_frame=1835 prep=0.002 cpu_io=15.727 audio_tail_misc=0.125 draw_sync=114.650 total=130.503 ms` (7.6 FPS!).
     - `worker_wait sampling=random-1-in-64 jobs=10118 waits=10118 samples=171 avg_ms=3.809 peak_ms=121.743 ms`.
     - Mid-frame flushes: `flushes end=9803 region=148 source_dependency=36 cpu_upload=486 cpu_draw=0`.
   - `cpu_upload` accounted for **486 mid-frame GPU pipeline flushes**!
   - In `epic12.cpp:590`, `gfx_upload` calls `epic12_gpu_cpu_write(dst_x_start, dst_y_start, dimx, dimy, EPIC12_GPU_FLUSH_CPU_UPLOAD)`.
   - In `epic12_gpu.h:496`, `epic12_gpu_cpu_write` called `epic12_gpu_flush(reason)` **unconditionally on EVERY single tile upload**, even though the upload rectangle is writing to off-screen sprite/font character RAM (`x >= 1024`) completely disjoint from the in-flight GPU batch (`x < 704`).
   - In dense bullet hell and boss entrance scenes, the game CPU streams dozens of bullet animations and boss pieces in quick succession. Each upload forced an EDRAM resolve, texture lock, and CPU untile, accumulating to **114.65 ms of draw_sync stalls** in a single frame.
   - Checking for conflicts against `epic12_gpu_bounds[0]` and `epic12_gpu_source_pages[0]` before flushing allows non-conflicting tile uploads to update CPU VRAM and invalidate the atlas cache (`epic12_gpu_invalidate(r)`) without interrupting or flushing the active GPU batch.

## What

1. **`libretro/FBNeo/src/burn/devices/epic12_gpu.h`**:
   - In `epic12_gpu_submit`: restored CPU blitter routing for pitch-black darkening strips and thin scanlines:
     ```cpp
     if (blend && ((dimy <= 2 && dimx >= 120) || (dimx <= 2 && dimy >= 120) ||
                  (dmode == 4 && tint_clr->r == 0 && tint_clr->g == 0 && tint_clr->b == 0))) {
         return false;
     }
     ```
   - In `epic12_gpu_cpu_write`: added conflict check for `EPIC12_GPU_FLUSH_CPU_UPLOAD`:
     - Checks if the uploaded rectangle `(x, y, w, h)` intersects `epic12_gpu_bounds[0]`.
     - Checks if any uploaded page intersects `epic12_gpu_source_pages[0]`.
     - Flushes the GPU batch ONLY when a true hazard exists. Non-conflicting uploads simply invalidate the atlas cache and proceed without flushing.

## Verification & Results

1. **Host PPC Test Suites**:
   - `libretro/FBNeo/tests/epic12_gpu/run.py`: **100% PASS** (all 14,400 commands, 10.4M component shader cases bit-exact).
   - `libretro/FBNeo/tests/epic12_blit/run.py`: **100% PASS** (all 65,536 rounding cases and 5,000 image cases).

2. **Xenia Canary Benchmark (`mmpork.state1`, 60 frames)**:
   - `cpu_upload` flushes: dropped from 17 down to **0** (100% false upload flushes eliminated).
   - `draw_sync`: dropped from 14.472 ms down to **0.120 ms** (**99.2% reduction**).
   - `worker_busy`: dropped from 2,226.7 ms down to **1,326.3 ms** (**40.4% reduction**).
   - `total` frame time: dropped from 42.299 ms down to **31.951 ms**.
   - `cpu_draw`: exactly 1 darkening strip per frame cleanly routed to CPU blitter.

3. **Artifacts & Deployment**:
   - Diagnostics XEX: `Distro360/fbneo.xex`
     - Size: 34,668,544 bytes
     - SHA256: `09f2f008823b48b1c74eb52ca76a70cf28b7ee3a30bf325e26805c155ba6f483`
   - Archive: `/home/humor/salvia-tests/xex-archive/fbneo-20261017-1846-fix-opaque-blocks-and-drops-diag.xex`
   - Automatically uploaded to Xbox 360 at `192.168.2.232` and launched via Aurora.
