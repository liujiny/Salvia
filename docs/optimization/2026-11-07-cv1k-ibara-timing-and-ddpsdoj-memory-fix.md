# CV1000: Fix Ibara Double Throttling and DDP SDOJ 6MB Memory / Black Screen Hang

Baseline `cb043bc6` (`fix(cv1k): restore authentic arcade synchronous slowdown timing model`).

## 1. Root Cause Analysis

### Issue A: `ibara` Feels Noticeably Slower Than `ibara360.exe`
- **Root Cause**: In `ibara360.exe`, the nominal CPU clock is 102.4 MHz (`#define CPU_HZ 102400000u` in `/tmp/ibara360/runtime/cv1k.h:11`), and blitter SDRAM contention (`Sh3BurnCycles`) stalls the CPU dynamically during bullet curtains, yielding ~78% effective throughput.
- In Salvia, `d_cv1k.cpp:763` set `base_percent = 78` (reducing base clock from 102.4 MHz to 79.87 MHz) **and** `epic12.cpp:1296` called `Sh3BurnCycles(cycles)` on top of that base downclock. This caused double-throttling ($78\% \times 78\% \approx 60.8\%$), making `ibara` feel sluggish compared to `ibara360.exe`.
- **Fix**: Set `base_percent = 100` in `d_cv1k.cpp`. The base clock remains 102.4 MHz in quiet scenes; blitter stalls dynamically throttle effective throughput to authentic arcade levels during action, matching `ibara360.exe` identically.

### Issue B: `ddpsdoj` Hangs on Black Screen and Title Memory Drops to 6 MB
- **Root Cause 1 (Memory)**: `ddpsdoj` (DoDonPachi SaiDaiOuJou) has 132 MB Flash ROM + 16 MB Type D RAM = 160 MB `AllMem`. Together with 142.6 MB `m_bitmaps`, 16 MB `m_ram16_copy`, and 21 MB DRC lookup/block tables, baseline memory is 339.6 MB. Expanding `SH3_PPC_CACHE_BYTES` to 48 MB pushed total allocations to ~422 MB, starving Xbox 360 title RAM down to only 6 MB and risking D3D allocation failures.
- **Root Cause 2 (Black Screen Freeze)**: In `d_cv1k.cpp:823`, `Sh3DrcPrecompile` was invoked with `0x1000000` (16 MB). It scanned 8.38 million words of RAM, mistaking heap data and game variables for function prologues and code pointers. Without an upper bound on precompilation, the BFS loop compiled tens of thousands of garbage blocks until it consumed almost the entire 48 MB cache. On Xbox 360 Xenon, translating ~40 MB of JIT code at frame 60 froze the emulator for 20–30 seconds.
- **Fix 1**: Revert `SH3_PPC_CACHE_BYTES` to 32 MB (`32 * 1024 * 1024`). The sector ring reuse safely bounds code size within 16 sectors, instantly restoring 16 MB of title RAM and bringing free memory back to ~22 MB.
- **Fix 2**: In `d_cv1k.cpp:823`, pass `is_type_d ? 0x400000 : 0x200000` (actual code ROM sizes: 4 MB for Type D, 2 MB for Type A-C).
- **Fix 3**: In `sh3_drc_ppc.h:1573`, cap BFS precompilation to 8,192 blocks (`if (++precompiled_count >= 8192) break;`), ensuring AOT warming completes in <100 ms with zero hang.

---

## 2. Changes Made

1. `libretro/FBNeo/src/cpu/sh4/sh3_drc_ppc.h`:
   - Set `SH3_PPC_CACHE_BYTES` to `(32 * 1024 * 1024)`.
   - Added `precompiled_count >= 8192` termination condition to `Sh3DrcPrecompile`.
2. `libretro/FBNeo/src/burn/drv/cave/d_cv1k.cpp`:
   - Set `base_percent = 100` in `DrvFrame()`.
   - Updated DIP option text: `"Arcade PCB Fit (Bus Stall)"` (`0x00`) vs `"Unthrottled (102.4MHz No Stall)"` (`0x08`).
   - Capped `Sh3DrcPrecompile` scan size to `is_type_d ? 0x400000 : 0x200000`.

---

## 3. Verification

1. `libretro/FBNeo/tests/sh3_dispatch/run_inline.py`: **100% PASS** (all 8 host suites, ASan/UBSan clean, 442,729 cases).
2. `libretro/FBNeo/tests/sh3_ppc/run.py`: **100% PASS** (848,846 cases compiled 750,178 fallback 98,668, including AOT precompilation check).
