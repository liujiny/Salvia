# CV1000: Precise Sprite Intersection Conflict Detection for CPU Draw Fallbacks

Baseline `282d1429`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

1. **30 FPS VSync Lock Root Cause**:
   - In baseline `282d1429`, routing feedback blends (`norm_da != 0 && norm_da != 31`) to the CPU blitter completely eliminated the visual mask and flickering bar on hardware.
   - However, gameplay was locked to 30 FPS (`total = 28.27 ms`) because `epic12_gpu_cpu_draw` unconditionally flushed all active GPU bins on every single CPU draw (triggering 22,325 flushes across 1,705 frames, or 13.1 flushes per frame).
   - In real gameplay, entities alternate between drawing their semi-transparent shadow and their body sprite. Because the GPU bin's bounding box (`epic12_gpu_bounds[b]`) encompasses all queued sprites across the screen, checking intersection against `bounds[b]` alone resulted in false collision positives for almost every draw, fragmenting GPU batches into tiny chunks (<8,192 px) that were then replayed on the CPU via `epic12_gpu_replay` (14,786 `small_cpu` flushes).

## What

1. **Precise Per-Sprite Intersection in `epic12_gpu_cpu_draw` (`epic12_gpu.h`)**:
   - Early-return without flushing when the CPU draw is clipped off-screen (`x1 < x0 || y1 < y0`).
   - For in-bounds draws, check if the CPU destination rectangle intersects `epic12_gpu_bounds[b]`. If it does, iterate through the actual queued commands in `epic12_gpu_commands[b]` to determine whether any existing sprite occupies the drawn rectangle.
   - Only flush bin $b$ if an actual sprite collision (WAW / WAR hazard) or RAW source page conflict exists.
   - Non-overlapping CPU draws (such as background scanlines and disjoint shadow draws) execute directly into `m_bitmaps` without disturbing the active GPU batch.

2. **Atlas Invalidation**:
   - Ensure `epic12_gpu_invalidate(rectangle(x0, x1, y0, y1))` updates atlas caching so subsequent GPU draws re-fetch any modified regions.

## Verification & Results

- Built with `SALVIA_FBNEO_DIAGNOSTICS 1`.
- Verified against Xenia Canary host benchmark and native Xbox 360 PPC execution.
