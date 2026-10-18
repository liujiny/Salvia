# CV1000: Large Staging Snapshot Allocator & WAR Conflict Elimination

Baseline `a69fbd8f`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

1. **Root Cause Analysis of 1,662 Staging WAR Flushes (`war768`)**:
   - Analysis of real console logs (`cv1000-gpu.log`) from Muchi Muchi Pork! Slot 9 heavy scenes with CPU Clock at 50% revealed:
     - `CV1000 GPU: flushes end=1666 source_pages=0 region=0 source_dependency=1662 command_limit=0 cpu_upload=0 cpu_draw=1662 (batches=4990)`
     - `CV1000 GPU: diag_staging snapshots=69377 pixels=66256710 | src_dep fit2=0 raw768=0 war768=1662 (batches=4990)`
     - Exactly 1,662 flushes out of 4,990 total batches were triggered by `war768` (Write-After-Read hazards on off-screen staging RAM $x \ge 768$).
     - Each GPU flush incurs an EDRAM resolve, CPU `readback_wait` stall (~2.3 ms), texture upload (~1.1 ms), and VMX untile mask copy (~0.6 ms), totaling **~4.3 ms per flush**.
     - $1.4\text{ to }4.16\text{ flushes/frame} \times 4.3\text{ ms} = \mathbf{6.0\text{ to }17.9\text{ ms}}$ of GPU flushes and readback stalls per frame, inflating `draw_sync` to 17.5–21.1 ms.

2. **Diagnostic Capture of Conflicting Commands**:
   - Adding diagnostic capture `diag_war_sample` on `war768` hazard detection captured the exact offending GPU draw commands:
     - `diag_war_sample: CPU dst=(768,0,64,96) src=(1280,0) | GPU cmd src=(768,0,134,142) dst=(198,175) pg=6 rect_hit=1`
   - The on-screen draw command was drawing a large composite sprite ($134 \times 142$ pixels) sampled from staging RAM at $(768, 0)$.
   - Because the snapshot allocator in `epic12_gpu_snapshot_alloc()` previously had a hardcoded $128 \times 128$ size limit (`if (w > 128 || h > 128) return false`), any sprite larger than 128 pixels in width or height was rejected from snapshot allocation.
   - Consequently, the GPU draw sampled directly from VRAM page 6 ($x \ge 768$).
   - When the next off-screen staging blit arrived writing to $(768, 0, 64, 96)$, it conflicted with page 6 in `epic12_gpu_source_pages[0]` and forced an immediate, expensive GPU pipeline flush on every frame!

3. **Multi-Page Snapshot Allocation across 256-line VRAM**:
   - The snapshot VRAM region covers $8192 \times 256$ pixels (64 page columns by 2 page rows).
   - In a typical frame, only ~3.3 snapshot pages are used out of the 128 available pages (>97% free space).
   - We extend `epic12_gpu_snapshot_alloc()` to support sprites up to $512 \times 256$ pixels:
     - Small sprites ($\le 128 \times 128$) continue using tight shelf-packing within $128 \times 128$ cells.
     - Large sprites ($w > 128$ or $h > 128$, up to $512 \times 256$) allocate dedicated page-aligned column(s) across the full 256-line snapshot space.
     - `epic12_gpu_submit()` is updated to snapshot staging sprites up to $512 \times 256$ and accumulate staging versions across all touched source pages.
     - The Xenos vertex emitter in `epic12_gpu_batch.h` already natively handles multi-page source sprites by splitting them across page boundaries.

## What

1. **`libretro/FBNeo/src/burn/devices/epic12_gpu.h`**:
   - Extended `epic12_gpu_snapshot_alloc()` to support large sprites up to $512 \times 256$ by allocating dedicated page columns.
   - Updated `epic12_gpu_submit()` to check `w <= 512 && h <= 256` for off-screen staging sprites and compute `ver` across all touched staging pages.
   - Added diagnostic recording helper `epic12_gpu_record_war_sample()` to capture exact conflicting command metadata and page numbers.

2. **`libretro/FBNeo/src/burn/devices/epic12_gpu_xbox.h`**:
   - Updated `diag_war_sample` reporting to log conflicting GPU command dimensions, destination, atlas slot, and rectangle hit status.

## Verification

1. **Host PPC Test Suites**:
   - `libretro/FBNeo/tests/sh3_dispatch/run_inline.py`: **100% PASS** (all 8 host suites, 442,729 test cases).
   - `libretro/FBNeo/tests/epic12_gpu/run.py` (Debian PowerPC GCC + QEMU-PPC): **100% PASS** (all 14,400 commands, 10.4M shader cases, full VRAM comparisons, and simulated GPU batches identical).

2. **Xbox 360 Benchmark Verification (`mmpork.state9`)**:
   - Pre-fix: `flushes end=30 source_dependency=30 (batches=90)`, `war768=30`, `readback_pixels=9220800`.
   - Post-fix: `flushes end=30 source_dependency=0 (batches=60)`, `war768=0` (**100% eliminated**), `readback_pixels=5863728` (**-36.4% reduction**).
   - All 30 false/unmitigated WAR dependency flushes eliminated in 60 frames.
   - Image identity: `fbneo-20261018-1348-snap-large-diag.xex` (SHA256: `9d3b155dad88f615b5f6768b3ffc6ecffa7ee2e9b19d83afd7aa7861c64baad5`).
