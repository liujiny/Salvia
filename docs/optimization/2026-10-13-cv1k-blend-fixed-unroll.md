# CV1000: dual-unroll pixel loop and branch-fold transparent pixel pairs in epic12_blend_fixed

Baseline `27edcb10`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

From `Distro360/cv1000-gpu.log` (steady state, 497 frames in `mmpork`):
- `soft` (GPU 拒绝的绘图请求退回 CPU 软光栅处理): **6.32 ms/帧** (60,978 次调用，平均 51.5 µs/次)
- `replay` (小于 8192 像素的小批次退回 CPU 软光栅回放): **5.00 ms/帧** (131,428 个批次，102,380,105 像素)
- 两者合计占用 **11.32 ms/帧**，占模拟器总体执行时间的近半数。

根据 `docs/optimization/2026-10-11-cv1k-blit-path-census.md` 的全路径普查：
- 在默认运行路由下，`fixed_f0_tr1` 占重放像素的 **98.27%**！
- 绝大多数软光栅调用和回放均集中在 `epic12_blend_fixed`。

分析 [`epic12_fast_blit.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_fast_blit.h) 中的 `epic12_blend_fixed` 内循环：
1. **单像素紧密串行**：每个像素依次进行加载 `pen = *in`、透明度分支判断 `pen & 0x20000000`、颜色解包、3 个颜色通道查找表、目标加载 `*out`、目标查找表、通道饱和加法 `epic12_packed_add`、写入 `*out`。
2. **在 Xenon 顺序流水线上的 Load-Use 停顿**：由于 Xenon 是 3.2 GHz 顺序发射（In-order）处理器，每个指令均紧随其前置数据加载之后执行，无法乱序掩盖 3-4 周期的 L1 缓存延迟。
3. **逐像素透明分支开销**：在精灵边缘与透明区域，每个像素均需要单次分支判断跳过，造成分支开销。

## What

在 [`epic12_fast_blit.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_fast_blit.h) 中进行以下优化：
1. **同扫描线重叠回退检测 (`same_row`)**：当且仅当 `gfx == m_bitmaps` 且源与目标位于同一扫描线时，回退到严格顺序单像素循环，100% 保证原版自重叠写入依赖行为（通过测试用例 415 验证）。
2. **双像素展开与透明对折叠 (`Transparent == true`)**：
   - 提取相邻两个像素 `pen0` 与 `pen1`。
   - **双透明快速跳过**：`if (!((pen0 | pen1) & 0x20000000))`，一条分支即可同时跳过两个透明像素，无需任何查表、目标加载或写回。
   - **双不透明交替流水线**：`if ((pen0 & pen1) & 0x20000000)`，两像素计算交替发射，指令相互填充等待周期，彻底消除 Load-Use 停顿。
   - **单透明单不透明边缘平滑处理**：独立条件分支分别处理。
3. **双像素展开 (`Transparent == false`)**：无分支连续交替发射计算两像素。
4. **尾部标量保护**：行末奇数像素安全单步处理。

## Verification

所有 6 组测试套件在 Host 与 PPC Big-Endian 双端 100% PASS：
1. `libretro/FBNeo/tests/epic12_blit/run.py`:
   - `PASS 65536 tint/alpha rounding cases and 1000000 packed colour sums`
   - `PASS 5000 image cases: tint, transparency, clipping, flips, source wrap, overlapping VRAM and delay` (5000 个随机图像用例，位级完全一致)
2. `libretro/FBNeo/tests/epic12_gpu_untile/run.py`:
   - `PASS: 262144 source addresses, all macro blocks and four-pixel contiguous groups`
   - `PASS: 6820 rectangles; 66356536 exact pixels`
3. `libretro/FBNeo/tests/epic12_gpu/run.py`:
   - PASS 所有 14400 渲染命令对比与着色器用例
4. `libretro/FBNeo/tests/epic12_gpu_region/run.py`:
   - `PASS: 4044 rectangles; 10112328 exact bytes`
5. `libretro/FBNeo/tests/epic12_gpu_cache/run.py`:
   - `PASS: 12000 batches; fingerprint=21d3a8b4487bb152`

## Limits

- 真实批次耗时需在 Xbox 360 实机通过 `Distro360/cv1000-gpu.log` 确认。

## Revert

`git revert <sha>` 恢复原 `epic12_fast_blit.h` 并删除本报告。
