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

## Results (300-frame Xenia Canary Benchmark in `mmpork`)

Image: `flush-census` (`2b880175`). Total frames: 300.

```
CV1000 GPU: flushes end=265 source_pages=0 region=23981 source_dependency=11736 command_limit=0 cpu_upload=35 cpu_draw=0 other=0 small_cpu=32891 gpu_fallback=0 (batches=3126)
CV1000 GPU: diag_dep true=7725 false=4011 (34.2% false) c1=0 c2_4=7477 c5_16=3513 c17_64=746 c_gt64=0 (batches=3126)
CV1000 GPU: diag_reg exceed_x=23981 exceed_y=0 c1=13852 c2_4=6156 c5_16=2148 c17_64=1276 c_gt64=549 (batches=3126)
CV1000 GPU: diag_small_cpu lt512=19246 512_2k=9444 2k_4k=3674 4k_8k=527 (batches=3126)
```

| Metric | Measured Count | Breakdown / Interpretation |
| :--- | :--- | :--- |
| `flushes region` | 23,981 (79.9/frame) | **exceed_x = 23,981 (100.0%)**, exceed_y = 0 (0.0%) |
| Region command counts | - | **c1 = 13,852 (57.8%)**, c2_4 = 6,156 (25.7%), c5_16 = 2,148 (9.0%), c17_64 = 1,276 (5.3%), >64 = 549 (2.3%) |
| `flushes source_dependency` | 11,736 (39.1/frame) | **true = 7,725, false = 4,011 (34.2% false alarms)** |
| Dependency command counts | - | c1 = 0, c2_4 = 7,477 (63.7%), c5_16 = 3,513 (29.9%), c17_64 = 746 (6.4%) |
| `small_cpu` Batches (< 8192 px) | 32,891 (109.6/frame) | **lt512 = 19,246 (58.5%)**, 512_2k = 9,444 (28.7%), 2k_4k = 3,674 (11.2%), 4k_8k = 527 (1.6%) |

## Analysis & Optimization Target

1. **False Dependency Alarms (34.2%)**:
   - Out of 11,736 dependency flushes, 4,011 were triggered by empty bounding-box overlap rather than actual sprite overlap.
   - 93.6% of dependency checks happen when the batch has $\le 16$ commands. Testing exact queued sprite rectangles (`(sx, sy, w, h)` vs queued `dest`) eliminates all 4,011 false flushes.

2. **Dominant Region Flushes (100% Horizontal X)**:
   - 23,981 flushes (79.9/frame) were caused by exceeding the 512-pixel horizontal span in X (0 in Y!).
   - Over half (57.8%, 13,852) occurred with **only 1 command** in the batch.
   - When sprites are drawn in different horizontal zones across the screen/VRAM, the bounding box limit splits them into single-command batches, resulting in 19,246 single-sprite `<512` pixel CPU replays.

