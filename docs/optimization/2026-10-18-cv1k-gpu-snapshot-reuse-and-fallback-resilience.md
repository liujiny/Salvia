# CV1000: Snapshot Reuse Fix, Auto-Flush on Overflow, and Resilient GPU Fallback

Baseline `6cf56920`. Iteration and release images built with snapshot cache hit recovery and isolated batch replay.

## Why

1. **User Gameplay Incident**:
   - During continuous gameplay in the release build, shortly after Stage 2 began (heavy bullet patterns and dense pork item drops from the giant drilling tank boss), the screen displayed:
     `CV1000 GPU: CPU fallback - transfer failed`
   - Following this message, GPU acceleration was permanently disabled for the remainder of the session, dropping frame rates to 15–20 FPS.

2. **Root Cause 1: Missing `reused = true` on Cache Hit**:
   - In commit `cd2bab8e`, when refactoring the staging snapshot hash lookup, `reused = true;` was omitted on cache hits:
     ```cpp
     if (s_snap_cache[h_idx].sx == sx && ... && s_snap_cache[h_idx].version == ver) {
         draw_sx = s_snap_cache[h_idx].snap_x;
         draw_sy = s_snap_cache[h_idx].snap_y;
         // reused = true; was missing!
     }
     ```
   - Because `reused` remained `false`, every composite sprite part was marked as a fresh snapshot, repeatedly triggering row copies and consuming allocation slots in every frame.

3. **Root Cause 2: Unhandled Snapshot Allocator Exhaustion**:
   - When the 64-column snapshot allocator was filled by high-density staging sprites, `epic12_gpu_snapshot_alloc()` returned `false`.
   - Without an eviction or retirement trigger, the code fell back to `draw_sx = sx` and `draw_sy = sy` directly in scattered offscreen VRAM pages.
   - Sourcing across dozens of disparate VRAM pages caused the batch to exceed the 128-page limit (`pages > epic12_gpu_batch_page_capacity()`), causing `epic12_xenos_run()` to return `false` (`fail: pages > 128`).

4. **Root Cause 3: Permanent GPU Kill on Single Batch Failure**:
   - In `epic12_gpu_xbox.h`, line 1069 unconditionally set:
     ```cpp
     epic12_xenos_failed = true;
     epic12_xenos_error = 6;
     ```
   - This permanently blocked all future batches from using the GPU for the rest of the game session, even though `epic12_gpu_flush_bin()` already safely replays failed batches in software on the CPU (`epic12_gpu_replay()`).

5. **Root Cause 4: Opaque Error Reporting**:
   - `epic12_xenos_log()` was completely compiled out in release builds (`SALVIA_FBNEO_DIAGNOSTICS 0`), and `epic12_gpu_take_message()` only printed a static string without the actual failure reason.

## What

1. **`libretro/FBNeo/src/burn/devices/epic12_gpu.h`**:
   - Restored `reused = true;` when the snapshot hash cache hits.
   - Added automatic retirement of bin 0 (`epic12_gpu_retire_bin0(EPIC12_GPU_FLUSH_SOURCE_PAGES)`) when the snapshot allocator runs out of columns. Retiring bin 0 resets `s_snap_page_x` and `s_snap_page_y` to 0, ensuring allocation success and completely preventing fallback to unpaginated VRAM.
   - Updated `s_snap_cache` if a snapshot is reallocated after a batch split.

2. **`libretro/FBNeo/src/burn/devices/epic12_gpu_xbox.h`**:
   - Added `epic12_xenos_last_error[128]` tracking in both diagnostics and release builds.
   - Single batch transfer failures now clear the atlas cache (`epic12_xenos_cache.clear()`) and fall back to CPU replay for that batch only, leaving the GPU active for subsequent batches.
   - Introduced `epic12_xenos_consecutive_fails`: the GPU is only permanently disabled if 5 consecutive batches fail.
   - Enhanced toast banner in `epic12_gpu_take_message` case 6 to display the exact root cause string (e.g. `CV1000 GPU: fallback - %s`).

## Verification

1. **Host PPC Test Suites**:
   - `libretro/FBNeo/tests/epic12_gpu/run.py`: **100% PASS** under QEMU PowerPC.
     - 6,291,456 component cases on unified and fast/feedback shaders.
     - 14,400 commands with full VRAM and delay comparison.
     - 128-slot and 256-slot cache coherency and snapshot lookahead.
   - `libretro/FBNeo/tests/sh3_dispatch/run_inline.py`: **100% PASS** (442,729 cases).

2. **Console Savestate Replay**:
   - The user's exact crash state (`mmpork.state`, frame 600, Stage 2 drilling tank boss) was verified with 1,421 GPU batches rendered cleanly and zero fallbacks.

3. **Build Identity**:
   - Commit: `da789a1e`
   - Archive Image: `fbneo-20261018-2150-da789a1e-release.xex`
   - Size: 34,668,544 bytes
   - SHA256: `b8803bd385f91d3d028d98889f0d12664e3bdc16e79c5b873b56311b7a3291ee`
