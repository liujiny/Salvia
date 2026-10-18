# CV1000: Single-Page Fast Path for Staging Source and Version Checks

Baseline `859fd4b0`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

1. **Console Profiling Analysis (Post-Staging Pipeline Optimization)**:
   - In commit `859fd4b0`, load-use scheduled 64-bit transfers, dedicated 2D blit loops for $16 \times 16$, and invalidation aggregation were deployed.
   - Real hardware profiling over 6,394 frames (`batches=13465`) demonstrated unprecedented performance:
     - Average total frame time dropped to **14.404 ms** (~69.4 FPS throughput).
     - Worker thread job duration dropped by **5.79 ms per job** (from 19.35 ms to 13.56 ms).
     - `draw_sync` dropped from 7.313 ms to **2.845 ms** (-61.1%).
     - `worker_wait avg_ms` dropped from 7.993 ms to **2.609 ms** (-67.4%).
     - Frontend limiter actively engaged with **2.087 ms of idle/limiter headroom** per frame.
     - Out of 6,394 frames, **99.97% of frames (6,392/6,394) were well under 25 ms**.

2. **Per-Sprite Submission Overhead in `epic12_gpu_submit`**:
   - Analysis of `epic12_gpu_submit()` showed that for every submitted sprite with $sx \ge 768$ (thousands of times per frame), two nested 2D loops were executed:
     1. Checking `epic12_gpu_staging_pages[row | px]` across `[spy0..spy1] x [spx0..spx1]`.
     2. Summing `epic12_gpu_staging_version[row | px]` across `[spy0..spy1] x [spx0..spx1]`.
   - In Cave CV1000 games, >99% of sprites are within a single $128 \times 128$ page (`spx0 == spx1 && spy0 == spy1`).
   - Evaluating both with a direct single array index lookup (`(spy0 << 6) | spx0`) eliminates nested loop setup, branch mispredictions, and index recomputations for thousands of sprites per frame.

## What

1. **`libretro/FBNeo/src/burn/devices/epic12_gpu.h`**:
   - Added `spx0 == spx1 && spy0 == spy1` direct index fast path for `is_staging` lookup.
   - Added matching direct index fast path for staging version accumulation.

## Verification

1. **Host PPC Test Suites**:
   - `libretro/FBNeo/tests/sh3_dispatch/run_inline.py`: **100% PASS** (all 8 host suites, 442,729 test cases).
   - `libretro/FBNeo/tests/epic12_gpu/run.py` (Debian PowerPC GCC + QEMU-PPC): **100% PASS** (all 14,400 commands, 10.4M shader cases, full VRAM comparisons, and simulated GPU batches identical).

2. **Benchmark Verification (`mmpork.state9`)**:
   - 60 frames executed with 0 errors, 0 invalid frames.
   - `war768 = 0`, `source_dependency = 0` maintained.
   - Image identity: `fbneo-20261018-1756-76d81b38-diag.xex` (SHA256: `c181e9b6bbbe8f34e288d5b75188d4cca0fb4d30ab212c351779051f92b11e80`).
