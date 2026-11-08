# CV1000 AOT 阶段 R2：剔除 0xAC 无缓存镜像预编译

日期：2026-11-08。基线提交：`d9351916`。
对应优化路线图阶段：**R2（剔除无用空间预编译，释放启动耗时与代码缓存）**。

---

## 1. 变更背景与依据

在 Xbox 360 Jasper（`192.168.2.232`）上运行 R0 诊断镜像采集的 9,951 帧真实游玩日志（`console_cv1000_gpu_r0.log`）中，精确分空间遥测揭示了重大事实：

```text
CV1000 GPU: aot_spaces 0c_compiled=8187 0c_exec=1668 0c_cold=20 ac_compiled=8189 ac_exec=0 ac_cold=6 (batches=7849)
CV1000 GPU: aot_resident total_aot=16344 (0c_hot=1662 0c_cold=6499 ac_hot=0 ac_cold=8183) total_jit=8718 (jit_hot=8718 jit_cold=0) (batches=7849)
```

1. **0xAC 空间执行率恒为 0.00%**：
   在近万帧完整游玩中，`0xAC000000` 空间预编译的 8,189 个原生块**从未被执行哪怕一次**。
2. **体系架构根因**：
   在 CV1000 硬件架构中，`0xAC000000` 是主内存 `0x0C000000` 的无缓存镜像别名（P2 区域）。SH-3 CPU 在执行游戏主程序代码时，100% 通过缓存空间 `0x0C000000` 取指。原 AOT 盲目对 `0x0C` 和 `0xAC` 双镜像各跑一遍，导致：
   - 第 30 帧 AOT 编译时间白白浪费了 **87.911 ms**；
   - 消耗了 **1,433,096 个 32-bit 字（5.73 MB）** 的 PPC 代码缓存空间；
   - 污染了 8,189 个永远不会被访问的槽位。

---

## 2. 修改方案与安全边界

本提交执行单一逻辑变更：**完全移除针对 0xAC 空间的静态预编译扫描**。

### 2.1 修改代码点 ([`d_cv1k.cpp`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/drv/cave/d_cv1k.cpp))
1. **冷启动预编译**：
   在 `DrvFrame()` 第 30 帧 AOT 触发逻辑中，保留 `0x0c000000` 预编译，移除 `0xac000000` 调用：
   ```cpp
   // 移除：Sh3DrcPrecompile(0xac000000, is_type_d ? 0x400000 : 0x200000, SH3_AOT_TRIGGER_BOOT);
   ```
2. **读档状态恢复**：
   在 `DrvScan()` 读档恢复 AOT 逻辑中，同样仅保留 `0x0c000000`，移除 `0xac000000` 调用。

### 2.2 安全与兼容性保证
- **动态别名访问完全不受影响**：DRC 核心、内存映射及 Shadow 数组对 `0xAC` 地址的动态别名处理、运行时有效性检查和 JIT 补编逻辑 100% 保持不变。如果游戏在任何非主程序阶段偶然通过 `0xAC` 执行代码，DRC 依然能正常进行动态 JIT 编译并执行。
- **生命周期与遥测守恒**：`sh3_aot_profile` 和 `sh3_drc_ppc.h` 中的 0xac 空间状态机与冷块结清统计完全保留，作为诊断守恒校验。

---

## 3. 测试与验证闭环

### 3.1 AOT 单元测试 (`test.cpp`)
```bash
g++ -std=c++11 -O2 -Ilibretro/FBNeo/src libretro/FBNeo/tests/sh3_aot_profile/test.cpp -o /tmp/test_aot && /tmp/test_aot
```
- 结果：**PASS**（9 组生命周期场景、多段 run 记录及 shadow/generation 代次核验全部通过）。

### 3.2 宿主 Dispatch 测试套件 (`run_inline.py`)
```bash
python3 libretro/FBNeo/tests/sh3_dispatch/run_inline.py --output /tmp/inline_test.log
```
- 结果：**PASS**（8 个测试套件在 optimized 和 ASan/UBSan 模式下全部通过）。

### 3.3 PPC QEMU 差分测试套件 (`run.py`)
```bash
python3 libretro/FBNeo/tests/sh3_ppc/run.py --toolchain-root /home/humor/salvia-tests/toolchains/ppc/root
```
- 结果：**PASS**（全部 848,846 个测试用例无一失败）。

---

## 4. 预期实机收益

1. **第 30 帧启动卡顿立减 34.1%**：
   - 剔除 Run 2（87.9 ms）后，第 30 帧 AOT prep 耗时预计由 174.8 ms 降至约 86.8 ms，该帧总耗时预计从 258 ms 降至约 170 ms。
2. **代码缓存大幅节省 5.73 MB**：
   - AOT 编译代码字数由 2,859,624 降至约 1,426,528 字，为运行期 JIT 腾出大量宝贵空间。
3. **消除 8,189 个无用冷代码块**：
   - 彻底避免死代码污染哈希表与 arena 槽位。
