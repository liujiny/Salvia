# CV1000: Fix DRC Sector 5-Bit Overflow and Enable 102.4MHz Unthrottled Timing

Baseline `2b2c3b68` (`feat(gpu,drc): vertical shelf packing for wide raster scanlines and expand DRC arena to 48MB`).

## 1. Real Hardware Telemetry Analysis (Nov 7 08:43 Run, Build `2b2c3b68`)

Following deployment of `cv1k-raster-war-fix`, analysis of `/Usb0/Salvia-360/cv1000-gpu.log` (13,970 frames) revealed:
1. **WAR Staging Flushes Eliminated**:
   - `war768` dropped from **1,911** down to **0**!
   - `draw_sync` dropped from 18.602 ms to 4.766 ms.
2. **Two Remaining Causes of Boss Explosion Slowdown**:
   - **Cause 1: 5-Bit Sector Field Overflow in `sh3_drc_ppc.h`**:
     - `cv1000-gpu.log` showed `evictions = 15` and `peak_bytes = 50315440` (out of 50331648 bytes) in interval 1.
     - With `CACHE_BYTES = 48MB` and `SECTOR_BYTES = 1MB`, `SECTOR_COUNT = 48`.
     - In `sh3_drc_ppc.h:1221 & 1238`, `slot_sector` is a `UINT8` where the low 5 bits (`0..31`) store the sector index:
       ```cpp
       if ((slot_sector[i] & 0x1Fu) != (UINT8)sector) continue;
       slot_sector[slot] = (UINT8)((((UINT32*)block.entry - code) / SECTOR_WORDS) | (sh3_block_end_reason << 5));
       ```
     - For sectors $\ge 32$, the sector index overflowed into the 3-bit block-end reason and aliased with sectors $0..15$, corrupting the table and causing 15 evictions and massive re-compilations (`cpu_io = 61.763 ms`).
   - **Cause 2: Guest Engine Underclock (78% / 79.872 MHz) + Blitter SDRAM Bus Stall (`Sh3BurnCycles`)**:
     - In `d_cv1k.cpp`, the default DIP was `0x07`, setting "Arcade PCB Fit (78% + Bus Stall)".
     - During the dense explosion sequence of the Stage 1 train boss, `epic12.cpp` called `Sh3BurnCycles(cycles)` on every blitter draw.
     - Coupled with the 78% clock underclock, this starved the guest SH-3 CPU instruction slice budget, forcing the Cave game engine to drop internal logic frames (guest arcade bullet-time slowdown).
     - Furthermore, `fbneo-audio.log` showed `tempo_current = 0.280`, heavily stretching audio and dragging playback.

---

## 2. Changes Made

1. **`libretro/FBNeo/src/cpu/sh4/sh3_drc_ppc.h`**:
   - Raised `SH3_PPC_SECTOR_BYTES` to `(2 * 1024 * 1024)` (2 MiB).
   - With 48 MiB cache, `SECTOR_COUNT = 24 <= 31`, perfectly fitting into the 5-bit sector index without overflow or aliasing.
2. **`libretro/FBNeo/src/burn/drv/cave/d_cv1k.cpp`**:
   - Changed default DIP B from `0x07` to `0x0f` (`Unthrottled (100% 102.4MHz)`).
   - Set "Unthrottled (100% 102.4MHz)" as the default option in `DefaultDIPList`.
   - Bypasses blitter SDRAM bus stall `Sh3BurnCycles` and runs SH-3 CPU at full 102.4 MHz, providing abundant cycle budget for boss explosions without game-engine dragging.

---

## 3. Verification

1. `libretro/FBNeo/tests/sh3_dispatch/run_inline.py`: **100% PASS** (all 8 host suites, ASan/UBSan clean).
2. `libretro/FBNeo/tests/sh3_ppc/run.py`: **100% PASS** (848,846 cases compiled 750,178 fallback 98,668).
3. `libretro/FBNeo/tests/epic12_gpu/run.py`: **100% PASS** (all 17 suites, 10.5M component cases, 0 WAR flushes).
