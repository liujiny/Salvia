# CV1000: Restore Authentic Arcade Synchronous Slowdown Timing Model

Baseline `95db7fe0` (`fix(drc,cv1k): fix 5-bit sector index overflow with 2MB sectors and default to 102.4MHz unthrottled`).

## 1. Real Hardware Telemetry Analysis (Nov 7 09:10 Run, Build `95db7fe0`)

Following deployment of `cv1k-2m-sector-unthrottled`, telemetry fetched from `/Usb0/Salvia-360/cv1000-gpu.log` revealed:

1. **2MB Sector Size Fix Succeeded**:
   - `drc_work_arena bytes=50331648, evictions=0, evicted_slots=0, peak_bytes=39704544` in the second interval.
   - The 5-bit sector field overflow was completely resolved with `SH3_PPC_SECTOR_BYTES = (2 * 1024 * 1024)` (24 sectors $\le 31$).
2. **WAR Staging Flushes Remained at 0**:
   - `war768 = 0` in all intervals.
3. **Failure Analysis of Unthrottled 102.4MHz Timing Model**:
   - Forcing `DrvDips[1] = 0x0f` (`Unthrottled 100% 102.4MHz`) disabled blitter SDRAM bus contention stalls (`Sh3BurnCycles`) and forced the SH-3 clock to 102.4 MHz.
   - **Loss of Authentic Arcade Slowdown**: In Cave CV1000 arcade PCBs, blitter bus contention stalls the SH-3 CPU during heavy explosions and bullet curtains, causing game logic to deliberately run at 20–30 ticks/s while CRT video output stays locked at 60 Hz. Disabling bus stalls destroyed this authentic arcade slowdown ("原机同步拖慢").
   - **Host Frame Drops and Jitter**: Without bus contention throttling, the game engine flooded the frame with massive blit commands—submitting **632,559 draws in 2,686 frames** (235 draws/frame, vs 68 draws/frame in the fit model).
   - This command surge overwhelmed the Xenos GPU command buffer, causing `draw_sync` to explode to **75.397 ms** and peak host active loop to **87.865 ms** (~11.5 FPS host drop), causing the host display to stutter severely ("帧数不能稳60了").

---

## 2. Changes Made

1. **`libretro/FBNeo/src/burn/drv/cave/d_cv1k.cpp`**:
   - Reverted default DIP B back to `0x07` in `Cv1kDIPList` and `Cv1ksDIPList`.
   - Restored "Arcade PCB Fit (78% + Bus Stall)" (`0x00`) as the primary default in `DefaultDIPList`.
   - Preserves authentic Cave arcade synchronous slowdown via `Sh3BurnCycles(cycles)` when the blitter is active, while keeping blit submission rates within the Xenos GPU real-time 16.67ms processing budget.
2. **Retained Fixes**:
   - Kept `SH3_PPC_SECTOR_BYTES (2 * 1024 * 1024)` in `sh3_drc_ppc.h` (0 DRC evictions).
   - Kept vertical shelf packing in `epic12_gpu.h` (0 WAR flushes on Bank 768).

---

## 3. Verification

1. `libretro/FBNeo/tests/sh3_dispatch/run_inline.py`: **100% PASS** (all 8 host suites, ASan/UBSan clean, 442,729 cases).
2. `libretro/FBNeo/tests/sh3_ppc/run.py`: **100% PASS** (848,846 cases compiled 750,178 fallback 98,668).
3. `libretro/FBNeo/tests/epic12_gpu/run.py`: **100% PASS** (10,485,760 component cases, 14,400 commands, 0 WAR flushes).
