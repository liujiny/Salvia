# CV1000: quad-unroll vector loop in input_upload (epic12_gpu_copy_rows)

Baseline `a7081376`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

From `Distro360/cv1000-gpu.log` (steady state, 14,100 sampled batches in `mmpork`), `input_upload` (VRAM 批次包围盒拷贝至 Xenos 纹理) 耗时达 **2.64 ms/帧** (每批 0.290 ms，每帧约 258 万像素 / 10.3 MB 数据传输)。

分析 [`epic12_gpu_region.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_gpu_region.h) 中的 `epic12_gpu_copy_rows`：
- 原实现为每次迭代单向量（16 字节）：
  ```cpp
  for (; x < body; x += 16)
      __stvx_volatile(__loadunalignedvector((const unsigned int*)(in + x)), out + x, 0);
  ```
- 在 Xbox 360 Xenon 架构上：
  1. 目标内存为锁定的 Xenos 纹理，属于 Write-Combined (WC) 显存，其内部拥有 64 字节/128 字节写合并队列。
  2. 原循环每次仅执行一次 16 字节加载和一次 16 字节易失写回，加载延迟紧随写回指令，在 Xenon 顺序发射流水线上无法隐藏内存总线延迟。
  3. 通过 64 字节（4 向量）四路展开，可以前置连续发射 4 个向量加载，让内存子系统重叠响应，随后以 4 次连续 16 字节对齐写回完整填满一个 64 字节 WC 突发传输块。

## What

在 [`epic12_gpu_region.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_gpu_region.h) 中进行以下优化：
1. **四向量展开（64 字节 / 迭代）**：
   - 循环以 `x + 63 < body` 为步长 64 展开：
   - 连续发射 `v0`, `v1`, `v2`, `v3` 四路向量加载。
   - 随后连续执行 4 次严格 16 字节对齐且顺序递增的 `__stvx_volatile` 写回。
2. **尾部阶梯处理**：
   - 剩余不满 64 字节的整向量继续由单向量步长 16 循环处理。
   - 剩余不满 16 字节的尾部字节继续由单字节标量循环精确处理。
3. **保持边界契约**：严格保证任何向量读写均在行宽 `[0, width)` 内，无越界读取。

## Verification

所有测试套件在 Host 与 PPC Big-Endian 双端 100% PASS：
1. `libretro/FBNeo/tests/epic12_gpu_region/run.py`:
   - `PASS: 4044 rectangles; 10112328 exact bytes; 196769 vector stores, every one 16-byte aligned and inside its row; 0 unaligned stores; 196769 unaligned vector loads; 20553 prefetch hints, all inside the rectangle; little-endian`
   - `PASS: 4044 rectangles; 10112328 exact bytes; 196769 vector stores, every one 16-byte aligned and inside its row; 0 unaligned stores; 196769 unaligned vector loads; 20553 prefetch hints, all inside the rectangle; big-endian`
   - 196,769 次向量存储 100% 对齐且处于行边界内，0 次未对齐写，数据位级完全一致。
2. `libretro/FBNeo/tests/epic12_blit/run.py`:
   - PASS 65,536 个加法/舍入用例及全部 5,000 个随机图像用例。
3. `libretro/FBNeo/tests/epic12_gpu_untile/run.py`:
   - PASS 6,820 个矩形用例。
4. `libretro/FBNeo/tests/epic12_gpu_cache/run.py`:
   - PASS 12,000 个批次。

## Build and Distro360 Handoff

Diagnostics XEX 构建完成并归档：
- **Tag**: `region-quad-unroll 6233e7a7 20261013-2335 diag`
- **Candidate Image**: `Distro360/fbneo-region-quad-unroll-diag.xex`
  - 大小: `34,635,776` 字节
  - SHA256: `18f306de374f7d7ee26b32ded1f620c1c4c1ce362ca2980f984cc6d11d4c3656`
  - 归档: `salvia-tests/xex-archive/fbneo-20261013-2335-region-quad-unroll-diag.xex`
- **Baseline Image**: `Distro360/fbneo-blend-fixed-opt-diag.xex`
  - 大小: `34,635,776` 字节
  - SHA256: `6daa65e68544859d823770a28670a15d33b8002c760e0311133470b32aaa7a68`
- **Distro360 状态**: 严格只保留上述对比 A/B 对，无任何重复镜像。
- **Xenia Canary 测试**: 使用本地 `E:\Xenia_Canary\xenia_canary.exe` 加载测试，正常启动并成功初始化 D3D12/Xenos 图形与音视频子系统。

## Limits

- 真实批次耗时需在 Xbox 360 实机通过 `Distro360/cv1000-gpu.log` 中的 `input_upload` 确认。

## Revert

`git revert <sha>` 恢复原 `epic12_gpu_region.h` 并删除本报告。
