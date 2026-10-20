# CV1000: Slice Interleave 30 Optimization & Pipeline Overhead Reduction (REJECTED)

Baseline `7a458e78`. Reduction of frame slice interleave frequency from `nInterleave = 60` to `nInterleave = 30` in `d_cv1k.cpp`.

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
   - Hypothesis: reduce `nInterleave` from 60 to 30 to further cut slice transition overhead.

## What

1. **`libretro/FBNeo/src/burn/drv/cave/d_cv1k.cpp`**:
   - Reduced `nInterleave` from 60 to 30:
     - Cuts slice transitions and timer sweeps from 60 to 30 per frame.
     - Doubles slice cycle budget to 56,888 cycles.

## Verification & Console Outcome (REJECTED)

1. **Real Console Verification (`slice30-opt e71e8c09`, 2,252 Frames)**:
   - **`core_slow_frame_ms`**: 3 samples, average `total = 36.685 ms` (**~27.2 FPS**)!
   - **`core_sampled_peak_ms`**: `cpu_io = 40.039 ms`, `total = 40.984 ms` (**~24.4 FPS**)!
   - **`cpu_io`**: Increased from **13.831 ms** (`nInterleave = 60`) to **16.320 ms**!
   - **`timer_callbacks`**: Dropped sharply from 34.9 callbacks/frame down to 6.9 callbacks/frame. Because TMU timer evaluations only occur at slice boundaries in `Sh3Run_timerhack`, a 555.5 us interval is too coarse for the SH-3 TMU timers, causing timer event delivery to lag and guest task queues to pile up during heavy bullet storms.
   - **User report**: "这次出现了最低30帧的情况，上次是没有的" (Exact match with the 36.7 ms - 41.0 ms spikes observed in the log).

2. **Outcome**:
   - `nInterleave = 30` is rejected.
   - `nInterleave = 60` (`5126c8e6`) remains the optimal slice frequency on Xbox 360 hardware.

## Revert

Reverted via `git revert e71e8c09`.
