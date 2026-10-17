# CV1000: Preserve Destination Alpha During Blending & Eliminate O(N) Command Scan

Baseline `5e1d6a27`. Commit `35cde267`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

1. **Root Cause of Dark Mask and Flickering Blocks**:
   - In `Distro360/draw_dump.txt`, darkening passes such as:
     `cmd 64: x=416 y=128 w=24 h=240 fx=0 fy=0 tr=256 bl=512 sm=0 dm=4 sa=16 da=16 tint=(0,0,0)`
     and `cmd 237` are issued by CV1000 games to darken dialogue backgrounds and screen regions.
   - When rendered via Direct3D hardware blending on the GPU, `D3DRS_DESTBLENDALPHA` was configured to `D3DBLEND_ZERO`:
     $DestAlpha_{new} = SrcAlpha \times 1 + DestAlpha_{old} \times 0$.
   - This zeroed out the destination pixel's alpha channel in EDRAM.
   - When untiled back to CPU VRAM with `& 0x20f8f8f8`, bit 29 (`0x20000000`, the CV1000 transparency flag) was erased to 0 across the entire 24x240 band.
   - Any subsequent sprite draws or background reads that sampled this region saw the pixels as transparent (`clip(raw.a - 0.0625)`), producing persistent dark strips and flickering corrupt blocks.
   - Routing 1–2 pixel dialogue scanlines (`dimy <= 2 && dimx >= 120`) to the CPU blitter avoids EDRAM 1-pixel resolve boundary distortion, while setting `D3DRS_DESTBLENDALPHA = D3DBLEND_ONE` for blended draws preserves the destination pixel's alpha flag.

2. **Root Cause of 30-40 FPS Frame Drops in Worker Thread**:
   - Hardware logs (`Distro360/cv1000-gpu.log`) showed `submit` taking **29,379 ms** across 3,453 frames (**8.50 ms per frame**), consuming over 53% of `worker_busy` (15.88 ms) and forcing `draw_sync = 6.29 ms`.
   - Inspection of `epic12_gpu_can_fit` revealed the rejected $O(N)$ linear command scan:
     ```cpp
     if (page_overlap && epic12_gpu_intersects(sx, sy, w, h, epic12_gpu_bounds[b])) {
         for (int i = 0; i < epic12_gpu_count[b]; ++i) ...
     }
     ```
   - On every sprite submission where `page_overlap` was true, the CPU iterated over up to 1,024 queued commands, creating over 245,000 loop iterations per frame on the in-order PowerPC Xenon cores and thrashing L1 cache.
   - Replacing this scan with an immediate return of `EPIC12_GPU_FLUSH_SOURCE_DEPENDENCY` (2) reduces `submit` CPU time by **71%** (from 11.5 µs to 3.3 µs per call).

## What

1. **`libretro/FBNeo/src/burn/devices/epic12_gpu.h`**:
   - In `epic12_gpu_can_fit`: eliminated the $O(N)$ loop over `epic12_gpu_commands`, returning flush reason 2 immediately when `page_overlap && epic12_gpu_intersects`.
   - In `epic12_gpu_submit`: restored CPU routing for thin 1–2 pixel dialogue scanlines (`(dimy <= 2 && dimx >= 120) || (dimx <= 2 && dimy >= 120)`), leaving player weapons and large sprites on the GPU.

2. **`libretro/FBNeo/src/burn/devices/epic12_gpu_xbox.h`**:
   - In `epic12_xenos_run`: set `D3DRS_DESTBLENDALPHA` to `D3DBLEND_ONE` when `D3DRS_ALPHABLENDENABLE` is true, ensuring destination alpha (bit 29) is never zeroed out during blending. Set `D3DRS_DESTBLENDALPHA` to `D3DBLEND_ZERO` only for opaque plain draws.

## Verification & Results

1. **Host PPC Test Suites**:
   - `libretro/FBNeo/tests/epic12_gpu/run.py`: **100% PASS** (all 14,400 commands, 10.4M component cases).
   - `libretro/FBNeo/tests/epic12_blit/run.py`: **100% PASS** (all 65,536 rounding cases and 5,000 image cases).

2. **Xenia Canary Benchmark (120 frames `mmpork.state`)**:
   - `submit` CPU time: reduced by **71%** (from 11.5 µs to 3.9 µs per call).
   - `worker_busy`: dropped to **14.98 ms / frame** (well below the 16.67 ms 60 FPS deadline).
   - `cpu_draw` flushes: **0**.
   - `draw_sync`: **0.155 ms**.

3. **Artifacts**:
   - Diagnostics XEX: `Distro360/fbneo.xex`
     - SHA256: `57c437da188659214d8a225215be7d4d2fce3dd2b9b7c28342bc7e72655aaedf`
   - Archive: `/home/humor/salvia-tests/xex-archive/fbneo-20261017-1242-fix-mask-dep-diag.xex`
