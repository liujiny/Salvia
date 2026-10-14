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
- Local Xenia Canary benchmark (300 frames in `mmpork`):

## Results (300-frame Xenia Canary Benchmark in `mmpork`)

Build: `precise-dep` (`645b824d`).

```
CV1000 GPU: flushes end=265 source_pages=0 region=23981 source_dependency=7725 command_limit=0 cpu_upload=35 cpu_draw=0 other=0 small_cpu=28842 gpu_fallback=0 (batches=3164)
CV1000 GPU: diag_dep true=7725 false=4011 (34.2% false) c1=0 c2_4=3466 c5_16=3513 c17_64=746 c_gt64=0 (batches=3164)
CV1000 GPU: diag_reg exceed_x=23981 exceed_y=0 c1=9841 c2_4=10167 c5_16=2148 c17_64=1276 c_gt64=549 (batches=3164)
CV1000 GPU: diag_small_cpu lt512=15097 512_2k=9576 2k_4k=3217 4k_8k=952 (batches=3164)
```

| Metric | Baseline (`2b880175`) | Optimized (`645b824d`) | Delta |
| :--- | :--- | :--- | :--- |
| **`source_dependency` Flushes** | 11,736 (39.1/frame) | **7,725 (25.8/frame)** | **-4,011 (-34.2% false flushes eliminated)** |
| **`small_cpu` Batches (< 8192 px)** | 32,891 (109.6/frame) | **28,842 (96.1/frame)** | **-4,049 (-12.3%)** |
| **`small_cpu lt512` (single sprites)** | 19,246 (64.2/frame) | **15,097 (50.3/frame)** | **-4,149 (-21.6%)** |
| **Region `c1` Single-draw Batches** | 13,852 (46.2/frame) | **9,841 (32.8/frame)** | **-4,011 (-28.9%)** |
| **CPU Replay Pixels** | 27,335,751 | **26,950,983** | **-384,768 pixels offloaded to GPU** |
| **GPU Rasterized Pixels** | 230,573,640 | **230,958,408** | **+384,768 pixels on GPU** |
| **GPU Batches** | 3,125 | **3,163** | **+38 batches executed on GPU** |
| **Build Identity** | `2b880175` | `645b824d` | `fbneo-20261014-2352-precise-dep-diag.xex` |

