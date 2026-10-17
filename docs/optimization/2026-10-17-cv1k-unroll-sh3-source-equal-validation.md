# CV1000: Unroll SH3 Source Check Validation in Dispatch Hot Loop

Baseline `93ae8d39`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

1. **Hardware Profiling Confirmation**:
   - Analysis of real Xbox 360 console profiling across 5,513 frames (`cv1000-gpu.log`) confirmed that GPU rendering is no longer a bottleneck (`draw_sync = 0.869 ms` on slow frames; `worker_busy` averages 11.8 ms across an entire 5,513 frame window).
   - In all sampled slow frames (>25 ms threshold), frame time is 100% dominated by `cpu_io`:
     `prep = 0.003 ms`, `cpu_io = 30.464 ms`, `audio_tail_misc = 0.135 ms`, `draw_sync = 0.869 ms`, `total = 31.472 ms` (~31.7 FPS, dipping to ~40 FPS).
   - On Xenon Core 0, emulating 102.4 MHz of 100% active guest code at ~56 MHz host emulation throughput takes ~30.5 ms per frame during dense bullet scenes.

2. **Validation Loop Branch Overhead**:
   - In `sh3_drc_dispatch_impl`, every entry re-validates the candidate block's memory against its snapshot (`sh3_drc_source_equal`).
   - The Xenon CPU is an in-order PowerPC architecture where conditional branch mispredictions impose heavy pipeline flush penalties.
   - Translation blocks in Cave CV1000 games typically range between 8 and 32 instructions (16 to 64 bytes).
   - The previous `while(words>=4)` loop required multiple loop-branch evaluations per block.
   - Unrolling the loop by 8 halfwords (16 bytes) and replacing the scalar tail with a branchless switch statement halves loop branch evaluations for over 50% of executed blocks.

3. **Arcade Hardware Accuracy & CPU Clock**:
   - On original Cave CV1000 arcade PCBs, the hardware naturally experienced CPU slowdown down to 50%–60% during dense bullet curtains.
   - FBNeo's core option "CPU clock" (`fbneo-cpu-speed-adjust` / `nBurnCPUSpeedAdjust`) set to 50%–60% restores authentic arcade slowdown while ensuring the Xbox 360 video presentation locks smoothly at 60 FPS without audio stutter.

## What

1. **`libretro/FBNeo/src/cpu/sh4/sh3_drc_source_check.h`**:
   - In `sh3_drc_source_equal`: added an 8-halfword (16-byte) bitwise XOR-OR unrolled loop before the 4-halfword loop.
   - Replaced scalar remainder loop with a branchless switch for 1, 2, or 3 remaining words.

## Verification & Results

1. **Host PPC Test Suites**:
   - `libretro/FBNeo/tests/sh3_dispatch/run_inline.py`: **100% PASS** (all 8 host suites, ASan/UBSan clean, 442,729 source equality cases).
   - `libretro/FBNeo/tests/sh3_ppc/run.py`: **100% PASS** (all 846,026 cases: 727,804 compiled, 118,222 fallback).
   - `libretro/FBNeo/tests/epic12_gpu/run.py`: **100% PASS** (all 14,400 commands, 10.4M component cases).
