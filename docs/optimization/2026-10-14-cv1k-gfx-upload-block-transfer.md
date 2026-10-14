# CV1000 Optimization: Contiguous Block Copy in `gfx_upload_shadow_copy` and Direct Pixel Pointers in `gfx_upload`

- Date: 2026-10-14
- Baseline Commit: `ad1d7323c4d2bd6b385278f49d0f944236c76051`
- Target: `libretro/FBNeo/src/burn/devices/epic12.cpp`
- Diagnostic XEX: `salvia-tests/xex-archive/fbneo-20261014-1618-gfx-upload-block-transfer-diag.xex` (SHA256: `c046977a17cd8a33b2948e63b860f8981b1f975884140f17ce30ba2e41e616a6`)

## Rationale
During VRAM uploads (`gfx_upload` and `gfx_upload_shadow_copy`):
1. `gfx_upload_shadow_copy` previously looped across all pixels (`for y < dimy for x < dimx`), executing `COPY_NEXT_WORD(addr)` per pixel. Each call performed stack address loading, masking, shifting, memory load, memory store, and stack write-back, creating tens of thousands of Load-Hit-Store cycles for large texture uploads.
2. In `gfx_upload`, `READ_NEXT_WORD(addr)` was similarly invoked per pixel inside the unpacking loop, along with repeated destination row pointer calculations `m_bitmaps + (dst_y_start + y) * 0x2000`.

## Changes Made
1. **Contiguous Buffer Block Transfer in `gfx_upload_shadow_copy`**:
   - Checked if the entire upload payload (`(8 + dimx * dimy) * 2` bytes) resides within contiguous RAM bounds.
   - Replaced per-pixel loops with a single streaming `memcpy` from `&m_ram16[...]` to `&m_ram16_copy[...]` at bus wire speed, updating `*addr` once.
2. **Direct Pixel Pointer Stepping in `gfx_upload`**:
   - Streamed pixels directly from `m_ram16_copy` via `*src_pixels++` in the fast path, completely eliminating per-pixel `READ_NEXT_WORD` calls and stack address dereferences.
   - Stepped row pointers with incremental addition (`dst_row += 0x2000`) instead of repeated multiplications.

## Verification & Test Results
- PPC Blit Test Suite (`libretro/FBNeo/tests/epic12_blit/run.py`):
  - PASS: 65,536 tint/alpha rounding cases & 1,000,000 packed colour sums.
  - PASS: 5,000 image cases (tint, transparency, clipping, flips, source wrap, overlapping VRAM, and delay).
- PPC GPU Test Suite (`libretro/FBNeo/tests/epic12_gpu/run.py`):
  - PASS: 6,291,456 component cases on unified and fast/feedback shaders.
  - PASS: 14,400 commands full VRAM & delay comparison.

## Performance Comparison (120-Frame Diagnostic Window)

| Metric | Baseline (`ad1d7323`) | Optimized (`gfx-upload-block-transfer`) | Delta |
| :--- | :--- | :--- | :--- |
| `blit_write` | 18.142 ms | **13.250 ms** | **-4.892 ms (-27.0%)** |
| `soft` | 264.036 ms | **263.342 ms** | **-0.694 ms** |
| `sampled_batch_cpu_ms peak_total` | 9.955 ms | **9.752 ms** | **-0.203 ms** |
| `async after-present submit` | 0.048 ms | **0.042 ms** | **-0.006 ms** |

## Cumulative Progress Across Today's Blit Optimizations
- `blit_write`: 23.37 ms -> **13.25 ms** (**-43.3% main-thread blitter write overhead!**)
- `game_and_ui`: 163.78 ms -> **159.21 ms** (**-4.57 ms per frame overall!**)
