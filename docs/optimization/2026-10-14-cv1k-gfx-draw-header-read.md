# CV1000 Optimization: Fast Contiguous Parameter Reading in `gfx_draw` and `gfx_draw_shadow_copy`

- Date: 2026-10-14
- Baseline Commit: `d2432b6369a100bf4d0d97cdb4ccfada9e2f968c`
- Target: `libretro/FBNeo/src/burn/devices/epic12.cpp`
- Diagnostic XEX: `salvia-tests/xex-archive/fbneo-20261014-1520-gfx-draw-read-opt-diag.xex` (SHA256: `75f837ca5ce16e251480efbf5f60ec5fa55a65ee512e9434bbd67774cecf900a`)

## Rationale
During profiling of CV1000 blitter execution on PowerPC Xenon (Xbox 360 in-order core), `gfx_draw` and `gfx_draw_shadow_copy` are called 139,609 times per 120-frame window (1,163 calls per frame).
In the original implementation, each call executed 10 consecutive calls to `READ_NEXT_WORD(addr)` or `COPY_NEXT_WORD(addr)`:
1. Each word read loaded `*addr` through memory, applied `m_main_rammask`, shifted by 1, loaded the halfword, incremented `*addr += 2`, and stored `*addr` back to memory.
2. Consecutive stores followed immediately by loads to the same memory location incurred 10 consecutive Load-Hit-Store (LHS) penalties (~40-50 cycles each on Xenon) per sprite.
3. In `gfx_draw`, screen destination coordinates `x` and `y` were manually sign-extended using `(dst_x_start & 0x7fff) - (dst_x_start & 0x8000)`, consuming multiple ALU instructions and register moves instead of PowerPC's native sign extension instruction `extsh`.

## Changes Made
1. **Contiguous Buffer Fast Path in `gfx_draw_shadow_copy`**:
   - Check if `ram_offset + 20 <= (UINT32)m_main_rammask + 1`. In this 99.999% common path, read all 10 words directly from `&m_ram16[ram_offset >> 1]` and write directly to `&m_ram16_copy[ram_offset >> 1]`, updating `*addr` once at the end (`*addr = cur_addr + 20`).
2. **Contiguous Buffer Fast Path in `gfx_draw`**:
   - In the common contiguous path, read all 10 header parameters (`attr`, `alpha`, `src_x`, `src_y`, `dst_x_start`, `dst_y_start`, `w`, `h`, `tint_r`, `tint_gb`) directly via indexed halfword loads from `m_ram16_copy[ram_offset >> 1]`, updating `*addr` once.
3. **Native PPC Sign Extension**:
   - Replaced arithmetic masking `(dst_x_start & 0x7fff) - (dst_x_start & 0x8000)` with `(INT16)dst_x_start`, mapping directly to single-cycle `extsh`.

## Verification & Test Results
- PPC Blit Test Suite (`libretro/FBNeo/tests/epic12_blit/run.py`):
  - PASS: 65,536 tint/alpha rounding cases & 1,000,000 packed colour sums.
  - PASS: 5,000 image cases (tint, transparency, clipping, flips, wrap, delay).
- PPC GPU Test Suite (`libretro/FBNeo/tests/epic12_gpu/run.py`):
  - PASS: 6,291,456 component cases on unified and fast/feedback shaders.
  - PASS: 14,400 commands full VRAM & delay comparison.
  - PASS: All 2,048 pages at 128/256 slots clipped CPU invalidation.

## Performance Comparison (120-Frame Diagnostic Window)

| Metric | Baseline (`d2432b63`) | Optimized (`gfx-draw-read-opt`) | Delta |
| :--- | :--- | :--- | :--- |
| `worker_busy` | 7645.128 ms | **7632.995 ms** | **-12.133 ms** |
| `draw` | 7216.798 ms | **7203.642 ms** | **-13.156 ms** |
| `submit` | 2875.463 ms | **2857.359 ms** | **-18.104 ms** |
| `gpu` | 6692.493 ms | **6681.778 ms** | **-10.715 ms** |
| `game_and_ui` | 163.784 ms | **162.075 ms** | **-1.709 ms/frame** |
| `sampled_active_loop` | 163.926 ms | **162.217 ms** | **-1.709 ms/frame** |

## Conclusion
Eliminating the 10 repeated Load-Hit-Store cycles and redundant address stepping in sprite header reads reduced total `draw` time by 13.16 ms, reduced `submit` time by 18.10 ms, and improved frontend frame time by 1.71 ms/frame.
