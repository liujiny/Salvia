# CV1000 GPU: Accelerate Remaining Blend Modes for DoDonPachi SaiDaiOuJou

## Baseline
- Baseline commit: `8c91645c8be9238318fa5dc206497f744654b0c7` (fix(cv1k,drc): fix read-map validation storm and in-game aot precompilation stutter)
- Target console: Xbox 360 Jasper (67MB available heap, unthrottled PPC DRC)
- Test cases: DoDonPachi SaiDaiOuJou (`ddpsdoj`) stage 1 opening entrance and mid-stage explosion state (`ddpsdoj.state1`).

## Problem Description
In `ddpsdoj`, after eliminating the DRC read-map invalidation storm, significant slowdown and pale white / cyan screen artifacts persisted during large explosions:
1. Cave's EPIC12 blitter uses a variety of blend modes for particle, laser, and explosion effects.
2. Previously, `epic12_gpu.h` only offloaded `smode == 0` (standard alpha / additive) and `smode == 2, dmode == 1` (feedback blend) to the GPU.
3. Unsupported blend combinations fell back to slow CPU software rasterization, or produced inaccurate colors when approximated:
   - `smode == 3, dmode in {0, 3, 7}`: Fixed additive blend with unit source alpha ($S + D \cdot da / 31$).
   - `smode == 2, dmode in {0, 3, 7}`: Feedback blend ($S \cdot D / 31 + D \cdot da / 31$).
   - `smode == 1, dmode in {0, 3, 7}`: Self-modulating additive blend ($S \cdot S / 31 + D \cdot da / 31$).
4. CPU software rasterization of full-screen explosion layers at 32-bit color depth on Xbox 360's in-order Xenon cores causes severe framerate drops (down to ~20 FPS) and stalls the emulation thread.

## Changes Made
1. **Command Normalization in `epic12_gpu.h`**:
   - Normalized `smode == 3` to `smode = 0, sa = 31` (hardware additive blend).
   - Normalized `smode == 1` to `sa = 31` for GPU handling.
   - Expanded supported blitter modes to include `smode in {0, 1, 2}` with `dmode in {0, 1, 3, 4, 7}`.
2. **Batch Classification and Pipeline Grouping in `epic12_gpu_batch.h`**:
   - `epic12_gpu_dest_mode`:
     - Maps `smode == 2` to `dest_mode = 2` (feedback blend shader).
     - Maps `smode == 1` to `dest_mode = 3` (square-modulating additive shader).
     - Maps regular alpha blits to `0` or `1`.
   - `epic12_gpu_feedback`:
     - Returns `true` for `dest_mode in {1, 2}` (requires destination readback).
     - Returns `false` for `dest_mode == 3` (can use hardware additive blending without readback).
   - `epic12_gpu_blend_state`:
     - Returns blend state 100 for feedback modes (`dest_mode in {1, 2}`).
     - Returns blend state 50 for mode 3 (square-modulating additive with hardware blend).
3. **Shader Emulation in `epic12_gpu_xbox.h`**:
   - Updated `shade`, `shade_fast`, and `shade_feedback` to evaluate `mode.z > 2.5` as $S = (S \times S) / 31$.
   - Accurately computes destination feedback for `dest_mode == 2`:
     $$D_{\text{out}} = \min(31, (S \times D) / 31 + (D \times da) / 31)$$
   - Correctly handles additive accumulation for `dest_mode == 3`:
     $$D_{\text{out}} = \min(31, (S \times S) / 31 + (D \times da) / 31)$$
4. **Test Suite Expansion**:
   - Updated `tests/epic12_gpu/test.cpp` and `epic12_gpu_mock.h`:
     - Added test cases covering `smode 1 dmode 3` and `smode 2 dmode 3`.
     - Verified all 10,485,760 destination alpha combinations match reference CPU rasterizer bit-for-bit.
     - Verified all 14,400 batch test commands across all 120 configurations pass.

## Validation Results
- `tests/epic12_gpu/run.py` (PPC build under QEMU): PASS (all batch tests, 10,485,760 dest-alpha cases, and special cases).
- `tests/epic12_blit/run.py`: PASS (all 65,536 rounding cases and 5,000 synthetic image cases).
- `tests/sh3_dispatch/run_inline.py`: PASS (all 8 host suites).
- Verification limits: Verified mathematically against Cave EPIC12 software blitter. Actual Xbox 360 framerate and visual fidelity require console gameplay verification.
