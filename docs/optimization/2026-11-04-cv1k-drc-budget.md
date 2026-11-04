# CV1000: DRC Elimination of Slice-End Budget Thrashing and Allowance of Native Tail Execution

Baseline `df5fc83e` (Audio IDCT32 and speedhack hoist). Implementation commit `2df61411`.

## Why

1. **Slice-End Budget Bailing Thrashing in DRC**:
   - In Cave CV1000 emulation, the frame execution loop runs 240 interleaved timeslices per frame (~3,500 to 7,000 guest SH-3 cycles per slice).
   - In `sh3_drc_dispatch.h`, when the remaining slice budget `m_sh4_icount` was less than the cycle cost of the next compiled block (`m_sh4_icount < cycles`, typically 1 to 15 cycles remaining at the tail of a slice), the dispatcher previously returned `false` with fallback origin `SH3_FB_BUDGET`.
   - Because `sh3_drc_dispatch_impl` returned `false`, the outer execution loop in `Sh3Run_timerhack_impl` (`sh4.cpp:4849`) treated this as an emulation fallback and dropped through to the C++ interpreter to execute **only a single guest instruction** (`EAT(1)`).
   - Crucially, after that single interpreter step, `m_sh4_icount` was still positive (e.g. decremented from 8 to 7). The outer `do { ... } while (m_sh4_icount > 0)` loop repeated, re-invoking `sh3_drc_dispatch_impl`.
   - In `sh3_drc_dispatch_impl`, the CPU performed a full hash table lookup (`sh3_drc_lookup4`), tag comparison, source stamp comparison, and found the valid compiled block in cache—only to fail the `m_sh4_icount < cycles` check **again**!
   - This failure loop repeated for every single remaining cycle in the slice budget, generating up to 10–15 full hash lookups, validations, and interpreter dispatches per slice.

2. **Real Hardware Evidence (`cv1000-gpu-latest.log` on *Ibara*)**:
   - Across 43,055 gameplay frames captured on Xbox 360 Jasper hardware:
     - `short_budget`: **19,171 events per pause window** (dominating all other exit reasons).
     - `interpreter_steps`: **61,200 steps per pause window** (averaging ~3 to 4 single-step loops per `short_budget` bail).
     - Interpreter opcode breakdown confirmed the single-stepped instructions were standard, fully compiled instructions (such as `0x27` `MOV.L Rm, @-Rn`, `0x60` `MOV.B @Rm, Rn`, `0x22` `MOV.B Rm, @Rn`), proving the fallback was purely budget-induced rather than due to missing opcode translation or hardware faults.
     - Slow peak frames reaching 25.160 ms were dominated by `dominant_cpu_io` due to this interpreter thrashing at slice boundaries.

3. **Lessons from `ibara360` (`runtime/recomp.h`) and MAME DRC Accounting**:
   - In `ibara360` (`recomp.h:262-272`), recompiled blocks decrement cycles first (`IC -= (n)`) and only check for timeslice expiration when `IC < 0`. Blocks are never refused execution beforehand.
   - In FBNeo, continuous cycle accounting across slices and frames is explicitly designed to handle negative `m_sh4_icount`:
     - `sh4.cpp:4895`: `cycles = cycles - m_sh4_icount;` correctly accounts for any cycle overrun (`cycles - (-7) = cycles + 7`).
     - `sh4_run_timers(cycles)` advances hardware timers by the exact number of elapsed cycles.
     - `d_cv1k.cpp:844`: `nExtraCycles[0] = nCyclesDone[0] - nCyclesTotal[0];` carries over any remaining frame residual, and `CPU_RUN` automatically compensates by reducing the requested cycles in the next slice.
   - A typical block is 1 to 10 instructions (at most 32 instructions = ~35 cycles), representing < 0.5% of a 7,000-cycle slice. Executing the block natively takes ~15 nanoseconds on Xenon, completely eliminating ~1,200 CPU cycles of repeated interpreter fallbacks and hash lookups.

## What

1. **`libretro/FBNeo/src/cpu/sh4/sh3_drc_dispatch.h`**:
   - Hoisted the budget check to the dispatcher entry gate:
     ```cpp
     if(m_sh4_icount<=0) {
      if(Count) { ++sh3_drc_work.exit_budget; sh3_drc_work.fallback.last_origin=SH3_FB_BUDGET; }
      return false;
     }
     if(failed || (!blocks && !allocate())) {
      if(Count) { ++sh3_drc_work.exit_gate; sh3_drc_work.fallback.last_origin=SH3_FB_GATE; }
      return false;
     }
     ```
   - Removed the mid-dispatch `if(m_sh4_icount<cycles)` rejection. When `m_sh4_icount > 0`, compiled blocks execute natively.
   - When the block completes, `m_sh4_icount` reflects the cycle cost. If `m_sh4_icount <= 0`, the chained while loop (`while (Chained && m_sh4_icount > 0 ...)`) terminates naturally, returning `true` with `exit_boundary`.
   - In `Sh3Run_timerhack_impl`, `dispatchOk` is `true`, `continue` executes, and the outer loop cleanly exits because `m_sh4_icount <= 0`, with zero interpreter fallback.

2. **`libretro/FBNeo/tests/sh3_dispatch/test.cpp`**:
   - Aligned the oracle reference dispatcher `reference_dispatch()`:
     ```cpp
     if(!b.entry || m_sh4_icount<=0)return false;
     ```
   - Updated `fallback_origin_checks()` to test mutually-exclusive fallback triggers:
     - `which == 0`: `Sh3Ppc::failed = true` -> `SH3_FB_GATE`
     - `which == 1`: `m_pc = 1` -> `SH3_FB_FETCH`
     - `which == 2`: `ram[0][0] = 0` -> `SH3_FB_NO_ENTRY`
     - `which == 3`: `m_sh4_icount = 0` -> `SH3_FB_BUDGET`
     - `which == 4`: `ram[0][0] = 0x3100` -> `SH3_FB_PARTIAL`

## Verification

1. **Host Differential & Sanity Suites**:
   - `python3 libretro/FBNeo/tests/sh3_dispatch/run_inline.py`:
     - All 8 host test suites passed in both `-O3` and `-O1 -fsanitize=address,undefined` modes.
     - 12,000 randomized traces $\times$ 12 slices (144,000 slices): original, single, and fused dispatcher states, memory, mappings, callback order, and cycles are 100% bit-for-bit identical.
     - Shadow dispatch records answered 436,090 entries (up from 427,690) with recompiles dropping from 439,247 to 431,331.
   - `python3 libretro/FBNeo/tests/ymz770_idct/run.py`:
     - 100,000 subband vectors bit-identical (`max diff = 0.000000e+00`).
   - `python3 libretro/FBNeo/tests/frontend_state/run.py`:
     - Full state streaming, truncation, and legacy tests passed.

2. **Cross-Compiled PowerPC Differential Suite**:
   - `python3 libretro/FBNeo/tests/sh3_ppc/run.py --toolchain-root /home/humor/salvia-tests/toolchains/ppc/root`:
     - 848,846 total test cases executed under QEMU PPC:
       - Baseline: `compiled 729787 fallback 119059`
       - Candidate: `compiled 750178 fallback 98668`
       - **20,391 fallbacks completely eliminated** and converted into native PowerPC execution!
       - All edge cases (GBR stack, long multiply, carry/borrow, delay slots, memory operands, MOV.L service) passed 100%.


## Build Artifacts & Evidence

- **Baseline Commit**: `df5fc83e9315d4d64e631ed74387fba1e7b1b06c`
- **Implementation Commit**: `2df6141186036ab5e8edf002ee758a42c5d019f8`
- **Build Tag**: `cv1k-drc-budget`
- **Diagnostic Flavor XEX**:
  - Path: `Distro360/fbneo.xex`
  - Archive: `/home/humor/salvia-tests/xex-archive/fbneo-20261104-2136-cv1k-drc-budget-diag.xex`
  - Size: 34,832,384 bytes
  - SHA256: `0924f81ba84fb0f28d0f16e756ffbd2ea09896e7a05da79a647aaf2eea421226`
- **Reviewed Mirrored Paths**:
  - `libretro/FBNeo/src/cpu/sh4/sh3_drc_dispatch.h` (SHA256: `c43519b695ade3f90c8302d5f59b18ac3501f7762d02a9b92e4a136207b1408b`)
  - `libretro/FBNeo/tests/sh3_dispatch/test.cpp` (SHA256: `3aace50d8516a64ce770bcbcff19dbca45110bbb6681171463bcd7baf1724d83`)
