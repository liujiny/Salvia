# CV1000: 128-byte VMX Cache-Line Copy & Aligned Load Specialization (Hardware WC Optimization)

Baseline `43fb6bf6`. Hardware-accelerated memory copy utilizing 128-byte Xenon cache line / Write-Combined buffer bursts and aligned VMX load specialization based on xenosworks/xsdk hardware profiling.

## Why

1. **Hardware Memory Characteristics on Xbox 360 Xenon**:
   - As documented in xenosworks (`xsdk/src/copy.c` and hardware measurements):
     - Scalar `memcpy()` writing into GPU Write-Combined (WC) memory achieves only **185 MB/s** due to premature write-buffer flushes and reordered stores.
     - VMX 128-bit sequential vector stores matching the 128-byte CPU cache line and WC buffer achieve **5,577 MB/s (5.6 GB/s, 30x faster)**.
   - In Salvia's CV1000 GPU pipeline, `epic12_gpu_copy_rows()` in `epic12_gpu_region.h` is executed during every single batch to upload the batch bounding box into the locked Xenos input texture (`epic12_xenos_input`), transferring ~10.3 MB per frame (~2.64 ms/frame).

2. **Identified Bottlenecks in Previous Implementation**:
   - **64-byte vs 128-byte Unroll**:
     - The previous loop in `epic12_gpu_copy_rows()` only unrolled 4 vectors (64 bytes) per iteration.
     - On Xenon, the CPU cache line and write-combining buffer is 128 bytes (8 vectors). Issuing 8 loads in flight followed by 8 consecutive aligned vector stores fills the entire 128-byte WC buffer in a single burst.
   - **Missing Aligned Load Specialization**:
     - When the source pointer `in` is 16-byte aligned (`(in & 15) == 0`), the previous code still unconditionally invoked `__loadunalignedvector()`.
     - On MSVC XDK, `__loadunalignedvector()` generates two `__lvx` vector loads, a permutation instruction (`vperm`/`lvsr`), and mask logic (~3-4 instructions per 16-byte load).
     - For aligned sources, a direct single-instruction `__lvx` reduces vector load instruction overhead by ~60-70%.
   - **Scalar `memcpy` Fallback in Untiled Atlas Upload**:
     - In `libretro/FBNeo/src/burn/devices/epic12_gpu_xbox.h` (line 718), untiled atlas page uploads looped 128 times calling scalar `memcpy()` row-by-row into locked texture Write-Combined memory (`lr.pBits`), suffering from the 185 MB/s memory stall.

## What

1. **`libretro/FBNeo/src/burn/devices/epic12_gpu_region.h`**:
   - **128-byte Unroll (8 vectors / iteration)**:
     - Implemented 128-byte unrolled chunk copying (`x + 127 < body`), issuing all 8 vector loads first to maximize memory latency overlap across Xenon's load pipelines, followed by 8 consecutive volatile aligned vector stores (`__stvx_volatile`).
     - Added 64-byte (`x + 63 < body`) and 16-byte (`x < body`) ladder steps for remaining vector blocks.
   - **Compile-Time Aligned vs Unaligned Load Specialization**:
     - Templated `epic12_gpu_copy_row_vector<bool ALIGNED_IN>()`.
     - When `aligned_in` is true (`(in & 15) == 0`), all loads directly issue `__lvx()`.
     - When unaligned, loads use `__loadunalignedvector()`.
     - The alignment branch is checked once outside the row loop with zero per-row or per-vector branch overhead.
   - **Fast 32-bit Dword Tail Copy**:
     - For remaining non-vector width (`x + 3 < width`), copy 32-bit words using `*(volatile unsigned int*)` instead of single-byte loops, saving 4x store instructions into WC memory.
     - Remaining 0–3 bytes are handled by scalar bytes.

2. **`libretro/FBNeo/src/burn/devices/epic12_gpu_xbox.h`**:
   - Replaced the 128 scalar `memcpy()` loop in untiled atlas page upload (lines 718-721) with `epic12_gpu_copy_rows()` and `build(source, 8192)`.

3. **`libretro/FBNeo/tests/epic12_gpu_region/test.cpp`**:
   - Added `__lvx` intrinsic mock and `alignedLoads` counter verification to prove that both aligned and unaligned vector load paths execute correctly without out-of-range memory accesses.

## Verification

1. **Unit Test Suites (Host & PPC Big-Endian)**:
   - `libretro/FBNeo/tests/epic12_gpu_region/run.py`:
     - **Host**: `PASS: 4044 rectangles; 10112328 exact bytes; 196769 vector stores, every one 16-byte aligned and inside its row; 0 unaligned stores; 118740 aligned loads; 78029 unaligned vector loads; 20553 prefetch hints, all inside the rectangle; little-endian`
     - **PPC (QEMU Big-Endian)**: `PASS: 4044 rectangles; 10112328 exact bytes; 196769 vector stores, every one 16-byte aligned and inside its row; 0 unaligned stores; 118740 aligned loads; 78029 unaligned vector loads; 20553 prefetch hints, all inside the rectangle; big-endian`
     - 100% bit-exact byte verification across all alignments and pitch combinations; 0 unaligned stores, 0 out-of-range reads or writes.
   - `libretro/FBNeo/tests/epic12_gpu/run.py` (QEMU-PPC):
     - **100% PASS**: all 14,400 commands, 10.4M shader cases, full VRAM comparisons identical.
   - `libretro/FBNeo/tests/epic12_gpu_untile/run.py`:
     - **100% PASS**: 6,820 rectangles, 66.3M pixels on both host and PPC.
   - `libretro/FBNeo/tests/epic12_gpu_cache/run.py`:
     - **100% PASS**: 12,000 batches on both host and PPC.
   - `libretro/FBNeo/tests/epic12_gpu_tile/run.py`:
     - **100% PASS**: 1,536 pages, 25.1M pixels on both host and PPC.
   - `libretro/FBNeo/tests/epic12_gpu_tile_alpha/run.py`:
     - **100% PASS**: 1,152 pages, 18.8M pixels on both host and PPC.

## Build and Distro360 Handoff

(Will be populated after XEX compilation)
