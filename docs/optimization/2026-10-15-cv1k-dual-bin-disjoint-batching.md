# CV1000 GPU: Dual Disjoint Batch Bins to Eliminate Region Ping-Pong Flushes

Baseline `37d6f1ce`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

In `mmpork`, 23,981 `REGION` flushes occurred in 300 frames (79.9 flushes/frame).
All 23,981 flushes (100%) were caused by exceeding the 512-pixel horizontal span in X (`exceed_x`).
The diagnostic probe (`37d6f1ce`) captured the exact coordinates:
- 84.4% of flushes occur under fullscreen clip (`[0..8191]`).
- The game's blitter command list continuously ping-pongs draws between the visible on-screen display (Zone 0: `[0..384]`) and off-screen buffers (Zone 1: `[768..896]` and Zone 2: `[1280..1344]`).
- Because the legacy compositor maintained only a single active batch buffer, every alternation between Zone 0 and Zone 1 immediately pushed the horizontal span $\ge 512$ ($864 - 96 = 768 \ge 512$).
- This prematurely truncated batches after just 1 command (9,841 flushes on `c1`) or 2-4 commands (10,167 flushes on `c2_4`).
- Because these tiny batches contained fewer than 8,192 pixels, they were repeatedly dumped into CPU software replay (`small_cpu`, 28,842 batches in 300 frames), burning 3.47 ms/frame of CPU time on the console.

## What

In [`epic12_gpu.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_gpu.h):
- Implemented dual disjoint batch bins (`bin[0]` and `bin[1]`).
- Disjoint Commutation Check: A draw to Zone 1 commutes with `bin[0]` if:
  1. Destination rectangles are disjoint (`!epic12_gpu_intersects(x, y, w, h, bin[0].bounds)`).
  2. No RAW dependency: incoming source does not read from `bin[0]`'s destination.
  3. No WAR hazard: no queued command in `bin[0]` reads from incoming destination.
- When a draw does not fit in `bin[0]`, it is placed into `bin[1]` without flushing `bin[0]`.
- Both bins accumulate draws concurrently across alternating zones.
- Flushes submit `bin[0]` followed by `bin[1]`, preserving historical ordering.
- If both bins are occupied and a 3rd zone arrives, `bin[0]` is retired, `bin[1]` is promoted, and the new zone occupies `bin[1]`.
- Each batch is submitted and rendered independently, strictly respecting the 128-source-page atlas limit and 512x512 render target.

## Verification

- `libretro/FBNeo/tests/epic12_gpu/run.py`: PASS (all 14,400 commands, full VRAM and delay 100% identical to CPU reference).
- `libretro/FBNeo/tests/epic12_blit/run.py`: PASS (all 65,536 rounding cases and 5,000 image cases).
