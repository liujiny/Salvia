# Salvia CV1000 / SH3 AOT 触发、分配就绪与预算解耦修正（第二优先级）

日期：2026-11-07。状态：**实机验证通过，数据记录完成。**
基线 SHA：`23a50dd1152dcf0be649c07ec7624cb5a909fc7f`。
实施提交：`6c4ec83b`。
依据文档：`cv1000_aot_optimization_handover.md`（Codex 第二优先级建议）与 `docs/optimization/2026-11-07-cv1k-aot-telemetry-baseline.md`（第一优先级采集事实）。

## 1. 动机与实机发现

在第一优先级遥测采集（运行 10,025 帧的 `mmpork.xex`）中，我们发现关键事实：
- `aot_summary total_aot_compiled=0 runs=0 jit_compiled=10375`
- AOT 预编译在 `mmpork` 上**从未实际执行**。

排查确认连锁根因：
1. **Active-low 投币逻辑反转**：`d_cv1k.cpp` 中 `bool force_early = (DrvInputs[0] & 0x0c) != 0;` 在未投币（输入为 `0xff`）时恒为真，导致在第 0 帧过早触发。
2. **冷启动内存尚未就绪**：在第 0 帧指令尚未 dispatch，`Sh3Ppc::blocks` 仍为 `NULL`。`Sh3DrcPrecompile` 起始检查 `!Sh3Ppc::blocks` 瞬间返回，但 `s_cv1k_aot_done` 已被置为 `true`，后续帧永远放弃预编译。
3. **队满时过早标记 visited**：`Sh3PrecompileQueue::push()` 在队列满时已先 `mark_visited`，导致丢弃项永久失去未来被后续分支重试的机会。
4. **循环预算与成功块双重自增**：出队时 `++precompiled_count` 与成功时 `precompiled_count++` 导致编译量失真。

## 2. 变更内容

### 2.1 修正投币电平有效性判定 (`d_cv1k.cpp`)
- 将 `bool force_early = (DrvInputs[0] & 0x0c) != 0;` 改为：
  ```cpp
  bool force_early = ((DrvInputs[0] & 0x0c) != 0x0c);
  ```
- 正常无投币游玩时，等待第 30 帧（让游戏在 RAM 中完成解包）再启动预编译。

### 2.2 确保预编译前分配就绪 (`sh3_drc_ppc.h`)
- 将入口检查改为：
  ```cpp
  if (!sh3_drc_enabled || (!Sh3Ppc::blocks && !Sh3Ppc::allocate()) || !Sh3Ppc::lookup || size < 2) return;
  ```
- 若预编译先于运行时 dispatch 调用，主动完成安全内存分配，绝不静默放弃。

### 2.3 队满 visited 延迟标记 (`sh3_drc_ppc.h`)
- 仅当地址成功推入环形队列后才调用 `mark_visited(a)`。
- 若队列满导致推入失败，保持该地址未被访问标记，以便在后续队列腾出空间时仍可被重新推入。

### 2.4 预算消耗与成功编译块数解耦 (`sh3_drc_ppc.h`)
- 引入独立的 `legacy_budget` 保持与既有 16,384 出队+成功复合预算完全一致的终止边界。
- `precompiled_count` 仅在 `b.entry != NULL` 时递增，忠实代表实际成功编译块数。
- 只有实际编译成功（`precompiled_count > 0`）时才执行末尾代码同步。

## 3. 验证结果

1. **AOT 统计与队列重试单元测试**：
   `libretro/FBNeo/tests/sh3_aot_profile/test.cpp` 编译并通过：
   - 满队列拒绝后重入队测试：PASS。
   - 重复入队过滤测试：PASS。
2. **宿主 Dispatch 回归套件**：
   `python3 libretro/FBNeo/tests/sh3_dispatch/run_inline.py --output /tmp/salvia-inline-tests`
   - 8 个测试子套件全部 PASS。
3. **PPC DRC 指令差分回归**：
   `python3 libretro/FBNeo/tests/sh3_ppc/run.py --toolchain-root /home/humor/salvia-tests/toolchains/ppc/root`
   - 848,846 个测试用例全部 PASS。

## 4. 构建产物与哈希

- 构建命令：
  ```bash
  bash /home/humor/salvia-tests/xex-build/build-xex.sh --baseline f61421f6 --tag aot-p2-fix --flavor diag --force
  ```
- 产物 SHA-256：
  - `Distro360/fbneo.xex`：`62502db39adc788e032258995ee85f54b537b2424aca67410da03ff54103187d`
  - `Distro360/mmpork.xex`：`db8840ecf0e8256d1d99526ac80350c4d5407f309547381ff44a34ebea90fef7`
  - `Distro360/ddpsdoj.xex`：`837c29768e1360b1a1197f594fd061c68f325859df8e546780509ef01c5828a8`
- 归档路径：`/home/humor/salvia-tests/xex-archive/`。

## 5. 实机遥测结果与分析 (Xbox 360 Jasper)

- 实机环境：Xbox 360 Jasper，`mmpork.xex`（SHA256: `db8840ec...`），游玩 10,669 帧后退出。
- 遥测日志来源：`cv1000-gpu.log` (516,601 字节，构建标识 `aot-p2-fix 6c4ec83b 20261107-2220 diag`)。

### 5.1 AOT 运行明细

| 指标 | Run 1 (`0x0C000000..0x0C200000`) | Run 2 (`0xAC000000..0xAC200000`) |
| --- | --- | --- |
| 触发类型 (`trigger`) | `boot` (冷启动) | `boot` (冷启动) |
| 镜像大小 (`size`) | 2048 KB | 2048 KB |
| 种子候选总数 (`seeds`) | 20,217 | 20,217 |
| 种子构成 (固定 / 序言 / 指针) | 4 / 7,308 / 12,905 | 4 / 7,308 / 12,905 |
| 入队尝试数 (`push_att`) | 33,590 | 33,598 |
| 入队成功数 (`push_ok`) | 14,883 | 14,889 |
| 重复丢弃数 (`push_dup`) | 18,707 | 18,709 |
| 队满丢弃数 (`push_full`) | **0** | **0** |
| 队列峰值深度 (`q_peak`) | 11,480 | 11,486 |
| 出队块数 (`popped`) | 8,590 | 8,587 |
| 编译成功数 (`compiled`) | 7,795 | 7,798 |
| 编译失败数 (`fail`) | 788 | 788 |
| 预算计数 (`legacy_cnt`) | 16,385 | 16,385 |
| 退出原因 (`exit`) | `budget_reached` | `budget_reached` |
| 队列剩余 (`q_rem`) | 6,293 | 6,302 |
| 新增 PPC 代码字数 (`words`) | +2,145,160 (8.58 MB) | +2,143,736 (8.57 MB) |
| 代码同步次数 (`syncs`) | 7,796 | 7,799 |
| 代码同步字数 (`sync_words`) | 4,301,068 | 6,443,877 |
| Arena 占用 (`arena_used`) | 2,183,420 字 (8.73 MB) | 4,327,156 字 (17.31 MB) |

### 5.2 全局执行总结

| 统计指标 | 实机测量值 | 含义与判定 |
| --- | --- | --- |
| 预编译总块数 (`total_aot_compiled`) | **15,593** | 修复生效：两段预编译均成功执行并生成大量 PPC 块 |
| 实际命中执行块数 (`aot_executed`) | **406** | 游玩 10,669 帧期间，实际执行过的 AOT 块数 |
| **AOT 执行命中率 (`aot_executed_pct`)** | **2.6%** | **极低！编译的 15,593 块中只有 2.6% 在实际游戏中跑过** |
| 冷块被逐出数 (`aot_cold_evicted`) | 41 | 未使用便被 arena 回收的块数 |
| 游戏中 JIT 补编数 (`jit_compiled`) | **9,669** | 实际游玩中仍触发了 9,669 次动态补编 |
| AOT 运行次数 (`runs`) | 2 | 0x0C 与 0xAC 两个 2MB 区域均按计划完成 |
| 重建原因 (`rebuild_causes`) | `uncovered=97, conflict=0, source=0, read_map=0` | 动态补编原因纯粹为未覆盖冷块，无哈希冲突与源码污染 |

### 5.3 核心发现与分析

1. **AOT 触发与执行链条彻底打通**：
   - 修复投币极性后，在第 30 帧顺利触发；DRC 动态内存分配正常，队列平稳运行。
   - 静态队列峰值 11,486（容量 65,536，利用率 17.5%），`push_full = 0`，未发生任何队满丢弃。
2. **极低命中率（2.6%）完全印证 Codex 第三优先级假设**：
   - 基于原始镜像内存扫描函数序言（`0x4f22`）和指针启发式的“盲扫”产生了 20,217 个种子。
   - 大量非代码数据（图片、表格、音效、保留数据）恰巧符合启发式特征，被作为种子入队并耗尽了 16,384 循环预算。
   - 耗费了 17.3 MB 的 PPC 代码缓存编译了 15,593 个“冷块”，然而 1 万多帧游玩只跑了其中 406 块。
   - 与此同时，队列中真正有效的分支可能排在后面（因 16,384 预算耗尽而留在 `q_rem=6,293` 中），导致游戏实际运行时仍需进行 9,669 次 JIT 补编！
3. **单块同步高昂开销 (`sync_code`)**：
   - 当前 `Sh3Ppc::compile()` 每编一个块就触发一次同步，两轮 AOT 共执行了 15,595 次 `sync_code()`。
   - 这在 PPC 架构上意味着大量的 `dcbst` / `icbi` 刷新，导致预编译耗时膨胀。
4. **定时器编译期消除现象**：
   - `sh3_aot_timer_ticks()` 返回 0 导致 `time_ms=0.000`。
   - 查阅 `libretro/xbox64/xenon/gx_d3d9.cpp:5470` 历史记录可知，MSVC PPC 优化器（`/Ox`）将 `__mftb()` 判定为无副作用内联，若上下文缺少内存屏障，CSE 会将其合并消除导致 `t1 - t0 == 0`。下一轮将加入 `_ReadWriteBarrier()` 修复。


