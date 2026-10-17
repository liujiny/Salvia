# CV1000: Disable Batch Command Reordering to Enforce Strict Painter's Algorithm Order

Baseline `a792a67b`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

1. **Root Cause of Boss Layer Misplacement & White Translucent Flickering**:
   - In commit `a7bd7f80`, `epic12_gpu_command_blend_type` was changed to return `epic12_gpu_blend_state(c)`, which assigns a distinct type to each destination alpha value (`da`) and multiply state (`200`).
   - This caused `epic12_gpu_reorder` to aggressively reorder commands within batches, recording **84,116 command moves** in real hardware runs.
   - `epic12_gpu_reorder` only checked geometric destination overlap (`epic12_gpu_intersects`). It did not check:
     - Read-After-Write (RAW) / Write-After-Read (WAR) hazards against source texture regions (`sx, sy`).
     - Multi-part hierarchical ordering dependencies of articulated boss assemblies (such as the Stage 1 Boss).
   - In Cave CV1000 hardware, sprite rendering strictly follows the Painter's Algorithm: sprites are drawn in the exact sequential order defined in the sprite list.
   - Reordering commands across this list pulled upper boss parts (cannons, turrets) behind or in front of lower parts out of order, and caused damage flash sprites (semi-transparent white flash) to swap z-order with boss components between frames, producing visible layer misplacement and flickering white translucency.

2. **Resolution**:
   - Set `epic12_xenos_reorder = false;` in `epic12_gpu_xbox.h`.
   - All draw commands execute in 100% strict sequential FIFO order, eliminating all layer misplacement and damage flash flicker.
   - Performance benchmark in Xenia Canary shows that disabling reordering eliminates the per-frame `memmove`/intersection overhead, reducing `worker_busy` from 2220.98 ms to 2187.71 ms.

## What

1. **Disable Reordering (`epic12_gpu_xbox.h`)**:
   - Changed `epic12_xenos_reorder = true;` to `epic12_xenos_reorder = false;`.

## Verification & Results

1. **Host PPC Test Suites**:
   - `libretro/FBNeo/tests/epic12_gpu/run.py`: **100% PASS** (all 14,400 commands, 10.4M component cases).
   - `libretro/FBNeo/tests/epic12_blit/run.py`: **100% PASS** (all 65,536 rounding cases and 5,000 image cases).

2. **Xenia Canary Benchmark (120 frames `mmpork`)**:
   - `cpu_draw` flushes: **0**.
   - `gpu_batches`: **119** (0.99 batches / frame).
   - `gpu_pixels`: **66,090,759** (100% rendered by GPU).
   - `worker_busy`: **2187.715 ms** (down from 2220.981 ms with reorder enabled).
   - `moved_commands`: **0** (strict Painter's Algorithm order preserved).

3. **Artifacts**:
   - Diagnostics XEX: `Distro360/fbneo.xex`
     - SHA256: `934f19fa78feedc13de11b95dc147538f12bc6771cb7a40b6a69c44eb5f0d832`
   - Archive: `/home/humor/salvia-tests/xex-archive/fbneo-20261017-1050-d5aa4be7-diag.xex`
