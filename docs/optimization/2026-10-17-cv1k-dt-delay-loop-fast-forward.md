# CV1000: Fast-Forward SH3 DT/BFS Delay Loops in Coin and Character Select

Baseline `802340b4`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

1. **Hardware Profiling & User Observation**:
   - The user reported severe frame rate drops during coin insertion and character selection: *"我还注意到开始投币及刚刚进入游戏选择人物的时候帧数很低，。CPU clock 调整为 60%还是一样"* (Also noticed when inserting coins and entering character select the frame rate is very low, and adjusting CPU clock to 60% had no effect).
   - In real hardware diagnostics (`cv1000-gpu.log`), the sampled peak spiked to **70.946 ms at frame 29** with `cpu_io = 69.974 ms`.
   - The diagnostic log explicitly identified the fallback hotspot at frame 29:
     - `pc=0C05C1DC opcode=4910 (DT R9)`
     - `pc=0C05C1CA opcode=4910 (DT R9)`
     - `pc=0C05C1B6 opcode=0009 (NOP)`
     - `pc=0C05C1CC opcode=8FFC (BF/S -4)`

2. **Root Cause Analysis**:
   - Cave CV1000 games communicate with the on-board Serial EEPROM / RTC9701 via bit-banging (`0x10C00001`). Whenever a coin is inserted (writing coin audit counters) or character select is entered (reading/writing credits and NVRAM high scores), the game executes delay loops.
   - For every bit transition, guest code loads `R9 = 6600` (`0x19C8`) and executes the 4-instruction loop:
     ```
     NOP
     DT R9
     BF/S -4
     NOP
     ```
   - In upstream FBNeo, an existing busy loop hack in `sh4.cpp:1751` handled `DT Rn; BF -1` (`0x8BFD`). However, Cave CV1000 games use `BF/S -4; NOP` (`0x8FFC; 0x0009`) and `BF/S -3; NOP` (`0x8FFD; 0x0009`).
   - In PPC DRC (`sh3_drc_ppc.h`), line 1260 only checked for `source[count+1] == 0x8bfd`. Because `0x8ffc` was unhandled, DRC compiled this delay loop as a native self-loop.
   - On the Xbox 360's in-order Xenon CPU, the compiled block executed 5 memory stores, 2 memory loads, and an `mfcr` serialization stall on every single iteration.
   - Over 12 bit-banging loops per bit $\times$ 512 bits $\times$ 6,600 iterations, the Xenon CPU spent over **1.6 billion host cycles** (~500 ms) stalled in load-hit-store and pipeline flushes.
   - Because `R9 = 6600` is hardcoded in the guest ROM code, throttling `fbneo-cpu-speed-adjust` from 100% to 60% had zero effect on the iteration count.

## What

1. **`libretro/FBNeo/src/cpu/sh4/sh4.cpp` (`BUSY_LOOP_HACKS`)**:
   - Added detection for CV1000 delay loop patterns in `DT()`:
     - `0x8ffd` followed by delay slot `NOP` (`0x0009`): 4 cycles per iteration.
     - `0x8ffc` followed by delay slot `NOP` (`0x0009`) with target `NOP` (`0x0009`): 5 cycles per iteration.
     - `0x8bfd` (`BF -1`): 4 cycles per iteration.
   - Replaced iterative C++ while loop with $O(1)$ arithmetic:
     - `count = min(m_r[n] - 1, (m_sh4_icount - 1) / step_cycles)`
     - Fast-forwards `m_r[n]`, decrements `m_sh4_icount`, and increments `sh3_total_cycles` in $O(1)$ time while strictly preserving exact guest cycle timing, interrupt eligibility, and final register states (`Rn = 0`, `T = 1`).

2. **`libretro/FBNeo/src/cpu/sh4/sh3_drc_ppc.h`**:
   - In `compile()`, extended the DT lookahead check to recognize `0x8ffd` and `0x8ffc` delay loops.
   - Breaks block compilation on busy loops so that the lightweight $O(1)$ interpreter hack handles the countdown instead of churning millions of PPC DRC memory stores and `mfcr` instructions.
   - Correctly marks `validate` for the full 3-word span of the delayed branch sequence to preserve code-write protection.

3. **`libretro/FBNeo/tests/sh3_ppc/test.cpp`**:
   - Added validation test cases for `0x8ffd` and `0x8ffc` lookahead invalidation and code-write detection.

## Verification & Results

1. **Host PPC Test Suites**:
   - `libretro/FBNeo/tests/sh3_dispatch/run_inline.py`: **100% PASS** (all 8 host suites, ASan/UBSan clean).
   - `libretro/FBNeo/tests/sh3_ppc/run.py`: **100% PASS** (all 846,030 cases: 727,804 compiled, 118,226 fallback).
   - `libretro/FBNeo/tests/epic12_gpu/run.py`: **100% PASS** (all 14,400 commands, 10.4M component cases).
   - Standalone simulation test: verified 100% bit-for-bit cycle and register equivalence across varied slice sizes (10 to 100,000 cycles).

2. **Artifacts**:
   - Pending XEX build and console testing.
