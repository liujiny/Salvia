# CV1000: template-specialize FullSource in epic12_blend_fixed to eliminate table lookups

- Date: 2026-10-14
- Baseline Commit: `c6c908b1c579ecb8d40b2b0d1b8894be9277fddb`
- Target: `libretro/FBNeo/src/burn/devices/epic12_fast_blit.h`

## Rationale
Census analysis (`2026-10-11-cv1k-blit-path-census.md`) demonstrated that `epic12_blend_fixed()` accounts for over 98.27% of all replayed CPU pixels in CV1000 emulation, consuming over 11.25 ms/frame of blitter worker time on console (`soft` + `replay`).

Previously, `epic12_blend_fixed()` handled destination scaling via the `FullDestination` template parameter, but always scaled the source channels through 3 separate memory table lookups per pixel (`red[...] | green[...] | blue[...]`). Each pixel performed 3 dependent shifts (`>> 19`, `>> 11`, `>> 3`), 3 masks (`& 31`), 3 table loads, and 2 bitwise OR operations.

When untinted (`tint_clr == {32, 32, 32}`) and opaque source alpha (`s_alpha == 31`), mathematical scaling `(c * 32 / 31) * 31 / 31 == c` is the exact identity for all 32 channel values. In this case, `red[...] | green[...] | blue[...]` is bit-for-bit identical to `pen & 0x00f8f8f8`.

## Changes Made
1. **Template-specialize `FullSource` in `epic12_blend_fixed`**:
   - Added `bool FullSource` template parameter to `epic12_blend_fixed`.
   - When `FullSource` is true, source color extraction is simplified to a single bitwise AND (`pen & 0x00f8f8f8`), completely eliminating 6 memory loads and 10 ALU instructions per 2-pixel iteration.
   - When both `FullSource` and `FullDestination` are active (`s_alpha == 31 && d_alpha == 31 && untinted`), both source and destination table lookups are completely bypassed, turning the entire inner loop into pure register ALU operations (`epic12_packed_add`).
2. **Dispatch specialization in `epic12_draw_fixed`**:
   - Computes `const bool full_source = (tint_clr->r == 32 && tint_clr->g == 32 && tint_clr->b == 32 && s_alpha == 31);` once per draw command.
   - Dispatches to specialized template instantiations without runtime branching in the inner loop.

## Verification
- PPC Blit Test Suite (`libretro/FBNeo/tests/epic12_blit/run.py`):
  - PASS: 65,536 tint/alpha rounding cases & 1,000,000 packed colour sums.
  - PASS: 5,000 randomized image cases (tint, transparency, clipping, flips, source wrap, overlapping VRAM, and delay).
- PPC GPU Test Suite (`libretro/FBNeo/tests/epic12_gpu/run.py`):
  - PASS: 6,291,456 component cases on unified and fast/feedback shaders.
  - PASS: 14,400 commands full VRAM & delay comparison.
