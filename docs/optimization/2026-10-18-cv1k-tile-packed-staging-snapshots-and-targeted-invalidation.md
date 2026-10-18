# CV1000: Tile-Packed Staging Snapshots and Targeted Atlas Invalidation

Baseline `c466985e`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

1. **Root Cause of Real Console Peak Spikes (118.5 ms ~ 122.8 ms / ~8 FPS)**:
   - During the Stage 1 Boss explosion and rotating gold pig medals sequence (`mmpork.state6`, `mmpork.state7`), the Cave CV1000 blitter software engine composites rotated sprites into an offscreen scratchpad at $(x \ge 768)$ line-by-line, queues an onscreen blit from $(sx \ge 768)$ to the screen $(x < 768)$, and immediately overwrites the scratchpad for the next rotated sprite.
   - To avoid corrupting queued GPU reads, `epic12_gpu_submit` marks these draws as `is_staging` and allocates snapshot space at $y \ge 4096$.
   - Previously, `epic12_gpu_snapshot_alloc` used a 1D horizontal bump allocator across the full 8192-pixel width (`s_snap_cur_x += aligned_w`).
   - Because $128 \times 128$ texture atlas pages are only 128 pixels wide, allocating 35 snapshots per frame horizontally caused draws to cross into **26 separate 128x128 pages per frame**.
   - Furthermore, `epic12_gpu_snapshot_reset()` invalidated the entire horizontal span `rectangle(0, 8191, 4096, 4351)` on every batch flush, forcing **every single snapshot page to be re-tiled with VMX and re-uploaded over D3D LockRect/UnlockRect every batch**.
   - Across 2,591 frames on real hardware, this resulted in:
     - `snapshots = 102,129`, copying **90.7 million pixels** on CPU via `memcpy`.
     - `upload_pages = 25,579`, uploading **1.67 GIGABYTES** of dynamic texture data to the GPU atlas.
     - `worker_wait peak_ms = 118.525 ms ~ 122.844 ms`, dropping gameplay below 20 FPS during dense medal frames.

## What

1. **`libretro/FBNeo/src/burn/devices/epic12_gpu.h`**:
   - Replaced horizontal 1D bump allocator with 2D tile-packed allocator inside $128 \times 128$ page boundaries:
     - Snapshots with width/height $\le 128$ are packed row-by-row within `(s_snap_page_u, s_snap_page_v)` inside the current $128 \times 128$ page tile.
     - A new page is only allocated when the current 128x128 page tile is completely filled vertically, packing up to 64 16x16 medals into a single 64 KB page rather than scattering them across 8 separate pages.
   - Replaced coarse 8192-wide invalidation with targeted invalidation:
     - Tracks `s_snap_max_page_x` and `s_snap_max_page_y`.
     - `epic12_gpu_snapshot_reset()` only invalidates `rectangle(0, ((max_px + 1) << 7) - 1, EPIC12_SNAPSHOT_Y, ((max_py + 1) << 7) - 1)`. When only 1 or 2 pages are used, the remaining 254 atlas pages stay warm.
   - Added `epic12_gpu_staging_version` and staging dirty rectangle invalidation in `epic12_gpu_staging_blit` and `epic12_gpu_staging_stream`.

2. **`libretro/FBNeo/src/burn/devices/epic12_gpu_xbox.h`**:
   - Added diagnostic counters `epic12_diag_upload_snap` and `epic12_diag_upload_vram` to isolate snapshot upload traffic from normal sprite VRAM uploads.

## Verification & Results

1. **Host Benchmark (`mmpork.state7`, 10 frames)**:
   - `atlas upload_pages`: reduced from **260** to **213** (-18.1%).
   - `upload_bytes`: reduced from **17,039,360 bytes** to **13,959,168 bytes** (-18.1%).
   - `upload_breakdown`: `snap_up = 134`, `vram_up = 82`.
   - `flushes`: `end = 4`, `source_dependency = 0`, `war768 = 0`.
   - `worker_busy`: **490.010 ms** (reduced from 513.098 ms).

2. **Artifacts & Identity**:
   - Tag: `snap-breakdown`
   - Flavor: `diag` (`SALVIA_FBNEO_DIAGNOSTICS 1`)
   - SHA256: `2194425ed80f520f72e7b07ff16ccd993ff9a9a75fa40959658e46d92b4fe6c4`
   - Size: 34,701,312 bytes
   - Archive: `/home/humor/salvia-tests/xex-archive/fbneo-20261018-1009-snap-breakdown-diag.xex`
   - Deployed: `Distro360/fbneo.xex`

## Console Verification Limits

- Tested on host Xenia Canary; real console deployment queued pending Xbox 360 network connectivity (`192.168.2.232`).
- As per `AGENTS.md`, console FPS and hardware frame times are not verified until fetched from `game:\cv1000-gpu.log`.
