# CV1000 性能阶段 R1：启动第 42 帧剖析与稳态慢帧 (>25ms / <40FPS) 诊断探针

日期：2026-11-08。基线提交：`08e6949a`。
对应优化路线图阶段：**R1（启动第 42 帧长尾剖析探针与稳态慢帧捕获）**。

---

## 1. 变更背景与问题剖析

在 Xbox 360 Jasper 主机实机游玩过程中，用户肉眼观察到在生成日志前的一段战斗中帧率下降到了 40 多帧。实机日志数据与架构审阅确认了两大核心问题：

### 1.1 稳态慢帧（31~40 FPS）被旧统计“平均”掩盖
1. **旧统计阈值盲区**：
   - 旧 `core_stall_watch` 设定的判定阈值为 `threshold_ms = 100`（即帧耗时超 100ms、帧率跌破 10 FPS 的严重死卡）。
   - 用户肉眼看到的 40 帧对应单帧耗时为 25ms，31 帧对应单帧耗时为 32ms。这些帧未达 100ms 阈值，全部未被 `core_stall_watch` 保留！
2. **底层确凿的慢帧证据**：
   - 抽样统计已确切捕捉到：
     `core_slow_frame_ms threshold_ms=25.000 samples=4 prep=0.003 cpu_io=31.239 audio_tail_misc=0.128 draw_sync=0.726 total=32.095 dominant_prep=0 dominant_cpu_io=4`
     `core_sampled_peak_ms interval_frame=732 total=35.978 ms (27.8 FPS)`
   - 慢帧平均耗时 32.1ms（31.1 FPS），峰值达 36.0ms（27.8 FPS），且 `cpu_io=31.239ms` 占总耗时的 97.3%（`dominant_cpu_io=4`）。
   - 由于抽样是 1-in-64，4 个抽中样本代表实机实际游玩中约有 250+ 帧处于 30~40 帧卡顿。

### 1.2 启动第 42 帧的 266ms 长尾停顿
- 日志显示启动第 42 帧耗时高达 **266.823 ms**（`cpu_io=265.916 ms`），但该帧仅编译了 `compiled_blocks=20`。
- 目前无法确定这 265.9ms 究竟是 20 个块的 JIT 编译耗时，还是 SH3 指令循环、解释器回退、EEPROM/硬件轮询或工作线程等待。

### 1.3 多核时钟回卷（Probe Skew）
- Xbox 360 具有 3 个物理核心/6 个硬件线程，线程在核心间迁移时可能产生微小的 timebase skew。原探针未加 `end >= start` 保护，导致 `drc_dispatch` 计时偶发下溢回卷（产生数千亿的大数）。

---

## 2. 修改方案与安全实现

本提交完全在现有架构和 `#if SALVIA_FBNEO_DIAGNOSTICS` 范围内增加高精度且零常态开销的诊断探针：

### 2.1 增加稳态慢帧捕获器 (`core_drop_watch`)
- 在 [`d_cv1k.cpp`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/drv/cave/d_cv1k.cpp) 中新增 `cv1k_review_drops`（`threshold_ms = 25.0`，对应 40 FPS）：
  ```cpp
  cv1k_review_drops.record(reviewMarks, cv1k_review.frames, cv1k_review_slow_ticks, compiledThisFrame);
  ```
- 暂停时输出前 8 个最严重的慢帧细节（帧号、prep、cpu_io、audio、draw_sync、总耗时、编译块数），彻底杜绝抽样遗漏。

### 2.2 启动第 42 帧与冷启动深度解构 (`core_startup_stall`)
- 在 [`sh3_drc_ppc.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/cpu/sh4/sh3_drc_ppc.h) 中增加单帧 JIT 编译耗时计量：
  在 `Sh3Ppc::compile()` 的入口和出口记录硬件 ticks，累加至 `s_jit_compile_ticks_frame`（仅在非 AOT 期间生效）。
- 在 [`d_cv1k.cpp`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/drv/cave/d_cv1k.cpp) 中，针对启动期（`frames <= 60` 且单帧耗时 `> 50ms`）记录 `StartupDetail`：
  - 帧号（如 42）
  - 总耗时与 `cpu_io` 耗时
  - 实际 JIT 编译耗时（精确测量这 20 个块的编译成本）
  - 编译块数与回退至解释器的步数（`interp_steps`）
- 暂停时输出：`core_startup_stall frame=... total=... cpu_io=... jit_compile=... compiled_blocks=... interp_steps=...`

### 2.3 探针时钟安全保护 (`salvia_cv1k_probe.h`, `sh4.cpp`)
- 在所有 `salvia_cv1k_tick()` 计算处加入 `end >= start` 边界守卫，彻底消除多核时钟回卷。

---

## 3. 测试与验证闭环

1. **AOT 单元测试套件** (`test.cpp`)：**PASS**
2. **宿主 Dispatch 测试套件** (`run_inline.py`)：**PASS**（8 个组件全部通过）
3. **PPC QEMU 差分测试套件** (`run.py`)：**PASS**（848,846 个用例全部通过）
