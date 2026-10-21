# CV1000: Harissa-Inspired SH-3 Idle Spin Loop Fast-Forwarding Across Scanline Slices

Baseline `4407eebe` (DRC NEGC implementation and delay slot expansion). Implementation of Harissa-inspired idle loop fast-forwarding in `libretro/FBNeo/src/burn/drv/cave/d_cv1k.cpp` and `libretro/FBNeo/src/cpu/sh4/sh4.cpp` (`Sh3GetRemainingCycles`), directly advancing the CPU cycle counter across active scanline slices during Cave speedhack polling loops rather than burning tiny 1,024-cycle chunks.

## Why

1. **Cave CV1000 Idle Loop Churn**:
   - Disassembly of the Cave CV1000 wait loop (e.g. `ddpdfk` at `0x0c1d1346`, `mmpork` at `0x0c05176a`) reveals a tight 4-instruction spin loop:
     ```asm
     0x0c1d1346:  add     #-1, r0
     0x0c1d1348:  cmp/eq  r0, r1
     0x0c1d134a:  bf      0x0c1d1346
     0x0c1d134c:  mov.l   @r2, r0     ! delay slot reading hacky_idle_ram (0x0c002310)
     ```
   - In upstream FBNeo, `speedhack_read_long` handled this by calling `Sh3BurnCycles(speedhack_burn)` where `speedhack_burn = 1024` cycles (~$10\,\mu\text{s}$).
   - With Salvia's restored `nInterleave = 240` scanline slices ($69.4\,\mu\text{s}$ or ~7,111 cycles per slice), the CPU spun ~7 times per scanline slice, repeatedly falling out to the dispatcher and invoking `drc_movll_service` tens of thousands of times per pause window (e.g., 73,782 calls in 3,600 frames).

2. **Harissa-Inspired Scanline-Slice Idle Skip**:
   - In `kernel64/HarissaDC` and `Harissa64`, when the dynamic recompiler or speedhack detects that the guest CPU is stuck spinning on a memory semaphore awaiting an interrupt or display event, it immediately fast-forwards through the remainder of the current event slice rather than grinding through the inner spin loop.
   - Because CV1000 uses 240 interleaved scanline slices, burning the remainder of the current slice (`Sh3GetRemainingCycles()`) yields the CPU directly to the next scheduled scanline event (TMU timer ticks, scanline raster interrupts, sound/blitter scheduling) without skipping any interrupt boundaries or timer events.

## What

1. **`libretro/FBNeo/src/cpu/sh4/sh4_intf.h` & `libretro/FBNeo/src/cpu/sh4/sh4.cpp`**:
   - Exported `INT32 Sh3GetRemainingCycles()`:
     ```cpp
     INT32 Sh3GetRemainingCycles()
     {
         return m_sh4_icount;
     }
     ```

2. **`libretro/FBNeo/src/burn/drv/cave/d_cv1k.cpp`**:
   - In `speedhack_read_long(UINT32 offset)`:
     ```cpp
     if ( offset == hacky_idle_ram && (pc == hacky_idle_pc || pc == hacky_idle_pc+2)) {
         INT32 rem = Sh3GetRemainingCycles();
         Sh3BurnCycles(rem > 0 ? rem : speedhack_burn);
     }
     ```
   - Guarded `g_benchmark_target_frames` with `#ifdef _XBOX` so host differential test harnesses link without missing symbol errors.

## Verification

1. **Host Differential Test Suites**:
   - `libretro/FBNeo/tests/sh3_dispatch/run_inline.py`:
     - **100% PASS**: 8 host suites, 442,729 test cases, ASan/UBSan clean.
   - `libretro/FBNeo/tests/sh3_ppc/run.py`:
     - **100% PASS**: 848,846 total test cases.

2. **Cave CV1000 Regression & Performance Benchmark**:
   - `cv1k-ddpdfk-profile-20261007/ab.sh` (DD-PDFK 60-frame deterministic benchmark):
     - **Emulation Parity**: Final state hash is 100% bit-identical: `0cf251c3d512ddbb` (0 divergence across all registers, RAM, VRAM, and blitter state).
     - **Wall Time**: Dropped from 19.08s to **15.89s (16.7% faster)**.
     - **System Time**: Dropped from 4.33s to **1.14s (73.7% reduction)** due to eliminating unnecessary spin loop exits and syscall roundtrips.

3. **Build and Distro360 Handoff**:
   - Baseline commit: `4407eebe`
   - Implementation commit: `98e3e597`
   - Image set compiled with `build-xex.sh --baseline 4407eebe --tag cv1k-idle-fastforward --flavor diag --force`.
   - **Candidate Image**: `Distro360/fbneo-cv1k-idle-fastforward-diag.xex`
     - Tag: `cv1k-idle-fastforward 98e3e597 20261021-1511 diag`
     - Size: `34,701,312` bytes
     - SHA256: `d70040351c44af848ad00f9c53414c5ce33128891729b3404938ada766f9b7fd`
     - Archive: `/home/humor/salvia-tests/xex-archive/fbneo-20261021-1511-cv1k-idle-fastforward-diag.xex`
   - **Baseline Image**: `Distro360/fbneo-drc-negc-slot-opt-diag.xex`
     - Tag: `drc-negc-slot-opt 4407eebe 20261020-2219 diag`
     - Size: `34,701,312` bytes
     - SHA256: `64656391115bb4cf723b26becbfa3fb78a0424785ce3a388da0491fe3a1d46c9`
     - Archive: `/home/humor/salvia-tests/xex-archive/fbneo-20261020-2219-drc-negc-slot-opt-diag.xex`
