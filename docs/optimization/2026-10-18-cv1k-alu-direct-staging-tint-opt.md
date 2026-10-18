# CV1000: ALU-Direct Staging Rasterization for Tint63 and Special Tints

Baseline `78c130b4`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

1. **Hardware Profiling of Heavy Bullet Scenes (Slot 8 / Slot 9)**:
   - Diagnostic analysis of the latest real Xbox 360 session (1,457 frames, `batches=4974`, heavy bullet patterns) showed total frame time at 19.74 ms (~50.6 FPS):
     - Main SH3 CPU thread finished its guest execution in **12.25 ms** (`cpu_io = 12.119 ms`, `audio = 0.126 ms`).
     - Main thread was blocked in `draw_sync` for **7.492 ms** waiting for the blitter worker thread.
     - Blitter worker thread ran for **20.00 ms per job** (`worker_busy = 25,467 ms` across 1,273 jobs).
     - GPU hardware execution was only **5.19 ms** (2 Xenos batches/job).
     - CPU staging rasterization consumed the remaining **14.81 ms** (`submit = 14.98 ms/job`).

2. **The Microarchitectural Bottleneck**:
   - Diagnostic counters revealed **988 off-screen staging blits per frame** (`stag_ident = 1,257,385` across 1,273 jobs).
   - Profiling samples showed:
     - ~70% of staging blits are $16 \times 16$ opaque blits with `tint=(63, 63, 63)`.
     - ~30% are $16 \times 16$ transparent blits with `tint=(32, 32, 32)` (identity).
   - In `epic12_staging_write_tint4()`, each pixel previously required 3 memory table lookups (`lut_r`, `lut_g`, `lut_b`).
   - Across 700 tinted blits/frame $\times$ 768 table loads/blit, this resulted in **>530,000 dependent memory loads per frame** on the in-order Xenon CPU, saturating the load-store queue and stalling execution pipelines.

3. **Mathematical Solution for Tint63**:
   - In CV1000 ARGB32 bitmap layout:
     - Alpha: bit 29 (`0x20000000`).
     - Red: bits 23..19 (`0x00f80000`).
     - Green: bits 15..11 (`0x0000f800`).
     - Blue: bits 7..3 (`0x000000f8`).
   - For `tint = 63`, `(c * 63) / 31` mathematically equals $\min(2 \times c, 31)$ for all $c \in [0..31]$.
   - The high bit (bit 4) of each 5-bit channel is isolated by mask `0x0080808000808080ULL`.
   - The saturation mask is `(hi << 1) - (hi >> 4)`, producing `0x00f8f8f800f8f8f8ULL` without channel borrowing.
   - The entire transformation is computed in 64-bit integer registers:
     ```cpp
     static inline UINT64 epic12_tint63_2pix(UINT64 s)
     {
         const UINT64 hi = s & 0x0080808000808080ULL;
         const UINT64 sat = (hi << 1) - (hi >> 4);
         const UINT64 doubled = (s << 1) & 0x00f8f8f800f8f8f8ULL;
         return (s & 0x2000000020000000ULL) | doubled | sat;
     }
     ```
   - This executes purely in register ALU pipelines with **ZERO memory loads**, processing 2 pixels simultaneously per double-word.

## What

1. **`libretro/FBNeo/src/burn/devices/epic12_gpu.h`**:
   - Implemented `epic12_tint63_pixel()`, `epic12_tint63_2pix()`, and `epic12_staging_write_tint63_trans4()`.
   - Introduced `tint_mode` classification:
     - `0`: Identity (`tint == 32`).
     - `1`: Tint63 (`tint == 63`).
     - `2`: Tint0 (`tint == 0`).
     - `3`: General LUT fallback.
   - For modes 0, 1, and 2, LUT table pointers (`lut_r`, `lut_g`, `lut_b`) are set to `NULL`, eliminating global memory pointer fetches.
   - Pipelined 64-bit double-word unrolling for `tint_mode == 1` for $16 \times 16$, $8 \times 8$, $4 \times 4$, and arbitrary dimensions.
   - 4-pixel unrolled chunk processing with `all_p` fast-path for transparent blits.

## Verification

1. **Standalone Bit-Identical Harness (`scratch/test_staging_opt.cpp`)**:
   - 1,664 test combinations across all dimensions (1, 2, 3, 4, 7, 8, 12, 16, 24, 32, 64, 128, 152), all alignments, transparent/opaque, flipx/flipy, and all tint modes.
   - **PASS**: 100% BIT-IDENTICAL on both host x86_64 and big-endian PowerPC QEMU (`powerpc-linux-gnu-g++-13`).

2. **Host PPC Test Suites**:
   - `libretro/FBNeo/tests/sh3_dispatch/run_inline.py`: **100% PASS** (all 8 host suites, 442,729 test cases).
   - `libretro/FBNeo/tests/epic12_gpu/run.py` (Debian PowerPC GCC + QEMU-PPC): **100% PASS** (all 14,400 commands, 10.4M shader cases, full VRAM comparisons).

3. **Build Identity**:
   - Image identity: `fbneo-20261018-1850-b1be3757-diag.xex`
   - SHA256: `4bd98b172d9a7736c0a78d07cd44946c8d183076e959339c124b90ff9bf49555`

