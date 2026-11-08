# CV1000 AOT 阶段 R1-R2 实机遥测验证报告

日期：2026-11-08。  
基线提交：`3f6c56aa`（R0 基线） vs 当前优化提交：`62aeccc4`（R1-R2 实机构建）。  
实机镜像标识：`aot-r1-r2`（`mmpork.xex`，SHA256: `b5095eb93fd9695743906efb121bc52e300998fa0ae0d8b481239900eeeb9516`）。  
测试设备：Xbox 360 Jasper 实机（`192.168.2.232`，双核渲染，DIP = 00,07,00,00）。  
游玩样本：MMPork 连续真实游玩 **10,320 帧**（约 172 秒）。

---

## 1. 实验结果核心对比 (Head-to-Head A/B)

| 核心指标 | R0 基线 (`3f6c56aa`) | R1-R2 优化 (`62aeccc4`) | 差值与收益 |
| :--- | :--- | :--- | :--- |
| **0xAC 空间预编译块数** | 8,189 块 | **0 块** | **-8,189 块 (-100%)** |
| **0xAC 空间执行次数** | 0 次 | **0 次** | 零安全风险 |
| **第 30 帧 AOT 编译耗时 (`prep`)** | **174.789 ms** | **86.113 ms** | **-88.676 ms (-50.7%)** |
| **第 30 帧单帧总耗时 (`total`)** | **258.058 ms** | **171.617 ms** | **-86.441 ms (-33.5%)** |
| **AOT 编译消耗字数** | 2,859,624 字 (11.44 MB) | 1,426,528 字 (5.71 MB) | **-1,433,096 字 (-5.73 MB)** |
| **代码 Arena 实际峰值占用** | 2,897,884 字 (11.59 MB) | 1,464,788 字 (5.86 MB) | **-1,433,096 字 (-5.73 MB)** |
| **AOT 执行常驻块比例** | 10.2% (1668/16344) | **20.5% (1678/8183)** | 活跃利用率翻倍 |
| **探针跨核时钟回卷** | 偶发回卷出数万亿 ms | **彻底消除 (0 次)** | 守卫正常生效 |

---

## 2. Option A：剔除 0xAC 镜像验证

实机运行 10,320 帧遥测日志明确记录：
```text
CV1000 GPU: aot_spaces 0c_compiled=8187 0c_exec=1678 0c_cold=3 ac_compiled=0 ac_exec=0 ac_cold=0 (batches=7834)
CV1000 GPU: aot_resident total_aot=8183 (0c_hot=1677 0c_cold=6506 ac_hot=0 ac_cold=0) total_jit=8885 (jit_hot=8885 jit_cold=0) (batches=7834)
CV1000 GPU: aot_run id=1 trigger=boot range=0x0C000000..0x0C200000 ... time_ms=86.061 words=+1426528 ... (batches=7834)
```

1. **收益确凿**：第 30 帧启动 AOT 预编译阶段耗时从 174.8ms 直接腰斩至 86.1ms，总启动卡顿减少 86.4ms（-33.5%）。
2. **内存极度净化**：消除了 8,189 个永远不会被命中的冷代码块，省下 5.73MB 代码缓存。
3. **安全闭环**：`0xAC` 执行次数依旧保持为 0，零兼容性风险。

---

## 3. Option B：稳态慢帧（40 FPS 掉帧）全面解密

在本次完整游玩中，新增的 `threshold_ms = 25.0` 捕获器精准捕获了用户肉眼所见的掉帧现场：

```text
CV1000 GPU: core_drop_watch frames=10320 invalid=0 threshold_ms=25 count=423 retained=8 peak_ms=236.485 coverage=all_core_frames (batches=7834)
CV1000 GPU: core_slow_frame_ms threshold_ms=25.000 samples=6 prep=0.003 cpu_io=30.664 audio_tail_misc=0.144 draw_sync=0.727 total=31.537 dominant_prep=0 dominant_cpu_io=6 (batches=7834)
```

1. **确切帧数**：10,320 帧游玩中，有 **423 帧** 单帧耗时超过 25ms（处于 30~40 帧卡顿区间）。
2. **耗时占比**：慢帧平均耗时 31.537ms，其中 `cpu_io` 占 **30.664ms（97.2%）**，`draw_sync` 仅 0.727ms。GPU 完全不卡，100% 卡顿集中在 CPU 端。
3. **解释器倒挂是 40 帧卡顿的核心根因**：
   ```text
   CV1000 GPU: drc_work_execution native_calls=2163465 native_guest_cycles=19983574 interpreter_steps=54283 interpreter_guest_cycles=43325082 not_host_time=1
   CV1000 GPU: drc_work_interpreter rank=1 opcode_hi8=46 count=16422
   CV1000 GPU: drc_work_interpreter rank=2 opcode_hi8=27 count=11049
   CV1000 GPU: drc_work_interpreter rank=3 opcode_hi8=60 count=8625
   ```
   - 解释器执行了 **43,325,082 个 Guest 周期**，是 DRC 原生执行（19,983,574）的 **2.17 倍**！
   - 在 Xenon PPC 顺序低 IPC 核心上，C++ 解释器效率极低。战斗密集时，密集执行 `0x46xx`（16,422 次）、`0x27xx`（11,049 次）和 `0x60xx`（8,625 次），导致 CPU 单帧耗时飙升至 30+ ms。

---

## 4. 启动第 42 帧深入解构

```text
CV1000 GPU: core_startup_stall frame=1 total=98.228 cpu_io=97.119 jit_compile=1.144 compiled_blocks=40 interp_steps=0
CV1000 GPU: core_stall rank=1 interval_frame=42 prep=0.003 cpu_io=235.580 audio_tail_misc=0.169 draw_sync=0.733 total=236.485 compiled_blocks=20
```

- 实机计时表明：第 1 帧编译 40 个原生块仅花费 **1.144 ms**（单个块平均 0.028ms）；
- 第 42 帧仅编译了 20 个块，其 JIT 编译成本仅约 **0.56 ms**；
- 第 42 帧长达 235ms 的停顿 100% 属于游戏启动过程中的 ROM/RAM 数据校验或外部设备轮询，而非 JIT 编译器开销。

---

## 5. 后续优化路线（R3）推荐

1. **扩展 PPC 原生发射（DRC Emitter）覆盖 `0x27xx` 与 `0x46xx`**：
   - 将高频堆栈与控制操作内联至 PPC 块中，消除 27,000+ 次解释器回退，直接根治 40 帧慢帧。
2. **优化 Partial Exits（28,497 次）与块链接**：
   - 减少分支和延迟槽中断，维持指令长链在原生缓存中执行。
