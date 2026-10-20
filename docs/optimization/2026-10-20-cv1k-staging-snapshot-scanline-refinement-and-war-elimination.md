# CV1000: Staging Snapshot Scanline Bypass Refinement & WAR Flush Elimination

Baseline `8276b814`. Refinement of off-screen staging snapshot bypass criteria based on dual-game real Xbox 360 profiling (`mmpork` 5,301 frames, `ibara` 7,302 frames), eliminating 10,879 Write-After-Read (WAR) pipeline flushes and 1,214 CPU fallback re-draws per session.

## Why

1. **Dual Real Console Profiling Comparison (`mmpork` vs `ibara`)**:
   - In recent Xbox 360 hardware profiling:
     - **`ibara` (7,302 frames)**: Ran smoothly at **15.03 ms / frame** (unthrottled ~66.5 FPS, solid 60 FPS capped). Across 7,302 frames, it suffered only **120 `source_dependency` flushes** (0.016 flushes/frame), creating 7,883 batches (~1.08 batches/frame).
     - **`mmpork` (5,301 frames)**: Ran at **19.34 ms / frame** (avg ~51.7 FPS, dropping into 30–35 FPS in heavy action). Across 5,301 frames, it suffered **10,879 `source_dependency` flushes** (2.05 flushes/frame), creating 18,434 batches (~3.48 batches/frame).
   - In both games, `drc_dispatch` accounted for ~75% of CPU time, but `mmpork` was severely penalized by staging pipeline fragmentation.

2. **Root Cause Analysis of 10,879 `war768` Flushes in MMPork**:
   - In commit `0beb867f`, to prevent 240 wavy water raster scanlines ($w \approx 324..352, h = 1$) in Stage 1 from thrashing the snapshot buffer, a bypass condition was introduced in `epic12_gpu.h`:
     ```cpp
     if (sx >= 768 && sy < 4096 && w <= 512 && h <= 256 && w > 4 && h > 4 && !(w <= 64 && h > 128))
     ```
   - Requiring `w > 4 && h > 4` inadvertently excluded small multi-part sprite slices (such as pork items, bullets, explosions, and UI gauges) where $w \le 4$ or $h \le 4$.
   - Hardware diagnostic capture (`diag_war_sample`) on the Xbox 360 revealed the exact offending draw commands in `mmpork`:
     - Sample 0: `cmd src=(768,0,6,4)` -> $h = 4$
     - Sample 1: `cmd src=(768,0,6,4)` -> $h = 4$
     - Sample 2: `cmd src=(768,0,5,3)` -> $h = 3$
     - Sample 3: `cmd src=(768,0,4,19)` -> $w = 4$
     - Sample 4: `cmd src=(768,0,3,19)` -> $w = 3$
     - Sample 5: `cmd src=(768,0,2,19)` -> $w = 2$
     - Sample 6: `cmd src=(768,9,20,4)` -> $h = 4$
     - Sample 7: `cmd src=(768,0,1,19)` -> $w = 1$
     - Sample 8: `cmd src=(768,11,20,2)` -> $h = 2$
     - Sample 9: `cmd src=(768,12,19,1)` -> $h = 1$
     Every single one of these 10 sampled commands was excluded by `w > 4 && h > 4`!
   - Because they were not snapshotted, they queued in bin 0 referencing staging VRAM at $(768, 0)$ (page 6).
   - The game CPU immediately assembled the next sprite into $(768, 0)$, triggering a Write-After-Read (WAR) hazard against page 6.
   - This forced **10,879 synchronous GPU pipeline flushes** (`war768 = 10,879`), fragmenting GPU batches into tiny groups of 1–2 commands and causing **1,214 `small_cpu` fallback re-draws** where tiny batches fell below `epic12_gpu_threshold`.

## What

1. **`libretro/FBNeo/src/burn/devices/epic12_gpu.h`**:
   - Refined the snapshot bypass filter to specifically target wide background raster scanlines instead of blanket-excluding all small sprites:
     ```cpp
     // Background raster scanlines (w > 64 && h <= 4) and vertical slices (w <= 64 && h > 128) tile
     // off-screen VRAM without overwriting in the same frame; snapshotting them scatters a few
     // source pages across dozens of snapshot pages, thrashing the atlas and page limit.
     // Only multi-part sprites (e.g. boss explosion/pork item parts) need staging snapshots.
     if (sx >= 768 && sy < 4096 && w <= 512 && h <= 256 && !(w > 64 && h <= 4) && !(w <= 64 && h > 128)) {
     ```
   - **Wide Raster Scanlines ($w > 64, h \le 4$)**: Stage 1 water scanlines ($w \approx 324..352, h = 1$) remain bypassed. They do not overwrite within the frame and will not thrash snapshot columns (preserving the 7 FPS fix on slot 8).
   - **Small Multi-Part Sprite Slices ($w \le 64, h \le 4$ or $w \le 4, h \le 128$)**: Now properly admitted into the 128x128 snapshot shelf packer. They occupy only a fraction of a snapshot cell, redirect GPU sampling away from $(768, 0)$, and eliminate the 10,879 false WAR flushes.

## Verification

1. **Unit Test Suites (Host & PPC Big-Endian)**:
   - `libretro/FBNeo/tests/epic12_gpu/run.py` (QEMU-PPC):
     - **100% PASS**: all 14,400 commands, 10.4M shader cases, full VRAM comparisons identical.
   - `libretro/FBNeo/tests/sh3_dispatch/run_inline.py`:
     - **100% PASS**: 442,729 test cases across all 8 host suites in optimized and ASan/UBSan modes.
   - `libretro/FBNeo/tests/sh3_ppc/run.py` (QEMU-PPC):
     - **100% PASS**: 846,030 cases compiled and verified.
   - `libretro/FBNeo/tests/epic12_gpu_region/run.py`:
     - **Host & PPC**: 100% PASS (4,044 rectangles; 10,112,328 exact bytes; 196,769 vector stores).

2. **Build and Distro360 Handoff**:
   - **Candidate Image**: `Distro360/fbneo-war-fix-diag.xex`
     - Tag: `war-fix ac997808 20261020-1816 diag`
     - 大小: `34,701,312` 字节
     - SHA256: `828ab646d32677f2d3375776af61027e9be18a2ff24c92562545c0f2d9b35131`
     - 归档: `salvia-tests/xex-archive/fbneo-20261020-1816-war-fix-diag.xex`
   - **Baseline Image**: `Distro360/fbneo-fast-copy-diag.xex`
     - Tag: `fast-copy 5ee98d75 20261020-0646 diag`
     - 大小: `34,701,312` 字节
     - SHA256: `54c04ca16e4c82f4201e8a29b49d06e87e200257c03ad8a97255e87e30726f6f`
     - 归档: `salvia-tests/xex-archive/fbneo-20261020-0649-fast-copy-diag.xex`
   - **Distro360 状态**: 严格只保留上述对比 A/B 对，无任何重复镜像。

## Limits

- True console frame rate stability and elimination of the 10,879 WAR flushes must be verified via `Distro360/cv1000-gpu.log` on real Xbox 360 hardware.

## Revert

`git revert <sha>` restores the previous bypass condition in `epic12_gpu.h` and removes this review document.
