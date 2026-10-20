# CV1000: SH-3 DRC NEGC Opcode Implementation & Delay Slot Expansion

Baseline `9034698c` (scanline 240-slice restoration). Implementation of native PowerPC dynamic binary translation for the previously unhandled `0x600a` (`NEGC Rm, Rn`) opcode in `sh3_drc_ppc.h`, along with expanding `delay_slot_eligible` opcode classification to support all 16 register operands via `op & 0xf0ff` masking for control register transfers and recognizing fundamental ALU instructions (`ADD #imm`, `ADD/SUB Rm,Rn`, and bitwise logic `TST/AND/XOR/OR Rm,Rn`).

## Why

1. **Elimination of DRC Block Interruption from Unhandled `NEGC`**:
   - `0x600a` (`NEGC Rm, Rn`) computes `Rn = 0 - Rm - T` and sets `T = (Rm != 0) | T_old` (indicating borrow).
   - In upstream `sh3_drc_ppc.h`, `0x600a` had no native DRC translation and was fallen back to the C++ interpreter.
   - Any SH-3 basic block encountering `NEGC` suffered a full DRC block termination, interpreter fallback penalty, and state synchronization overhead.

2. **Resolution of Register Masking Bug in `delay_slot_eligible`**:
   - In upstream `delay_slot_eligible(UINT16 op)`, control and PR register transfers (`STS/LDS MACH/MACL/PR`, `STC/LDC GBR`) were checked with an unmasked `switch (op)`:
     ```cpp
     switch (op) {
      case 0x0009: return true; // NOP
      case 0x0012: case 0x401e: return true; // STC/LDC GBR
      case 0x000a: case 0x001a: case 0x002a: return true; // STS MACH/MACL/PR
      case 0x400a: case 0x401a: case 0x402a: return true; // LDS MACH/MACL/PR
     }
     ```
   - In the SH-3 encoding, the register operand `Rn` occupies bits 8..11 (`0x0n00`). Exact matches on `0x000a`, `0x400a`, etc. only recognized register `R0` (`n=0`). Any branch delay slot utilizing `R1` through `R15` for PR or GBR transfers was wrongly categorized as ineligible, forcing branch blocks to terminate without delay slot fusion.
   - Masking `op & 0xf0ff` properly recognizes all 16 registers for these operations.

3. **Expansion of Delay Slot ALU Instruction Eligibility**:
   - Compilers frequently schedule fundamental single-cycle ALU instructions into branch delay slots:
     - `0x7000`: `ADD #imm, Rn`
     - `0x300c`: `ADD Rm, Rn`
     - `0x3008`: `SUB Rm, Rn`
     - `0x2008`..`0x200b`: `TST`, `AND`, `XOR`, `OR Rm, Rn`
     - `0x600a`: `NEGC Rm, Rn`
     - `0x600b`: `NEG Rm, Rn`
   - Adding these to `delay_slot_eligible` allows the DRC to fold them directly into the compiled branch block instead of terminating the trace prematurely.

## What

1. **`libretro/FBNeo/src/cpu/sh4/sh3_drc_ppc.h`**:
   - Expanded `delay_slot_eligible(UINT16 op)`:
     ```cpp
     switch (op & 0xf00f) {
      case 0x6000: case 0x6001: case 0x6002: case 0x6003: return true; // MOV/MOV.B/MOV.W/MOV.L Rm,Rn
      case 0x6008: case 0x6009: case 0x600a: case 0x600b: case 0x600c: case 0x600d: case 0x600e: case 0x600f: return true; // SWAP/NEGC/NEG/EXTU/EXTS
      case 0x0007: return true; // MUL.L
      case 0x0029: return true; // MOVT
     }
     switch (op & 0xf0ff) {
      case 0x0012: case 0x401e: return true; // STC/LDC GBR
      case 0x000a: case 0x001a: case 0x002a: return true; // STS MACH/MACL/PR
      case 0x400a: case 0x401a: case 0x402a: return true; // LDS MACH/MACL/PR
     }
     if (op == 0x0009) return true; // NOP
     if ((op & 0xf000) == 0x7000) return true; // ADD #imm,Rn
     if ((op & 0xf000) == 0xe000) return true; // MOV #imm,Rn
     if ((op & 0xf00f) == 0x300c || (op & 0xf00f) == 0x3008) return true; // ADD/SUB Rm,Rn
     if ((op & 0xf00f) >= 0x2008 && (op & 0xf00f) <= 0x200b) return true; // TST/AND/XOR/OR Rm,Rn
     ```
   - Implemented native PowerPC code generator for `0x600a` (`NEGC Rm, Rn`):
     ```cpp
     case 0x600a: { // NEGC Rm, Rn: Rn = -Rm - T, T = (Rm != 0) | T
      a=reg(m); dst=reg(n,false); int sr=reg(G_SR);
      rotate(11,sr,0,31,31);             // r11 = old T bit
      cmpi(a,0);                         // cr0 = (Rm == 0)
      emit(0x7c000026);                  // mfcr r0
      rotate(0,0,3,31,31);               // r0 = (Rm != 0) via bit 2 EQ test
      emit(d(26,0,0,1));                 // xori r0, r0, 1
      logic(0,0,11,444);                 // r0 = (Rm != 0) | old_T (or)
      emit(x(12,a,0,104));               // r12 = -Rm (neg)
      sub(dst,12,11);                    // dst = -Rm - old_T
      rotate(sr,0,0,31,31,true);         // update T bit in SR
      dirty(dst); dirty(sr); return true;
     }
     ```
   - Correctly handles operand aliasing (when `Rm == Rn`), registers `r11`, `r12`, `r0` scratches, and condition register borrowing without clobbering live allocations.

## Verification

1. **Host Differential Test Suites**:
   - `libretro/FBNeo/tests/sh3_dispatch/run_inline.py`:
     - **100% PASS**: 8 host suites, 442,729 test cases, AddressSanitizer and UndefinedBehaviorSanitizer clean.

2. **QEMU PowerPC Native DRC Suite**:
   - `libretro/FBNeo/tests/sh3_ppc/run.py`:
     - **100% PASS**: **848,846 total test cases** (729,787 compiled, 119,059 fallback).
     - Compiled test cases increased from 726,971 to 729,787 (+2,816 newly compiled cases covering `NEGC` and expanded delay slot opcodes).
     - Zero regressions across all edge conditions (native carry/borrow, delay slots, memory operands, aliased registers, cycle budget contracts).

3. **Build and Distro360 Handoff**:
   - Baseline commit: `9034698c`
   - Implementation commit: `4407eebe`
   - Image set compiled with `build-xex.sh --baseline 9034698c --tag drc-negc-slot-opt --flavor diag`.
   - **Candidate Image**: `Distro360/fbneo-drc-negc-slot-opt-diag.xex`
     - Tag: `drc-negc-slot-opt 4407eebe 20261020-2219 diag`
     - Size: `34,701,312` bytes
     - SHA256: `64656391115bb4cf723b26becbfa3fb78a0424785ce3a388da0491fe3a1d46c9`
     - Archive: `/home/humor/salvia-tests/xex-archive/fbneo-20261020-2219-drc-negc-slot-opt-diag.xex`
   - **Baseline Image**: `Distro360/fbneo-scanline240-opt-diag.xex`
     - Tag: `scanline240-opt 9034698c 20261020-2157 diag`
     - Size: `34,701,312` bytes
     - SHA256: `c307ebeda270295a9f3134dba889f885ea71bd658baa61cb7826bdb5fe9d9585`
     - Archive: `/home/humor/salvia-tests/xex-archive/fbneo-20261020-2157-scanline240-opt-diag.xex`
