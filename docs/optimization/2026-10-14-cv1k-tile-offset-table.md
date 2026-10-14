# CV1000: precompute atlas tile slot block offset table

Baseline `7d5b5348`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

From `Distro360/cv1000-gpu.log` on 120-frame benchmark window in `mmpork`:
- `upload_pages = 2209` pages uploaded into the 256-slot 2048x2048 GPU atlas.
- In [`epic12_gpu_tile.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_gpu_tile.h) and [`epic12_gpu_tile_alpha.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_gpu_tile_alpha.h):
  - Every 128x128 page consists of 4x4 (16) 32x32 tiled blocks.
  - In each block, the code was executing `XGAddress2DTiledOffset(ax+x, ay+y, 2048, 4)*4` on Xbox 360 (or `epic12_gpu_tile_address(ax+x, ay+y, 2048)`).
  - For 2,209 uploaded pages, that was `2209 * 16 = 35,344` calls to the 2D tiling coordinate transform in just 120 frames!
  - In reality, the atlas width is constant (2048), block sizes are constant (32x32), and slots are fixed (0..255). The byte offset of slot `slot` block `(by, bx)` is completely invariant across the lifetime of the process.
  - Furthermore, `sourcePage + y * sourcePitchWords + x` was recomputing `y * sourcePitchWords` inside the inner `x` loop.

## What

1. **Precomputed Slot Block Offset Table**:
   - Added `static unsigned epic12_tile_slot_block_offset[256][16]` initialized once via `epic12_gpu_tile_slot_init()`.
   - Precomputes all 4,096 block byte offsets (256 slots x 16 blocks = 16 KB table, completely fitting in L1/L2 cache).
   - Replaced all runtime calls to `XGAddress2DTiledOffset` / `epic12_gpu_tile_address` with a single array lookup: `slotOffsets[(by << 2) | bx]`.
2. **Row Pointer Hoisting**:
   - Hoisted `sourcePage + y * sourcePitchWords` (`srcRow`) outside the inner `bx` loop in both `epic12_gpu_tile_page` and `epic12_gpu_tile_alpha_page`.

## Verification

1. **Host & PPC Unit Tests (QEMU Big-Endian)**:
   - `libretro/FBNeo/tests/epic12_gpu_tile/run.py`: **100% PASS** (host + PPC, 512 randomized slots, byte-for-byte identical output with alignment permutations).
   - `libretro/FBNeo/tests/epic12_gpu_tile_alpha/run.py`: **100% PASS** (host + PPC).
   - `libretro/FBNeo/tests/epic12_gpu/run.py`: **100% PASS** (all 6,291,456 component cases, 14,400 commands, 256-slot cache, reorder, snapshot lookahead).
2. **Emulator Benchmark (Xenia Canary, `mmpork.state`, 120 gameplay frames)**:
   - Batches: 1396 (identical)
   - Pixels: 79,826,600 (identical)
   - Upload pages: 2209 (identical)
   - `batch_timing mode=async position=later`:
     - Baseline: `source_upload = 0.747 ms/batch`
     - Candidate: `source_upload = 0.738 ms/batch`
   - `sampled_batch_cpu_ms`:
     - Baseline: `upload = 0.839 ms/batch`
     - Candidate: `upload = 0.830 ms/batch`
   - `blit_flush_ms`:
     - Baseline: `gpu = 6713.249 ms`
     - Candidate: `gpu = 6706.265 ms`
   - `blit_exec_ms`:
     - Baseline: `draw = 7222.282 ms`
     - Candidate: `draw = 7220.214 ms`
3. **Artifact Archive**:
   - Diagnostic binary archived to `/home/humor/salvia-tests/xex-archive/fbneo-20261014-tile-offset-table-diag.xex`.

## Revert

`git revert <sha>` to restore original `epic12_gpu_tile.h` and `epic12_gpu_tile_alpha.h` and delete this report.
