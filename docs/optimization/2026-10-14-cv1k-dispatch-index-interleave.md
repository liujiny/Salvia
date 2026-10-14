# CV1000: interleave set index calculation with page table load in DRC dispatch

Baseline `a09ae94b`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

In [`sh3_drc_dispatch.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/cpu/sh4/sh3_drc_dispatch.h), `sh3_drc_dispatch_impl()` executes on every block entry (84.1 million times across 120 frames):
- The original prologue ordered operations as:
  ```cpp
  UINT32 pc = m_pc, phys = pc & AM;
  const UINT8 *page = MemMapF[phys >> SH3_SHIFT];
  if ((uintptr_t)page < SH3_MAXHANDLER || (phys & 1)) { ... return false; }
  const UINT16 *source = (const UINT16*)(page + (phys & SH3_PAGEM));
  unsigned index = ((pc >> 1) ^ (pc >> 11) ^ (pc >> 21)) & (CACHE_SETS - 1);
  ```
- Because Xenon has an in-order dual-issue execution pipeline:
  1. `page = MemMapF[...]` initiates a memory load.
  2. The next instruction immediately checks `if ((uintptr_t)page < SH3_MAXHANDLER ...)`, stalling the pipeline while waiting for the load result.
  3. `index = ((pc >> 1) ^ ...)` is completely independent of `page` and depends only on `pc`, yet was placed *after* the stall.

## What

- Reordered the prologue to calculate `index` immediately after reading `pc`:
  ```cpp
  UINT32 pc = m_pc, phys = pc & AM;
  unsigned index = ((pc >> 1) ^ (pc >> 11) ^ (pc >> 21)) & (CACHE_SETS - 1);
  const UINT8 *page = MemMapF[phys >> SH3_SHIFT];
  if ((uintptr_t)page < SH3_MAXHANDLER || (phys & 1)) { ... return false; }
  const UINT16 *source = (const UINT16*)(page + (phys & SH3_PAGEM));
  ```
- The 5 ALU instructions for `index` (`srwi`, `xor`, `srwi`, `xor`, `rlwinm`) pair into the dual-issue pipeline during the latency window of the `MemMapF` load, eliminating load-use bubbles on every dispatch call.
- Provides `index` earlier for addressing `shadow[index]`.

## Verification

1. **Host Standalone & ASan/UBSan Suites**:
   - `python3 libretro/FBNeo/tests/sh3_dispatch/run_inline.py`: **100% PASS** across all 8 host suites (including 12,000 traces x 12 slices, straight chains, cache lifetime).
2. **PPC Test Suite (QEMU Big-Endian)**:
   - `python3 libretro/FBNeo/tests/sh3_ppc/run.py --toolchain-root /home/humor/salvia-tests/toolchains/ppc/root`:
   - **PASS 846,026 cases** (compiled 727,804, fallback 118,222).
3. **Emulator Benchmark (Xenia Canary, `mmpork.state`, 120 gameplay frames)**:
   - `worker_busy`: **7645.128 ms** (down from 7660.007 ms, **-14.88 ms** reduction)
   - `draw`: **7216.798 ms** (down from 7230.125 ms, **-13.33 ms** reduction)
   - `frontend_frame_ms game_and_ui`: **156.153 ms** (down from 157.207 ms, **-1.05 ms/frame** improvement)
   - Dispatch calls: 84,190,202 (identical)
   - Batches: 1,396 (identical)
4. **Artifact Archive**:
   - Diagnostic binary: `/home/humor/salvia-tests/xex-archive/fbneo-20261014-1446-dispatch-sched-opt-diag.xex`
   - SHA256: `8ccf57a9db7a33293f1b2cc8ccf986d857dd7ab56340426e3e543c727400a7da`

## Revert

`git revert <sha>` to restore original `sh3_drc_dispatch.h` and delete this report.
