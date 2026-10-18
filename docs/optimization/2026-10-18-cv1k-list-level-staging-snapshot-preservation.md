# CV1000: List-Level Staging Snapshot Preservation and Safe Wrapping

Baseline `55c767fb`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

1. **Premature Batch-Level Snapshot Eviction**:
   - In previous builds, `epic12_gpu_snapshot_reset()` was invoked inside `epic12_gpu_flush_bin()` at every intermediate batch flush.
   - When a single frame generated multiple GPU batches (e.g. 2 batches per frame), flushing Batch 0 wiped out `s_snap_page_x` and invalidated all snapshot pages in the atlas cache.
   - Consequently, Batch 1 was forced to re-allocate snapshot space and re-upload texture pages from scratch, preventing snapshots and texture atlas slots from surviving across intermediate batch flushes within the same list/frame.
   - By deferring snapshot reset to list boundaries (`EPIC12_GPU_FLUSH_END_LIST` and `OTHER`), snapshot pages remain valid and cache-hot across all intermediate batches in the frame.

## What

1. **`libretro/FBNeo/src/burn/devices/epic12_gpu.h`**:
   - Removed `epic12_gpu_snapshot_reset()` from `epic12_gpu_flush_bin()`.
   - Placed `epic12_gpu_snapshot_reset()` inside `epic12_gpu_flush()` gated on `reason == EPIC12_GPU_FLUSH_END_LIST || reason == EPIC12_GPU_FLUSH_OTHER`.
   - Added safe wrap recovery in `epic12_gpu_snapshot_alloc()`: if an exceptionally dense frame ever exhausts all 128 available snapshot pages (`s_snap_page_y >= 2`), it performs a targeted snapshot reset to recycle page coordinates safely rather than returning an unhandled failure.

## Verification & Artifacts

1. **Artifacts & Identity**:
   - Tag: `list-snap-reset`
   - Flavor: `diag` (`SALVIA_FBNEO_DIAGNOSTICS 1`)
   - SHA256: `8e0098ff180a7e7603d6e943d5027f9674f505469ac0e7d97f64d8de62d38377`
   - Size: 34,701,312 bytes
   - Archive: `/home/humor/salvia-tests/xex-archive/fbneo-20261018-1017-list-snap-reset-diag.xex`
   - Deployed to Xbox 360: `/Usb0/Salvia-360/fbneo.xex` via Aurora FTP / SITE LAUNCH.
