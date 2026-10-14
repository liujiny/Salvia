# CV1000: breakdown of rejected blend modes (smode, dmode)

Baseline `66ac2070`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

In the previous census, 100% of the 6,380 draw rejections were caused by the `blend` check in `epic12_gpu_submit`:
`blend && (smode != 0 || (dmode != 0 && dmode != 1 && dmode != 4))`

To determine whether these blend modes can be accepted by the Xenos GPU pixel shaders or unified feedback shader, we must know the exact `(smode, dmode)` pairs occurring during gameplay.

## What

In [`epic12_gpu.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_gpu.h) and [`epic12_gpu_xbox.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_gpu_xbox.h):
- Added an 8x8 matrix `epic12_gpu_reject_smode_dmode[8][8]` to count occurrences of each `(smode, dmode)` pair.
- Logged each non-zero `reject_mode smode=X dmode=Y count=Z` line to `cv1000-gpu.log` in `epic12_xenos_report()`.

## Verification

- `libretro/FBNeo/tests/epic12_gpu/run.py`: PASS.
- Xenia Canary benchmark (60 frames in `mmpork`).
