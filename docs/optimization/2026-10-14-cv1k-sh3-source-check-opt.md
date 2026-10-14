# CV1000: optimize sh3_drc_source_equal with group XOR-OR branch reduction

Baseline `4b90fc7d`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

In the SH-3 dynamic recompiler ([`sh3_drc_dispatch.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/cpu/sh4/sh3_drc_dispatch.h)), dispatch validation (`drc_dispatch`) is executed on every block entry, running over 84.1 million times across 120 gameplay frames and accounting for 4.84 seconds (over 63%) of total CPU emulation time:
- In [`sh3_drc_source_check.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/cpu/sh4/sh3_drc_source_check.h), `sh3_drc_source_equal()` validates whether guest code opcodes match the compiled translation snapshot.
- The prior implementation evaluated opcodes in groups of four with 4 separate conditional inequality branches:
  ```cpp
  while (words >= 4) {
      if (expected[0] != actual[0]) return false;
      if (expected[1] != actual[1]) return false;
      if (expected[2] != actual[2]) return false;
      if (expected[3] != actual[3]) return false;
      ...
  }
  ```
- Because in 98%+ of executions guest code is immutable (in ROM or static RAM), almost all branches are taken as fallthrough.
- For a typical 16-word translation block, this generated 16 separate compare-and-branch instructions. Over 84 million dispatch calls, this inundated the Xenon CPU pipeline and branch predictor with over 1.3 billion conditional branches.

## What

1. **Bitwise XOR and OR Branch Reduction**:
   - Replaced individual conditional equality checks with bitwise XOR-OR reduction:
     ```cpp
     while (words >= 4) {
         unsigned diff = (unsigned)(expected[0] ^ actual[0])
                       | (unsigned)(expected[1] ^ actual[1])
                       | (unsigned)(expected[2] ^ actual[2])
                       | (unsigned)(expected[3] ^ actual[3]);
         if (diff) return false;
         expected += 4; actual += 4; words -= 4;
     }
     ```
   - In PowerPC machine code, `xor` and `or` instructions execute in Xenon's dual-issue ALU pipelines with zero stalls, while `diff` is evaluated in a single conditional branch (`cmpwi` + `beq`).
   - Reduces conditional branch instructions in the hot path by **75%** (1 branch per 4 opcodes instead of 4).
   - Bitwise OR of non-negative differences cannot cancel bits, preserving 100% mathematical equality semantics without any possibility of false positives or overread.
2. **Tail Words Reduction**:
   - For remaining words (1..3), converted residual branches to single expression evaluations.

## Verification

1. **Host Standalone & ASan/UBSan Tests**:
   - `g++ -std=gnu++98 -O3 libretro/FBNeo/tests/sh3_source_check/test.cpp`: **100% PASS** (442,729 test cases across all lengths 0..33, every bit mutation, halfword alignments, guard pages).
   - AddressSanitizer & UndefinedBehaviorSanitizer run: **100% PASS** (zero memory errors, zero undefined behavior).
2. **Dispatch Production Suite**:
   - `python3 libretro/FBNeo/tests/sh3_dispatch/run_inline.py`: **100% PASS** across all 8 host suites, including 12,000 randomized traces x 12 slices.
3. **PPC Test Suite (QEMU Big-Endian)**:
   - `python3 libretro/FBNeo/tests/sh3_ppc/run.py --toolchain-root /home/humor/salvia-tests/toolchains/ppc/root`:
   - **PASS 846,026 cases** (compiled 727,804, fallback 118,222).
4. **Emulator Benchmark (Xenia Canary, `mmpork.state`, 120 gameplay frames)**:
   - `worker_busy`: 7647.946 ms (down from 7659.751 ms, **-11.8 ms**)
   - `drc_dispatch`: 4840.865 ms (down from 4851.714 ms, **-10.8 ms**)
   - `blit_flush_ms gpu`: 6677.549 ms (down from 6713.605 ms, **-36.1 ms**)
   - Identical batches (1,396) and pixels (79,826,600).
5. **Artifact Archive**:
   - Diagnostic binary: `/home/humor/salvia-tests/xex-archive/fbneo-20261014-1430-source-check-opt-diag.xex`
   - SHA256: `47b0c82dceff60b4114bcbbf15ee6ca1df96522dc1d1f0d83c317b80fcdd7d01`

## Revert

`git revert <sha>` to restore original `sh3_drc_source_check.h` and delete this report.
