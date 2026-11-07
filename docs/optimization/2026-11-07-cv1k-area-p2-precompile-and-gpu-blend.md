# CV1000 DRC & GPU: Area P2 Precompilation, Direct Fallback, and GPU Blend Expansion

## Baseline
- Baseline commit: `a4f233436d4f9bf3cfcb56da452fd4d1d9ecdd96` (fix(cv1k,gpu): accelerate remaining ddpsdoj blend modes on xenos gpu)
- Target console: Xbox 360 Jasper (67MB available heap, unthrottled PPC DRC)
- Test cases: DoDonPachi SaiDaiOuJou (`ddpsdoj`) stage 1 opening entrance, mid-stage explosion state (`ddpsdoj.state1`), and MMPork stage 1 boss death dialogue.

## Problem Description
1. **`ddpsdoj` Stage 1 Opening Freeze**:
   - Right when the player aircraft spawns at Stage 1, the game freezes for 2–3 seconds.
   - SDOJ's game main loop and aircraft spawn routines execute from **Area P2 (`0xac000000`)** (uncached SDRAM alias).
   - In RAM, there are 6,367 function pointers in `0xac...` and 4,263 in `0x0c...`.
   - `Sh3DrcPrecompile` only pushed `start_pc` (`0x0c000000`, Area P0) into its BFS precompile queue.
   - Cache lookup tags missed 100% of precompiled blocks when running in Area P2, forcing thousands of Area P2 basic blocks to be dynamically compiled on the fly at runtime during the entrance sequence.
   - Furthermore, `direct` memory access check in `sh3_drc_ppc.h` checked `((block_pc & 0x7fffffffu) & ~(ram.span - 1)) == ram.start`, which rejected Area P2 (`0x2c000000 != 0x0c000000`) and disabled direct RAM addressing.

2. **`0xAC002A66` DrvCacheRAM Guard Bails**:
   - In `d_cv1k.cpp`, `0xf0000000..0xf0003fff` is mapped to `DrvCacheRAM` (16KB on-chip RAM).
   - At `0xac002a66`, SDOJ runs a 1,024-iteration loop clearing `DrvCacheRAM` (`mov.l r0, @r1`).
   - Every single store tripped the generic `>= 0xe0000000` guard, aborting to the interpreter 4,096 times in sampled runs.

3. **MMPork Stage 1 Boss Death Dialogue Drop to ~20 FPS**:
   - At `0x0C05C084` (`mov.b @r7, r1`), the dialogue text strings are read from ROM (`0x00000000..0x03ffffff`).
   - Because `block_pc` is in SDRAM, the `direct` path was emitted.
   - The direct path guarded `cmpi(0, ram.start >> ram.span_bits, true); guard(2, true);`.
   - Every character read from ROM tripped guard 2, bailing to the C++ interpreter 7,728 times per pause window. Branch delay slots then forced interpreter execution for every loop iteration.

4. **`ddpsdoj` Explosion Freeze and Pale White Screen**:
   - Mid-stage enemy explosions (captured in savestate slot 1 `ddpsdoj.state1`) triggered unmapped blend modes (`smode 4, 7`, and `dmode 4`).
   - Unhandled blend modes rejected GPU submission, forcing `epic12_gpu_cpu_draw` with command queue flush, EDRAM resolve, CPU readback wait (5.1 ms stall), software drawing, and re-upload up to 120 times per frame.
   - Software blits repeatedly added overlapping particle layers until all pixels clamped at `(248, 248, 248)` pure white.

## Changes Made
1. **Area P2 Precompilation and Pointer Scan (`d_cv1k.cpp`, `sh3_drc_ppc.h`)**:
   - In `d_cv1k.cpp`, added dual-alias precompilation for both `0x0c000000` (Area P0) and `0xac000000` (Area P2) during boot and scan.
   - In `Sh3DrcPrecompile`:
     - Updated code pointer scanning to test `(ptr & AM) >= phys_start && (ptr & AM) < phys_start + size`.
     - Preserved Area prefix when enqueuing: `(start_pc & ~AM) | phys_ptr`.
     - Increased precompiled basic block limit from 8,192 to 16,384.

2. **Direct Memory Access for Area P2 (`sh3_drc_ppc.h`)**:
   - Replaced `0x7fffffffu` with `AM` (`0x1fffffffu`) for both `constant_ram` and `direct` memory access qualification.
   - Allows code executing in Area P2 (`0xac000000`) to use native direct RAM window addressing.

3. **Direct Read Fallback to `read_map` (`sh3_drc_ppc.h`)**:
   - In `direct` path, when `r0 != ram.start >> span_bits` on read instructions:
     - Instead of bailing out to interpreter via `guard(2, true)`, emit a branch to `not_direct_ram`.
     - In `not_direct_ram`, index `read_map` using `rotate(0, 11, 18, 17, 29)` without modifying `r11` (preserving guest address for `m_ea`).
     - Load page pointer, check `cmpi(12, SH3_MAXHANDLER, true)`, guard against hardware handlers, and add page offset `rotate(0, 11, 0, 16, 31)` to `r12`.
     - Keeps ROM string and table reads within SDRAM code running in PPC native instructions.

4. **Area P4 DrvCacheRAM Access (`sh3_drc_ppc.h`)**:
   - In generic path, inspect `(address >> 16) & 0xffff`.
   - If in Area P4 (`>= 0xe000`), check if `(address >> 16) == 0xf000` (`DrvCacheRAM`).
   - If `0xf000`, skip `AM` masking and index `MemMapR` / `MemMapW` directly, avoiding 4,096 guard bails in SDOJ's cache clear loop.

5. **GPU Blend Modes Expansion (`epic12_gpu.h`)**:
   - Normalized `dmode == 4` to `dm = 0, da = 31 - da`.
   - Added `smode == 4` mapped to `sm = 0, sa = 31 - sa` for `dm in {0, 3, 7}`.
   - Added `smode == 7` mapped to `sm = 0, sa = 31` for `dm in {0, 3, 7}`.
   - Eliminates CPU blitter fallbacks during explosions.

## Validation Results
- `sh3_dispatch/run_inline.py`: PASS (all 8 host suites in optimized and ASan/UBSan modes).
- `sh3_ppc/run.py` (PPC build under QEMU): PASS (848,846 cases; 750,948 compiled, 97,898 fallback).
- `epic12_gpu/run.py` (PPC build under QEMU): PASS (all 6,291,456 component shader cases, 14,400 batch commands, reorder and cache models).
- Scope note: Builds and host/QEMU synthetic tests verify instruction correctness and blend mathematics; console FPS and in-game performance require console hardware verification.
