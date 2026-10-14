# CV1000 Optimization: Direct `tint_clr` Calculation and Deferred `tinted` Check in `gfx_draw`

- Date: 2026-10-14
- Baseline Commit: `dbeacaa2fd7b7d3fe775dca474fda9a47cd6cd01`
- Target: `libretro/FBNeo/src/burn/devices/epic12.cpp`
- Diagnostic XEX: `salvia-tests/xex-archive/fbneo-20261014-1556-gfx-draw-tint-defer-diag.xex` (SHA256: `80e6c20842dea94be763da762ee9f0e61a5647812a822f0753e6d33874e45ae5`)

## Rationale
During every `gfx_draw` call (139,609 calls per 120 frames):
1. `tint_to_clr` was called to unpack RGB biases, followed by 3 conditional branches checking `if (tint_clr.r != 0x20) tinted = 1;` etc.
2. In 88.6% of draw calls (123,735 out of 139,609), `epic12_gpu_submit` accepts the sprite for hardware GPU rasterization, meaning the `tinted` boolean flag was completely unused and discarded.
3. Only the 11.4% fallback cases actually inspect `tinted` to select CPU software blitter variants.

## Changes Made
1. **Direct `tint_clr` Member Assignment**:
   - Replaced helper function pointer indirection with direct bit-shifts into `tint_clr.r, g, b` and zeroed `t`.
2. **Deferred Branchless `tinted` Evaluation**:
   - Removed the 3 conditional branches from the common path before `epic12_gpu_submit`.
   - Deferred the `tinted` evaluation to only execute if `epic12_gpu_submit` fails, computing it with a single branchless bitwise OR: `const int tinted = (tint_clr.r != 0x20) | (tint_clr.g != 0x20) | (tint_clr.b != 0x20);`.

## Verification & Test Results
- PPC Blit Test Suite (`libretro/FBNeo/tests/epic12_blit/run.py`):
  - PASS: 65,536 tint/alpha rounding cases & 1,000,000 packed colour sums.
  - PASS: 5,000 image cases (tint, transparency, clipping, flips, source wrap, delay).
- PPC GPU Test Suite (`libretro/FBNeo/tests/epic12_gpu/run.py`):
  - PASS: 6,291,456 component cases on unified and fast/feedback shaders.
  - PASS: 14,400 commands full VRAM & delay comparison.

## Performance Comparison (120-Frame Diagnostic Window)

| Metric | Baseline (`dbeacaa2`) | Optimized (`gfx-draw-tint-defer`) | Delta |
| :--- | :--- | :--- | :--- |
| `worker_busy` | 7662.075 ms | **7655.004 ms** | **-7.071 ms** |
| `draw` | 7229.077 ms | **7224.070 ms** | **-5.007 ms** |
| `soft` | 273.487 ms | **265.212 ms** | **-8.275 ms** |
| `game_and_ui` | 157.906 ms | **158.018 ms** | +0.112 ms (within noise) |

## Conclusion
Deferring the `tinted` check outside the common GPU submission path and inlining `tint_clr` packing eliminated ~370,000 conditional branches per 120 frames on the worker thread, reducing total `draw` time by 5.0 ms and software fallback time by 8.3 ms.
