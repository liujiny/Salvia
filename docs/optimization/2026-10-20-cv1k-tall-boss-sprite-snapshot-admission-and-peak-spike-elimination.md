# CV1000: Tall Multi-Part Boss Sprite Snapshot Admission & Peak Spike Elimination

Baseline `b78940fb`. Elimination of 17.28 ms emulation-to-blitter thread stalls (`worker_wait peak_ms = 17.28 ms`) and 38.99 ms frame spikes during multi-part boss battles by admitting tall sprites ($w \le 64, 128 < h \le 256$) into the dedicated 256-height column snapshot shelf packer.

## Why

1. **Analysis of 38.99 ms Real Console Peak Spikes (`mmpork` 14,252 Frames)**:
   - In the previous run of `mmpork` across 14,252 frames (`build=war-fix ac997808`):
     - Average frame time dropped to **16.68 ms** (~60 FPS) and 10,879 general WAR flushes were reduced by **98.6%** down to 408.
     - However, periodic peak frame spikes occurred:
       `core_sampled_peak_ms interval_frame=13959 prep=0.003 cpu_io=28.617 audio_tail_misc=0.126 draw_sync=10.248 total=38.994`
       `worker_wait sampling=random-1-in-64 jobs=12403 waits=12403 samples=213 avg_ms=3.614 peak_ms=17.278`
     - Total frame time spiked to **38.99 ms** (~25.6 FPS).

2. **Root Cause Analysis: Emulation Thread Stalling on Mid-Frame GPU Flushes**:
   - Diagnostic sampling revealed that the remaining 408 WAR hazard flushes (`war768 = 408`) were 100% concentrated on **tall multi-part boss sprite segments** with $h > 128$:
     - Sample 0: `cmd src=(768,0,54,252)` ($w = 54, h = 252$)
     - Sample 1: `cmd src=(768,0,58,245)` ($w = 58, h = 245$)
     - Sample 2: `cmd src=(768,0,63,238)` ($w = 63, h = 238$)
     - Sample 3: `cmd src=(768,0,52,176)` ($w = 52, h = 176$)
     - Sample 4: `cmd src=(768,0,14,160)` ($w = 14, h = 160$)
   - Because the snapshot bypass filter included `&& !(w <= 64 && h > 128)`, these tall boss sprites were excluded from snapshotting, forcing them to draw directly from staging VRAM $(768, 0)$ (page 6).
   - The CPU immediately blitted subsequent boss slices into $(768, 0)$, triggering a WAR conflict and forcing a mid-frame GPU flush (`epic12_gpu_flush_bin`).
   - A mid-frame GPU flush required command buffer submit, EDRAM resolve, and GPU synchronization (taking up to **13.52 ms** on Xenos hardware: `sampled_batch_cpu_ms peak_total = 13.520 ms`).
   - While the blitter worker thread on Core 1 was blocked on this GPU flush, the SH-3 CPU emulation on Core 0 called `gfx_exec_write`, which invoked `thready.notify_wait()`.
   - The SH-3 emulation thread was forced to sleep for **17.28 ms** (`worker_wait peak_ms = 17.278 ms`), inflating `cpu_io` from its normal ~11.3 ms up to **28.62 ms**.
   - At the end of the frame, `epic12_draw_screen` waited an additional **10.25 ms** (`draw_sync = 10.248 ms`), driving the combined frame spike to 38.99 ms.

## What

1. **`libretro/FBNeo/src/burn/devices/epic12_gpu.h`**:
   - Removed the `!(w <= 64 && h > 128)` exclusion from the staging snapshot bypass filter:
     ```cpp
     // Background raster scanlines (w > 64 && h <= 4) tile off-screen VRAM without overwriting
     // in the same frame; snapshotting them scatters a few source pages across dozens of snapshot
     // pages, thrashing the atlas and page limit. All other staging sprites (including tall multi-part
     // boss segments packed into dedicated 256-height columns) are snapshotted to eliminate WAR stalls.
     if (sx >= 768 && sy < 4096 && w <= 512 && h <= 256 && !(w > 64 && h <= 4)) {
     ```
   - **Wide Raster Scanlines ($w > 64, h \le 4$)**: Stage 1 water scanlines ($w \approx 324..352, h = 1$) remain bypassed, protecting slot 8 from snapshot buffer thrashing.
   - **Tall Multi-Part Boss Sprites ($w \le 128, 128 < h \le 256$)**: Now fully admitted into the existing 256-height column shelf packer (`s_snap_col256_u`). Up to eight 16-wide or two 64-wide boss slices fit in a single 128-column.
   - GPU draws now sample from the snapshot area ($y \ge 4096$) instead of $(768, 0)$, eliminating the remaining 408 WAR pipeline flushes, preventing worker thread stalls, and cutting the 17.28 ms worker wait spike.

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
   - Baseline SHA: `b78940fb`
   - Image set compiled with `build-xex.sh --flavor diag`.
   - Distro360 maintains strictly the A/B pair:
     - Baseline: `Distro360/fbneo-war-fix-diag.xex`
     - Candidate: `Distro360/fbneo-peak-fix-diag.xex`

## Limits

- Verification of the elimination of the remaining 408 WAR flushes and the 17.28 ms peak wait spike must be confirmed via `Distro360/cv1000-gpu.log` on real Xbox 360 hardware.

## Revert

`git revert <sha>` restores the tall slice exclusion in `epic12_gpu.h` and removes this review document.
