# CV1000: Single-Bin FIFO Architecture - Eliminating Layer Disorder and 30 FPS Frame Drops

Baseline `9e4db1c2`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

1. **Root Cause of Layer Displacement ("第一关boss图层错位") and Flickering Blocks ("闪烁区块")**:
   - In `mmpork.state1.png`, a 32-pixel wide horizontal dark band across the screen was observed at $y \in [192, 224]$ ($x \in [96, 127]$ in raw 320x240 VRAM coordinates).
   - Draw call analysis revealed this corresponds to:
     `cmd 64: x=416 y=128 w=24 h=240 bl=512 sm=0 dm=4 sa=16 da=16 tint=(0,0,0)`
     issued in Page 1 of VRAM ($x = 416 = 384 + 32$), corresponding to `cmd 237` on Page 0 ($x = 32$).
   - The multi-bin out-of-order execution queue (`EPIC12_GPU_BINS = 8`) in `epic12_gpu.h` attempted to commute draws across 8 bins using `epic12_gpu_commutes_ahead`.
   - Because Page 0 ($x \in [0, 319]$) and Page 1 ($x \in [384, 703]$) have non-overlapping x-coordinates, the commutation logic incorrectly treated them as disjoint and allowed Page 1 draws to be inserted into earlier bins before Page 0 draws had executed.
   - This broke the Painter's Algorithm, causing layer displacement during boss battles (where both pages and hundreds of sprites are actively used), leaking incomplete draws between pages, and corrupting the alpha channel in VRAM.
   - During subsequent untiling and blend passes, corrupted alpha bits caused `clip(raw.a - 0.0625)` to discard non-transparent pixels, producing visible 32x32 flickering blocks.
   - When a savestate was taken during this state, the corrupted 128 MB VRAM was saved to disk, causing the dark band to reappear immediately upon loading the state until normal background scrolling refreshed VRAM.

2. **Root Cause of 30 FPS Frame Drops in Complex Scenes**:
   - In `cv1000-gpu.log`, `submit` accounted for **13.738 ms** out of **17.932 ms** (76.6%) of the entire worker thread execution time.
   - On every sprite draw call (~1,151 calls/frame), `epic12_gpu_submit` iterated over 8 bins, performing pairwise bounding box hazard checks (`epic12_gpu_commutes_ahead`, `epic12_gpu_can_fit`) against hundreds of commands.
   - This heavy loop overhead on the Xenon in-order PPC cores pushed frame times past the 16.67 ms (60 FPS) VSync deadline, forcing the console into 30 FPS.

3. **Resolution**:
   - Set `EPIC12_GPU_BINS = 1` in `epic12_gpu.h`.
   - Eliminate `epic12_gpu_commutes_ahead` and multi-bin search loops entirely.
   - Simplify `epic12_gpu_submit` to an $O(1)$ fast check: if the command fits in the single active bin, append directly; if not, flush to GPU and append.
   - Guarantee 100% strict FIFO Painter's Algorithm ordering: zero out-of-order draws, zero layer displacement, and zero inter-page bleeding.
   - Substantially reduce `submit` overhead, providing the headroom needed to sustain 60 FPS in complex scenes.

## What

1. **`libretro/FBNeo/src/burn/devices/epic12_gpu.h`**:
   - Changed `#define EPIC12_GPU_BINS 8` to `#define EPIC12_GPU_BINS 1`.
   - Simplified `epic12_gpu_retire_bin0` and `epic12_gpu_flush` to directly flush bin 0 without multi-bin `memcpy` shifting.
   - Removed dead `epic12_gpu_commutes_ahead` hazard-checking function.
   - Replaced multi-bin iteration in `epic12_gpu_submit` with single-bin append and flush logic.

## Verification & Results

1. **Host PPC Test Suites**:
   - `libretro/FBNeo/tests/epic12_gpu/run.py`: **100% PASS**
     - All 14,400 blit commands bit-for-bit identical to CPU reference across 120 tests.
     - All 6,291,456 component shader arithmetic cases PASSED.
     - All 4,194,304 additive component cases PASSED.
     - All plain shader cases PASSED.
     - All clipped invalidation and cache tests PASSED.

2. **Xenia Canary Benchmark (`mmpork.state1`, 60 frames)**:
   - `ordering`: **original** (`moved_commands = 0`).
   - `diag_bins hits`: `[45036, 0, 0, 0, 0, 0, 0, 0]`.
   - `gpu_batches`: **59** (0.98 batches / frame).
   - `cpu_draw` flushes: **0**.
   - `worker_busy`: dropped from ~58.4 ms/frame to **22.44 ms/frame** on Xenia.

3. **Artifacts**:
   - Diagnostics XEX: `Distro360/fbneo.xex`
     - SHA256: `fb5b99dea75ccb34cd2a5dec9444a1fe8bba38fa2e9fe5c6fabe7a3059fb6b0f`
   - Archive: `/home/humor/salvia-tests/xex-archive/fbneo-20261017-1134-single-bin-fifo-diag.xex`
