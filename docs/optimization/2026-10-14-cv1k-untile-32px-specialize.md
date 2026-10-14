# CV1000: specialize full 32-pixel block loop in epic12_gpu_untile_mask

Baseline `82860df3`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

In Cave CV1000 GPU accelerated blitting, `untile_mask` unpacks and bit-masks the tiled Xenos render-target readback back into CPU VRAM.
From the real hardware `cv1000-gpu.log` (2,787 frames in `mmpork`):
- `untile_mask` consumes **3.78 ms/frame** (0.449 ms/batch across 8.43 batches/frame), representing 28.9% of all GPU batch time.
- The GPU render surface is 512x512 pixels divided into 32x32 pixel macro-tiles.
- For almost all internal blocks across the surface, `tileWidth == 32` (exactly 8 Altivec 16-byte vectors per row).
- Previously, `epic12_gpu_untile_mask` looped with `for (; x + 8 <= tileWidth; x += 8)` and `if (x + 4 <= tileWidth)`. For every line of every 32x32 block, this executed 4 loop iterations, 4 loop condition checks, and multiple index pointer arithmetic operations.

## What

In [`epic12_gpu_untile.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_gpu_untile.h):
1. **Specialized branch-free 32-pixel fast path (`tileWidth == 32`)**:
   - Detects `tileWidth == 32` outside the column loop.
   - Fully unrolls the row into 8 direct vector loads (`__lvx`) with pre-indexed `row_offsets[0..7]`.
   - Performs parallel bitwise masking (`__vand`) with `mask` (`0x20f8f8f8`).
   - If destination is 16-byte aligned (`aligned`), emits 8 direct aligned vector stores (`__stvx`).
   - If unaligned, emits 8 unaligned vector stores (`__storeunalignedvector`).
   - Completely eliminates loop branch overhead, loop counter increments, and index computations for standard 32-pixel macro blocks.
2. **Fallback for edge partial blocks (`tileWidth < 32`)**:
   - Maintains the existing dual-unrolled 8-pixel loop and 4-pixel remainder path for partial blocks at the right frame boundary.
   - Preserves exact per-pixel masking for scalar odd remainders.

## Verification

- **Altivec Untile Test Suite** (`libretro/FBNeo/tests/epic12_gpu_untile/run.py`):
  - `PASS: 262144 source addresses, all macro blocks and four-pixel contiguous groups`
  - `PASS: 6820 rectangles; 66356536 exact pixels; 9064836 aligned / 7200456 unaligned vector stores; little-endian`
  - `PASS: 6820 rectangles; 66356536 exact pixels; 9064836 aligned / 7200456 unaligned vector stores; big-endian`
- **PPC Blit Test Suite** (`libretro/FBNeo/tests/epic12_blit/run.py`):
  - `PASS 65536 tint/alpha rounding cases and 1000000 packed colour sums`
  - `PASS 5000 image cases: tint, transparency, clipping, flips, source wrap, overlapping VRAM and delay` (100% bit-exact).
- **Xenia Canary Benchmark Verification**:
  - Booted and completed 60-frame benchmark run in 15s.
  - All 5 GPU self-tests passed cleanly.
  - Sampled `untile_mask` in emulator: 0.109 ms / batch.

## Build Identity and Handoff

Diagnostics XEX built via `build-xex.sh --tag untile-32px --baseline 82860df3 --flavor diag`:
- **Tag**: `untile-32px a80d1df5 20261014-2051 diag`
- **Deployed Binary**: `Distro360/fbneo.xex`
  - Size: `34,668,544` bytes
  - SHA256: `d86fed13d8438e41374d203f8b944b901b3ce14579ac9837eaddb2a1d795a610`
- **Archived Binary**: `salvia-tests/xex-archive/fbneo-20261014-2051-untile-32px-diag.xex`
- **Source Commit**: `a80d1df5`

