# CV1000 Optimization: Inline Sprite Draw Dispatch and Consecutive Sprite Streaming

- Date: 2026-10-14
- Baseline Commit: `914ab9976d398144da9a93e514d8fad092ea511a`
- Target: `libretro/FBNeo/src/burn/devices/epic12.cpp`
- Diagnostic XEX: `salvia-tests/xex-archive/fbneo-20261014-1800-gfx-draw-inline-loop-diag.xex` (SHA256: `53d4396faa33c6b62ad023500070039f3800680d9883b06def2638d266be6691`)

## Rationale
In Cave CV1000 emulation (`gfx_exec` and `gfx_create_shadow_copy`), display lists consist predominantly (over 98.8%) of sprite draw commands (`0x1000`).
Previously:
1. `gfx_exec` read each command's opcode using `READ_NEXT_WORD(&addr)` which incremented `addr` by 2, tested `data & 0xf000 == 0x1000`, immediately subtracted 2 (`addr -= 2`), and invoked `gfx_draw(&addr)`.
2. Because `gfx_draw` and `gfx_draw_shadow_copy` were not marked `inline`, MSVC generated out-of-line functions requiring stack frame setup, argument passing, and stack pointer dereferences. Taking `&addr` forced `addr` into stack memory rather than staying in a CPU register across iterations.
3. Every single sprite in a sprite list incurred a full return to the outer `while(1)` switch loop, requiring a memory read, pointer advance, branch, subtraction, and call instruction.

## Changes Made
1. **Marked Functions as `static inline`**:
   - `gfx_draw(UINT32 *addr)` and `gfx_draw_shadow_copy(UINT32 *addr)` are now explicitly `static inline`, allowing the compiler to keep loop state and pointers in CPU registers across calls without function call overhead.
2. **Consecutive Sprite Streaming**:
   - In `gfx_exec`, when a `0x1000` command is detected, execution loops over consecutive `0x1000` commands in place (`do { gfx_draw(&addr); } while ((m_ram16_copy[(addr & m_main_rammask) >> 1] & 0xf000) == 0x1000);`), completely bypassing `READ_NEXT_WORD`, `addr -= 2`, and the outer `switch` overhead for the entire run of sprites.
   - Applied identical streaming in `gfx_create_shadow_copy` for `m_ram16`.

## Verification & Test Results
- PPC Blit Test Suite (`libretro/FBNeo/tests/epic12_blit/run.py`):
  - PASS: 65,536 tint/alpha rounding cases & 1,000,000 packed colour sums.
  - PASS: 5,000 image cases (tint, transparency, clipping, flips, source wrap, overlapping VRAM, and delay).
- PPC GPU Test Suite (`libretro/FBNeo/tests/epic12_gpu/run.py`):
  - PASS: 6,291,456 component cases on unified and fast/feedback shaders.
  - PASS: 14,400 commands full VRAM & delay comparison.

## Performance Comparison (120-Frame Diagnostic Window)

| Metric | Baseline (`914ab997`) | Optimized (`gfx-draw-inline-loop`) | Delta |
| :--- | :--- | :--- | :--- |
| `draw` execution | 7256.068 ms | **7225.568 ms** | **-30.500 ms (-0.25 ms/frame)** |
| `submit` execution | 2880.300 ms | **2852.695 ms** | **-27.605 ms** |
| `worker_busy` | 7693.272 ms | **7650.960 ms** | **-42.312 ms (-0.35 ms/frame)** |
| `gpu` rasterization | 6760.047 ms | **6710.673 ms** | **-49.374 ms (-0.41 ms/frame)** |
| `upload` | 117.547 ms | **114.755 ms** | **-2.792 ms** |
| `sampled_batch_cpu_ms peak` | 9.955 ms | **9.526 ms** | **-0.429 ms** |
| `worker_wait avg` | 9.446 ms | **8.822 ms** | **-0.624 ms** |
| `worker_wait peak` | 18.888 ms | **17.642 ms** | **-1.246 ms** |
