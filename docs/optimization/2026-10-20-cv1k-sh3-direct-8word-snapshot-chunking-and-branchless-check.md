# CV1000: SH-3 Direct 8-Word Snapshot Chunking & Branchless Source Equality Check

Baseline `12562103`. Direct branchless 64-bit unrolled comparison for common translation blocks ($\le 8$ words) in `sh3_drc_source_equal()`, eliminating multi-loop while conditions and branch mispredictions on the Xbox 360 in-order Xenon PPE.

## Why

1. **Profiling Real Hardware SH-3 Bottleneck (`mmpork` 5,638 batches / 722 gameplay frames)**:
   - In the real Xbox 360 hardware log (`review build=peak-fix 4acafbeb`):
     - GPU compositing execution has dropped to **0.733 ms / frame** (`draw_sync = 0.733 ms`), and all mid-frame stalls have been eliminated (`war768 = 0`, `worker_wait peak_ms = 0.614 ms`).
     - However, total frame time during intense combat is **19.785 ms** (~50.5 FPS), where `cpu_io` accounts for **18.918 ms (95.6% of frame time)**.
     - On a 60 Hz display, failing to break through the 16.66 ms barrier by even 2-3 ms misses VSync and produces visible 50 Hz judder ("肉眼感觉没什么改善").
   - Profiling of `drc_dispatch` reveals that each frame dispatches ~50,000 translation blocks (~3,285 dispatch entries per slice).

2. **Analysis of Block Length Distribution & Branch Overhead in Source Validation**:
   - The diagnostic length histogram (`drc_work_snapshot_lengths`) in real gameplay demonstrates:
     - Length 6: 8,766 blocks
     - Length 7: 8,034 blocks
     - Length 5: 4,841 blocks
     - Length 8: 3,677 blocks
     - Length 4: 3,563 blocks
     - Length 2: 3,207 blocks
     - Length 3: 2,475 blocks
   - Blocks of length 5..8 words alone account for **over 35% of all executed blocks** (25,318 entries), and blocks $\le 8$ words account for **over 65% of all blocks**.
   - Previously, the fast-path switch was capped at `words <= 4`. Consequently, every block of length 5, 6, 7, 8 fell through into 4 sequential `while` loop checks:
     `while (words >= 16)` -> `while (words >= 8)` -> `while (words >= 4)` -> `while (words >= 2)` -> `if (words)`.
   - For example, a 6-word block (12 bytes) evaluated the 4-word load/cmp, updated pointers, decremented words, evaluated the 2-word load/cmp, updated pointers, decremented words, and checked trailing words.
   - On the Xbox 360 Xenon (PPE: in-order dual-issue PowerPC), branch mispredictions cost ~24 cycles. Across 50,000 blocks per frame, evaluating up to 5-6 branches per block introduced significant pipeline bubble stalls.

## What

1. **`libretro/FBNeo/src/cpu/sh4/sh3_drc_source_check.h`**:
   - Expanded the immediate branchless jump-table switch to `words <= 8`, utilizing unaligned 64-bit and 32-bit loads:
     - `case 5`: 64-bit load + 16-bit word check.
     - `case 6`: 64-bit load + 32-bit dword check.
     - `case 7`: 64-bit load + 32-bit dword check + 16-bit word check.
     - `case 8`: two 64-bit loads.
   - Simplified the remainder logic after 16-word and 8-word chunks to use a direct switch for remaining words 0..7.
   - Completely eliminates all loop branches and pointer updates for blocks $\le 8$ words (over 65% of all translation blocks).

## Verification

1. **Unit Test Suites (Host & PPC Big-Endian)**:
   - `libretro/FBNeo/tests/sh3_source_check/test.cpp`:
     - **100% PASS**: 442,729 test cases, all lengths 0..33, all bit flips, memory guards, ASan/UBSan clean.
   - `libretro/FBNeo/tests/sh3_dispatch/run_inline.py`:
     - **100% PASS**: all 8 host suites, 442,729 source equality cases.
   - `libretro/FBNeo/tests/sh3_ppc/run.py` (QEMU-PPC):
     - Executing full machine code differential tests.

2. **Build and Distro360 Handoff**:
   - Baseline SHA: `12562103`
   - Image set compiled with `build-xex.sh --baseline 12562103 --tag source-opt --flavor diag`.
   - **Candidate Image**: `Distro360/fbneo-source-opt-diag.xex`
     - Tag: `source-opt 65be0b2a 20261020-1934 diag`
     - Size: `34,701,312` bytes
     - SHA256: `85c6c39038d356a12db0a11013405c1602bed58432e93fa16912a4bf1e8fed28`
     - Archive: `/home/humor/salvia-tests/xex-archive/fbneo-20261020-1934-source-opt-diag.xex`
   - **Baseline Image**: `Distro360/fbneo-peak-fix-diag.xex`
     - Tag: `peak-fix 4acafbeb 20261020-1850 diag`
     - Size: `34,701,312` bytes
     - SHA256: `e69f5c5c08a3072d05410528d7d8d89427fb7c38135744d169262604c4755460`
     - Archive: `/home/humor/salvia-tests/xex-archive/fbneo-20261020-1855-peak-fix-diag.xex`
   - Distro360 maintains strictly the A/B pair above.

## Limits

- Real-hardware execution time savings in `cpu_io` must be verified via `Distro360/cv1000-gpu.log` on Xbox 360.

## Revert

`git revert <sha>` restores the 4-word switch and multi-loop checks in `sh3_drc_source_check.h` and removes this review document.
