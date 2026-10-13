# CV1000: template-specialize 8-row groups and eliminate runtime branches in epic12_gpu_tile_alpha_block

Baseline `30cd70e7`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

From `Distro360/cv1000-gpu.log` (steady state, 14,100 sampled batches in `mmpork`), `source_upload` (图集页瓦片化打包入显存及 alpha 掩码生成) 耗时达 **3.79 ms/帧** (每批 0.417 ms，每帧约 61.5 页 / 3.93 MB 显存数据流)。

分析 [`epic12_gpu_tile_alpha.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_gpu_tile_alpha.h) 中的 `epic12_gpu_tile_alpha_block`：
- 原实现在 32 行的外层循环 `for(int y=0;y<32;y+=2)` 中，每个 2 行步长迭代内均执行多处动态计算与运行时分支：
  1. `int first=(y&8)?16:0;` 计算。
  2. `if(first) ... else ...` 动态条件分支选择掩码打包置换。
  3. `if(y&7) ... else ...` 动态条件分支判断是否为每 8 行组的首对累加器重置。
  4. `if((y&7)==6) ...` 动态条件分支判断是否为每 8 行组的尾对组掩码写回。
- 在 Xbox 360 Xenon 架构上，每个 32x32 块执行 16 次该循环，整页 16 个块执行 256 次，每帧 61.5 页合计执行逾 15,600 次运行时分支判断。
- 实际上，瓦片化重排列结构每 8 行为一组：
  - 组 0 与组 2 (行 0..7 和 16..23) 的 `first` 恒为 0；
  - 组 1 与组 3 (行 8..15 和 24..31) 的 `first` 恒为 16；
  - 组内 4 个 2 行对中，第 0 对重置累加器，第 1、2、3 对累加，第 3 对结束后将 `summary` 写入组掩码。
- 这些属性在编译期完全确定，无需在内循环中进行任何运行时分支预测与取模计算。

## What

在 [`epic12_gpu_tile_alpha.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_gpu_tile_alpha.h) 中进行以下优化：
1. **模板化 8 行组特化 (`epic12_gpu_tile_alpha_group<First>`)**：
   - 将 `First` (0 或 16) 提升为模板编译期常量。
   - `if (First)` 成为编译期常量分支，组 0/2 与组 1/3 生成完全特化的无分支置换指令序列。
2. **消减组累加运行时分支**：
   - 组内循环 `p = 0..3` 中，第 0 对直接赋值 `summary = pair`，其余对 `summary = __vor(summary, pair)`，循环结束后单次执行 `__stvewx(summary, ...)`，彻底移除循环体内的 `y&7` 取模与分支。
3. **精准保持硬件内存约束**：
   - 保持每页严格 4096 次向量加载与 4096 次顺序对齐向量存储，完全符合 Xenos 瓦片显存写入约束。

## Verification

所有测试套件在 Host 与 PPC Big-Endian 双端 100% PASS：
1. `libretro/FBNeo/tests/epic12_gpu_tile_alpha/run.py`:
   - `PASS fused tile/alpha: 1152 pages, 18874368 pixels; all slots, masks, groups, bbox cache, guards, source immutability, fallback and injected self-test failure`
   - `PASS exactly 4096 source vector loads/page plus five constants; 4096 sequential aligned vector stores/page; no atlas vector reads` (Host & PPC)
2. `libretro/FBNeo/tests/epic12_gpu_region/run.py`:
   - PASS 4,044 个矩形用例。
3. `libretro/FBNeo/tests/epic12_blit/run.py`:
   - PASS 65,536 个加法/舍入用例及全部 5,000 个随机图像用例。
4. `libretro/FBNeo/tests/epic12_gpu_untile/run.py`:
   - PASS 6,820 个矩形用例。
5. `libretro/FBNeo/tests/epic12_gpu_cache/run.py`:
   - PASS 12,000 个批次。

## Limits

- 真实批次耗时需在 Xbox 360 实机通过 `Distro360/cv1000-gpu.log` 中的 `source_upload` 确认。

## Revert

`git revert <sha>` 恢复原 `epic12_gpu_tile_alpha.h` 并删除本报告。
