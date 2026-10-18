# CV1000: Raster Scanline & Tall Background Slice Snapshot Bypass

Baseline `a4247e56`. Elimination of 650 `source_pages` flushes and 2.8 GB texture upload bottleneck on Stage 1 water backgrounds.

## Why

1. **User Observation on Slot 8 Savestate**:
   - Loading savestate slot 8 (`mmpork.state8`, Stage 1 water surface with torpedo barrage) resulted in severe stuttering locked at **7 FPS**.
   - User inquiry: Does slot 8 trigger CPU fallback?

2. **Hardware Profiling Analysis of Slot 8 (60 Frames)**:
   - Hardware diagnostics revealed `gpu_fallback = 0` (the GPU compositor was fully active, not fallen back).
   - However, extreme pipeline thrashing was occurring:
     - `CV1000 GPU: flushes end=60 source_pages=650 ... (batches=770)`: **10.8 GPU pipeline flushes per frame** triggered by `fit == 3` (`source_pages`).
     - `CV1000 GPU: atlas slots=256 batch_page_limit=128 hits=2071 upload_pages=42764 upload_bytes=2802581504 (snap_up=42573 vram_up=194)`:
       - **42,573 snapshot pages** uploaded in 60 frames (709 page uploads per frame).
       - **2,802,581,504 bytes (2.8 GB)** uploaded in 60 frames (**46.7 MB per frame**).
     - At 300 MB/s texture streaming over D3D, transferring 46.7 MB/frame required **130–140 ms per frame**, strictly capping the console at **7.25 FPS**.

3. **Root Cause: 240 Scanlines ($w \approx 352, h = 1$) Consuming 720 Snapshot Columns**:
   - Diagnostics logging revealed that Muchi Muchi Pork renders its wavy water background using **240 horizontal raster scanlines ($w \approx 324..352, h = 1$)**:
     `CV1000 GPU: SNAP[0]: sx=832 sy=134 w=352 h=1`
     `CV1000 GPU: SNAP[1]: sx=832 sy=135 w=352 h=1`
     ...
     `CV1000 GPU: SNAP[21]: sx=864 sy=155 w=324 h=1`
   - Because $w > 128$, `epic12_gpu_snapshot_alloc()` treated every single 1-pixel high line as a "large staging sprite" and allocated **`cols = (w + 127) >> 7 = 3` full columns ($384 \times 256$ pixels = 98,304 pixels)** for a single 352-pixel line (99.6% space waste).
   - The entire 64-column snapshot buffer was exhausted after only 21 scanlines ($21 \times 3 = 63$).
   - Generating 240 scanlines required 720 columns, overflowing the buffer **11 times per frame**.
   - Slicing 12 contiguous source pages of VRAM across 128 distinct snapshot pages caused `epic12_gpu_source_count[0] + newPages > 128` to fire 11 times per frame, forcing constant GPU flushes, EDRAM resolves, and 2.8 GB of re-uploads.
   - Background scanlines are written once and drawn with raster offsets; they are never overwritten in the same frame and have zero WAR hazards.

## What

1. **`libretro/FBNeo/src/burn/devices/epic12_gpu.h`**:
   - Excluded raster scanlines ($w \le 4$ or $h \le 4$) and tall background slices ($w \le 64 \&\& h > 128$) from staging snapshots:
     ```cpp
     if (sx >= 768 && sy < 4096 && w <= 512 && h <= 256 && w > 4 && h > 4 && !(w <= 64 && h > 128)) {
     ```
     Background scanlines draw directly from their native off-screen VRAM pages (only 8–12 contiguous pages total), completely eliminating page fragmentation.
   - Added horizontal shelf packing within 128-wide columns for tall sprites ($w \le 128, 128 < h \le 256$) to fit up to four $32 \times 224$ sprites per column.
   - Expanded snapshot cache `s_snap_cache` from 64 to 256 entries (`& 255`) to eliminate hash collisions across complex frames.
   - Added safe fallback `draw_sx = sx; draw_sy = sy;` if snapshot allocation fails after retiring bin 0, preventing uninitialized coordinates.

## Verification

1. **Benchmark on Slot 8 Savestate (`mmpork.state8`)**:
   - `flushes source_pages`: **650 -> 0** (**100% eliminated**).
   - `gpu_batches`: **769 -> 119** (reduced from 12.8 batches/frame to under 2 batches/frame).
   - `snap_up` pages: **42,573 -> 63** (**99.85% reduction**).
   - `upload_bytes`: **2.80 GB -> 48.7 MB** (**98.3% reduction**, from 46.7 MB/frame down to 812 KB/frame).
   - `blit_exec draw`: **118 ms -> 0.77 ms/frame** (**99.3% reduction**).
   - `draw_sync`: **0.145 ms**.
   - Total GPU blit time reduced by **90%**, completely removing the 7 FPS bottleneck.

2. **Host & PPC Test Suites**:
   - `libretro/FBNeo/tests/sh3_dispatch/run_inline.py`: **100% PASS** (442,729 test cases).
   - `libretro/FBNeo/tests/epic12_gpu/run.py` (QEMU-PPC): **100% PASS** (all 14,400 commands, 10.4M shader cases, full VRAM comparisons identical).
