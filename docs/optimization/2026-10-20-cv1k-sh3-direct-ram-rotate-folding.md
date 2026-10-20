# CV1000: SH-3 Direct RAM Rotate Folding

Baseline `5221a2ed` (restoring stable `slice-opt` 60-slice baseline `5126c8e6`). Elimination of redundant back-to-back bitfield extraction rotates on direct RAM loads/stores in `sh3_drc_ppc.h`, saving 1 PPC instruction and 1 pipeline dependency stall per memory operation across all direct RAM accesses.

## Why

1. **Analysis of DRC Memory Address Translation Overhead**:
   - In SH-3 dynamic binary translation on PowerPC 64 (`sh3_drc_ppc.h`), direct RAM accesses (main RAM 8 MiB with `span_bits = 24`, `backing_bits = 23`) were generating two consecutive rotate instructions targeting the same destination register `r12`:
     ```asm
     # Previous codegen:
     rlwinm r0, r11, 8, 27, 31       # span tag check
     cmpi   0, 0, 0x0c               # check RAM page prefix
     bc     2, 1, fallback_device    # guard
     rlwinm r12, r11, 0, 8, 31       # mask to span (16 MiB)
     # ... optional watch check ...
     rlwinm r12, r12, 0, 9, 31       # mask to backing (8 MiB)
     add    r12, r12, r30            # bias with RAM base
     ```
   - Because `r12` is both the destination and source of the second `rlwinm`, the in-order Xenon PPE core suffers a 1-cycle pipeline RAW dependency stall between the two rotate operations.
   - Across a typical CV1000 gameplay session (e.g. 4,400+ frames), the SH-3 CPU executes over **6.18 million** direct RAM loads and stores. Emitting two masks when `backing_bits < span_bits` was entirely redundant: any address masked directly to `backing_bits` (23 bits) is mathematically already masked to `span_bits` (24 bits).

## What

1. **`libretro/FBNeo/src/cpu/sh4/sh3_drc_ppc.h`**:
   - Calculated `extract_bits = (ram.backing_bits && ram.backing_bits < ram.span_bits) ? ram.backing_bits : ram.span_bits`.
   - Replaced the initial `span_bits` rotate with a single direct `extract_bits` rotate:
     ```cpp
     const unsigned extract_bits=(ram.backing_bits && ram.backing_bits<ram.span_bits)?ram.backing_bits:ram.span_bits;
     rotate(12,11,0,32-extract_bits,31);
     if(!write && size==4 && ram.watch!=0xffffffffu) {
      UINT32 watch_off=(ram.watch-ram.start)&((1u<<extract_bits)-1);
      if(watch_off<=0xffff) { cmpi(12,(int)watch_off,true); guard(2,false,service_opcode); }
      else { imm(0,watch_off); cmp(12,0); guard_watch(service_opcode); }
     }
     ram_bias(12);
     ```
   - Removed the second `if(ram.backing_bits!=ram.span_bits)rotate(12,12,0,32-ram.backing_bits,31);`.
   - Result: Emits exactly **1** `rlwinm` instruction for all direct RAM addresses, completely eliminating the redundant instruction and the dependent pipeline stall.

## Verification

1. **Host Differential Test Suites**:
   - `libretro/FBNeo/tests/sh3_dispatch/run_inline.py`:
     - **100% PASS**: 8 host suites, 442,729 test cases, ASan and UBSan clean.

2. **QEMU PowerPC Native DRC Suite**:
   - `libretro/FBNeo/tests/sh3_ppc/run.py`:
     - **100% PASS**: 846,030 total test cases (726,971 compiled, 119,059 fallback).
     - Direct RAM, mirrored direct RAM, compiled watched `MOV.L`, device `MOV.L`, edge branch dependencies, cycle budgets, register aliases: all PASS with zero errors.

3. **Build and Distro360 Handoff**:
   - Baseline commit: `5221a2ed`
   - Implementation commit: `b868b5ab`
   - Image set compiled with `build-xex.sh --baseline HEAD~1 --tag rotfold-opt --flavor diag`.
   - **Candidate Image**: `Distro360/fbneo-rotfold-opt-diag.xex`
     - Tag: `rotfold-opt b868b5ab 20261020-2054 diag`
     - Size: `34,701,312` bytes
     - SHA256: `8c6333cbbe8f283b3ffdf596f6d3b77908607484bad122761f624854155e20f7`
     - Archive: `/home/humor/salvia-tests/xex-archive/fbneo-20261020-2054-rotfold-opt-diag.xex`
   - **Baseline Image**: `Distro360/fbneo-slice-opt-diag.xex`
     - Tag: `slice-opt 5126c8e6 20261020-1948 diag`
     - Size: `34,701,312` bytes
     - SHA256: `775e20d76444827569759bf21c996d713ae2dba68926dc5f63b2e59ebc041a13`
     - Archive: `/home/humor/salvia-tests/xex-archive/fbneo-20261020-1948-slice-opt-diag.xex`
