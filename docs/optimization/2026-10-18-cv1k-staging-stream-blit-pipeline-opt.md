# CV1000: Staging Blit Load-Use Scheduling & Stream Invalidation Aggregation

Baseline `84ac9355`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

1. **Console Profiling Analysis Post-WAR Elimination**:
   - In commit `84ac9355`, all 1,662 `war768` pipeline flushes were completely eliminated, cutting `readback_wait` by 83.5% (from 2.335 ms down to 0.385 ms) and dropping average frame time from 25.88 ms to 17.89 ms (~56 FPS).
   - Profiling of 2,352 frames on real Xbox 360 hardware revealed the remaining 1.22 ms bottleneck to reach solid 60 FPS ($16.66\text{ ms}$ target):
     - `core_frame_ms`: `prep=0.004 ms`, `cpu_io=10.399 ms`, `audio=0.169 ms`, `draw_sync=7.313 ms`, `total=17.886 ms`.
     - In `core_phase_ms`: `worker_busy = 39,747 ms` across 2,054 jobs = **19.35 ms per job**.
     - Because the blitter worker thread takes 19.35 ms while the main SH3 CPU thread finishes in 10.40 ms, the main thread waits $19.35 - 10.40 = \mathbf{8.95\text{ ms}}$ at `epic12_wait_blitterthread()`, creating the 7.31 ms `draw_sync` penalty.

2. **Deconstruction of the 19.35 ms Worker Thread Cost**:
   - Total GPU flush execution across 2,054 jobs: `10,444 ms` = **5.08 ms per job** (2 batches per job at ~2.5 ms each).
   - CPU blit and command dispatch execution: `35,636 ms` = **17.35 ms per job**, of which `submit = 14.92 ms`.
   - Real hardware diagnostic counters revealed `stag_ident = 2,172,045` (~1,057 off-screen staging blits per frame). Staging blit samples (`diag_stag_sample`) captured predominantly $16 \times 16$ sprites:
     - Sample 0, 1, 3, 4, 6, 7: `dst=(1280,0,16,16) src=(6832,64) fl=0x0000 tint=(63,63,63)` (opaque, saturated tint).
     - Sample 2, 5, 8: `dst=(768,0,16,16) src=(1280,0) fl=0x0001 tint=(32,32,32)` (transparent, identity tint).

3. **Bottlenecks in CPU Staging Execution**:
   - **Load-Use Pipeline Stalls on In-Order Xenon**:
     - The Xbox 360 Xenon CPU is an in-order, dual-issue core with 4-cycle L1 load latency.
     - In `epic12_staging_rasterize_blit()`, copying 64-bit double-words sequentially (`d64[0] = s64[0]; d64[1] = s64[1]; ...`) forced a 4-cycle pipeline stall between every load and store. Loading all words into local registers before issuing stores (`w0 = s64[0]; ...; d64[0] = w0; ...`) completely eliminates load-use latency.
   - **Missing 2D Specialized Loops for $16 \times 16$ and $4 \times 4$**:
     - Previously, only $8 \times 8$ identity-transparent blits had a dedicated unrolled path; $16 \times 16$ transparent and all tinted blits fell back to per-row function calls in `epic12_staging_rasterize_row()`.
     - Adding dedicated 4-pixel chunk helpers (`epic12_staging_write_trans4`, `epic12_staging_write_tint4`, `epic12_staging_write_tint_trans4`) for $dimx \in \{4, 8, 16\}$ allows processing full $16 \times 16$ sprites with zero row dispatch overhead.
   - **Per-Blit GPU Cache Invalidation Overhead**:
     - `epic12_gpu_staging_stream` called `epic12_gpu_invalidate(r_inv)` for every single 8x240 / 16x16 slice (1,057 times per frame).
     - Aggregating the bounding box of modified destination pixels across the stream loop and invalidating once at stream exit (or immediately prior to a forced WAR flush) eliminates >1,000 redundant cache invalidation lookups per frame.
   - **Single-Page WAR Check**:
     - For $99.9\%$ of staging sprites ($dimx, dimy \le 16$), the destination is entirely within a single $128 \times 128$ page (`dpy0 == dpy1 && dpx0 == dpx1`). Replacing the 2D loop with a direct single-index check eliminates loop overhead for >1,000 sprites/frame.

## What

1. **`libretro/FBNeo/src/burn/devices/epic12_gpu.h`**:
   - Implemented load-use pipelined 64-bit transfers in `epic12_staging_rasterize_blit()` for $dimx \in \{4, 8, 16\}$.
   - Added specialized 4-pixel write helpers:
     - `epic12_staging_write_trans4`: Bit-29 fast-skip (transparent block) and fast-copy (opaque block) with branchless masking only on mixed edges.
     - `epic12_staging_write_tint4`: Precomputed LUT application without row dispatch overhead.
     - `epic12_staging_write_tint_trans4`: Combined LUT tinting and bit-29 transparency testing.
   - Added specialized blit loops for $dimx \in \{4, 8, 16\}$ across all 4 combinations (opaque/trans $\times$ identity/tinted).
   - In `epic12_gpu_staging_stream()`:
     - Replaced per-blit `epic12_gpu_invalidate()` calls with stream bounding box aggregation.
     - Added single-page fast-path to the WAR dependency check.

## Verification

1. **Host PPC Test Suites**:
   - `libretro/FBNeo/tests/sh3_dispatch/run_inline.py`: **100% PASS** (all 8 host suites, 442,729 test cases).
   - `libretro/FBNeo/tests/epic12_gpu/run.py` (Debian PowerPC GCC + QEMU-PPC): **100% PASS** (all 14,400 commands, 10.4M shader cases, full VRAM comparisons, and simulated GPU batches identical).

2. **Benchmark Verification (`mmpork.state9`)**:
   - 60 frames executed with 0 errors, 0 invalid frames.
   - `war768 = 0`, `source_dependency = 0` flushes maintained.
   - Image identity: `fbneo-20261018-1439-859fd4b0-diag.xex` (SHA256: `8b92fa76576ffafd7d9cd047c107b5070c3c774ebe98378bbef1d1bf84fcc9bc`).
