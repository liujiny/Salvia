# CV1000 GPU: Defer End-List Flushing to Frame Presentation (Rejected Experiment)

Baseline `77c7f81b` (`c1fc9771`). Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Hypothesis

In `cv1000-gpu.log`, 2,120 out of 2,973 GPU batches (71.3%) were logged as `flushes end=2120`. It was hypothesized that Cave CV1000 issued multiple blit sub-lists per frame, each ending with opcode `0x0000` / `0xf000`, causing premature pipeline flushes. By removing the flush at `0x0000` / `0xf000` in `gfx_exec()` and deferring it to `epic12_draw_screen()` (frame end), we hoped sub-lists could coalesce into fewer GPU batches.

## What

In [`epic12.cpp`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12.cpp):
- Removed `epic12_gpu_flush(EPIC12_GPU_FLUSH_END_LIST)` at opcode `0x0000`/`0xf000` in `gfx_exec()`.
- Added `epic12_gpu_flush(EPIC12_GPU_FLUSH_END_LIST)` inside `epic12_draw_screen()` after `thready.notify_wait()`.
- Added `epic12_gpu_flush(EPIC12_GPU_FLUSH_OTHER)` in `epic12_scan()` before save state serialization.

## Benchmark Results (300 frames `mmpork`, Xenia Canary)

| Metric | Baseline (`c1fc9771`) | Defer End-List (`039e4b3b`) | Delta / Evaluation |
| :--- | :--- | :--- | :--- |
| **GPU Batches (300f)** | 2,973 | **2,973** | **0 saved (0.0%)** |
| **Flushes (`end`)** | 2,120 | **2,120** | **0 saved (0.0%)** |
| **Draw Sync Wait Time** | 1.829 ms | **3.445 ms** | **+1.616 ms (+88.4% regression)** |
| **Worker Busy Time (300f)** | 14,032.8 ms | **12,529.2 ms** | -1,503.6 ms (shifted to main thread) |
| **Total Core Frame Time** | 69.558 ms | **69.269 ms** | -0.289 ms (negligible) |
| **Active Loop Time** | 70.396 ms | **70.003 ms** | -0.393 ms (negligible) |

## Root Cause Analysis

1. **Single Blit Call per Frame**: In `mmpork`, `blit_calls = 300` across 300 frames. The CPU emulation invokes `gfx_exec()` exactly once per video frame.
2. **Multi-Bin Draining, Not Sub-Lists**: The 2,120 `end` flushes ($2,120 / 300 = 7.07$ bins/frame) represent the 8-bin dynamic queue draining its 7-8 simultaneously active bins at the end of that single list.
3. **Thread Work Inversion**: Moving the final drain from `gfx_exec()` (which runs on the parallel worker thread) to `epic12_draw_screen()` (which runs on the main emulation thread) forced the main thread to stall for 1.62 ms executing the remaining D3D batches, causing a regression in `draw_sync`.

## Outcome

Experiment rejected per reversible workflow guidelines. Reverted via `git revert 039e4b3b`.
