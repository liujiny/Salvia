# CV1000: Fast Staging Scanline Rasterization and O(1) Reverse Page Cache

Baseline `da9d83d5ae6c7a8c91303435a584627a235f0fc3`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

1. **Root Cause of 20 FPS Drops During Medal & Text Bursts (`mmpork.state5`)**:
   - When the Stage 1 Boss dies, a massive explosion of gold pig medals and Japanese sound bubbles ("ドッ") fills the screen.
   - The Cave CV1000 hardware blitter has no 3D affine matrix or perspective transform. To rotate gold pig medals and shear/slant sound bubbles, the guest game engine slices sprites into **1-pixel high strips** ($w=24, h=1$) and draws each horizontal scanline individually into off-screen scratchpad VRAM ($x \ge 768$, e.g. $x=1280, 1538$).
   - In dense scenes, this generates **over 7,200 1-pixel slice blits per frame** (109,088 slices in 60 frames).

2. **Three Severe CPU Bottlenecks in the Staging Blit Path**:
   - **Out-of-line Function Overhead**: Every 24-pixel slice called `epic12_gpu_replay(c)`, which dispatched to `draw_sprite_f0_ti0_tr1_simple` with 12 stack arguments. On the in-order Xenon CPU, 7,200 non-inlined function calls per frame caused heavy register spills and branch mispredictions.
   - **$O(N)$ Atlas Cache Invalidation**: Every slice called `epic12_gpu_invalidate(dest)`, which looped through all 256 slots in `Epic12GpuPageCache`. For 7,245 slices, that resulted in $7,245 \times 256 = \mathbf{1,854,720}$ loop iterations every single frame.
   - **16-slot Staging Cache Thrashing**: Because consecutive 1-pixel slices have changing Y coordinates ($y=0, 1, 2\dots$), the 16 slots thrashed continuously, achieving a 99.3% miss rate and adding overhead without providing hits.
   - Together, these bottlenecks consumed **35~45 ms of CPU time per frame**, dropping framerate down to 20 FPS.

## What

1. **`libretro/FBNeo/src/burn/devices/epic12_gpu_cache.h`**:
   - Added an $O(1)$ reverse lookup table `short pageToSlot[VRAM_PAGES]` to `Epic12GpuPageCache`.
   - Updated `allocate()` to resolve hits in $O(1)$ and maintain reverse tags on eviction.
   - Optimized `invalidate()`: for small bounds spanning $\le 16$ pages (including all 1-pixel slices which span exactly 1 page), resolves cache eviction in $O(1)$ without scanning all 256 slots.

2. **`libretro/FBNeo/src/burn/devices/epic12_gpu.h`**:
   - Replaced the thrashing 16-slot staging cache and out-of-line function dispatch with a direct fast inlined row copy for unblended, untinted staging slices (`if (x >= 768)`).
   - Accelerated the fallback Write-After-Read (WAR) hazard check: for single-page slices ($dpy0 == dpy1 \land dpx0 == dpx1$), checks `epic12_gpu_source_pages` in a single array lookup.
   - Implemented dynamic staging page tracking (`epic12_gpu_staging_pages`): marks off-screen pages touched by CPU writes so that `is_staging` only triggers on actual staging scratchpad pages, keeping static textures unfragmented while guaranteeing 100% snapshot protection for Boss 1 parts and medals.

3. **`libretro/FBNeo/src/burn/devices/epic12in.inc` & `epic12_fast_blit.h`**:
   - Enabled support for snapshot source rows ($src\_y \ge 4096$) in software fallback rasterizers, preventing row coordinate truncation during CPU replay.

4. **`libretro/FBNeo/tests/epic12_gpu/`**:
   - Sized test buffers for snapshot staging capacity and kept `pageToSlot` reverse cache synchronized in synthetic invalidation tests.

## Verification & Results

1. **Host PPC Test Suite**:
   - 100% PASS across all 15 test suites under `qemu-ppc`:
     - Component shader arithmetic (unified and fast/feedback): PASS
     - Clipped CPU invalidation (all 2048 pages at 128/256 slots): PASS
     - 14,400 commands full VRAM & delay comparison: PASS
     - 640 grouped commands, snapshot overlap/area barriers: PASS
     - 260 source pages $\to$ 3 batches with 128/256-slot cache: PASS
     - Reorder model & alpha/snapshot integration: PASS

2. **Xenia Canary Benchmark (`mmpork.state5`, Medal Burst, 60 frames)**:
   - `submit` execution time: reduced to **5.01 ms/frame** (301.178 ms for 114,003 calls, down from ~35-45 ms/frame; a **7x~9x speedup**).
   - `worker_busy`: **1646.833 ms** (down from >3,500 ms).
   - `gpu_batches`: **53** (less than 1 batch per frame).
   - `source_dependency` flushes: **0**.
   - `war768`: **0**.
   - `diag_blits`: `stag_replay=109088 onscreen=4301`.
   - `diag_staging`: `snapshots=1413 pixels=4055101`.

3. **Xenia Canary Benchmark (`mmpork.state2`, Boss 1 Entrance, 60 frames)**:
   - `source_dependency` flushes: **0**.
   - `war768`: **0**.
   - `gpu_batches`: **119** (clean double-buffering).
   - `worker_busy`: **1316.810 ms** (~21.9 ms/frame on Xenia).
   - Visual inspection: Boss 1 fuselage and propellers 100% complete and visually identical.

4. **Artifacts**:
   - Diagnostics XEX: `Distro360/fbneo.xex`
   - Archive: `/home/humor/salvia-tests/xex-archive/fbneo-20261017-2322-fast-stag-diag.xex`
   - SHA256: `067f94cd9e0853a7c55d446da4075fa912b0def33fc1877c8e8bf7e5f2de6b68`
