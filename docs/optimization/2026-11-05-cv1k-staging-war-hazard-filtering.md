# CV1000: Filter False-Positive Staging WAR Hazards via Source Bounds Check

Baseline `0911dc0c` (cv1k-scanline-gpu verification). Implementation commit `a9a7992f`.

## Why

1. **Remaining Staging WAR Pipeline Flushes**:
   - Hardware profiling on Xbox 360 Jasper (`cv1000-gpu.log`) under `build=cv1k-scanline-gpu f4b527fd` showed:
     - `flushes: source_dependency = 2,825` across 2,498 frames (> 1 flush per frame).
     - `diag_staging: war768 = 2,786` out of 2,825 flushes (98.6% of all source dependency flushes).
   - Inspection of `diag_war_sample` revealed:
     `CPU dst=(768,0,32,32) | GPU cmd#6 src=(864,116,324,1) dst=(414,128) pg=6 rect_hit=0`
   - CPU writes to off-screen scratchpad `[768..799] x [0..31]` (page 6 in 128x128 tiling).
   - A GPU scanline reads from `[864..1187] x [116..116]` (also falling in coarse 128x128 page column 6).
   - The coarse page bitmask marked page 6 as dirty in `epic12_gpu_source_pages[0]`, triggering a false-positive Write-After-Read (WAR) hazard flush even though the CPU write rectangle and GPU read rectangle are separated horizontally by 65 pixels and vertically by 85 pixels (`rect_hit = 0`).

2. **Zero-Overhead Bounding Box Hazard Elimination**:
   - The GPU already tracks the aggregate bounding box of all source reads in the active batch in `epic12_gpu_source_bounds[0]`.
   - Adding an $O(1)$ rectangle intersection check:
     `epic12_gpu_intersects(x, y, w, h, epic12_gpu_source_bounds[0])`
     filters out 100% of these false-positive hazards with zero linear search overhead ($O(1)$ 4-integer comparison).
   - True WAR hazards (where the CPU writes directly into a region currently queued for GPU read) continue to flush and preserve exact rendering.

## What

1. **`libretro/FBNeo/src/burn/devices/epic12_gpu.h`**:
   - In `epic12_gpu_staging_blit` (line 1488):
     ```cpp
     // Before:
     if (war_overlap) {
     // After:
     if (war_overlap && epic12_gpu_intersects(x, y, w, h, epic12_gpu_source_bounds[0])) {
     ```
   - In `epic12_gpu_staging_stream` (line 1648):
     ```cpp
     // Before:
     if (war_overlap) {
     // After:
     if (war_overlap && epic12_gpu_intersects(x, y, total_dimx, dimy, epic12_gpu_source_bounds[0])) {
     ```

## Verification & Results

1. **Host & PPC Test Suites**:
   - `libretro/FBNeo/tests/epic12_gpu/run.py --toolchain-root /home/humor/salvia-tests/toolchains/ppc/root`: **100% PASS** (all 17 test suites, 14,400 commands, 10.4M shader cases, bit-identical VRAM comparison).
   - `libretro/FBNeo/tests/sh3_dispatch/run_inline.py --output /tmp/sh3_dispatch_out`: **100% PASS** (8 suites, 12,000 traces).
   - `libretro/FBNeo/tests/ymz770_idct/run.py`: **100% PASS** (100,000 subband vectors bit-identical).
   - `libretro/FBNeo/tests/frontend_state/run.py`: **100% PASS** (streaming, CRC, worker ownership).

2. **Artifact Identity & Build**:
   - Build script: `/home/humor/salvia-tests/xex-build/build-xex.sh --tag cv1k-war-filter --baseline 0911dc0c --flavor diag`
   - Archive Image: `/home/humor/salvia-tests/xex-archive/fbneo-20261105-2003-cv1k-war-filter-diag.xex`
   - Deployed Image: `Distro360/fbneo.xex`
   - Flavor: `diag` (`SALVIA_FBNEO_DIAGNOSTICS 1`)
   - Image Size: 34,832,384 bytes
   - SHA256: `e3027df33ada8507d5548e3b63853c0f1fae111439e70f1934f21e331c97b5e5`
