# CV1000: Harissa/Ibara-Inspired Blitter Busy Spin-Loop Fast-Forwarding and Branchless Fixed Staging Blend

Baseline `1dedf6ac` (upstream develop merge). Implementation commit `779a9a16`.

## Why

1. **Cave CV1000 Blitter Busy Polling Churn**:
   - In Cave CV1000 games, when the CPU kicks off a hardware blitter command list, it polls register `0x18000010` awaiting blitter completion.
   - Upstream FBNeo handled `m_blitter_busy` by calling `Sh3BurnCycles(m_burn_cycles)` where `m_burn_cycles = 1024` cycles (~$10\,\mu\text{s}$).
   - With Salvia's 240 scanline slices (~7,111 cycles per slice), the CPU spun ~7 times per slice, repeatedly exiting and re-entering the DRC dispatcher, generating thousands of unnecessary context transitions and timer evaluations per frame.
   - Fast-forwarding by the remaining slice cycles (`Sh3GetRemainingCycles()`) yields the CPU directly to the next scheduled scanline event, advancing the blitter delay timer in $O(1)$ without dropping any hardware timer interrupts.

2. **Branchless Alpha Blend for In-Order Xenon PPC**:
   - In `epic12_fast_blit.h`, rendering mixed transparent and opaque 4-pixel and 2-pixel chunks previously navigated up to four nested levels of conditional branches (`if ((pen0 & pen1) & 0x20000000)`, `else if (pen0 & ...)`, etc.).
   - On the Xbox 360's Xenon in-order processor, every branch misprediction stalls the execution pipeline for 24 clock cycles. In scenes with dense bullet patterns and complex sprite outlines, these branch bubbles severely degrade software blitting and staging throughput.
   - Following `ibara360`'s arithmetic mask approach, transparent pixel selection is vectorized branchlessly:
     ```cpp
     UINT32 m = 0 - ((pen >> 29) & 1);
     out[i] = (res & m) | (orig & ~m);
     ```
     This executes purely as straight-line ALU instructions (`rlwinm`, `subf`, `and`, `andc`, `or`), completely eliminating branch mispredictions while maintaining 100% bit-for-bit output equivalence.

## What

1. **`libretro/FBNeo/src/burn/devices/epic12.cpp`**:
   - In `epic12_blitter_read(0x10)`:
     ```cpp
     case 0x10:
         if (m_blitter_busy)
         {
             if (sleep_on_busy) {
                 INT32 rem = Sh3GetRemainingCycles();
                 Sh3BurnCycles(rem > 0 ? rem : m_burn_cycles);
             }
             return 0x00000000;
         }
         else
             return 0x00000010;
     ```

2. **`libretro/FBNeo/src/burn/devices/epic12_fast_blit.h`**:
   - Replaced multi-level nested branching in the 4-pixel and 2-pixel mixed transparent loops of `epic12_blend_fixed` with branchless arithmetic masking `m = 0 - ((pen >> 29) & 1)`.

## Verification

1. **Cross-Compiled GPU & Blitter Suite (PPC / QEMU)**:
   - `libretro/FBNeo/tests/epic12_gpu/run.py --toolchain-root /home/humor/salvia-tests/toolchains/ppc/root`:
     - **100% PASS**:
       - `PASS 6291456 component cases on unified and fast/feedback shaders, including additive saturation`
       - `PASS 4194304 additive component cases on dmode 7 and 3`
       - `PASS 14400 commands, full VRAM and delay comparison; simulated GPU batches=1475 commands=7682`
       - `PASS staging snapshot WAR prevention: 71..128px multi-part strips snapshotted, >128px rasters bypassed`
       - All component cases, CPU invalidation, and reorder checks bit-identical.

2. **SH-3 Host and PPC Dispatch Suites**:
   - `libretro/FBNeo/tests/sh3_dispatch/run_inline.py --output /tmp/sh3_dispatch_out`:
     - **100% PASS**: 8 host suites, 442,729 cases, ASan/UBSan clean.
   - `libretro/FBNeo/tests/sh3_ppc/run.py --toolchain-root /home/humor/salvia-tests/toolchains/ppc/root`:
     - **100% PASS**: 848,846 total cases (729,787 compiled, 119,059 fallback).

3. **Build and Distro360 Handoff**:
   - Baseline commit: `1dedf6ac`
   - Implementation commit: `779a9a16`
   - Image set compiled with:
     `build-xex.sh --baseline 1dedf6ac --tag cv1k-spin-simd --flavor diag --force`
   - **Candidate Image (Diagnostic)**: `Distro360/fbneo.xex` (and `Distro360/fbneo-diag.xex`)
     - Tag: `cv1k-spin-simd 779a9a16 20261104-0729 diag`
     - Size: `34,832,384` bytes
     - SHA256: `316359ee11f7e1292dd31f565b3d39e7fcf26688fbfbb4f50139fdb9aa859eda`
     - Archive: `/home/humor/salvia-tests/xex-archive/fbneo-20261104-0729-cv1k-spin-simd-diag.xex`
