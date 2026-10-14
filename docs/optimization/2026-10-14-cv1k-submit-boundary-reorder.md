# CV1000: reorder bounds and dependency checks in epic12_gpu_submit

Baseline `6ea1d0c3`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

In [`epic12_gpu.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_gpu.h), `epic12_gpu_submit()` is called for every blit command (139,609 times in 120 frames):
- Prior implementation computed candidate pages into `newPageList[32]` and checked `epic12_gpu_source_count + newPages > capacity` *before* evaluating command boundary limits (`COMMAND_LIMIT`, `REGION >= 512`, `SOURCE_DEPENDENCY`).
- In actual CV1000 gameplay (`mmpork`), `SOURCE_PAGES` flush occurred 0 times, whereas `REGION` and `SOURCE_DEPENDENCY` flushes occurred 13,996 times (10,098 region + 3,898 dependency flushes).
- Every time a batch flushed, `epic12_gpu_count` became 0, causing the function to perform a second redundant page loop (`py0..py1, px0..px1`) to mark the sprite's pages into the new batch.
- Additionally, on the very first command of every fresh batch (`epic12_gpu_count == 0`), it also scanned pages twice (first into `newPageList`, then directly into `epic12_gpu_source_pages`).

## What

1. **Bounds and Dependency Check Hoisting**:
   - Hoisted `epic12_gpu_bounds` merge and flush checks (`COMMAND_LIMIT`, `REGION`, `SOURCE_DEPENDENCY`) before page allocation.
   - If a flush is triggered, `epic12_gpu_count` resets to 0 immediately without wasting any preliminary page scanning or temporary array writes.
2. **Dedicated Initial Page Insertion Helper**:
   - Introduced `epic12_gpu_add_initial_pages(px0, px1, py0, py1)` and inlined `epic12_gpu_intersects`.
   - When starting a batch (`!epic12_gpu_count`), directly marks all pages of the sprite in a single pass without allocating or iterating through `newPageList`.

## Verification

1. **PPC Test Suite (QEMU Big-Endian)**:
   - `python3 libretro/FBNeo/tests/epic12_gpu/run.py --toolchain-root /home/humor/salvia-tests/toolchains/ppc/root`
   - Output:
     - `PASS 6291456 component cases on unified and fast/feedback shaders`
     - `PASS 14400 commands, full VRAM and delay comparison`
     - `PASS reorder: eight-command boundary, edge/overlap barriers, full-field permutation`
     - `PASS alpha/snapshot/reorder integration 128 & 256 slots`
     - **100% PASS** on all suites.
2. **Emulator Benchmark (Xenia Canary, `mmpork.state`, 120 gameplay frames)**:
   - Batches: 1,396 (identical)
   - Commands: 82,780 (identical)
   - Pixels: 79,826,600 (identical)
   - Flushes: `region=10098, source_dependency=3898, small_cpu=17947` (identical)
   - Sampled batch submit time: 0.056 ms/batch (down from 0.057 ms/batch)
3. **Artifact Archive**:
   - Diagnostic binary: `/home/humor/salvia-tests/xex-archive/fbneo-20261014-1404-submit-opt-diag.xex`
   - SHA256: `b10c466cfe272336826f00f9461df09b07449c9201e6ea545f1d1f6456f7536e`

## Revert

`git revert <sha>` to restore original `epic12_gpu.h` and delete this report.
