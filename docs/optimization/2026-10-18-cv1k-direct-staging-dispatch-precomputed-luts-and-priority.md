# CV1000: Direct Staging Dispatch, Precomputed Tint Tables, and Thread Priority for 60 FPS

Baseline `3cce93d8f97452f55a05c334e87dd9e757f6765c`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

1. **20 FPS Bottleneck on Console (`mmpork.state6`, `mmpork.state7`)**:
   - In save states 6 and 7, dense gold pig medal scatterings generate **2,838 to 3,080 draw calls per frame**, over 91.5% to 93.4% of which are off-screen staging blits ($x \ge 768$, `blend == 0`).
   - Detailed analysis of the real console log (`cv1000-gpu-real-console-latest.log`) revealed:
     - Peak frame took **99.6 ms**, with **84.96 ms blocking inside `draw_sync`** waiting for the blitter worker thread to finish processing the draw list.
     - Blitter worker thread CPU execution took **39,639 ms across 2,513,298 blits** (~15.7 µs per blit).

2. **Root Causes Identified**:
   - **Stack Parameter Spilling (LHS Hazards on Xenon)**:
     `epic12_gpu_submit()` accepted 17 parameters (`int flipx, int transparent, int blend, int smode, int dmode, const rectangle *clip, UINT32 *gfx, int src_x, int src_y, const int dst_x_start, const int dst_y_start, int dimx, int dimy, const int flipy, const UINT8 s_alpha, const UINT8 d_alpha, const clr_t *tint_clr`).
     On PowerPC 64/32 ABI, only 8 arguments fit into registers `r3..r10`; 9 arguments were spilled onto the stack for every single blit. On Xenon's in-order execution pipeline, reading spilled arguments from the stack immediately after writing causes a Load-Hit-Store (LHS) hazard stalling the core for 40-60 cycles per call (3,000 times/frame = ~150,000 to 180,000 wasted cycles/frame).
   - **Dynamic Stack Table Recomputation Inside Draw Loop**:
     When `tint != 32`, 32-entry tables `lut_r`, `lut_g`, `lut_b` were populated on the stack inside the blitter function on every blit (96 shifts, table reads, and stack writes), costing ~500 cycles per blit.
   - **CRT `memcpy` Overhead on 1-Pixel Slices**:
     Opaque staging blits ($w=16, h=1$) invoked CRT `memcpy(d, s, 64)`, introducing function call overhead and alignment setup for tiny 64-byte chunks.
   - **Blitter Thread Priority Starvation**:
     `BurnRenderWorker` ran at normal thread priority (`THREAD_PRIORITY_NORMAL`), competing with system background worker threads on physical Core 1.
   - **Lack of Early Clip Rejection in `gfx_draw`**:
     Out-of-bounds sprites passed through full register decoding before reaching software clipping.

## What

1. **`libretro/FBNeo/src/burn/devices/render_worker.h`**:
   - Added `SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST)` in `BurnRenderWorker::run()` so the blitter thread runs at top priority without thread scheduling starvation on Core 1 (HW Thread 2).

2. **`libretro/FBNeo/src/burn/devices/epic12_gpu.h`**:
   - **Precomputed Universal Tint LUTs**:
     Initialized `epic12_gpu_tint_r_lut[64][32]`, `epic12_gpu_tint_g_lut[64][32]`, `epic12_gpu_tint_b_lut[64][32]` (24 KB total, fitting entirely within Xenon's 32 KB L1 data cache). Tinting now resolves via 3 direct pointer assignments (`lut_r = epic12_gpu_tint_r_lut[tint_r & 63]`), eliminating all dynamic stack computation.
   - **Vectorized 64-bit Opaque Transfer**:
     For opaque staging blits ($!transparent$), replaced `memcpy` with direct 64-bit integer copy (`UINT64*`) unrolled for 16-pixel chunks ($w=16$), transferring 64 bytes in 8 store instructions.
   - **Direct `epic12_gpu_staging_blit()` Fast Path**:
     Accepts only 12 scalar arguments, fitting within registers `r3..r10` and minimizing stack pressure.

3. **`libretro/FBNeo/src/burn/devices/epic12.cpp`**:
   - **Early Bounding-Box Clip Rejection**:
     Immediately drops sprites completely outside active clip boundaries before decoding sprite parameters (`x > m_clip.max_x || y > m_clip.max_y || x + dimx - 1 < m_clip.min_x || y + dimy - 1 < m_clip.min_y`).
   - **Direct Staging Fast Dispatch**:
     In `gfx_draw()`, off-screen non-blended staging blits ($x \ge 768, !blend$) directly dispatch to `epic12_gpu_staging_blit()`, completely bypassing `epic12_gpu_submit()` and eliminating 17-parameter stack spilling.

## Verification & Results

1. **Host PPC Test Suite**:
   - Executed `libretro/FBNeo/tests/epic12_gpu/test.cpp` natively and under `qemu-ppc`:
     - 6,291,456 component cases on unified and fast/feedback shaders: PASS
     - 4,194,304 additive component cases on dmode 7 and 3: PASS
     - 4,096 plain shader cases: PASS
     - Clipped CPU invalidation & 14,400 command comparison: PASS
     - 100% bit-exact pass across all tests.
