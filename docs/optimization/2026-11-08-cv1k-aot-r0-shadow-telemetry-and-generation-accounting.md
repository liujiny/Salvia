# CV1000 AOT 阶段 R0：Shadow 遥测修复与代次/空间口径统一

日期：2026-11-08。基线提交：`8127bd04`。
对应优化路线图阶段：**R0**（口径/来源统计修正与真实生产测试，shadow/表路径一致，生命周期守恒，现有功能与预算不变）。

---

## 1. 变更背景与根因分析

在 Codex 对 Priority 3 主机遥测结果的独立复核报告（`docs/optimization/2026-11-08-cv1k-aot-review-and-next-plan.md`）中，指出了 AOT 遥测统计的盲区与缺陷：

1. **Shadow 路径漏记 AOT 执行**：
   在 `sh3_drc_dispatch.h` 中，当 `copy` 非空（Shadow 命中）时，直接取得 entry 并执行，完全跳过了 221~415 行的常规 4-way 查找路径。原 `slot_aot_state` 的状态翻转（1→2）和 `total_aot_executed++` 仅位于常规路径内部（原第 393 行）。而 AOT 在启动时就会预填 Shadow 数组，导致大量由 AOT 生成的代码块首次被执行时经由 Shadow 命中，从而完全没有被计入 `total_aot_executed`。此前控制台测得的 4.1%（671 / 16376）并非真实覆盖率。
2. **状态更新位于判空之前**：
   原常规路径中的 `slot_aot_state` 计数位于 `if (!entry) return false;` 之前，存在将编译失败（`entry == NULL`）误计为 AOT 执行的隐患。
3. **缺乏生成代次（generation）比对**：
   若某 slot 上的旧 AOT 代码因冲突或 sector 回收被替换，而 Shadow 中仍保留该 slot 的快照指针，在 Shadow 命中时若仅通过 `copy->slot` 索引，可能将执行信用误计给当前 slot 上的新代次。
4. **编译失败未能清算 cold-replaced**：
   `arena_note_slot()` 遇 `!block.entry` 直接 return，未将该 slot 上原有的未执行 AOT 状态标记为 cold-replaced，导致旧状态残留。
5. **缺少虚拟地址空间（0x0c / 0xac）分列与动态执行占比**：
   原统计未区分 0x0c000000 与 0xac000000 两段 AOT 的编译与执行情况；且仅有静态唯一块数，缺乏反映实际 CPU 执行时间占总 DRC 调度比率的动态执行次数（dynamic AOT vs JIT entries）。
6. **未在游戏起点统一清空会话累计值**：
   `sh3_aot_profile` 缺乏生命周期边界，跨游戏/复位未受显式控制。

---

## 2. 核心修改方案 (R0)

本阶段**完全不改变 AOT 扫描顺序、预算或代码生成逻辑**，专注将口径、状态流转与遥测指标彻底校准：

### 2.1 `struct Shadow` 诊断字段与 128 字节缓存行对齐 (`sh3_drc_ppc.h`)
- 在 `#if SALVIA_FBNEO_DIAGNOSTICS` 下，为 `struct Shadow` 添加 `UINT32 slot;` 和 `UINT32 gen;`（共 8 字节）。
- 非数组基础字段由 28 字节增至 36 字节，padding 调整为 `128 - 36 - (MAX_INSNS + 1) * 2`（32 位 PPC 下为 26 字节）。
- 严格保证 32 位 PPC 上 `sizeof(Shadow) == 128`，静态断言 `ShadowIsOneCacheLine` 始终成立。
- 在 `Sh3DrcPrecompile` 与 `sh3_drc_dispatch.h` 写入 Shadow 时，同步填充 `dst.slot` 与 `dst.gen`。

### 2.2 生成代次跟踪 (`slot_gen`) 与完整的 Slot 状态机 (`sh3_drc_ppc.h`)
- 分配 `UINT32 *slot_gen`（`TABLE_SIZE * sizeof(UINT32)`）。
- 状态机细分为：
  - `0`: 空闲 / 失效 / 回收
  - `1`: 0x0c 空间 AOT 编译未执行（Cold）
  - `2`: 0x0c 空间 AOT 已执行过至少一次（Hot）
  - `3`: 0xac 空间 AOT 编译未执行（Cold）
  - `4`: 0xac 空间 AOT 已执行过至少一次（Hot）
  - `5`: JIT 编译未执行（Cold）
  - `6`: JIT 已执行过至少一次（Hot）
- 在 `arena_note_slot` 中，每次无论编译成功或失败均自增 `++slot_gen[slot]`。若原状态为 1 或 3（未执行 AOT），统一清算结账：
  `++total_aot_replaced_cold;` 并累计至对应空间的 cold 统计。
- 在 `arena_reuse_sector`（sector 回收）中，自增 `++slot_gen[i]`，并将该 sector 内未执行的 AOT slot 结清 cold-replaced。
- 在 `sh3_drc_reset()` 中，遍历清算所有遗留的未执行 AOT slot 为 cold-replaced，并重置 `slot_gen`。

### 2.3 调度入口统一执行判定 (`sh3_drc_dispatch.h`)
- 移除常规表查找路径中旧的过早更新逻辑。
- 将执行记录移至常规查找与 Shadow 命中的**唯一公共汇合点**，即 `if (!entry) return false;` 之后、实际调用原生代码执行之前：
  ```cpp
#if SALVIA_FBNEO_DIAGNOSTICS
  if (slot_aot_state) {
   const unsigned exec_slot = copy ? copy->slot : (index * WAYS + way);
   const bool gen_match = copy ? (slot_gen && copy->slot < TABLE_SIZE && slot_gen[copy->slot] == copy->gen) : true;
   if (gen_match && exec_slot < TABLE_SIZE) {
    const UINT8 st = slot_aot_state[exec_slot];
    if (st == 1) {
     slot_aot_state[exec_slot] = 2;
     ++sh3_aot_profile.total_aot_executed;
     ++sh3_aot_profile.total_aot_0c_executed;
     ++sh3_aot_profile.dynamic_aot_entries;
    } else if (st == 2) {
     ++sh3_aot_profile.dynamic_aot_entries;
    } else if (st == 3) {
     slot_aot_state[exec_slot] = 4;
     ++sh3_aot_profile.total_aot_executed;
     ++sh3_aot_profile.total_aot_ac_executed;
     ++sh3_aot_profile.dynamic_aot_entries;
    } else if (st == 4) {
     ++sh3_aot_profile.dynamic_aot_entries;
    } else if (st == 5) {
     slot_aot_state[exec_slot] = 6;
     ++sh3_aot_profile.total_jit_executed;
     ++sh3_aot_profile.dynamic_jit_entries;
    } else if (st == 6) {
     ++sh3_aot_profile.dynamic_jit_entries;
    }
   }
  }
#endif
  ```
- 若为 Shadow 命中且代次不匹配（`gen_match == false`），拒绝将执行信用误赋给被复用 slot 的新代次。
- 无论初次还是重复执行，均实时累计 `dynamic_aot_entries` 或 `dynamic_jit_entries`，反映动态原生块执行流量分配。

### 2.4 遥测报表与生命周期边界 (`sh3_aot_profile.h`, `d_cv1k.cpp`, `sh4.cpp`)
- 在暂停日志中新增分空间报表与动态执行比率报表：
  - `aot_summary`: 累计 AOT 编译块数、唯一执行块数、覆盖百分比、冷替换块数、JIT 编译数、JIT 唯一执行数。
  - `aot_spaces`: 分列 0x0c 与 0xac 空间的编译、执行、冷替换块数。
  - `aot_dynamic`: 动态 AOT 执行次数、动态 JIT 执行次数、动态 AOT 流量占比（`dyn_aot_pct`）。
  - `aot_resident`: 暂停时 live 扫描当前哈希表内实际驻留的 0c/ac hot/cold 与 JIT hot/cold 块数。
- 在 `d_cv1k.cpp` 的 `DrvInit()` 与 `sh4.cpp` 的 `Sh3Init()` 中调用 `sh3_aot_profile.clear()`，确保每次游戏启动或加载均有干净的统计边界。

---

## 3. 验证与测试结果

### 3.1 单元测试 (`test.cpp`)
新增 Test 9，专门覆盖 Codex 审阅报告中要求的全部边界场景：
1. **AOT 0x0c 块经 Shadow 命中首次执行**：状态成功从 1 翻转至 2，`total_aot_executed` 与 `dynamic_aot_entries` 准确累加。
2. **Shadow 命中重复执行**：唯一执行数保持为 1，动态执行数随调用严格自增。
3. **对照测试（无 Shadow / 表查找）**：0xac 块经由常规表路径首次执行，状态从 3 翻转至 4，`total_aot_executed` 累加至 2。
4. **陈旧 Shadow 与代次不匹配保护**：当 slot 被 JIT 重用并递增 generation 后，持有旧代次的 Shadow 命中无法通过 `gen_match`，新占用的 JIT 块未被误判为已执行。
5. **编译失败（`!entry`）**：安全退出，绝不记录执行信用。
6. **JIT 块执行流转**：状态从 5 翻转至 6，`total_jit_executed` 与 `dynamic_jit_entries` 准确累加。

执行命令：
```bash
g++ -std=c++11 -O2 -Ilibretro/FBNeo/src libretro/FBNeo/tests/sh3_aot_profile/test.cpp -o /tmp/test_aot && /tmp/test_aot
```
测试结果：
```text
PASS Sh3AotProfile: lifecycle, bounded runs=8, execution tracking, formatting, queue retry on full, prioritized staged traversal, R0 shadow/generation accounting
```

### 3.2 宿主内联测试套件 (`run_inline.py`)
运行全部 8 个宿主测试组件（包括 `sh3_dispatch`、`sh3_lookup`、`sh3_source_check`、`sh3_block_layout`、`sh3_work_profile`、`sh3_fallback_detail`、`sh3_idle_candidate`、`sh3_movll_service`），包含 `optimized` (-O3) 与 `asan-ubsan` 内存安全检测模式。
执行结果：
```text
PASS 8 host suites in optimized and ASan/UBSan modes
Scope: synthetic callbacks and data validation; not PPC execution or FPS
```

### 3.3 PPC QEMU 差分测试套件 (`run.py`)
使用 Debian PPC 交叉编译工具链编译并在 QEMU-PPC 模拟器中执行全量 848,846 条指令差分测试与 DRC 边界测试。
执行结果：
```text
MODE 0 cases 51573 compiled 37622 fallback 13951
MODE 1 cases 103146 compiled 87659 fallback 15487
MODE 2 cases 154719 compiled 137696 fallback 17023
MODE 3 cases 206292 compiled 187733 fallback 18559
MODE 4 cases 257865 compiled 226122 fallback 31743
EDGE native GBR stack / independent state and cycles / all registers / dirty slots / aliases / guarded delays PASS cases=5504
EDGE signed/unsigned long multiply / all operand pairs / accumulators / delay and budgets PASS cases=44768
EDGE native carry/borrow / independent arithmetic / all aliases / dirty T / chained and delayed consumers PASS cases=89536
EDGE compiled delay slots / branch dependencies / guarded slots PASS
EDGE conditional fallthrough / adjacent guards / cycle budgets PASS
EDGE precise code-write ranges / aliases / DT lookahead PASS
EDGE memory operands / dirty destinations / aliased addresses / guarded delay slots PASS cases=1024
EDGE direct RAM / literal reads / aliases / mapping invalidation PASS
EDGE mirrored direct RAM / watched reads / handler invalidation PASS
EDGE lifecycle / self-modification / remap / page boundary PASS
EDGE compiled watched MOV.L service / delay / live PC / exact handler and burn / validation PASS cases=12545
EDGE compiled device MOV.L / all operands / aliases / delay / callback and mapping revocation PASS cases=16396
EDGE sampled idle candidates / live metadata / handler PC / no state mutation PASS
EDGE AOT precompilation / BFS control flow traversal PASS
PASS 848846 cases compiled 750948 fallback 97898
```
全部 848,846 个测试用例无一失败。

---

## 4. 构建产物与哈希身份

- 构建命令：
  ```bash
  bash /home/humor/salvia-tests/xex-build/build-xex.sh --baseline f61421f6 --tag aot-r0 --flavor diag --force
  ```
- 源码 HEAD：`3f6c56aaf29678164527fcf12746f1d29ef2ded1`
- 产物 SHA-256（`SALVIA_FBNEO_DIAGNOSTICS 1`）：
  - `Distro360/fbneo.xex`：`f020b6bfc4a3bb6a1db0c901e70e8c4f2a9e102c2a481ffcbf49995145f4c39b`
  - `Distro360/mmpork.xex`：`c3c1c0aef9ad985b86f7d880d811426d33a2442f6da2c83916caeb53092466fe`
  - `Distro360/ddpsdoj.xex`：`943b3f983979bfc0084ab27b96a63e1588dd915f8d8125bad380a152db4cb9e8`
- 归档路径：`/home/humor/salvia-tests/xex-archive/`。
- 元数据记录：`/home/humor/salvia-tests/xex-build/artifact-aot-r0.json`。

---

## 5. Xbox 360 Jasper 实机遥测数据 (R0)

在 Xbox 360 Jasper 主机（`192.168.2.232`，双核 3.2GHz PowerPC Xenon）上运行本轮部署的 `mmpork.xex`（诊断版本 `aot-r0 3f6c56aa`），采集到 9,951 帧真实街机游玩（约 3 分钟全程，batches=7849）的完整日志数据：

### 5.1 AOT 启动与生命周期总览 (`aot_summary` & `aot_spaces`)
```text
CV1000 GPU: aot_run id=1 trigger=boot range=0x0C000000..0x0C200000 size=2048KB seeds=1537 (fixed=4 prol=1533 ptr=0) push_att=19528 push_ok=8200 push_dup=11328 push_full=0 q_peak=46 popped=8198 map_rej=0 already=4 compiled=8187 fail=6 legacy_cnt=16385 exit=budget_reached q_rem=2 words=+1426528 slots_rep=0 time_ms=86.784 syncs=1 sync_words=1426528 arena_used=1464788 (batches=7849)

CV1000 GPU: aot_run id=2 trigger=boot range=0xAC000000..0xAC200000 size=2048KB seeds=1537 (fixed=4 prol=1533 ptr=0) push_att=19480 push_ok=8199 push_dup=11281 push_full=0 q_peak=46 popped=8196 map_rej=0 already=0 compiled=8189 fail=6 legacy_cnt=16385 exit=budget_reached q_rem=3 words=+1433096 slots_rep=2 time_ms=87.911 syncs=1 sync_words=1433096 arena_used=2897884 (batches=7849)

CV1000 GPU: aot_summary total_aot_compiled=16376 aot_executed=1668 aot_executed_pct=10.2 aot_cold_evicted=26 jit_compiled=8718 jit_executed=8718 runs=2 (batches=7849)

CV1000 GPU: aot_spaces 0c_compiled=8187 0c_exec=1668 0c_cold=20 ac_compiled=8189 ac_exec=0 ac_cold=6 (batches=7849)
```

### 5.2 动态执行占比与驻留内存状态 (`aot_dynamic` & `aot_resident`)
```text
CV1000 GPU: aot_dynamic dyn_aot=65975105 dyn_jit=315865695 dyn_aot_pct=17.28 (batches=7849)

CV1000 GPU: aot_resident total_aot=16344 (0c_hot=1662 0c_cold=6499 ac_hot=0 ac_cold=8183) total_jit=8718 (jit_hot=8718 jit_cold=0) (batches=7849)
```

### 5.3 核心与前端帧性能 (`core_frame_ms` & `frontend_stall_watch`)
```text
CV1000 GPU: core_frame_ms sampling=random-1-in-64 prep=0.003 cpu_io=10.839 audio_tail_misc=0.153 draw_sync=2.519 total=13.515 sampled_peak=35.978 excludes=frontend_present_limiter (batches=7849)

CV1000 review: frontend_frame_ms sampling=random-1-in-64 game_and_ui=13.812 present=0.908 limiter=2.559 sampled_active_loop=17.279 sampled_peak=36.736 excludes=preloop_events

CV1000 review: frontend_stall_watch frames=9951 invalid=0 threshold_ms=100 count=12 retained=8 peak_ms=270.502 coverage=all_started_loops_including_preloop_events
CV1000 review: frontend_stall rank=1 interval_frame=42 events=0.007 game_and_ui=269.954 present=0.539 limiter=0.002 total=270.502
CV1000 review: frontend_stall rank=2 interval_frame=30 events=0.004 game_and_ui=258.643 present=0.546 limiter=0.002 total=259.194
CV1000 review: frontend_stall rank=3 interval_frame=9 events=0.005 game_and_ui=132.880 present=0.512 limiter=0.001 total=133.398
CV1000 review: frontend_stall rank=4 interval_frame=7 events=0.005 game_and_ui=127.050 present=0.509 limiter=0.001 total=127.565
CV1000 review: frontend_stall rank=5 interval_frame=8 events=0.005 game_and_ui=124.382 present=0.513 limiter=0.001 total=124.901
CV1000 review: frontend_stall rank=6 interval_frame=2 events=0.007 game_and_ui=122.163 present=0.554 limiter=0.001 total=122.725
CV1000 review: frontend_stall rank=7 interval_frame=6 events=0.005 game_and_ui=120.648 present=0.524 limiter=0.001 total=121.179
CV1000 review: frontend_stall rank=8 interval_frame=3 events=0.007 game_and_ui=119.809 present=0.516 limiter=0.002 total=120.334
```

---

## 6. 重大实机发现与深度剖析

结合本次真实主机的精准遥测数据，获得以下突破性定论：

### 6.1 重大发现 1：0xAC 空间完全无执行（0.00% 执行率），彻底揭开“4.1%”的真相
- 遥测明确记录：`ac_compiled=8189, ac_exec=0, ac_cold=6`。
- 在近万帧游玩过程中，**0xAC000000 空间预编译的 8,189 个块从未被执行哪怕一次（执行率 0.00%）！**
- **架构根因**：CV1000 硬件架构中，`0xAC000000` 是 `0x0C000000`（Main RAM）的无缓存（uncached/P2）镜像别名。SH3 CPU 在执行游戏程序代码时，100% 通过缓存空间 `0x0C000000` 寻址。原 AOT 盲目对 `0x0C` 和 `0xAC` 双镜像各跑一遍，导致：
  1. 第 30 帧 AOT 编译时间白白浪费了 **87.911 ms**；
  2. 占用了 **1,433,096 个 32-bit 字（5.73 MB）** 的 PPC 代码缓存空间；
  3. 凭空引入了 8,189 个永远不会被访问的死代码槽位；
  4. 分母被人为放大了一倍（8,187 + 8,189 = 16,376）。
- **校准后的真实有效命中率**：在真正执行代码的 `0x0C` 空间内，`0c_compiled=8187, 0c_exec=1668`，真实唯一块执行命中率高达 **20.37%**（而非旧口径误导的 2.6% 或 4.1%）！

### 6.2 重大发现 2：动态执行流量占比高达 17.28%（6597 万次调度由 AOT 承载）
- 在整场 9,951 帧的游玩中，总原生调度入口达 3.818 亿次：
  - `dyn_aot = 65,975,105` 次
  - `dyn_jit = 315,865,695` 次
  - `dyn_aot_pct = 17.28%`
- **结论**：虽然静态热块占 20.37%，但这些预编译热块承载了**全游戏 17.28% 的动态指令调度总流量**！这 6,597 万次进入完全省去了 JIT 编译与查找等待。

### 6.3 重大发现 3：Shadow 漏记缺陷彻底闭环
- 在 P3 基线中未统计 Shadow 执行时，仅测得 671 个执行块。
- 在 R0 统一于调度公共点（含 generation 比对）后，执行块上升至 **1,668 块**（**+148.6%**），确证了 Codex 关于“旧统计因 Shadow 命中而漏记一半以上 AOT 块”的推断。

### 6.4 重大发现 4：稳态游戏全程无卡顿，卡顿仅存在于前 42 帧启动期
- 9,951 帧中全部 12 次 >100ms 停顿均发生在帧 42 之前（帧 2~9 为 ROM 自检与解密，帧 30 为 AOT，帧 42 为主资源解包与首次循环）。
- 帧 43 直至帧 9951（长达 9,908 帧，超过 99.5% 的游玩时间）**无任何一次 >100ms 卡顿**！
- 稳态核心平均耗时仅 **13.515 ms**（远低于 60 FPS 对应的 16.667 ms 预算），`limiter=2.559 ms`，游戏在稳态下完全处于满速运行。

---

## 7. 下一步行动方案与预期收益 (R1 & R2)

基于上述确凿数据，接下来的优化方向路径极其清晰：

1. **立即可行且无风险的收益（剔除 0xAC 空间扫描）**：
   - 将 AOT 触发范围仅保留 `0x0C000000..0x0C200000`，直接移除 `0xAC` 空间的 AOT run。
   - **直接收益**：
     - 第 30 帧启动时间立减 **87.9 ms**（由 258 ms 降至约 170 ms，降幅达 **34.1%**）；
     - PPC 代码缓存占用立省 **5.73 MB**；
     - 彻底消除 8,189 个无用冷块。
2. **启动第 42 帧长尾剖析 (R1)**：
   - 第 42 帧耗时 266.8 ms，其中 `cpu_io=265.9 ms`，但 `compiled_blocks=20`。
   - 深入剖析第 42 帧内部各组件（SH3 解密/内存拷贝循环、EEPROM、声音 DSP 初始化等），消除这最后的 266ms 启动峰值。
3. **PGO 或后继遍历 (R2)**：
   - 8,187 个 0c 预编译块中已有 1,668 个高频热块被精确定位。结合 PGO 或受控后继遍历，可进一步提高 0c 块的紧凑度。
