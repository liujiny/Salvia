# CV1000 GPU: Accept pure additive blend modes (smode=0, dmode=7 and 3)

Baseline `66092d8a`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

In `mmpork`, 100% of all rejected soft draws (34,637 calls across 300 frames, 115.5/frame) were caused by `smode == 0, dmode == 7`.
These rejections forced 10,221 premature `cpu_draw` GPU batch flushes (34 flushes/frame), causing severe pipeline stall and CPU rasterization overhead.

Analysis of the CV1000 blending arithmetic in `epic12pixel.inc` revealed that `_SMODE == 0, _DMODE == 7` and `_DMODE == 3` evaluate to:
`Out = Saturate(Source * (s_alpha / 32) + Dest)`.
Destination is added unattenuated (factor 1.0). This is pure hardware additive blending (`D3DBLEND_ONE + D3DBLEND_ONE`), identical in math to `dmode == 0, da == 31`. It requires zero destination texture feedback passes or resolves.

## What

1. In [`epic12_gpu_batch.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_gpu_batch.h):
   - Normalize `dmode == 7` and `dmode == 3` in `epic12_gpu_dest_mode` to 0, and in `epic12_gpu_dest_alpha` to 31.
   - Automatically marks them as `epic12_gpu_additive` (true) and `epic12_gpu_feedback` (false), enabling fast hardware blending and batch consolidation.
2. In [`epic12_gpu.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_gpu.h):
   - In `epic12_gpu_submit()`: permit `dmode == 7` and `dmode == 3`.
   - In `epic12_gpu_replay()`: route `dmode == 7` to `draw_sprite_..._s0_d7` and `dmode == 3` to `draw_sprite_..._s0_d3` for CPU fallback.
3. In [`epic12_gpu_mock.h`](file:///home/humor/src/Salvia/libretro/FBNeo/tests/epic12_gpu/epic12_gpu_mock.h) and [`test.cpp`](file:///home/humor/tests/epic12_gpu/test.cpp):
   - Use `epic12_gpu_additive()` in mock pixel evaluation.
   - Added 4,194,304 component test cases covering all `(tint, sa, s, d)` combinations for `dmode == 7` and `dmode == 3`.

## Verification

- `libretro/FBNeo/tests/epic12_gpu/run.py`: PASS (all 10.4M component cases and regression suites).
- `libretro/FBNeo/tests/epic12_blit/run.py`: PASS (all 5,000 cases).
- Local Xenia Canary benchmark (300 frames in `mmpork`):

| Metric | Baseline (`66092d8a`) | Optimized (`d86663c7`) | Delta |
| :--- | :--- | :--- | :--- |
| **GPU Rejections** | `blend=34637` | **`blend=0`** | **-34,637 (-100%)** |
| **Soft CPU Draw Calls** | 34,637 (115.5/frame) | **0 (0.0/frame)** | **-100% eliminated** |
| **Soft CPU Draw Time** | 621.58 ms (2.07 ms/f) | **0.00 ms (0.00 ms/f)** | **-621.58 ms** |
| **`cpu_draw` Batch Flushes** | 10,221 (34.1/frame) | **0 (0.0/frame)** | **-10,221 (-100%)** |
| **GPU Rasterized Pixels** | 175.66 Mpx | **230.57 Mpx** | **+54.91 Mpx offloaded to GPU** |
| **GPU Raster Commands** | 158,486 | **193,123** | **+34,637 commands on GPU** |

