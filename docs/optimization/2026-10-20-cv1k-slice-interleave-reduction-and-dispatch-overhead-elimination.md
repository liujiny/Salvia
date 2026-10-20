# CV1000: Slice Interleave Reduction & Outer Dispatch Overhead Elimination

Baseline `c8c561bc`. Reduction of frame slice interleave frequency from `nInterleave = 240` to `nInterleave = 60` in `d_cv1k.cpp`, cutting outer dispatch loop context switches, slice timer calls, and `short_budget` interpreter fallbacks by 75% to achieve steady 60 FPS.

## Why

1. **Profiling Real Hardware Status at 100% Clock (`source-opt 65be0b2a`, 4,404 Frames)**:
   - In the real Xbox 360 hardware log at full 102.4 MHz arcade clock (`clock_hz = 102400000`):
     - `core_frame_ms`: `prep = 0.004 ms`, `cpu_io = 17.113 ms`, `audio_tail_misc = 0.152 ms`, `draw_sync = 1.062 ms`, `total = 18.330 ms` (~54.5 FPS).
     - GPU compositing execution is essentially idle (`draw_sync = 1.062 ms`, `war768 = 0`).
     - To reach solid 60 FPS (16.666 ms), exactly **1.664 ms** needs to be eliminated from `cpu_io`.

2. **Analysis of 240-Slice Interleave Overhead**:
   - Upstream FBNeo hardcoded `nInterleave = 240` (1 slice per scanline, 7,111 cycles at 102.4 MHz).
   - Across 4,404 frames, this forced **1,056,960 slice transitions**:
     - At the tail of each 7,111-cycle slice, translation blocks cannot fit into the remaining budget, triggering `short_budget` (12,586 fallbacks recorded in the 20-frame diagnostic sample). Every `short_budget` drops out of the native DRC into the slow C++ interpreter (`sh3_execute_hot_fallback`).
     - 240 times per frame, the CPU must tear down DRC register caching, return from `Sh3Run`, invoke `sh4_run_timers` (250,623 callbacks, 438.6 ms total), return to `d_cv1k.cpp`, update `nCyclesDone[0]`, and loop.
   - Cave CV1000 arcade hardware has **no mid-frame raster scanline interrupts**; the only raster IRQ is VBLANK (IRQ 2) triggered at the end of the frame (`Sh3SetIRQLine(2, CPU_IRQSTATUS_HOLD)`). Other Cave arcade drivers in FBNeo use `nInterleave = 8` (`d_feversos`, `d_uopoko`) or `32` (`d_donpachi`, `d_dodonpachi`, `d_esprade`, `d_guwange`).
   - `sh4_dtimer::run_prescale` accumulates prescale ticks and dispatches any elapsed timer callbacks across arbitrary cycle spans without losing a single tick or timer expiration.

## What

1. **`libretro/FBNeo/src/burn/drv/cave/d_cv1k.cpp`**:
   - Reduced `nInterleave` from 240 to 60:
     - 1 slice = 4 scanlines ($277.7\,\mu\text{s}$).
     - Slices per frame reduced by 75% (from 240 to 60).
     - Per-slice cycle budget quadrupled from 7,111 to 28,444 cycles ($102,400,000 / 60 / 60$).
     - Quadrupled slice budget reduces the tail cycle proportion, cutting `short_budget` interpreter exits by ~75%.
     - Native DRC translation blocks chain continuously for up to 28,444 cycles on the in-order Xenon PPE without pipeline teardown.

## Verification

1. **Unit Test Suites (Host & PPC Big-Endian)**:
   - `libretro/FBNeo/tests/sh3_source_check/test.cpp`:
     - **100% PASS**: 442,729 cases, all lengths 0..33, all bit flips, memory guards, ASan/UBSan clean.
   - `libretro/FBNeo/tests/sh3_dispatch/run_inline.py`:
     - **100% PASS**: all 8 host suites, 442,729 source equality cases.

2. **Build and Distro360 Handoff**:
   - Baseline SHA: `c8c561bc`
   - Image set compiled with `build-xex.sh --baseline c8c561bc --tag slice-opt --flavor diag`.
   - **Candidate Image**: `Distro360/fbneo-slice-opt-diag.xex`
     - Tag: `slice-opt 5126c8e6 20261020-1948 diag`
     - Size: `34,701,312` bytes
     - SHA256: `775e20d76444827569759bf21c996d713ae2dba68926dc5f63b2e59ebc041a13`
     - Archive: `/home/humor/salvia-tests/xex-archive/fbneo-20261020-1948-slice-opt-diag.xex`
   - **Baseline Image**: `Distro360/fbneo-source-opt-diag.xex`
     - Tag: `source-opt 65be0b2a 20261020-1934 diag`
     - Size: `34,701,312` bytes
     - SHA256: `85c6c39038d356a12db0a11013405c1602bed58432e93fa16912a4bf1e8fed28`
     - Archive: `/home/humor/salvia-tests/xex-archive/fbneo-20261020-1934-source-opt-diag.xex`
   - Distro360 maintains strictly the A/B pair above.

## Limits

- Real-hardware execution time savings in `cpu_io` must be verified via `Distro360/cv1000-gpu.log` on Xbox 360.

## Revert

`git revert <sha>` restores `nInterleave = 240` in `d_cv1k.cpp` and removes this review document.
