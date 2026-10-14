# CV1000 GPU: Precise queued sprite dependency check to avoid false flushes

Baseline `9f83a9fc`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

In `mmpork`, 11,736 `source_dependency` flushes occurred in 300 frames (39.1 flushes/frame).
The diagnostic census (`2b880175` / `9f83a9fc`) revealed that **4,011 of these flushes (34.2%) were false alarms**:
- The legacy check tested `epic12_gpu_intersects(sx, sy, w, h, epic12_gpu_bounds)`.
- `epic12_gpu_bounds` is the coarse bounding box containing all queued destinations. If two distant sprites are queued, `epic12_gpu_bounds` spans tens of thousands of empty pixels.
- Any incoming sprite reading from that empty space was falsely flagged as a read-after-write hazard, immediately terminating the batch.
- Each premature flush splits the batch into tiny `< 8192` pixel fragments that are dumped to CPU replay (`small_cpu`), which consumes 3.47 ms/frame on real console hardware.
- The census proved that 93.6% of dependency flushes occur when the batch contains $\le 16$ commands (`c2_4 = 63.7%`, `c5_16 = 29.9%`). Testing exact rectangle overlap against the queued sprites has virtually zero CPU cost and avoids 4,011 false flushes.

## What

In [`epic12_gpu.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_gpu.h):
- When `epic12_gpu_intersects(sx, sy, w, h, epic12_gpu_bounds)` is true, check `(sx, sy, w, h)` against the exact destination rectangle of each queued command `epic12_gpu_commands[0..count-1]`.
- If a true intersection is found, flush with `EPIC12_GPU_FLUSH_SOURCE_DEPENDENCY`.
- If no queued sprite intersects, avoid the flush, update `epic12_gpu_bounds = merged`, and continue batching.

## Verification

- `libretro/FBNeo/tests/epic12_gpu/run.py`: PASS (all 14,400 commands and full VRAM/delay identical to CPU reference; simulated GPU batches reduced from 1,180 to 1,155 while queued commands increased from 7,104 to 7,147).
- `libretro/FBNeo/tests/epic12_blit/run.py`: PASS (all 65,536 rounding cases and 5,000 image cases).
