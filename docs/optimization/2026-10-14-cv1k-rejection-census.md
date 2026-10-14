# CV1000: census of GPU draw submission rejection reasons

Baseline `76082236`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

In Cave CV1000 GPU accelerated blitting, software fallback rasterization (`soft`) accounts for:
- 6.27 ms / frame (93.8 soft calls / frame) on real console.
- 130.9 ms across 60 frames (106 soft calls / frame) on emulator benchmark.
- 30.4 premature GPU batch flushes per frame (`cpu_draw` flushes) triggered by these refused draws.

In `epic12_gpu_submit`, there are 5 rejection conditions:
1. `blend && (smode != 0 || (dmode != 0 && dmode != 1 && dmode != 4))`
2. `w > 512 || h > 512`
3. Out-of-bounds: `x < 0 || y < 0 || x + w > 8192 || y + h > 4096`
4. Source vertical wrap: `sy + h > 4096`
5. In-place self-intersection: `epic12_gpu_intersects(sx, sy, w, h, dest)`

To safely accept these draws onto the GPU or optimize their path, we must identify which specific condition causes the rejections.

## What

In [`epic12_gpu.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_gpu.h) and [`epic12_gpu_xbox.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_gpu_xbox.h):
- Added non-invasive counters for each of the 5 rejection conditions.
- Logged `rejections blend=... size=... oob=... sy=... intersect=...` to `cv1000-gpu.log` in `epic12_xenos_report()`.

## Verification

- `libretro/FBNeo/tests/epic12_gpu/run.py`: PASS all 6,291,456 component cases and 14,400 commands on PPC QEMU.
- Xenia Canary benchmark to measure the census numbers.
