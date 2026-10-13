# CV1000: pre-shifted tables and row prefetch for epic12_blend_fixed

Baseline `2886a69f`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

The console profile from `Distro360/cv1000-gpu.log` (commit `371911ee`, window 618 frames in `mmpork`) split the blitter execution time:

| term | ms/window | ms/frame | calls/frame | notes |
| --- | --- | --- | --- | --- |
| `worker_busy` | 16,286.58 | **26.35 ms** | 1 | Sole critical bottleneck gating frame rate |
| `draw` | 15,439.80 | 24.98 ms | 1149 | Whole draw command execution |
| `submit` | 9,381.33 | 15.18 ms | 1149 | Includes synchronous GPU flushes |
| `gpu` flush | 8,346.27 | 13.51 ms | 8.5 | Pure Xenos command execution |
| queue construction | 1,035.06 | 1.67 ms | 1149 | `submit - gpu` |
| `soft` | 3,671.34 | **5.94 ms** | 88.6 | Software rasterizer for refused commands |
| `replay` | 3,284.40 | **5.31 ms** | 208 | CPU rasterizer for batches < 8192 px |
| walk residual | 846.78 | 1.37 ms | | `worker_busy - draw` |

Key observations:
1. Pure queue construction (`1.67 ms`) and command walk (`1.37 ms`) are small fractions of the frame.
2. The software rasterizers on the CPU (`soft` 5.94 ms + `replay` 5.31 ms) account for **11.25 ms/frame** (42.7% of `worker_busy`).
3. Census data (`2026-10-11-cv1k-blit-path-census.md`) demonstrated that 74.5% to 98.3% of all replayed pixels pass through `epic12_blend_fixed()` (`epic12_fast_blit.h`). For refused draws in `soft`, the dominant blend path (`s_mode==0 && d_mode==0`) also executes `epic12_blend_fixed()`.
4. In `epic12_blend_fixed()`, each visible pixel previously executed:
   - 3 separate byte lookups (`lbzx`) from `epic12_source_scale`
   - 3 separate dependent shifts (`<< 19`, `<< 11`, `<< 3`)
   - Dependent destination table lookups and shifts when `!FullDestination`
   - No cache prefetch across rows separated by 32 KB VRAM strides. On in-order Xenon (PowerPC 64), dependent shifts immediately following byte loads cause pipeline stalls.

## What

1. **Pre-shifted 32-bit blend tables**:
   - Add `epic12_source_scale_r[64][32][32]`, `_g`, `_b` and `epic12_dest_scale_r[32][32]`, `_g`, `_b`.
   - Pre-shift each channel during `epic12_init_blend_tables()` at startup (`<< 19` for red, `<< 11` for green, `<< 3` for blue).
   - In `epic12_blend_fixed()`, lookup tables are loaded once per draw call:
     `const UINT32 *red = epic12_source_scale_r[tint_clr->r][s_alpha];`
     Inside the inner pixel loop, the 3 dependent shifts (`<< 19`, `<< 11`, `<< 3`) are completely eliminated. The 32-entry lookup array (128 bytes) fits in exactly one Xenon cache line per channel.
   - The original `UINT8 epic12_source_scale[64][32][32]` table is preserved for compatibility and test validation.

2. **Row-level software prefetching (`__dcbt`)**:
   - Source scanlines in `gfx` and destination scanlines in `m_bitmaps` are separated by 32 KB strides, which hardware stream prefetchers cannot bridge.
   - Issue `__dcbt` prefetch hints for both `in` and `out` scanlines ahead of the inner loop: row `y0` is prefetched before the row loop starts, and row `y + 1` is prefetched at the start of each row `y`.
   - Pre-touches 128-byte cache lines across the scanline width (`FlipX ? -b : b` for backwards source scanlines).
   - Clean platform abstraction: maps to native `__dcbt` intrinsic on `_XBOX`, inline assembly on GCC PPC, and no-op on host.

## Verification

All six test suites passed 100% on both host and PPC big-endian emulation:
1. `libretro/FBNeo/tests/epic12_blit/run.py`:
   - `PASS 65536 tint/alpha rounding cases and 1000000 packed colour sums`
   - `PASS 5000 image cases: tint, transparency, clipping, flips, source wrap, overlapping VRAM and delay`
2. `libretro/FBNeo/tests/epic12_gpu/run.py`:
   - `PASS 6291456 component cases on unified and fast/feedback shaders, including additive saturation`
   - `PASS 4096 plain shader cases independent of alpha and destination`
   - `PASS clipped CPU invalidation: all 2048 pages at 128/256 slots`
   - `PASS 14400 commands, full VRAM and delay comparison`
   - `PASS 640 grouped commands, snapshot overlap/area barriers`
   - `PASS 640 varying-style commands on all paths`
   - `PASS restored batch cap: 260 source pages -> 3 batches`
   - `PASS cache 128/256 slots coherent`
   - `PASS reorder: eight-command boundary, edge/overlap barriers`
   - `PASS alpha/snapshot/reorder integration 128/256 slots`
3. `libretro/FBNeo/tests/epic12_gpu_region/run.py`:
   - `PASS 4044 rectangles; 10112328 exact bytes` on host and PPC
4. `libretro/FBNeo/tests/epic12_gpu_untile/run.py`:
   - `PASS 262144 source addresses`
   - `PASS 6820 rectangles; 66356536 exact pixels` on host and PPC
5. `libretro/FBNeo/tests/epic12_gpu_cache/run.py`:
   - `PASS 12000 batches; fingerprint=21d3a8b4487bb152` on host and PPC
6. `libretro/FBNeo/tests/sh3_ppc/run.py`:
   - `PASS 846026 cases compiled 727804 fallback 118222`

## Console image

Diagnostics flavor only, as the iteration rule requires.
Built with `build-xex.sh --tag blend-fixed-opt --baseline 2886a69f --flavor diag`:
- Build tag: `blend-fixed-opt 46eb6d35 20261013-2226 diag`
- Size: `34,635,776` bytes
- SHA256: `6daa65e68544859d823770a28670a15d33b8002c760e0311133470b32aaa7a68`
- Archive: `/home/humor/salvia-tests/xex-archive/fbneo-20261013-2239-blend-fixed-opt-diag.xex`
- Deployed to `Distro360/`: exactly the A/B pair:
  - Candidate: `Distro360/fbneo-blend-fixed-opt-diag.xex` (`6daa65e68544859d823770a28670a15d33b8002c760e0311133470b32aaa7a68`)
  - Baseline: `Distro360/fbneo-blit-exec-split-diag.xex` (`d9c5397b4fc2be8873163fee6febbb7129ced06d4599ed24a8b02046d5aa3df4`)

## Console Result

Tested on Xbox 360 hardware (`Distro360/cv1000-gpu.log`, window 497 frames in `mmpork`):

| 指标 | Baseline (`blit-exec-split` 618 帧) | Candidate (`blend-fixed-opt` 497 帧) | 变化 |
| :--- | :--- | :--- | :--- |
| `game_and_ui` | **27.300 ms** | **24.912 ms** | **-2.39 ms (-8.7%)** |
| `sampled_active_loop` | 27.804 ms | 27.361 ms | -0.44 ms |
| `limiter` (空闲帧预算) | 0.001 ms (跑满掉帧) | **0.909 ms** (恢复余量) | **+0.91 ms** |
| `soft` 单次调用耗时 | **0.0671 ms** (67.1 µs) | **0.0515 ms** (51.5 µs) | **-23.3% 提速** |
| `replay` 每帧耗时 | 5.31 ms / 帧 | 5.00 ms / 帧 | -0.31 ms / 帧 |
| `worker_wait` 峰值等待 | 20.657 ms | 19.189 ms | -1.47 ms |
| `drc_dispatch` (SH3) | 9.73 ms / 帧 | 9.10 ms / 帧 | -0.63 ms / 帧 |

实机数据显示：
1. 预移位查表与行预取使 `soft` 软件光栅化单次调用平均耗时从 67.1 µs 下降至 51.5 µs（**单次耗时降低 23.3%**）。
2. 前端主循环核心耗时 `game_and_ui` 显著降低 **2.39 ms / 帧**（从 27.30 ms 降至 24.91 ms）。
3. 帧率限制器 `limiter` 从 0.001 ms 上升到 0.909 ms，意味着部分帧已不再处于完全跑满的掉帧状态，留出了空闲缓冲时间。

## Limits

- Console frame rate must be verified on Xbox 360 hardware with `Distro360/cv1000-gpu.log`.
- Host synthetic tests verify bit-level arithmetic identity, but console FPS depends on Xenon memory bandwidth and in-order pipeline execution.

## Revert

Revert commit with `git revert <sha>` to restore original `epic12_fast_blit.h` and delete this document.

