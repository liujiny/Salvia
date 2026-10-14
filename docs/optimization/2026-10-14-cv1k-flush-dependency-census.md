# CV1000: instrumentation for REGION, SOURCE_DEPENDENCY, and small_cpu flushes

Baseline `ded48003`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

In the 6,340-frame real console benchmark, eliminating `dmode=7` soft calls reduced frame time from 25.96 ms (38.5 FPS) to 20.09 ms (50.0 FPS).
To reach 60 FPS (<=16.67 ms), the remaining 3.42 ms gap is dominated by:
1. `small_cpu`: 1,124,660 small batches (< 8192 pixels) replayed on CPU, taking 3.47 ms/frame.
2. `flushes region = 775,805` (122.4/frame) and `flushes source_dependency = 407,195` (64.2/frame), which continually chop batches into tiny fragments.

Before redesigning the batching logic, we need concrete measurements:
- In `SOURCE_DEPENDENCY` flushes, how many are true dependencies on previously drawn sprites vs false alarms on the empty bounds box?
- How many commands are queued when `REGION` and `SOURCE_DEPENDENCY` flush?
- Does `REGION` exceed the 512-pixel limit in X, Y, or both?
- What is the pixel distribution of the `small_cpu` batches?

## What

In [`epic12_gpu.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_gpu.h) and [`epic12_gpu_xbox.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_gpu_xbox.h):
- Added `epic12_gpu_diag_dep_true` vs `epic12_gpu_diag_dep_false` comparison.
- Added command count buckets (`c1`, `c2_4`, `c5_16`, `c17_64`, `c_gt64`) for both region and dependency flushes.
- Added `exceed_x` and `exceed_y` flags for region flushes.
- Added pixel size buckets (`<512`, `512..2048`, `2048..4096`, `4096..8192`) for `small_cpu` flushes.
- Logged all new stats to `cv1000-gpu.log`.

## Verification

- `libretro/FBNeo/tests/epic12_gpu/run.py`: PASS.
- Local Xenia Canary benchmark (300 frames in `mmpork`).
