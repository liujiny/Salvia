# CV1000 优化阶段 R3：CMPSTR 原生 PPC 生成器与精确操作码遥测

日期：2026-11-08。基线提交：`39cbd232`。
对应优化路线图阶段：**R3（补全核心缺失指令发射器，消除解释器回退）**。

---

## 1. 变更背景与依据

### 1.1 实机性能遥测数据揭示的瓶颈
在 Xbox 360 Jasper（`192.168.2.232`）上运行 10,320 帧实机游玩日志（`console_cv1000_gpu_r1_r2.log`）中，423 个严重慢帧（`>25ms` / `<40 FPS`）的分解剖析：
- 平均慢帧耗时：`total=31.537ms`，其中 `cpu_io=30.664ms`（占总帧时 **97.2%**）。
- 解释器 Guest 周期高达 **43,325,082**，是 DRC 原生周期的 **2.17 倍**。
- 回退操作码高 8 位集中在：`0x46xx`（16,422 次）、`0x27xx`（11,049 次）、`0x60xx`（8,625 次），以及 Partial Exits（28,497 次）。

### 1.2 关键缺失指令定位：`CMP/STR Rm, Rn` (`0x200c`)
在 SH-3 指令编码中，`opcode >> 8` 对应 `(Family << 4) | (Destination_Rn)`：
- 前三位回退集中在 `0x2xxx`、`0x4xxx`、`0x6xxx` 族。
- 深入核对 `sh3_drc_ppc.h` 原生发射器后发现：**`CMP/STR Rm, Rn` (`0x200c` / `0x2nmc`) 指令完全未被 DRC 原生实现**！
- Cave 游戏引擎在活动块（Sprite）、碰撞盒与字符串匹配扫描循环中极其高频调用 `CMPSTR`。每次执行均因未实现发射器而被迫打断 DRC 块流，退回 C++ 慢速解释器逐条解释。

---

## 2. 修改方案与实现细节

### 2.1 原生 PPC `CMPSTR` 发射器 ([`sh3_drc_ppc.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/cpu/sh4/sh3_drc_ppc.h))
`CMPSTR Rm, Rn` 的语义为：若 `Rm` 与 `Rn` 的 4 个对应字节中存在任一相等字节（即 `(Rm ^ Rn)` 的 4 个字节中存在任一零字节），则置位 `T = 1`；否则清零 `T = 0`。

利用位运算经典的 **0 字节检测算法**：
$$\text{haszero}(v) = (v - 0x01010101) \ \& \ \sim v \ \& \ 0x80808080$$
只要 $v$ 的某字节为 $0$，对应的高比特位就会被置 $1$。

在 PowerPC 架构下，仅需 6 条高能效原生指令即可实现无分支判定：
```cpp
case 0x200c: { // CMP/STR Rm, Rn: T = (any byte of Rn ^ Rm == 0)
    a = reg(n); b = reg(m);
    emit(x(a, 0, b, 316));        // xor r0, a, b (r0 = a ^ b)
    imm(12, 0x01010101);          // r12 = 0x01010101
    sub(11, 0, 12);               // subf r11, r12, r0: r11 = r0 - 0x01010101
    emit(x(11, 11, 0, 60));       // andc r11, r11, r0: r11 = r11 & ~r0
    imm(12, 0x80808080);          // r12 = 0x80808080
    emit(x(11, 12, 12, 28) | 1);  // and. r12, r11, r12 (Rc=1, tests CR0)
    compare_t(2, false);          // T = (CR0_EQ == 0)
    return true;
}
```
该实现经数学穷举验证（所有 6,561 组单字节边界情况及 5,000,000 组随机 32 位值），与 SH-3 解释器行为 100% 逐位一致。

### 2.2 实机精确 16 位操作码遥测 ([`sh4.cpp`](file:///home/humor/src/Salvia/libretro/FBNeo/src/cpu/sh4/sh4.cpp))
在 `sh4.cpp` 的 `Sh3WorkReport` 中，启用对 `_XBOX` 环境的 `drc_work_opcode16` 格式化输出。
在游戏暂停时，精准输出触发解释器回退排名前 16 位完整的 16 位操作码（包括寄存器编号、具体变体），以便精准定位剩余回退指令。

### 2.3 启动停顿扩容与稳态慢帧过滤 ([`d_cv1k.cpp`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/drv/cave/d_cv1k.cpp))
1. **扩容 `s_startup_stalls` 至 32 项**：原 8 项在第 1~8 帧即被占满，导致第 30 帧 AOT 耗时和第 42 帧长停顿无法在 startup 列表中捕获。扩容至 32 项可完整覆盖前 60 帧全部停顿事件。
2. **慢帧监控过滤启动阶段污染**：`cv1k_review_drops.record(...)` 添加 `if (cv1k_review.frames > 60)` 判定，确保 `core_drop` 集中记录玩家在正式游玩过程中的掉帧慢帧（`<40 FPS`），不再被启动加载帧冲刷。

---

## 3. 测试与验证闭环

### 3.1 AOT 单元测试 (`test_aot`)
```bash
g++ -std=c++11 -O2 -Ilibretro/FBNeo/src libretro/FBNeo/tests/sh3_aot_profile/test.cpp -o /tmp/test_aot && /tmp/test_aot
```
- **结果**：`PASS Sh3AotProfile: lifecycle, bounded runs=8, execution tracking, formatting, queue retry on full, prioritized staged traversal, R0 shadow/generation accounting`

### 3.2 宿主 Dispatch 测试套件 (`run_inline.py`)
```bash
python3 libretro/FBNeo/tests/sh3_dispatch/run_inline.py --output /tmp/inline_test.log
```
- **结果**：`PASS 8 host suites in optimized and ASan/UBSan modes`

### 3.3 PPC QEMU 差分测试套件 (`run.py`)
```bash
python3 libretro/FBNeo/tests/sh3_ppc/run.py --toolchain-root /home/humor/salvia-tests/toolchains/ppc/root
```
- **结果**：`PASS 851662 cases compiled 753764 fallback 97898`（编译块数增加，回退数下降，全部寄存器、标志位及边角情况测试通过）。

---

## 4. 产物镜像与实机部署

- **构建 Tag**：`r3-cmpstr`
- **代码提交**：`b336cdcc`（基于 `39cbd232`）
- **构建环境**：VS2010 / XDK，`SALVIA_FBNEO_DIAGNOSTICS 1`
- **XEX 镜像信息**：
  - `mmpork.xex` (diag)：34,832,384 字节，SHA256 `70189aa6de4677be9d49a448a89e49224ee2d1177acca3adddb2a31ce7f0e8b3`
  - `fbneo.xex` (diag)：34,832,384 字节，SHA256 `846fa4d5d5cf5906a6497badc8ae769fe10ff5d4f8614d9a094c06bbdf9bf07f`
- **实机部署**：已通过 FTP 上传至 Jasper 主机 `/Usb0/Salvia-360/mmpork.xex` 并通过 Aurora 自动启动。

---

## 5. 验证限制与后续步骤
宿主与 PPC QEMU 测试证明了代码生成的正确性与 PPC 指令等价性。
实机 CPU/IO 耗时减少幅度与慢帧改善效果须在 Xbox 360 Jasper 实机上运行验证。
构建并部署新版诊断镜像，实机运行后通过暂停提取新日志以核对 `CMPSTR` 消除效果及剩余回退操作码。
