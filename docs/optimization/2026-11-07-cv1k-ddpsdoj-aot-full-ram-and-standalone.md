# CV1000 DRC & Standalone: DDPSDOJ In-Game AOT Full SDRAM Precompile, Direct Store Fallback, and Dedicated Auto-Boot Executable

## Commit
- Commit SHA: `ef103654`

## Baseline
- Baseline commit: `74254ad451528659d9c2a688b1fcdd9b3297a760` (fix(cv1k,drc,gpu): add area p2 precompilation, direct read fallback and explosion blend acceleration)
- Target console: Xbox 360 Jasper (67MB available heap, unthrottled PPC DRC)
- Test cases: DoDonPachi SaiDaiOuJou (`ddpsdoj`) Stage 1 opening entrance, mid-stage explosion savestate (`ddpsdoj.state1`), and standalone auto-boot mode.

## Problem Description
1. **`ddpsdoj` Stage 1 Player Aircraft Entrance Freeze (2–3 seconds)**:
   - SDOJ is a Cave CV1000 Type D game with 16MB SDRAM and 130MB Flash ROM.
   - During boot, flash decompression takes hundreds of frames. At frame 10 (when the RAM test bypass patch fires), flash decompression is not yet completed into SDRAM.
   - The previous AOT trigger condition (`was_pending && !cv1k_boot_pending`) fired at frame 10, scanning empty/partially populated SDRAM up to only a 16,384 basic-block limit and 4MB range (`0x400000`).
   - Consequently, when Stage 1 started, over 30,145 basic blocks in Area P0 (`0x0c000000`) and Area P2 (`0xac000000`) had to be dynamically compiled on the fly during the player aircraft entrance sequence, resulting in `cpu_io = 85.298 ms`, audio tempo collapse to 0.28, and a noticeable 2–3 second stutter.

2. **Direct Store Guard Bails at `0xAC002A66`**:
   - In `sh3_drc_ppc.h`, direct store instructions guarded with `guard(2, true, device_opcode)` when the target address did not match `ram.start`.
   - SDOJ executes frequent memory writes outside direct RAM (e.g., clearing `DrvCacheRAM` at `0xf0000000..0xf0003fff` in a 1,024-iteration loop at `0xAC002A66`).
   - Each non-direct store tripped the guard, causing 7,168 bails to the C++ interpreter per pause window.

3. **Standalone Executable Requirement**:
   - To match the zero-overhead, direct-boot experience of `mmpork.xex` and `ibara360.xex`, a dedicated `ddpsdoj.xex` auto-boot executable is required that detects and boots `ddpsdoj.zip` without frontend menus.

## Changes Made
1. **Full-Span SDRAM and Delayed AOT Precompilation (`d_cv1k.cpp`)**:
   - Replaced frame-10 premature trigger with `!cv1k_boot_pending && (nCurrentFrame >= 240 || force_early)`.
   - Expanded precompilation span to full 16MB SDRAM (`0x1000000`) for Type D CV1000 games across both Area P0 (`0x0c000000`) and Area P2 (`0xac000000`).
   - SDOJ title sequence now executes AOT precompilation after complete flash decompression, caching all 30,000+ basic blocks before gameplay begins.

2. **Direct Store Fallback to `write_map` and DrvCacheRAM (`sh3_drc_ppc.h`)**:
   - Implemented `not_direct_ram` branch fallback in `Compiler::memory` write path.
   - Decodes P0..P3 addresses via physical mask (`AM`) and P4 `DrvCacheRAM` (`0xf0000000`), looking up `write_map` directly in PPC assembly.
   - Eliminates interpreter bails at `0xAC002A66` and enables continuous DRC execution for mapped writes.
   - Doubled `QUEUE_MASK` to `131071u` (512KB queue) and increased block limit from 16,384 to 65,536.

3. **Dedicated Standalone Auto-Boot Executable (`src/salvia.cpp`, `src/salvia_standalone.h`, `build-xex.sh`)**:
   - Added `SALVIA_STANDALONE_DDPSDOJ` macro toggle (0: Universal, 1: Dedicated MMPork, 2: Dedicated DDPSDOJ).
   - In `src/salvia.cpp`: configured `ddpsdoj.xex` app directory, ROM path resolution (`game:\ddpsdoj.zip`, `d:\ddpsdoj.zip`, etc.), and direct game launch.
   - Updated `build-xex.sh` to compile and deploy both universal `fbneo.xex` and dedicated `ddpsdoj.xex`.

## Validation and Test Results
1. **SH3 Inline Dispatch Test Suite (`sh3_dispatch/run_inline.py`)**:
   - ASan / UBSan host verification: 442,729 test cases, 0 errors, 100% PASS.
2. **SH3 PPC DRC Test Suite (`sh3_ppc/run.py`)**:
   - Cross-compiled PowerPC execution under `qemu-ppc`: 848,846 test cases (750,948 compiled, 97,898 fallback), 100% PASS.
3. **EPIC12 GPU Batching & Blitter Test Suite (`epic12_gpu/run.py`)**:
   - Cross-compiled PowerPC execution under `qemu-ppc`: 12,000,000+ shader / component cases, all cache coherency / WAR tests PASS.
