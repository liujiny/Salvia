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
- Local Xenia Canary benchmark (300 frames in `mmpork`).
