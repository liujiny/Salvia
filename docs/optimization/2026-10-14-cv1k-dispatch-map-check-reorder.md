# CV1000: hoist read-map checks ahead of opcode array loop in dispatch validation

Baseline `e080a380`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

In [`sh3_drc_dispatch.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/cpu/sh4/sh3_drc_dispatch.h), `sh3_drc_dispatch_impl()` executes on every block entry, running over 84.1 million times in 120 gameplay frames:
- In the Shadow cache path (line 188):
  ```cpp
  if (cand.pc == pc && cand.source == source && sector_epoch[cand.sector] == cand.epoch &&
      sh3_drc_source_equal(cand.original, source, cand.words) &&
      (!cand.check_read_map || MemMapR[phys >> SH3_SHIFT] == page)) copy = &cand;
  ```
- And in the cold record path (line 349):
  ```cpp
  rebuild = (b.source != source || b.pc != pc || !sh3_drc_source_equal(b.original, source, b.words) ||
             (b.check_read_map && MemMapR[phys >> SH3_SHIFT] != page));
  ```
- In both validation paths, the scalar memory mapping check `(!check_read_map || MemMapR[...] == page)` was evaluated *after* calling `sh3_drc_source_equal(...)`.
- `sh3_drc_source_equal` iterates over an array of guest opcodes in memory.
- Conversely, `check_read_map` is a 1-byte scalar flag residing inside the already loaded `cand` structure in L1 cache: 99.9% of blocks have `check_read_map == 0`.
- Evaluating `!check_read_map` takes a single scalar instruction and immediately succeeds without even touching `MemMapR`. If `check_read_map != 0` and mismatched, it completely avoids calling the array loop.

## What

1. **Shadow Path Condition Reordering**:
   - Hoisted `(!cand.check_read_map || MemMapR[phys >> SH3_SHIFT] == page)` before `sh3_drc_source_equal(cand.original, source, cand.words)`.
2. **Cold Record Rebuild Condition Reordering**:
   - Hoisted `(b.check_read_map && MemMapR[phys >> SH3_SHIFT] != page)` before `!sh3_drc_source_equal(b.original, source, b.words)`.

## Verification

1. **Host Standalone & ASan/UBSan Suites**:
   - `python3 libretro/FBNeo/tests/sh3_dispatch/run_inline.py`: **100% PASS** across all 8 host suites (including 12,000 traces x 12 slices, guards, aliases, partial exits).
2. **PPC Test Suite (QEMU Big-Endian)**:
   - `python3 libretro/FBNeo/tests/sh3_ppc/run.py --toolchain-root /home/humor/salvia-tests/toolchains/ppc/root`:
   - **PASS 846,026 cases** (compiled 727,804, fallback 118,222).
3. **Emulator Benchmark (Xenia Canary, `mmpork.state`, 120 gameplay frames)**:
   - `drc_dispatch`: **4793.391 ms** (down from 4840.865 ms, **-47.47 ms** reduction!)
   - `submit`: **2860.609 ms** (down from 2879.606 ms, **-19.0 ms**)
   - `sampled_batch_cpu_ms upload`: **0.831 ms/batch** (down from 0.838 ms/batch)
   - Batches: 1,396 (identical)
   - Commands: 82,780 (identical)
   - Dispatch calls: 84,190,202 (identical)
4. **Artifact Archive**:
   - Diagnostic binary: `/home/humor/salvia-tests/xex-archive/fbneo-20261014-1438-dispatch-map-opt-diag.xex`
   - SHA256: `fb8ee7536cb08612f8f202853cd9d88e84c23ad5eb639d6c878695edecb44e2e`

## Revert

`git revert <sha>` to restore original `sh3_drc_dispatch.h` and delete this report.
