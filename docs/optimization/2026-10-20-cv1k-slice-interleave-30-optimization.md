# CV1000: Slice Interleave 30 Optimization & Pipeline Overhead Reduction

Baseline `7a458e78`. Reduction of frame slice interleave frequency from `nInterleave = 60` to `nInterleave = 30` in `d_cv1k.cpp`, doubling per-slice cycle budget from 28,444 to 56,888 cycles, further halving outer dispatch loop context switches, slice timer checks, and `short_budget` interpreter fallbacks.

## Why

1. **Profiling Real Hardware Status at `nInterleave = 60` (`slice-opt 5126c8e6`, 1,843 Frames)**:
   - In the real Xbox 360 hardware log at full 102.4 MHz arcade clock (`clock_hz = 102400000`):
     - `core_frame_ms`: `prep = 0.004 ms`, `cpu_io = 13.831 ms`, `audio_tail_misc = 0.234 ms`, `draw_sync = 1.605 ms`, `total = 15.673 ms` (~63.8 FPS).
     - Successfully achieved the first sub-16.66 ms core frame time on Xbox 360 hardware.
     - `short_budget` exits plunged by 93% (from 12,586 to 886).
   - In the frontend frame profile:
     - `game_and_ui = 16.078 ms` (at 102.4 MHz) to `18.526 ms` (during heavy bullet storm sections).
     - `present = 1.002 - 1.327 ms`.
     - `sampled_active_loop = 18.437 - 19.673 ms` (~50.8 - 54.2 FPS).
     - In heavy scenes, the active loop slightly exceeds the 16.666 ms VSync interval, causing perceived 50 Hz judder.
   - To achieve rock-solid 60 FPS across even the heaviest bullet hell peaks with comfortable headroom, another 1.5 - 2.0 ms needs to be eliminated from `cpu_io`.

2. **Analysis of 30-Slice Interleave (8 Scanlines per Slice)**:
   - Cave CV1000 arcade hardware has **no mid-frame raster scanline interrupts**; the only raster IRQ is VBLANK (IRQ 2) triggered at the end of the frame (`Sh3SetIRQLine(2, CPU_IRQSTATUS_HOLD)`).
   - In `d_cv1k.cpp`, there is no driver or device work performed inside the slice loop:
     ```cpp
     for (INT32 i = 0; i < nInterleave; i++) {
         CPU_RUN(0, Sh3);
     }
     ```
   - In `Sh3Run_timerhack`, timers (`sh4_run_timers`) are only evaluated at slice boundaries when `m_sh4_icount <= 0`.
   - Reducing `nInterleave` from 60 to 30:
     - 1 slice = 8 scanlines ($555.5\,\mu\text{s}$).
     - Slices per frame cut by 50% (from 60 to 30).
     - Per-slice cycle budget doubles from 28,444 to 56,888 cycles ($102,400,000 / 60 / 30$).
     - Quadrupled block execution headroom allows long straight-line chains and loop sequences to run natively without budget truncation.
     - Halves context switches between C++ outer dispatch and native DRC execution.

## What

1. **`libretro/FBNeo/src/burn/drv/cave/d_cv1k.cpp`**:
   - Reduced `nInterleave` from 60 to 30:
     - Cuts slice transitions and timer sweeps from 60 to 30 per frame.
     - Doubles slice cycle budget to 56,888 cycles.

## Verification

1. **Unit Test Suites (Host & PPC Big-Endian)**:
   - `libretro/FBNeo/tests/sh3_dispatch/run_inline.py`:
     - **100% PASS**: all 8 host suites in optimized and ASan/UBSan modes (including 12,000 traces x 12 slices, guards, aliases, partial exits, 442,729 source check cases).
   - `libretro/FBNeo/tests/sh3_ppc/run.py`:
     - **100% PASS**: 846,030 cases in QEMU PPC big-endian (compiled 727,804, fallback 118,226).

2. **Build and Distro360 Handoff**:
   - Baseline SHA: `7a458e78`
   - Image set compiled with `build-xex.sh --baseline HEAD~1 --tag slice30-opt --flavor diag`.
   - **Candidate Image**: `Distro360/fbneo-slice30-opt-diag.xex`
     - Tag: `slice30-opt e71e8c09 20261020-2013 diag`
     - Size: `34,701,312` bytes
     - SHA256: `15ec251015e22eeddaabbd3b8fa3f6b97f93040a10819a83d44fb6d0a5f33eb1`
     - Archive: `/home/humor/salvia-tests/xex-archive/fbneo-20261020-2013-slice30-opt-diag.xex`
   - **Baseline Image**: `Distro360/fbneo-slice-opt-diag.xex`
     - Tag: `slice-opt 5126c8e6 20261020-1948 diag`
     - Size: `34,701,312` bytes
     - SHA256: `775e20d76444827569759bf21c996d713ae2dba68926dc5f63b2e59ebc041a13`
     - Archive: `/home/humor/salvia-tests/xex-archive/fbneo-20261020-1948-slice-opt-diag.xex`
   - Distro360 maintains strictly the A/B pair above.

## Limits

- Real-hardware execution time savings in `cpu_io` and frontend `game_and_ui` must be verified via `Distro360/cv1000-gpu.log` on Xbox 360.

## Revert

`git revert <sha>` restores `nInterleave = 60` in `d_cv1k.cpp` and removes this review document.
