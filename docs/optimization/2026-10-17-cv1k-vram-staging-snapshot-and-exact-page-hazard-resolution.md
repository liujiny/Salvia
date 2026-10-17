# CV1000: VRAM Staging Snapshot and Exact-Page Hazard Resolution (Zero Flushes & Complete Boss Textures)

Baseline `674d14d4`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

1. **Root Cause of Boss 1 Texture Artifacts & Missing Fuselage**:
   - The Cave CV1000 blitter software engine composes composite sprites (such as Boss 1's left/right wings, propellers, rotating mechanical parts, and death explosion pig medals) into an offscreen scratchpad at $(x=768, y=0)$ before queuing them for presentation onto the main display.
   - In rapid succession, the guest CPU writes a sub-component to $(768, 0)$, queues a GPU draw from $(768, 0)$ to the screen, and immediately writes the next sub-component over $(768, 0)$ in the same frame.
   - Previously, to prevent corrupting in-flight GPU reads, Write-After-Read (WAR) hazard checks forced an immediate GPU queue flush on every single overwrite.
   - During the Stage 1 Boss sequence (`mmpork.state2`), this triggered **over 1,200 GPU queue flushes per 60 frames** (~20 flushes per frame), dropping framerate down to 17~30 FPS.
   - When attempting to bypass flushes via simple deduplication or skipping, in-flight reads were corrupted by CPU overwrites, causing Boss 1's fuselage to disappear or render with black opaque squares.

2. **Root Cause of 30 FPS / 20 FPS Drops with Initial Snapshot Implementation**:
   - Redirecting staging reads to a reserved snapshot buffer at $y \ge 4096$ solved the visual texture corruption.
   - However, a fallback WAR hazard check in `epic12_gpu_cpu_write` checked for intersection with the active batch's `source_bounds[0]`.
   - Because normal sprites reside at $y \approx 0$ and snapshots reside at $y \ge 4096$, `source_bounds[0]` had a vertical span of $[0, 4100+]$, falsely encompassing all writes to $(768, 0)$.
   - This triggered **393 false WAR flushes** (`war768 = 393`, `batches = 491`), causing framerates to drop to 30 FPS on boss entrance and 20 FPS on boss death.

## What

1. **`libretro/FBNeo/src/burn/devices/epic12.cpp`**:
   - Expanded allocated `m_bitmaps` buffer height from 4096 rows to 4352 rows (`m_gfx_size + 256 * 8192 * sizeof(UINT32)`).
   - The Cave CV1000 blitter coordinates are hardware-masked to 12 bits (`& 0x0fff`), meaning guest code can never read or write $y \ge 4096$. This reserved staging space is permanently protected from guest CPU overwrites.

2. **`libretro/FBNeo/src/burn/devices/epic12_gpu.h`**:
   - Defined `EPIC12_VRAM_PAGES = 2176`, `EPIC12_SNAPSHOT_Y = 4096`, and `EPIC12_SNAPSHOT_MAX_Y = 4352`.
   - Implemented `epic12_gpu_snapshot_alloc()` and `epic12_gpu_snapshot_reset()` to allocate dynamic snapshot rectangles in the reserved space.
   - Automatically copies $(w \times h)$ pixels from $(sx, sy)$ to the allocated snapshot and points the GPU draw command to the snapshot.
   - Replaced coarse `source_bounds` bounding box WAR check with exact page-table verification (`epic12_gpu_source_pages[0][row | px]`). Because snapshot draws only mark page bits $\ge 2048$, CPU writes to $(768, 0)$ (Page 6) no longer generate false-positive WAR conflicts.

3. **`libretro/FBNeo/src/burn/devices/epic12_gpu_cache.h` & `epic12_gpu_alpha.h` & `epic12_gpu_xbox.h`**:
   - Sized page tracking and cache arrays (`needed`, `slots`, `VRAM_PAGES`) to `EPIC12_VRAM_PAGES = 2176`.
   - Integrated snapshot reset on GPU batch flush and detailed staging diagnostics.

## Verification & Results

1. **Xenia Canary Benchmark (`mmpork.state2`, Boss Entrance, 60 frames)**:
   - `snapshots`: **3,354** staging draws snapshotted and batched.
   - `war768`: **0** (dropped from 393 to 0).
   - `source_dependency` flushes: **0** (dropped from 1,241 to 0).
   - `gpu_batches`: **119** (reduced from 491).
   - `worker_busy`: **1376.069 ms** (~22.9 ms/frame on Xenia, completely eliminating pipeline stalls).
   - Visual inspection: Boss 1 fuselage, propellers, rotating mechanical parts 100% complete and visually identical to CPU blit reference.

2. **Xenia Canary Benchmark (`mmpork.state3`, Boss Death Pig Medals Explosion, 60 frames)**:
   - `snapshots`: **1,620** medals snapshotted.
   - `source_dependency` flushes: **0**.
   - `gpu_batches`: **119** (reduced from >500).
   - `worker_busy`: **1426.485 ms** (down from >3,500 ms, completely eliminating the 20 FPS drop).

3. **Artifacts**:
   - Diagnostics XEX: `Distro360/fbneo.xex`
   - Archive: `/home/humor/salvia-tests/xex-archive/fbneo-20261017-2219-vram-snapshot-diag.xex`
   - SHA256: `9f81ecdf8f0f0eaa86b352d36d28cee3bba4feba872837a0ea6d9ea568f12534`
