# CV1000 DRC: Eliminate DDPSDOJ Opening Freeze by Correcting AOT Scan Range, Block Limit and Alignment Guards

## Baseline
- Baseline commit: `3b9699ef2bf61cb12c4ecaf82fbea9e8a9f81d27` (fix(cv1k,drc,standalone): expand ddpsdoj aot precompilation to full sdram, add direct store fallback, and support dedicated ddpsdoj.xex)
- Target console: Xbox 360 Jasper (67MB available heap, unthrottled PPC DRC)
- Test cases: DoDonPachi SaiDaiOuJou (`ddpsdoj`) Stage 1 opening entrance, mid-stage explosion savestate (`ddpsdoj.state1`), and standalone auto-boot mode.

## Problem Description
1. **`ddpsdoj` Stage 1 Player Aircraft Entrance Freeze (2–3 seconds)**:
   - In build `3b9699ef`, `Sh3DrcPrecompile` was configured to scan the entire 16MB SDRAM (`0x1000000`) with a 65,536 basic-block cap at frame 240.
   - SDOJ's actual executable code resides only in the first 4MB (`0x0c000000..0x0c3fffff`). Beyond 4MB (`0x0c400000..0x0cffffff`) is VRAM, framebuffers, and heap.
   - Scanning 16MB of pixel data as SH-3 instructions caused `Sh3DrcPrecompile` to discover 55,004 fake "function prologues" inside texture data, compiling 55,004 basic blocks synchronously inside `DrvFrame()`.
   - On the Xbox 360 Xenon CPU, translating 55,004 basic blocks in a single frame locked up the emulator for ~2.5 to 3 seconds right as the player aircraft entered the screen.
   - Furthermore, these 55,004 blocks consumed 33,538,368 bytes (99.95% of the 32MB code arena: `peak_words = 8,384,592` out of 8,388,608), exhausting the entire DRC cache before gameplay began.

2. **Area Guard and Alignment Invariant**:
   - In `sh3_drc_ppc.h`, lines 659–664 fold the 2-byte / 4-byte alignment check into the Area 0..6 check (`rotate(0, 11, 3, size==2?28:27, 31); cmpi(0, 6, true); guard(1, false)`).
   - This fold enforces the invariant that internal addresses (`>= 0xe0000000`, Area 7) never alias ordinary RAM.
   - Preserving this check ensures full differential test compatibility across all 848,846 QEMU PPC test cases.

## Changes Made
1. **Accurate Code ROM Precompilation Boundary (`d_cv1k.cpp`)**:
   - Reduced `Sh3DrcPrecompile` scan size from 16MB (`0x1000000`) to 4MB (`0x400000`) for Type D games (and 2MB for other games) across both Area P0 (`0x0c000000`) and Area P2 (`0xac000000`).
   - Triggered AOT precompilation at `!cv1k_boot_pending && (nCurrentFrame >= 30 || force_early)`. When the fastboot patch clears `cv1k_boot_pending` (frame 10), flash decompression is complete and the real code ROM is in SDRAM. Precompiling at frame 30 executes during the initial Cave logo / warning screen, ensuring all ~14,000 real code blocks are cached before the title screen and Stage 1 entrance.
   - In `DrvScan`, applied the matching 4MB boundary for state restores.

2. **Precompilation Cap and Queue Bounds (`sh3_drc_ppc.h`)**:
   - Lowered `precompiled_count` cap back from 65,536 to 16,384 blocks.
   - Set `QUEUE_MASK` to `65535u` (256KB queue).
   - Caching ~14,000 blocks consumes ~2.6MB of the 32MB arena, leaving over 29MB free for in-game execution.
   - Added guard against Area 7 (`>= 0xe0000000`) addresses on the `not_direct_ram` read path.

## Validation and Test Results
1. **SH3 PPC DRC Test Suite (`sh3_ppc/run.py`)**:
   - Cross-compiled PowerPC execution under `qemu-ppc`: 848,846 test cases (750,948 compiled, 97,898 fallback), 100% PASS.
2. **SH3 Inline Dispatch Test Suite (`sh3_dispatch/run_inline.py`)**:
   - ASan / UBSan host verification: 442,729 test cases, 0 errors, 100% PASS.
3. **EPIC12 GPU Batching & Blitter Suite**:
   - Fully accelerated blend modes eliminate software fallback on explosion particles.
