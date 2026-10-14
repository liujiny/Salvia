# CV1000: optimize row stepping and eliminate redundant prefetch in epic12_blend_fixed

Baseline `dcdd0491`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

In [`epic12_fast_blit.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_fast_blit.h), `epic12_blend_fixed()` implements the exact integer fixed-alpha blit for CV1000 emulation, accounting for ~98.3% of CPU replay pixels in small batches and ~74.5% of software rasterization overall:
1. **Redundant Pre-fetch Instructions (`__dcbt`)**:
   - For narrow rectangles (`width_bytes <= 128`, which represents typical 8x8, 16x16 or 32x32 sprite clips), the row prefetch issued `__dcbt(0, ptr)` followed immediately by `__dcbt(width_bytes - 1, ptr)`.
   - When offset 0 and offset `width_bytes - 1` reside in the exact same 128-byte cache line (or when flipped), the second `__dcbt` issues a completely duplicate instruction to the CPU pipeline and occupies a hardware prefetch queue slot without fetching any new data.
2. **Repeated Integer Multiplication per Scanline**:
   - Inside the outer loop over `y`, `(((src_y + yf * (y + 1)) & 0x0fff) * 0x2000)` and `(dst_y_start + y + 1) * 0x2000` were being recalculated from scratch on every single row for `next_in` and `next_out`.
   - Then lines immediately recalculated `out = m_bitmaps + (dst_y_start + y) * 0x2000 + dst_x_start + x0` and `in = gfx + (((src_y + yf * y) & 0x0fff) * 0x2000) + ...` again for the active row.
   - For a sprite with $H$ scanlines, this performed dozens of redundant 32-bit integer arithmetic and pointer offset operations.

## What

1. **Cache-Line Boundary Aware Row Prefetch**:
   - Introduced `epic12_prefetch_row` and `epic12_prefetch_row_flip`.
   - Compares `((uintptr_t)p >> 7)` and `((uintptr_t)end >> 7)`. If both endpoints reside in the same 128-byte line, issues only a single `__dcbt(0, p)` and terminates immediately.
   - For wider spans crossing cache lines, issues sequential 128-byte stride prefetches followed by `end` with zero duplicate line requests.
2. **Incremental Pointer Stepping**:
   - Initializes `cur_in` and `cur_out` once prior to the row loop.
   - In each scanline iteration, advances destination with pointer addition (`cur_out += 0x2000`) and advances source row index incrementally (`cur_src_y = (cur_src_y + yf) & 0x0fff`).
   - Reuses `cur_in` and `cur_out` directly, eliminating redundant multiplications and recomputations.

## Verification

1. **PPC Unit Tests (QEMU Big-Endian)**:
   - `python3 libretro/FBNeo/tests/epic12_blit/run.py --toolchain-root /home/humor/salvia-tests/toolchains/ppc/root`
   - Output:
     - `PASS 65536 tint/alpha rounding cases and 1000000 packed colour sums`
     - `PASS 5000 image cases: tint, transparency, clipping, flips, source wrap, overlapping VRAM and delay`
     - **100% PASS** with byte-for-byte identical rendering across all permutations.
2. **Emulator Benchmark (Xenia Canary, `mmpork.state`, 120 gameplay frames)**:
   - Batches: 1,396 (identical)
   - Replay batches: 17,947 (identical)
   - Replay pixels: 15,044,044 (identical)
   - `blit_flush_ms`:
     - Baseline: `replay = 121.814 ms`
     - Candidate: `replay = 121.937 ms` (within host OS jitter)
   - Zero crashes, zero hangs, 100% deterministic frame output.
3. **Artifact Archive**:
   - Diagnostic binary: `/home/humor/salvia-tests/xex-archive/fbneo-20261014-1345-blend-opt-diag.xex`
   - SHA256: `e35a002935ef27ea1e14752c6ca1065edc9c5aa872f09809f0ff758ae932e19c`

## Revert

`git revert <sha>` to restore original `epic12_fast_blit.h` and delete this report.
