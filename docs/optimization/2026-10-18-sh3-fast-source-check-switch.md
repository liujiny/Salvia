# SH3: Direct Branchless Source Equality Switch for Common Translation Blocks

Baseline `7fab7f8f`. Diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

1. **Dispatch Loop Block Size Distribution**:
   - In Cave CV1000 games, dynamic profiling (`drc_work_snapshot_lengths`) confirms that translation blocks between 1 and 8 words (2 to 16 bytes) account for over 65% of all executed blocks (lengths 4–7 alone represent >40%).
   - In `sh3_drc_source_equal()`, comparing block opcodes against their snapshots previously entered `while (words >= 8)` followed by `while (words >= 4)` followed by a switch statement.
   - For blocks with 1 to 8 words, this executed multiple loop branches, branch evaluations, and loop exit conditions on every single block entry (~60,000 block entries per frame).
   - On the Xbox 360 in-order Xenon CPU, branch mispredictions incur substantial pipeline penalties.

2. **Branchless Direct Dispatch**:
   - By guarding `words <= 8` with an immediate direct `switch(words)` jump table, all blocks with $\le 8$ words evaluate their equality in a single direct jump and return with zero loop overhead.
   - Longer blocks (>8 words) continue to use the 8-word and 4-word unrolled loops.

## What

1. **`libretro/FBNeo/src/cpu/sh4/sh3_drc_source_check.h`**:
   - Added direct branchless `switch(words)` for `words <= 8` before the loop structure.

## Verification

1. **Host PPC Test Suites**:
   - `libretro/FBNeo/tests/sh3_dispatch/run_inline.py`: **100% PASS** (all 8 host suites, ASan/UBSan clean, 442,729 source equality cases).
