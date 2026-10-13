# CV1000: inline sh3_touch_line prefetch to eliminate function call and blr in DRC dispatch

Baseline `37662cd5`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

From `Distro360/cv1000-gpu.log` (steady state, 497-frame window in `mmpork`):
- `drc_dispatch=4524.604 ms (dispatch_calls=844080)`：平均每帧约 1,700 次调度调用，占主核时间的 9.10 ms/帧。
- `drc_work_shadow hits=123577 entries=125053 hits_pct=98`：Shadow Cache 命中率高达 98% ~ 99%。
- 在 [`sh3_drc_dispatch.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/cpu/sh4/sh3_drc_dispatch.h) 的内联调度循环中，每次调用原生块 `entry(&sh3_ppc_state)` 前，均执行 `sh3_shadow_touch(shadow[index].next_pc)`，预取下一个预测 PC 对应的 Shadow 缓存行。
- 分析 [`sh3_drc_ppc.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/cpu/sh4/sh3_drc_ppc.h)：
  - 原实现将 `sh3_touch_line` 定义为 `static __declspec(naked) void sh3_touch_line(const void *line)`，内部使用 `__emit(0x7c001a2c); __asm { blr }`。
  - 在 Xbox 360 MSVC 编译器下，标记为 `__declspec(naked)` 的函数**绝不会被编译器内联**。
  - 导致每个调度步长内，CPU 都必须产生真正的子函数调用 `bl sh3_touch_line`，保存并恢复链接寄存器 (LR)，并在叶函数末尾执行 `blr`。在 Xenon 顺序执行流水线上，这给每帧 ~1,700 次的极高频调度带来了无谓的流水线停顿与分支跳转开销。

## What

在 [`sh3_drc_ppc.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/cpu/sh4/sh3_drc_ppc.h) 与 [`sh3_drc_dispatch.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/cpu/sh4/sh3_drc_dispatch.h) 中进行以下优化：
1. **编译器内置函数内联预取 (`__dcbt`)**：
   - 在 Xbox 360 下直接使用 `<ppcintrinsics.h>` 提供的 `__dcbt(0, line)` 内置函数，替换原裸函数。
   - 编译器直接在调用处生成单条内联 `dcbt 0, r3` 指令，彻底消减 `bl` / `blr` 函数调用开销。
2. **GCC PowerPC 内联汇编支持**：
   - 针对 GCC PowerPC 目标（QEMU 与单元测试环境），使用 `__asm__ volatile ("dcbt 0, %0" : : "r"(line));` 实现相同内联。
3. **保持严格的影子缓存行验证**：
   - 依然保持 128 字节 Shadow 缓存行结构对齐与 `next_pc` 预测预取语义。

## Verification

所有测试套件在 Host (x86_64) 与 PPC Big-Endian 双端 100% PASS：
1. `libretro/FBNeo/tests/sh3_ppc/run.py` (PPC under QEMU):
   - `PASS 846026 cases compiled 727804 fallback 118222`
   - `MODE 0..4` 全部通过
   - `EDGE native carry/borrow / independent arithmetic... PASS cases=89536`
   - `EDGE signed/unsigned long multiply... PASS cases=44768`
   - `EDGE compiled device MOV.L... PASS cases=16396`
   - `EDGE compiled watched MOV.L service... PASS cases=12545`
2. `libretro/FBNeo/tests/sh3_dispatch/run_inline.py`:
   - `PASS 8 host suites in optimized and ASan/UBSan modes`
3. `libretro/FBNeo/tests/epic12_gpu/run.py` (Host):
   - `PASS 6291456 component cases on unified and fast/feedback shaders`
   - `PASS 14400 commands, full VRAM and delay comparison`
4. `libretro/FBNeo/tests/epic12_gpu_untile/run.py`:
   - `PASS: 6820 rectangles; 66356536 exact pixels` (Host + PPC)
5. `libretro/FBNeo/tests/epic12_gpu_tile_alpha/run.py`:
   - `PASS fused tile/alpha: 1152 pages, 18874368 pixels` (Host + PPC)

## Limits

- 预取命中对执行延迟的改善受缓存行命中状态影响，实际主核 CPU 时间收益需在 Xbox 360 实机运行时通过 `cv1000-gpu.log` 中的 `drc_dispatch` 确认。

## Revert

`git revert <sha>` 恢复原 `sh3_drc_ppc.h` 和 `sh3_drc_dispatch.h` 并删除本报告。
