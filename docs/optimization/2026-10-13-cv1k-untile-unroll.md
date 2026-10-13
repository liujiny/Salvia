# CV1000: hoist alignment check and dual-unroll vector stores in untile_mask

Baseline `7b07fd8d`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

From `Distro360/cv1000-gpu.log` (steady state, 14,100 sampled batches in `mmpork`), the per-batch GPU transfer breakdown shows:

| 阶段 | 每批耗时 (ms) | 9.1 批/帧折合 (ms/帧) | 性质与说明 |
| :--- | :--- | :--- | :--- |
| **`untile_mask`** | **0.437 ms** | **3.98 ms** | Xenos 渲染目标解瓦片回写 CPU VRAM |
| `source_upload` | 0.417 ms | 3.79 ms | 图集页打包瓦片入显存 |
| `input_upload` | 0.290 ms | 2.64 ms | VRAM 批次包围盒入输入纹理 |
| `readback_wait` | 0.178 ms | 1.62 ms | 等待 GPU 渲染完成 |
| `submit` | 0.138 ms | 1.26 ms | GPU 命令提交 |
| `plan` | 0.075 ms | 0.68 ms | 批次规划 |

`untile_mask` 耗时达 **3.98 ms/帧**，是 GPU 批次执行中耗时最长的单一数据传输步骤。

分析 [`epic12_gpu_untile.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_gpu_untile.h) 原内循环发现：
1. **循环体内分支**：在每 4 个像素的内循环中，均存在 `if (aligned) __stvx(...) else __storeunalignedvector(...)` 条件分支。而在任何给定行 `y` 中，步长 `x` 每次递进 16 字节（4 像素），因此整行像素写入的 16 字节对齐属性完全相同。在 Xenon 顺序执行架构上，4 像素一次的分支带来流水线预测开销。
2. **重复二维偏移寻址**：内循环反复计算 `block + epic12_untile_offsets[y][x>>2]`。
3. **单向量加载延迟停顿**：原循环单向量依次执行 `__lvx -> __vand -> __stvx`，在 Xenon 顺序流水线上，加载数据延迟（3-4 周期）无法被其他指令掩盖。

## What

在 [`epic12_gpu_untile.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_gpu_untile.h) 中进行以下加速：
1. **外提对齐检查**：将 `aligned` 检查提至行循环级别，对齐路径与非对齐路径分离，消除了 4 像素内循环中的所有分支。
2. **行偏移指针缓存**：在行开始处缓存 `const unsigned short *row_offsets = epic12_untile_offsets[y]`。
3. **双向量展开（8 像素 = 32 字节 / 迭代）**：对齐与非对齐路径均按 8 像素双向量展开。前置交替发射两个向量加载 `__lvx(in0)` / `__lvx(in1)`，利用 Xenon 双发射流水线在数据到达期间重叠执行，完全隐藏内存加载停顿；剩余 4 像素与尾部像素继续保持精确处理。
4. **精确度保证**：对齐存储数量（`alignedStores`）与非对齐存储数量（`partialStores`）完全不变，位级数据 100% 相同。

## Verification

所有相关测试套件在 Host 与 PPC Big-Endian 双端 100% PASS：
1. `libretro/FBNeo/tests/epic12_gpu_untile/run.py`:
   - `PASS: 262144 source addresses, all macro blocks and four-pixel contiguous groups`
   - `PASS: 6820 rectangles; 66356536 exact pixels; 9064836 aligned / 7200456 unaligned vector stores; little-endian`
   - `PASS: 6820 rectangles; 66356536 exact pixels; 9064836 aligned / 7200456 unaligned vector stores; big-endian`
2. `libretro/FBNeo/tests/epic12_gpu/run.py`:
   - `PASS 6291456 component cases on unified and fast/feedback shaders, including additive saturation`
   - `PASS 4096 plain shader cases`
   - `PASS 14400 commands, full VRAM and delay comparison`
   - `PASS cache 128/256 slots coherent`
3. `libretro/FBNeo/tests/epic12_blit/run.py`:
   - `PASS 65536 tint/alpha rounding cases and 1000000 packed colour sums`
   - `PASS 5000 image cases`
4. `libretro/FBNeo/tests/epic12_gpu_region/run.py`:
   - `PASS: 4044 rectangles; 10112328 exact bytes` on host and PPC
5. `libretro/FBNeo/tests/epic12_gpu_cache/run.py`:
   - `PASS: 12000 batches; fingerprint=21d3a8b4487bb152` on host and PPC

## Limits

- 真实批次耗时需在 Xbox 360 实机通过 `Distro360/cv1000-gpu.log` 中的 `batch_timing` 确认。

## Revert

`git revert <sha>` 恢复原 `epic12_gpu_untile.h` 并删除本报告。
