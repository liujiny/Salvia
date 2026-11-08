# CV1000 优化：大复活 (DDPDFK) smode=6 GPU 加速与 64MB DRC 代码区扩容

日期：2026-11-08  
基线提交：`8626c67346eba21861292c8ac78ac8f0e855cc41`  
对应分析结论：`cv1000_ddpdfk_telemetry_analysis.md`（累计 86,382 帧实机游玩日志瓶颈定位）

---

## 一、 优化背景与依据

在 Xbox 360 Jasper 实机上对《怒首领蜂大复活》(DDPDFK) 进行的 86,382 帧深度遥测揭示了两个制约满帧 60 FPS 的主要瓶颈：

1. **反向调制混合 (`smode=6, dmode=0`) 65 万次回退 CPU 软件渲染**：
   - 遥测显示：`reject_mode smode=6 dmode=0 count=650892`，导致 `flushes cpu_draw=598812`。
   - 烟雾和高光特效高频使用该混合模式，由于 GPU 未加速，导致主 CPU 线程被迫执行 59.8 万次批次中断和像素级软栅格化，挤占了原本就紧张的 CPU 预算（平均 `cpu_io` 达到 12.4ms）。
2. **32MB DRC 代码区触顶 (99.95%) 频繁置换基本块**：
   - 遥测显示：`peak_words=8384584`（32MB 容量为 8,388,608 字，利用率 99.95%），触发 4 次全局置换，移出 9,500+ 个已编译块，迫使运行期 JIT 动态重编译高达 59,988 次。

---

## 二、 变更方案与实现细节

### 1. GPU 反向调制着色器加速 (`smode=6, dmode=0`)
- **批次规划与反馈判定** ([`epic12_gpu_batch.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_gpu_batch.h))：
  - 在 `epic12_gpu_dest_mode` 中为 `smode == 6`（且 `dmode` 为 0/3/7）分配独立模式代码 `4`。
  - 在 `epic12_gpu_feedback` 与 `epic12_gpu_blend_state` 中将模式 `4` 纳入 GPU Feedback 调度管线。
- **提交校验与回放安全** ([`epic12_gpu.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_gpu.h))：
  - 在 `epic12_gpu_submit` 中放行 `smode6_valid`，解除对其的丢弃拦截。
  - 在 `epic12_gpu_replay` 中接入 `s6` 原生光栅化支持，保障边界与测试回放严格等价。
- **Xenos HLSL 像素着色器实现** ([`epic12_gpu_xbox.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_gpu_xbox.h))：
  - 在 `shade` 与 `shade_feedback` 中实现对应运算：
    $$\text{outc} = \min(31.0, \lfloor (31.0 - d) \times s / 31.0 + 0.0001 \rfloor + \lfloor d \times \text{mode.w} / 31.0 + 0.0001 \rfloor)$$
  - 严格限制 `mode.z` 平方运算区间为 `[2.5, 3.5]`，防止模式 `4` 误触发平方面积计算。

### 2. 扩充 DRC Arena 预算至 64MB
- **代码容量提升** ([`sh3_drc_ppc.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/cpu/sh4/sh3_drc_ppc.h))：
  - 将 `SH3_PPC_CACHE_BYTES` 由 `32 * 1024 * 1024` 扩容至 `64 * 1024 * 1024`。
  - 扇区数量由 16 个增至 32 个（每个扇区 2MB），严格保持扇区数量为 2 的整次幂（$32 = 2^5$），精确满足 `arena_sector_of` 的掩码计算要求，且扇区索引 0..31 完美贴合 `slot_sector` 5-bit 容量。
  - 消除全部淘汰置换，使 JIT 代码全程持久驻留。

---

## 三、 测试与验证闭环

1. **宿主单元测试套件**：
   - `review_profile`：**PASS**
   - `sh3_movll_service`：**PASS (12,309 cases)**
   - `sh3_aot_profile`：**PASS**
2. **PPC QEMU 着色器模型与批次全量回归测试** (`epic12_gpu`)：
   - 包含新增的 2,097,152 组 `smode=6, dmode=0` 反向调制混合测试用例，100% 与整数参考实现一致。
