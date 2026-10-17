# CV1000: Fix VRAM Buffer 1 Split & Accelerate epic12_gpu_submit Hot Path

Baseline `7ae6b1c2`. Commit `ff795274`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

1. **Root Cause of Flickering Layer Mask & Partial Screen Artifacts**:
   - Analysis of real console logs and screenshots ([mmpork.state1_upright.png](file:///home/humor/src/Salvia/Distro360/mmpork.state1_upright.png)) revealed a prominent 24-pixel horizontal dark strip running across the screen at the player's level.
   - The game issues:
     `cmd 237: x=32 y=128 w=24 h=240 dm=4 sa=16 da=16 tint=(0,0,0)` (Buffer 0)
     `cmd 64: x=416 y=128 w=24 h=240 dm=4 sa=16 da=16 tint=(0,0,0)` (Buffer 1, $416 = 384 + 32$).
   - This command is an authentic in-game darkening filter issued by the Cave CV1000 arcade ROM to dim specific screen regions.
   - In [`epic12_gpu.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_gpu.h), the off-screen bypass check was hardcoded to `if (x >= 512)`.
   - Cave CV1000 games use double-buffered VRAM:
     - Buffer 0: $X \in [0..319]$
     - Buffer 1: $X \in [384..703]$
   - Hardcoding `x >= 512` split Buffer 1 in half:
     - Left half ($X \in [384..511]$): rendered on GPU.
     - Right half ($X \in [512..703]$): diverted to CPU software replay (`epic12_gpu_replay`), calling `epic12_gpu_invalidate` and wiping the GPU atlas cache.
   - Alternating between GPU and CPU rendering across the middle of Buffer 1 caused severe layer tearing, flickering semi-transparent blocks, and cache invalidation thrashing.
   - Updating the condition to `x >= 768` (Zones 1 & 2 scratchpads) ensures both Buffer 0 and Buffer 1 remain 100% on the GPU.

2. **Root Cause of Frame Rate Ceiling (Worker Thread Stalls)**:
   - Real console hardware profiling (5,994 frames) showed `worker_busy = 15.76 ms/f` with `submit = 10.55 ms/f` (67% of worker time!).
   - In `epic12_gpu_can_fit` and `epic12_gpu_add_to_bin`, evaluating destination page loops and bounding box calculations on every sprite submission (931 calls/frame) caused significant PowerPC CPU instruction stalls.
   - Introducing single-page fast-path checks and bounding box enclosure fast-paths reduces CPU queue construction overhead significantly.

## What

1. **`libretro/FBNeo/src/burn/devices/epic12_gpu.h`**:
   - In `epic12_gpu_submit`: moved off-screen bypass filter from `x >= 512` to `x >= 768`, keeping 100% of visible on-screen Buffer 0 and Buffer 1 draws on the GPU.
   - In `epic12_gpu_can_fit`: added box enclosure check (skipping span expansion when `dest` is already within active bounds) and single-page fast path for `page_overlap`.
   - In `epic12_gpu_add_to_bin`: added single-page fast path for destination page marking.

## Verification & Results

1. **Host PPC Test Suites**:
   - `libretro/FBNeo/tests/epic12_gpu/run.py`: **100% PASS** (all 14,400 commands, 10.4M component cases).
   - `libretro/FBNeo/tests/epic12_blit/run.py`: **100% PASS** (all 65,536 rounding cases and 5,000 image cases).

2. **Xenia Canary Benchmark (60 frames `mmpork.state1`)**:
   - `cpu_draw`: **0**.
   - `flushes`: `end=43`, `source_pages=0`, `region=0`, `source_dependency=0`.
   - `draw_sync`: **0.138 ms**.
   - `worker_busy`: **1,203.3 ms** (down from 2,627 ms baseline).

3. **Artifacts**:
   - Diagnostics XEX: `Distro360/fbneo.xex`
     - SHA256: `512de518c7c783f8f648bf85fce3fd45916368b46d2b7e4adfa6a6e5cde70b7b`
   - Archive: `/home/humor/salvia-tests/xex-archive/fbneo-20261017-1607-fix-buffer1-split-diag.xex`
