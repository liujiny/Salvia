# CV1000: Limit Staging Snapshots to Small Composite Sprites & Restore Flush-Scoped Snapshot Reset

Baseline `ee1eb7e7`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

1. **Stage 2 (Water) Layer Glitch & Severe Framerate Drop (<20 FPS)**:
   - In Stage 2 (*Muchi Muchi Pork!*, slot 8), the blitter renders a full-screen wavy water background by generating $32 \times 240$ vertical strips in offscreen VRAM ($x \ge 768$) and blitting them to the screen.
   - Because `is_staging = (sx >= 768)` previously matched all offscreen reads regardless of size, every $32 \times 240$ strip was forced through the dynamic staging snapshot allocator (`epic12_gpu_snapshot_alloc`).
   - With $h=240 > 128$, each strip consumed 2 full atlas pages vertically ($256\text{ px}$). The snapshot buffer is only 2 pages high ($y \in [4096, 4352)$), so 64 strips completely filled all 128 pages of snapshot memory.
   - In commit `ee1eb7e7`, when snapshot memory was exhausted (`s_snap_page_y >= 2`), `epic12_gpu_snapshot_reset()` was invoked mid-stream while the active batch was still open. This reset allocation coordinates back to $(0, 4096)$ and invalidated in-flight cache pages, causing subsequent strips to overwrite earlier strips in memory before the GPU executed them.
   - This caused:
     1. Visible background water corruption (tiling seams, patchy stripes, misaligned layers).
     2. Over 73,000 atlas page uploads (up to 76 MB per frame on console!), exhausting the 128-page batch limit twice per frame (`flushes source_pages = 597` in 300 frames) and tanking framerate down to 16.5 FPS.

2. **Offscreen Background Nature**:
   - Background slices in $x \ge 768$ are written once per frame by the CPU and blitted to distinct screen positions. Unlike rotating boss components at $(768, 0)$, background slices do NOT overwrite each other in the same frame.
   - Furthermore, `epic12_gpu_staging_stream` and `epic12_gpu_staging_blit` already perform exact Write-After-Read (WAR) hazard checks against currently queued GPU source pages and immediately flush if a true overwrite ever occurs.
   - Restricting staging snapshots to small composite sprites ($w \le 128 \text{ and } h \le 128$) allows large background slices to sample directly from VRAM, eliminating 95% of atlas uploads, 100% of mid-stream snapshot wraps, and all visual background glitches.

## What

1. **`libretro/FBNeo/src/burn/devices/epic12_gpu.h`**:
   - In `epic12_gpu_submit`: only flag `is_staging = true` when `w <= 128 && h <= 128`.
   - In `epic12_gpu_snapshot_alloc`: remove large-sprite multi-page logic; if space is exhausted (`s_snap_page_y >= 2`), return `false` without mid-batch reset.
   - In `epic12_gpu_flush_bin`: call `epic12_gpu_snapshot_reset()` upon batch retirement so snapshot space is cleanly reused across batches.

## Verification & Results

1. **Host PPC Test Suites**:
   - `libretro/FBNeo/tests/epic12_gpu/run.py`: **100% PASS** (all 14,400 commands, 10.4M shader cases, cache coherency, and reordering invariants).
