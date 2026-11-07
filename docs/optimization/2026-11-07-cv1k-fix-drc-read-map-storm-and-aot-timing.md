# CV1000 DRC: Fix Read-Map Validation Storm and In-Game AOT Timing

## Baseline
- Baseline commit: `3cecd5955ac7605efbf3dc03904f217200e61d25`
- Target console: Xbox 360 Jasper (67MB available heap, unthrottled PPC DRC)
- Test cases: DoDonPachi SaiDaiOuJou (`ddpsdoj`) stage 1 opening entrance and mid-stage explosion state (`ddpsdoj.state1`).

## Problem Description
Console diagnostics on `ddpsdoj.state1` revealed severe performance stalls dropping framerate below 20 FPS:
```
[drc-rebuild] conflict=16 source=0 map=20480
[drc-arena] peak=33554432 evictions=150
```
Two distinct bugs caused the multi-second freeze during gameplay:
1. **DRC Read-Map Validation Failure Storm**: In `sh3_drc_dispatch.h`, delay loops (`DT` opcodes) set `check_read_map = true`. The validation checked `MemMapR[phys >> SH3_SHIFT] == page`. In CV1000 drivers, `0x0c000000..0x0c00ffff` is mapped with `Sh3MapHandler(1, ...)` (speedhack idle loop handler), so `MemMapR` contains `(UINT8*)1`, while `page` (`MemMapF`) is `DrvMainRAM`. Because `MemMapR != page`, `map_ok` failed 100% of the time, triggering 20,480 recompilations and 150 full 32MB cache evictions in seconds.
2. **In-Game AOT Timing Stutter**: In `d_cv1k.cpp`, `Sh3DrcPrecompile` was scheduled inside `DrvFrame()` at `nCurrentFrame >= 60` or `180`. This stalled the main emulation loop right as the player aircraft entered the screen at stage 1.

## Changes Made
1. **Mirror-Aware Read-Map Check**:
   In `libretro/FBNeo/src/cpu/sh4/sh3_drc_dispatch.h`, added `sh3_drc_check_read_map(phys, page)`:
   ```cpp
   static inline bool sh3_drc_check_read_map(UINT32 phys, const UINT8 *page)
   {
       const UINT8 *r = MemMapR[phys >> SH3_SHIFT];
       if (r == page) return true;
       if (sh3_ppc_state.read_mirror &&
           (uintptr_t)r == (uintptr_t)sh3_ppc_state.mirror_handler &&
           (phys & ~(unsigned)SH3_PAGEM) == sh3_ppc_state.mirror_page &&
           sh3_ppc_state.read_mirror == page) {
           return true;
       }
       return false;
   }
   ```
   Replaced direct `MemMapR[phys >> SH3_SHIFT] == page` comparisons in shadow and table validation with this helper.
2. **Cold-Boot AOT Trigger**:
   In `libretro/FBNeo/src/burn/drv/cave/d_cv1k.cpp`, trigger `Sh3DrcPrecompile` immediately when fastboot unpacks and skips the RAM test during black-screen boot (`was_pending && !cv1k_boot_pending`), before any title screens or gameplay frames render. For non-fastboot sets, precompile during the boot logo sequence (`nCurrentFrame >= 300`).
3. **Unit Tests**:
   Added `read_mirror_validation_checks()` in `tests/sh3_dispatch/test.cpp` verifying direct matches, mirror handler matches, and rejection on handler/page/RAM pointer mismatches.

## Validation Results
- `tests/sh3_dispatch/run_inline.py`: PASS (all 8 host suites in optimized and ASan/UBSan modes, including new `read_mirror_validation_checks`).
- `tests/sh3_ppc/run.py`: PASS (all 848,846 cases compiled and verified via QEMU PPC).
- Verification limits: Host synthetic and QEMU tests verify logic correctness; Xbox 360 FPS and freeze elimination require console runtime verification.
