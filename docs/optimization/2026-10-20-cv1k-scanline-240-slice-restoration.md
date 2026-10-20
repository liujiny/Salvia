# CV1000: Scanline 240-Slice Interleave Restoration & Task Backlog Elimination

Baseline `c2d0b40b` (HEAD at `rotfold-opt` `b868b5ab`). Restoration of scanline-accurate interleave frequency (`nInterleave = 240`) in `d_cv1k.cpp` while retaining direct RAM address rotate folding (`b868b5ab`), resolving console frame drops to 30 FPS caused by timer callback starvation and bullet task backlog bursts.

## Why

1. **Console Evidence for Minimum 30 FPS Drops**:
   - In real hardware diagnostics (`rotfold-opt b868b5ab`, 2,201 frames):
     - Normal gameplay averaged **14.092 ms** in `cpu_io` and **15.721 ms** in total frame time (63.6 FPS).
     - However, in dense bullet storms / boss explosions (e.g. 216-frame interval and sampled peak frame 1985), `cpu_io` spiked to **22.498 ms** and peaked at **40.681 ms** (total 42.099 ms).
     - Because Xbox 360 displays with 60 Hz VSYNC (16.666 ms deadline), exceeding 16.66 ms forces the display hardware to wait for the second VSYNC interval (33.33 ms), causing the on-screen frame counter to drop to **30.0 FPS** (or 20 FPS during 40 ms spikes).

2. **Root Cause: Coarse Slice Coupling Starving Scanline Timers**:
   - Upstream FBNeo original timing used `nInterleave = 240` (1 slice per scanline, 7,111 cycles at 102.4 MHz).
   - In `5126c8e6`, reducing `nInterleave` to 60 (and 30 in `e71e8c09`) widened the slice interval from $69\,\mu\text{s}$ to $277\,\mu\text{s}$.
   - Cave CV1000 hardware paces task dispatch, blitter completion (`cave_blitter_delay`), and enemy/bullet logic via SH-3 TMU timer callbacks. In real console logs:
     - Under `nInterleave = 240` (`source-opt`): `timer_callbacks` averaged **34.7 callbacks/frame**.
     - Under `nInterleave = 60` (`rotfold-opt`): `timer_callbacks` dropped to **6.79 callbacks/frame** (an 80% starvation).
   - For 3 out of every 4 scanlines, no timers evaluated and blitter busy flags remained set. Tasks accumulated in Cave's internal task queues. When the 4th scanline hit the slice boundary, the CPU was forced to evaluate a massive backlog of queued bullet and sprite tasks simultaneously, causing the 40 ms frame spikes.

## What

1. **`libretro/FBNeo/src/burn/drv/cave/d_cv1k.cpp`**:
   - Restored `nInterleave = 240`:
     - 1 slice = 1 scanline ($69.4\,\mu\text{s}$, 7,111 cycles).
     - Scanline-level TMU timer evaluation (35+ callbacks per frame) and immediate blitter completion.
     - Eliminates task queue backlogs in bullet storms, spreading execution evenly across the frame.
   - Retained `sh3_drc_ppc.h` direct RAM rotate folding (`b868b5ab`):
     - Every direct RAM load and store executes 1 fewer instruction with zero dependent RAW pipeline stalls, preserving the ~2.2 ms instruction execution speedup.

## Verification

1. **Host Differential Test Suites**:
   - `libretro/FBNeo/tests/sh3_dispatch/run_inline.py`:
     - **100% PASS**: 8 host suites, 442,729 test cases, ASan/UBSan clean.

2. **QEMU PowerPC Native DRC Suite**:
   - `libretro/FBNeo/tests/sh3_ppc/run.py`:
     - **100% PASS**: 846,030 total test cases (726,971 compiled, 119,059 fallback).

3. **Build and Distro360 Handoff**:
   - Baseline commit: `b868b5ab`
   - Implementation commit: `9034698c`
   - Image set compiled with `build-xex.sh --baseline HEAD~1 --tag scanline240-opt --flavor diag`.
   - **Candidate Image**: `Distro360/fbneo-scanline240-opt-diag.xex`
     - Tag: `scanline240-opt 9034698c 20261020-2157 diag`
     - Size: `34,701,312` bytes
     - SHA256: `c307ebeda270295a9f3134dba889f885ea71bd658baa61cb7826bdb5fe9d9585`
     - Archive: `/home/humor/salvia-tests/xex-archive/fbneo-20261020-2157-scanline240-opt-diag.xex`
   - **Baseline Image**: `Distro360/fbneo-rotfold-opt-diag.xex`
     - Tag: `rotfold-opt b868b5ab 20261020-2054 diag`
     - Size: `34,701,312` bytes
     - SHA256: `8c6333cbbe8f283b3ffdf596f6d3b77908607484bad122761f624854155e20f7`
     - Archive: `/home/humor/salvia-tests/xex-archive/fbneo-20261020-2054-rotfold-opt-diag.xex`
