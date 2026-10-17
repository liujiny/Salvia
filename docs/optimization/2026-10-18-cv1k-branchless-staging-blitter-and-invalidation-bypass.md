# CV1000: Branchless Staging Blitter and Invalidation Bypass for Massive Medal Bursts

Baseline `3ab1df469e623e75c07d6652b5a07d2747be9c8b`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

1. **Root Cause of 20 FPS Drops During Massive Gold Pig Medal Bursts (`mmpork.state6`, `mmpork.state7`)**:
   - In save states 6 and 7, the Stage 1 Boss explosion produces an even denser wave of rotating gold pig medals, bomb effects, and Japanese speech bubbles.
   - Real console diagnostics (`cv1000-gpu-real-console.log`) showed:
     - `drc_dispatch = 292.479 ms` over 57 video frames (**5.1 ms/frame** on guest SH-3 CPU, fast).
     - `gpu = 216.591 ms` over 57 video frames (**3.8 ms/frame** on Xenos GPU, fast).
     - `flushes`: `source_dependency = 0, source_pages = 0, war768 = 0` (zero pipeline stalls).
     - `worker_busy = 2316.760 ms` over 57 frames (**40.6 ms/frame** on the blitter worker thread).
     - `draw = 1981.969 ms, submit = 1946.221 ms, draw_calls = 173,688` (over **3,000 to 9,000 blits per frame**).
   - Each blit took **11.4 microseconds on the in-order 3.2 GHz Xenon CPU**, consuming over 35~40 ms per frame on the blitter thread alone and dropping framerate to 20 FPS.

2. **Analysis of Bottlenecks in the Staging Blit Path**:
   - **Per-Pixel Branch Mispredictions in In-Order PowerPC**:
     The previous fast path used `if (p & 0x20000000) d[col] = p;`. On the in-order PowerPC Xenon CPU, which lacks out-of-order execution and suffers a 15~24 cycle branch misprediction penalty, alternating transparent and opaque pixels in rotating medal bounding boxes caused up to 8 branch mispredictions per 16-pixel slice (~150-200 cycles lost per slice).
   - **Narrow Fallback Triggers**:
     Draws with `tint == (63,63,63)` (Cave CV1000 hardware blitter register default `0x00ff, 0xffff` for highlighted/white sprites), `flipx`, or `flipy` failed the `tint == 32 && !flipx && !flipy` check and fell back to `epic12_gpu_replay()`, running un-inlined scalar software loops in `epic12in.inc`.
   - **Redundant Atlas Cache Invalidation**:
     `epic12_gpu_invalidate(dest)` was called for every single staging blit (170,000 times per 60 frames), even though off-screen staging VRAM ($x \ge 768, y < 4096$) is never uploaded directly to the GPU atlas (onscreen draws from staging use snapshot rows $y \ge 4096$).
   - **Nested Loops in Page Bookkeeping & WAR Hazard Check**:
     Two nested loops ran per slice for `epic12_gpu_staging_pages` and WAR check even for 1-pixel scanlines.

## What

1. **`libretro/FBNeo/src/burn/devices/epic12_gpu.h`**:
   - **Branchless 4-Pixel Vectorized Staging Blitter**:
     - Check `(p0 | p1 | p2 | p3) & 0x20000000`: if zero, skips all 4 pixels with zero writes and zero cache touches.
     - Check `(p0 & p1 & p2 & p3) & 0x20000000`: if non-zero, performs direct stores `d[0..3] = p[0..3]` without masking.
     - For mixed transparency, uses arithmetic sign-extension `INT32 mask = ((INT32)p << 2) >> 31; d[col] = (p & mask) | (d[col] & ~mask);` eliminating branch mispredictions completely.
   - **Universal Tint Support**:
     - Precomputes 32-entry stack lookup tables (`lut_r`, `lut_g`, `lut_b`) directly from `epic12_device_colrtable[c][tint]` for non-identity tints (e.g. `(63,63,63)`), executing bit-exact color multiplication in 3 lookups per pixel with no divisions.
   - **Horizontal and Vertical Flip Support**:
     - Handles `flipx` (stepping $s$ backwards) and `flipy` (inverting row order) directly within the fast path.
   - **O(1) Staging Page Bookkeeping & Guarded WAR Check**:
     - Single-page $O(1)$ fast path when $dpy0 == dpy1 \land dpx0 == dpx1$.
     - Entire WAR check guarded by `if (epic12_gpu_count[0])` to bypass checking when no GPU draws are queued.
   - **Bypass Redundant Cache Invalidation**:
     - Removed `epic12_gpu_invalidate(dest)` on staging buffers ($x \ge 768$).

2. **`libretro/FBNeo/src/burn/devices/epic12_gpu_xbox.h`**:
   - Formatted diagnostic staging samples to log flags and tint values for comprehensive offline analysis.

3. **`libretro/FBNeo/tests/epic12_gpu_cache/test.cpp`**:
   - Updated reference cache test array bounds to `Epic12GpuPageCache::VRAM_PAGES` (2176), resolving AddressSanitizer stack overflow.

## Verification & Results

1. **Host PPC Test Suite**:
   - 100% PASS across all 16 test suites under `qemu-ppc`:
     - 6,291,456 component cases on unified and fast/feedback shaders: PASS
     - 4,194,304 additive component cases on dmode 7 and 3: PASS
     - 4,096 plain shader cases: PASS
     - Clipped CPU invalidation & 14,400 command comparison: PASS
     - Restored batch cap, shader selection, cache 128/256 slots: PASS
     - Snapshot lookahead & alpha/snapshot/reorder integration: PASS

2. **Xenia Benchmark on `mmpork.state6` (60 frames)**:
   - `stag_ident` hit rate: surged from 0 (earlier) / 153,694 to **155,853** draws.
   - `stag_replay` fallback: plummeted from 2,543 to **only 384 draws** (**99.75% fast path hit rate**).
   - `game_and_ui`: reduced from 93.766 ms to **80.791 ms**.
   - `gpu_batches`: **47** (zero pipeline flushes, zero `war768` stalls).

3. **Xenia Benchmark on `mmpork.state7` (60 frames)**:
   - `stag_ident`: **172,644 draws** handled branchlessly.
   - `stag_replay`: **only 336 draws** (**99.8% fast path hit rate**).
   - `game_and_ui`: **72.918 ms**.

4. **XEX Artifact Identity**:
   - Built: `Distro360/fbneo.xex` (flavor `diag`, `SALVIA_FBNEO_DIAGNOSTICS 1`)
   - Size: `34,701,312 bytes`
   - SHA-256: `2a6d5dd356baddbdd170df29b5df01062e22cb6667418e03e8ebba7f73356aeb`
   - Archive: `/home/humor/salvia-tests/xex-archive/fbneo-20261018-0551-fast-stag-opt-diag.xex`
