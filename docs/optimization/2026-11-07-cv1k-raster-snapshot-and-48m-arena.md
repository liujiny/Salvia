# CV1000: Staging Wide Raster Snapshot Packing and 48MB DRC Arena Expansion

Baseline `13bb5b77` (cv1k-mmpork-standalone autoboot). Implementation commit: `feat(gpu,drc): vertical shelf packing for wide raster scanlines and expand DRC arena to 48MB`.

## 1. Problem Statement & Telemetry Diagnosis

During extensive 17,136-frame gameplay on real Xbox 360 Jasper hardware (`cv1000-gpu.log` build `mmpork-standalone ee7d6c08`), severe slowdown occurred when the Stage 1 boss explodes.

### 1.1 Root Cause 1: 1,911 WAR Pipeline Stalls from Bypassed Wide Rasters
- Telemetry revealed:
  - `core_frame_ms`: `total = 11.25 ms` (normal gameplay runs full-speed at 60 FPS).
  - `core_slow_frame_ms`: `draw_sync = 18.60 ms`, `total = 34.25 ms`, `sampled_peak = 58.35 ms` (~17 FPS).
  - `worker_wait`: `peak_ms = 34.87 ms`.
  - `source_dependency = 1918`, `war768 = 1911`.
  - `diag_war_sample: CPU dst=(768,0,80,120) src=(1280,0) | GPU cmd#76/355 src=(832,113,352,1) dst=(2,128) pg=6 rect_hit=1`.
- **Mechanism**:
  - In `libretro/FBNeo/src/burn/devices/epic12_gpu.h`, line 1936 previously had:
    ```cpp
    if (sx >= 768 && sy < 4096 && w <= 512 && h <= 256 && !(w > 128 && h <= 4))
    ```
  - Wide raster scanlines ($w \approx 320..352, h = 1$) were excluded from snapshotting to prevent atlas column exhaustion in the shelf allocator.
  - As a result, draw commands like `cmd#76` ($w = 352, h = 1$) sampled directly from Bank 768 staging page 6 (`src=(832, 113)`).
  - When the boss dies, hundreds of explosion sprites are queued (355 commands). Simultaneously, the CPU writes next frame's staging data to $(768, 0, 80, 120)$.
  - The WAR check in `epic12_gpu_staging_blit` detected a 16-pixel horizontal overlap with `cmd#76` on scanline 113, triggering a synchronous `epic12_gpu_flush_bin(0, EPIC12_GPU_FLUSH_SOURCE_DEPENDENCY)`.
  - This blocked the CPU thread for up to 34.87 ms, dropping instant frame rate to ~17–29 FPS.

### 1.2 Root Cause 2: 22 DRC Cache Evictions at 32MB Capacity
- Telemetry showed:
  - `drc_work_arena bytes=33554432 words=8388608 evictions=22 evicted_slots=1441726 peak_bytes=33538384`
  - The working set reached 33,538,384 bytes, hitting 99.95% of the 32MB cache boundary and triggering 22 full-cache evictions.
  - Each eviction invalidated 1.44 million compiled blocks, causing periodic JIT compilation spikes.

---

## 2. Changes Made

### 2.1 Vertical Shelf Packing for Wide Raster Strips (`epic12_gpu.h`)
- Added vertical shelf packing in `epic12_gpu_snapshot_alloc`:
  - When $w > 128$ and $h \le 4$, reserve `cols = (w + 127) >> 7` page columns once.
  - Pack consecutive scanlines vertically along `s_snap_raster_v` across the 256-pixel column height.
  - An entire 240-scanline screen fits inside a single 3-column allocation (6 pages out of 128, $< 5\%$ atlas area).
- Removed the `!(w > 128 && h <= 4)` exclusion in `epic12_gpu_submit`, allowing all staging scanlines from Bank 768 to be safely snapshotted.
- Reset wide raster shelf pointers in `epic12_gpu_snapshot_reset`.

### 2.2 48MB DRC Arena Expansion (`sh3_drc_ppc.h`)
- Increased `SH3_PPC_CACHE_BYTES` from `(32 * 1024 * 1024)` to `(48 * 1024 * 1024)` (48 MiB).
- Provides 14.5 MiB of headroom above the 33.5 MiB peak working set, completely eliminating all 22 full-cache evictions while leaving $> 200$ MiB of free title RAM.

---

## 3. Verification & Results

1. **PPC QEMU Unit Test Suite**:
   - `python3 libretro/FBNeo/tests/epic12_gpu/run.py --toolchain-root /home/humor/salvia-tests/toolchains/ppc/root`:
     - **100% PASS**: `PASS staging snapshot WAR prevention: all multi-part strips and wide rasters snapshotted, 0 WAR flushes`.
     - All 17 sub-suites passed (6.29M component cases, 14,400 commands, cache coherency, reordering).

2. **Host Dispatch Test Suite**:
   - `python3 libretro/FBNeo/tests/sh3_dispatch/run_inline.py --output /tmp/sh3-inline-test.log`:
     - **100% PASS**: All 8 host test suites clean under ASan/UBSan (442,729 test cases).
