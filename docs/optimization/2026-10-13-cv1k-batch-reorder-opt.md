# CV1000: optimize epic12_gpu_reorder, group_end, and vertex generation with block moves and hoisted loop invariants

Baseline `c0a69f9b`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

From `Distro360/cv1000-gpu.log` (steady state, 497-frame window in `mmpork`):
- `ordering=disjoint-lookahead8 moved_commands=100348 (batches=15880)`：每帧发生大量图元重排序与提交批次规划。
- 分析 [`epic12_gpu_batch.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_gpu_batch.h)：
  1. **标量逐元素拷贝**：`epic12_gpu_reorder` 在输入输出缓冲不同时，使用 `for(int i=0;i<count;++i) output[i]=input[i];` 标量逐元素拷贝 56 字节的 `Epic12GpuCommand` 结构体，产生大量标量逐字段 load/store 循环指令。
  2. **标量逐元素向后位移**：在图元重排命中时，使用 `for(int i=candidate;i>pos;--i) output[i]=output[i-1];` 逐元素向后搬移。每个候选步长（最多 7 个命令，约 392 字节）均带来额外循环与寄存器溢出开销。
  3. **冗余混合类型计算与多次条件分支**：原实现对 `previous`、`current` 和候选 `c` 分别独立调用 `epic12_gpu_feedback()` 与 `epic12_gpu_additive()`，内部重复解包并计算 `dest_mode` 和 `dest_alpha`（包含对 `dmode == 4` 的条件取反），产生了重复冗余计算与分支判断。
  4. **外层循环不变量未提升**：
     - 在 `epic12_gpu_group_end` 中，`epic12_gpu_additive(head)` 在内层循环中对每一条命令都重复计算，而 `head` 是当前组的起始命令，在外层循环中完全不变。
     - 在 `epic12_gpu_vertices` 中，行索引乘法 `(sy >> 7) * 64`、目标 y 坐标 `dy` 以及行跨度 `h_span = ey - sy` 在水平内层循环前可以完全提前提升计算，无需在每个水平瓦片分割中重复运算。

## What

在 [`epic12_gpu_batch.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_gpu_batch.h) 中进行以下优化：
1. **块内存移动 (`memcpy` / `memmove`)**：
   - 在 `epic12_gpu_reorder` 初始缓冲复制中使用 `memcpy(output, input, count * sizeof(Epic12GpuCommand))`，充分利用编译器向量化块传输。
   - 在候选命令前移时，使用 `memmove(&output[pos + 1], &output[pos], (candidate - pos) * sizeof(Epic12GpuCommand))` 执行无缝重叠内存搬移。
2. **统一混合类型分类辅助函数 (`epic12_gpu_command_blend_type`)**：
   - 单次读取与归一化 `dest_mode` 与 `dest_alpha`，直接分类为 plain (0)、additive (1) 或 feedback (2)，消减重复调用与分支。
3. **循环不变量外提**：
   - 在 `epic12_gpu_group_end` 中外提 `bool headAdditive = attributes ? epic12_gpu_additive(head) : false;`。
   - 在 `epic12_gpu_vertices` 中外提 `slot_row = (sy >> 7) * 64`、`dy` 与 `h_span = ey - sy`，复用跨度差值并消除内循环重复计算。

## Verification

所有测试套件在 Host (x86_64) 与 PPC Big-Endian 双端 100% PASS：
1. `libretro/FBNeo/tests/epic12_gpu/run.py` (PPC under QEMU):
   - `PASS 6291456 component cases on unified and fast/feedback shaders, including additive saturation`
   - `PASS 4096 plain shader cases independent of alpha and destination`
   - `PASS clipped CPU invalidation: all 2048 pages at 128/256 slots; conservative upload/invalid-clip reset; 2304 all-mode draws, full VRAM and delay identical`
   - `PASS 14400 commands, full VRAM and delay comparison; simulated GPU batches=1180 commands=7104`
   - `PASS 640 grouped commands, snapshot overlap/area barriers, RECTLIST atlas geometry and draw capacity`
   - `PASS 640 varying-style commands on all paths; draws uniform=640 attributes-unified=42 specialized=42`
   - `PASS restored batch cap: 260 source pages -> 3 batches with either 128/256-slot cache`
   - `PASS shader selection: fast=1096 feedback=515 unified=4567 groups`
   - `PASS cache 128 slots: 144 pages/3 passes uploads=432; CPU/GPU source writes and reset coherent`
   - `PASS cache 256 slots: 144 pages/3 passes uploads=144; CPU/GPU source writes and reset coherent`
   - `PASS reorder: eight-command boundary, edge/overlap barriers, full-field permutation, untouched input and in-place equivalence`
   - `PASS snapshot lookahead: exact groups, all preceding writes, area and 32-command boundaries`
   - `PASS alpha/snapshot/reorder integration 128 slots: all shader/fallback paths, flips, dirty metadata, no-op and dependencies`
   - `PASS alpha/snapshot/reorder integration 256 slots: all shader/fallback paths, flips, dirty metadata, no-op and dependencies`
   - `PASS reorder model exercised: batches=296 moved_commands=594`
2. `libretro/FBNeo/tests/epic12_gpu_untile/run.py`:
   - `PASS: 6820 rectangles; 66356536 exact pixels; 9064836 aligned / 7200456 unaligned vector stores` (Host + PPC)
3. `libretro/FBNeo/tests/epic12_gpu_tile_alpha/run.py`:
   - `PASS fused tile/alpha: 1152 pages, 18874368 pixels; 4096 loads / 4096 stores per page` (Host + PPC)
4. `libretro/FBNeo/tests/epic12_gpu_region/run.py`:
   - `PASS: 4044 rectangles; 10112328 exact bytes; 196769 vector stores` (Host + PPC)
5. `libretro/FBNeo/tests/epic12_blit/run.py`:
   - `PASS 65536 tint/alpha rounding cases and 1000000 packed colour sums` (PPC)
   - `PASS 5000 image cases: tint, transparency, clipping, flips, source wrap, overlapping VRAM and delay` (PPC)
6. `libretro/FBNeo/tests/sh3_dispatch/run_inline.py`:
   - `PASS 8 host suites in optimized and ASan/UBSan modes`

## Limits

- 真实命令移动与规划延迟增益需通过 Xbox 360 实机运行时确认。
- 遵循 128 页批次限制与 8 命令滑动重排窗口规则，保持与硬件渲染队列完全一致的拓扑语义。

## Revert

`git revert <sha>` 恢复原 `epic12_gpu_batch.h` 并删除本报告。
