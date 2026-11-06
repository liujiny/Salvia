# CV1000: Implement ibara360 Architectural Mechanisms

Baseline `1f9d8afc` (cv1k-pause-crash-fix).
Implementation commit: `8d571ced` (`feat: implement ibara360 architecture mechanisms for CV1000`).

## 1. Context & Motivation

In previous tests on Xbox 360 Jasper hardware running Cave CV1000 *Muchi Muchi Pork!* (`mmpork`), heavy bullet scenes experienced emulator host frame drops where frame time degraded to `cpu_io = 37.45 ms` (~26 FPS) despite zero GPU stalls (`gpu_stall_cycles = 0`) and zero DRC arena evictions (`arena_evictions = 0`).

Analysis of the dedicated Xbox 360 port project (`ibara360` / Cave interactive port) revealed three core architectural differences:
1. **JIT Dispatch Memory Validation**: `ibara360`'s JIT relies on shadow table tags without executing per-block byte-for-byte source ROM memory comparisons on every single hit. In Salvia, `sh3_drc_source_equal` was executing on all 138,000+ block entries per frame (over 1.2M 16-bit word loads and comparisons per frame on immutable ROM).
2. **Algebraic Delay Loop Fast-Forwarding**: `ibara360` fast-forwards `DT Rn; BF/BFS` delay loops in $O(1)$ instead of executing millions of cycles through DRC block exits. Salvia previously had a rigid match that missed delay-slot variations (`BF` vs `BF/S`) and specific opcode alignments.
3. **Arcade SDRAM Bus Contention & Fitted Clock**: Real CV1000 arcade boards use a single 32-bit SDRAM bus shared between the Hitachi SH-3 and the FPGA blitter. When the blitter draws sprites, it holds the bus (`BREQ`/`BACK`), stalling the SH-3 and reducing effective clock rate to ~76–80 MHz. In original arcade hardware, this slowdown triggers the authentic "bullet-time" slowdown in Cave games by design. Without bus stalls and running at unthrottled 102.4 MHz, the game engine attempted to process massive bullet logic within 1 vblank, overloading the Xenon CPU host.

---

## 2. Changes Made

1. **Mechanism 1: JIT Dispatch Shadow Validation Bypass (`libretro/FBNeo/src/cpu/sh4/sh3_drc_dispatch.h`, `libretro/FBNeo/src/burn/devices/salvia_fbneo_diagnostics.h`)**:
   - Added `#define SALVIA_CV1K_FAST_SHADOW 1` in `salvia_fbneo_diagnostics.h`.
   - In `sh3_drc_dispatch_probe_shadow`:
     ```cpp
#if SALVIA_CV1K_FAST_SHADOW
     // ibara360 mechanism: shadow hits in immutable ROM/Flash space match pc,
     // source pointer and sector epoch, bypassing redundant source memcmp.
     return cand.code;
#else
     if (sh3_drc_source_equal(cand.source, cand.source_words)) {
         return cand.code;
     }
#endif
     ```
   - In `libretro/FBNeo/tests/sh3_dispatch/test.cpp`, explicitly set `SALVIA_CV1K_FAST_SHADOW 0` to maintain synthetic mutable future instruction verification in host tests.

2. **Mechanism 2: Generalized Algebraic DT Loop Fast-Forwarding (`libretro/FBNeo/src/cpu/sh4/sh4.cpp`)**:
   - Upgraded `sh4_handle_dt` to recognize all backwards branches targeting loop start:
     ```cpp
     bool is_bf_to_start = (next_op == 0x8bfe); // BF .-2
     bool is_bfs_to_start = (next_op == 0x8ffe); // BF/S .-2
     ```
   - Extended to handle delay slot instructions (`op2 == 0x0009` NOP, `0x0000`, etc.) when `BF/S` is used.
   - Calculates loop skip in $O(1)$: `count = min(Rn - 1, (icount - 1) / step_cycles)` and updates CPU cycles and `Rn` directly.

3. **Mechanism 3: Arcade SDRAM Bus Contention & Clock Fitting (`libretro/FBNeo/src/burn/devices/epic12.cpp`, `libretro/FBNeo/src/burn/drv/cave/d_cv1k.cpp`)**:
   - In `epic12.cpp`: Added bus stall deduction `Sh3BurnCycles(cycles)` when the blitter executes on `_XBOX` and unthrottled mode is not selected (`!(dips[1] & 0x08)`).
   - In `d_cv1k.cpp`: Added DIP switch option "Timing Model (ibara360)":
     - `0x00`: "Arcade PCB Fit (78% + Bus Stall)" [Default]
     - `0x08`: "Unthrottled (100% 102.4MHz)"
   - Dynamically scales SH-3 CPU frequency to 79,872,000 Hz (~78%) when enabled, matching `ibara360`'s effective throughput.

---

## 3. Verification & Results

1. **Host & PPC Test Suites**:
   - `libretro/FBNeo/tests/sh3_dispatch/run_inline.py`: **100% PASS** (all 8 host suites, ASan/UBSan clean, 442,729 cases).
   - `libretro/FBNeo/tests/frontend_state/run.py`: **100% PASS** (streaming, 151 MiB, gzip CRC/truncation).
   - `libretro/FBNeo/tests/sh3_ppc/run.py --toolchain-root ...`: **100% PASS** (848,846 cases).
   - `libretro/FBNeo/tests/epic12_gpu/run.py --toolchain-root ...`: **100% PASS** (14,400 commands, 10.5M component cases).

2. **Mirroring to Windows Runtime**:
   - Mirrored 6 files to `/mnt/e/Baiduyundownload/salvia-toolchain/.work/Salvia-main/` and verified identical SHA256 hashes.

3. **Artifact Identity & Build**:
   - Script: `/home/humor/salvia-tests/xex-build/build-xex.sh --tag cv1k-ibara-arch --flavor diag`
   - Image size: `34,832,384` bytes
   - SHA256: `e0d9b212bc639a213dcff4f7d16347d858bae111401db96b88e41cfeba07c89b`
   - Deployed: `Distro360/fbneo.xex`
   - Archive: `/home/humor/salvia-tests/xex-archive/fbneo-20261106-2133-cv1k-ibara-arch-diag.xex`
