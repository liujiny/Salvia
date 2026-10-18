# CV1000: 64-Bit Chunked DRC Snapshot Validation and Onscreen Submit Fast Paths

Baseline `63e6bfb0`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

1. **Hardware Profiling Analysis (The 40 FPS Peak Frame Spike)**:
   - Analysis of `core_slow_frame_ms` and `core_sampled_peak_ms` in `cv1000-gpu.log` (from real console execution of `mmpork`, 2,274 frames in `build=be9c95a9`) pinpointed the root cause of the frame rate drops into the ~40 FPS range:
     - `core_sampled_peak_ms interval_frame=165`: `prep=0.003 ms`, `draw_sync=0.836 ms`, `audio_tail_misc=0.126 ms`, **`cpu_io=24.132 ms`**, `total=25.097 ms` (~39.8 FPS).
     - Notice that `draw_sync` is only 0.836 ms (the GPU blitter worker thread finished well in advance and was not blocking the main thread).
     - The frame rate spike is **96.1% dominated by `cpu_io`** spiking to 24.132 ms.
   - Across all slow frames (`dominant_cpu_io=1`, `dominant_draw_sync=0`), the bottleneck is the guest SH3 execution and dispatch loop during bullet-dense scenes.

2. **Dispatcher Snapshot Comparison Overhead (`sh3_drc_source_equal`)**:
   - `core_phase_ms` showed `drc_dispatch=20,631.985 ms` over 2,274 frames (~9.07 ms per frame), with 98% of block entries hitting `shadow`.
   - On every block entry, `drc_dispatch` calls `sh3_drc_source_equal(cand.original, source, cand.words)` to verify that guest RAM code matches the compiled snapshot.
   - Analysis of `drc_work_snapshot_lengths` revealed that the vast majority of executed blocks are 8 to 32 halfwords long.
   - The existing `sh3_drc_source_equal` implementation performed 16-bit loads (`expected[0] ^ actual[0]`), XORs, and ORs in a loop. For a 32-halfword block, this resulted in 64 individual memory loads and 31 ALU operations, creating heavy memory load-unit pressure and dispatch pipeline stalls on the in-order 3.2 GHz PowerPC Xenon core.

3. **Batch Submission Overhead in `epic12_gpu_submit`**:
   - For every onscreen sprite (bullets, player, enemies), `epic12_gpu_submit` computed 4-way boundary clipping branches and subtractions (`x0`, `y0`, `x1`, `y1`) even when the sprite was completely inside the visible clipping area (>99% of draws).
   - In addition, for ROM-sourced sprites ($sx < 768$, which constitute >91% of all onscreen draws), the code computed staging page coordinates and looped over staging metadata arrays even though ROM sprites can never be staging targets.

## What

1. **`libretro/FBNeo/src/cpu/sh4/sh3_drc_source_check.h`**:
   - **Pointer Identity Fast Path**:
     - Added `if (expected == actual) return true;`.
   - **64-Bit and 32-Bit Unaligned Doubleword Comparisons**:
     - Introduced `sh3_load64_unaligned` and `sh3_load32_unaligned` (using standard `memcpy` intrinsics recognized by MSVC, GCC, and Clang as single-instruction unaligned loads `ld` / `lwz`).
     - Added specialized fast paths for small snapshots ($words \le 4$):
       - $words == 2$: single 32-bit load and equality check.
       - $words == 3$: 32-bit load check plus single 16-bit word check.
       - $words == 4$: single 64-bit load and equality check.
     - For $words \ge 8$: unrolled 64-bit XOR and OR loops checking 8 halfwords (16 bytes) in two 64-bit loads and XORs instead of 16 individual 16-bit loads, cutting memory loads by up to 75%.
     - Handled $words \ge 4$, $words \ge 2$, and odd tails safely without reading beyond the bounded snapshot length.

2. **`libretro/FBNeo/src/burn/devices/epic12_gpu.h`**:
   - **Unclipped Onscreen Sprite Fast Path**:
     - Added fast-path condition for sprites completely contained within `clip` bounds: bypasses all 4 clipping branches and 4 subtractions for >99% of draws.
   - **Non-Staging ROM Sprite Fast Path**:
     - Nested staging page lookups, staging version queries, and snapshot allocation checks under `if (sx >= 768 && sy < 4096 && w <= 512 && h <= 256)`: completely eliminates staging page scanning and hash lookups for all ROM-sourced sprites ($sx < 768$).

## Verification

1. **Host Test Suites**:
   - `libretro/FBNeo/tests/sh3_dispatch/run_inline.py`: **100% PASS** in both `-O3` and `-fsanitize=address,undefined` modes. All 442,729 test cases (including guard-page boundary tests, paired equal-bit mutations, exact allocations) passed with zero errors or leaks.
   - `libretro/FBNeo/tests/epic12_gpu/run.py` (Debian PowerPC GCC + QEMU-PPC): **100% PASS** across all 14,400 commands, 10.4M shader cases, and full VRAM state comparisons.

2. **Build Identity**:
   - Commit: `cd2bab8e`
   - Archive Image: `fbneo-20261018-2018-cd2bab8e-diag.xex`
   - Image SHA256: `0b3d426085f940faf42ffa44806a3a0c83ed87012b034285b496859d8bb87d13`

