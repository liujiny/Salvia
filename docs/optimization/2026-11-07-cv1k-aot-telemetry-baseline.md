# Salvia CV1000 / SH3 AOT 静态预编译事实统计（第一优先级）

日期：2026-11-07。状态：**功能实现与宿主验证通过，等待实机基准采集。**
基线 SHA：`f4b3c303fb5a7f9bd7ee5ec65eedd016ce1fca6c`。
依据文档：`docs/optimization/2026-11-07-cv1k-aot-optimization-plan-handover.md`（Codex 第一优先级建议）。

## 1. 目标与设计约束

本阶段的目标是**建立确凿、低开销的静态预编译（AOT）与运行时覆盖事实统计**，为后续的预编译优化提供客观数字依据。
严格遵守 Codex 在建议中设立的铁律：
1. **严格保持既有编译工作量与行为**：不改变 16,384 循环上限判定，不改变出入队顺序，不提前或延后退出。
2. **零运行时侵入与时钟无害**：不添加每条指令时钟或实时文件 I/O，所有统计在 AOT 调用边界及暂停时统一由 `emit` 输出到 `cv1000-gpu.log`。
3. **保持 32-bit `Block` 结构体严格 84 字节**，不破坏 PPC DRC 布局约束。

## 2. 变更内容

### 2.1 新增 AOT 统计组件 (`sh3_aot_profile.h`)
- 定义 `Sh3AotRunRecord`，记录每次 AOT 调用的全流程遥测：
  - `start_pc` / `size` / `trigger`（boot=0, load=1, reset=2）。
  - 种子数分类：`seed_fixed`（引导向量 4 个）、`seed_prologues`（函数序言匹配数）、`seed_pointers`（代码指针匹配数）。
  - 队列行为：`push_attempts`（入队尝试）、`push_success`（实际入队）、`push_duplicate`（重复访问拒绝）、`push_full`（队满拒绝）、`queue_peak`（队列峰值深度）。
  - 编译结果：`popped`（出队数）、`map_reject`（非法/奇数页拒绝）、`already_compiled`（已存在跳过）、`compile_attempts`（编译尝试）、`compile_success`（成功生成 entry）、`compile_failed`（失败）、`legacy_precompiled_count`（退出时的累加计数器值）。
  - 退出原因：`queue_empty`（队列排空）、`budget_reached`（达到 16384 累积上限）、`arena_full`（Arena 显存耗尽）。
  - 工作量与资源：`words_added`（本次新增 PPC 机器码字数）、`slots_replaced`（覆盖既有块数）、`sync_calls`（调用 `sync_code` 次数）、`sync_words`（同步机器码字数）、`duration_ticks`（硬件定时器耗时）。
- 定义 `Sh3AotProfile`，维护最多 8 次运行的滑动记录窗口及全局累计指标。
- 提供 `Sh3AotReport(emit)`，在暂停时按标准格式打印单次运行及全局汇总：
  ```text
  aot_run id=1 trigger=boot range=0x0C000000..0x0C200000 size=2048KB seeds=... push_att=... push_ok=... push_dup=... push_full=... q_peak=... popped=... map_rej=... already=... compiled=... fail=... legacy_cnt=... exit=... q_rem=... words=+... slots_rep=... time_ms=... syncs=... sync_words=... arena_used=...
  aot_summary total_aot_compiled=... aot_executed=... aot_executed_pct=... aot_cold_evicted=... jit_compiled=... runs=...
  ```

### 2.2 块生命周期与覆盖率追踪 (`slot_aot_state`)
- 在 `Sh3Ppc` 中分配 `slot_aot_state`（128 KB 外部字节数组，与 `TABLE_SIZE` 槽位一一对应，不改变 `Block` 内存结构）。
- 状态机：
  - `0`: 空或未标记。
  - `1`: 由 AOT 预编译生成，尚未执行。
  - `2`: 由 AOT 预编译生成，且**已在游戏运行中至少执行过一次**（首次执行时平滑转换）。
  - `3`: 运行时由 JIT 编译生成。
- 冷块置换检测：若运行时 JIT 或 Arena 扇区复用覆盖了处于状态 `1` 的槽位，累加 `total_aot_replaced_cold`。以此精确衡量“预编译了但一次都没跑就被冲掉”的冷代码浪费。

### 2.3 运行时重编原因细分 (`rebuild_uncovered`)
- 在 `Sh3DrcWorkProfile` 中新增 `rebuild_uncovered`，将原来的 `rebuild_conflict` 拆解为：
  - `rebuild_uncovered`: 槽位为空（`!b.entry`），首次执行未被 AOT 覆盖到的全新代码。
  - `rebuild_conflict`: 槽位已有其他有效代码（`b.entry != NULL && b.pc != pc`），同 Cache Set 发生哈希槽位冲突置换。
- 在 `drc_work_rebuild_causes` 中同步输出 `uncovered` 统计。

### 2.4 调用点上下文标记
- `d_cv1k.cpp` 在启动第 11/30 帧调用 `Sh3DrcPrecompile` 时传入 `SH3_AOT_TRIGGER_BOOT`。
- `d_cv1k.cpp` 在读档（`ACB_WRITE & ACB_VOLATILE`）调用时传入 `SH3_AOT_TRIGGER_LOAD`。

## 3. 验证结果

1. **AOT 统计单元测试**：
   `libretro/FBNeo/tests/sh3_aot_profile/test.cpp` 编译并通过：
   - 生命周期清理与累计验证：PASS。
   - 8 次运行环形缓冲上限截断：PASS。
   - 格式化输出与字段匹配：PASS。
2. **宿主 Dispatch 回归套件**：
   `python3 libretro/FBNeo/tests/sh3_dispatch/run_inline.py --output /tmp/salvia-inline-tests`
   - 8 个测试子套件（包含优化模式与 ASan/UBSan 模式）全部 PASS。
3. **PPC DRC 指令差分回归**：
   `python3 libretro/FBNeo/tests/sh3_ppc/run.py --toolchain-root /home/humor/salvia-tests/toolchains/ppc/root`
   - 全指令集执行回归通过。

## 4. 构建与产物

1. **Xbox 360 原生工具链构建**：
   - 解决 `<xtl.h>` / `<windef.h>` 中 `typedef float FLOAT;` 与 `sh4.cpp` FPU 操作码 `FLOAT()` 的宏冲突：在 `sh3_aot_profile.h` 中彻底移除 `<xtl.h>`，使用 PPC 硬件定时器内联函数 `#pragma intrinsic(__mftb)`（50MHz TimeBase，50,000 ticks/ms）。
   - 构建命令：
     ```bash
     bash /home/humor/salvia-tests/xex-build/build-xex.sh --baseline f61421f6 --tag aot-telemetry --flavor diag --force
     ```
   - 产物与哈希：
     - `Distro360/fbneo.xex`：SHA-256 `700142fdf89ea7ab7a3b9973a34144df83374ff55e3ccd4c7cbc7af86ef94005`
     - `Distro360/mmpork.xex`：SHA-256 `a95284deb312e1ebffef77c6ebbe07b45a1aa37b0f7ca7a4f83f1aa824b446f3`
     - `Distro360/ddpsdoj.xex`：SHA-256 `4af1246ecce63e3fd204031563fd40f4e6fbfe7ddcf959df6f0723dda725d6b4`
   - 归档路径：`/home/humor/salvia-tests/xex-archive/`。
   - 实机部署：通过 LAN FTP 上传 `Distro360/mmpork.xex` 至主机 `/Usb0/Salvia-360/mmpork.xex`，并通过 Aurora `SITE LAUNCH` 触发运行。

## 5. 实机基线数据采集与关键发现 (`mmpork`)

2026-11-07 22:08 采集自 Xbox 360 Jasper（`mmpork.xex` 运行 10,025 帧，包含冷启动、吸引演示、投币选人与关卡实际游玩）：

```text
CV1000 review: build=aot-telemetry 39b306f9 20261107-2158 diag frontend_interval_frames=10025 samples=147 invalid=0 work_count_skipped=0
CV1000 GPU: review build=aot-telemetry 39b306f9 20261107-2158 diag game=mmpork interval_frames=10025 samples=147 invalid=0 clock_hz=102400000 render_cores=2 dips=00,07,00,00 bpp=4 (batches=7722)
CV1000 GPU: aot_summary total_aot_compiled=0 aot_executed=0 aot_executed_pct=0.0 aot_cold_evicted=0 jit_compiled=10375 runs=0 (batches=7722)
CV1000 GPU: drc_work_rebuild_causes uncovered=11 conflict=0 source=0 read_map=0 (batches=7722)
CV1000 GPU: drc_work_arena bytes=33554432 words=8388608 recycles=0 evictions=0 evicted_slots=0 peak_words=1814400 peak_bytes=7257600 (batches=7722)
CV1000 GPU: drc_work_shadow hits=2235576 entries=2239532 hits_pct=99 bytes=4194304 entry_bytes=128 pred_miss=210417 pred_miss_pct=9 touch=1 (batches=7722)
```

### 关键客观事实：AOT 在 MMPork 上从未实际执行 (`runs=0, total_aot_compiled=0`)

遥测数据暴露了一个严重的隐蔽缺陷，证实了 Codex 在建议书第 2 节的推断，并揭示了更深层次的连锁触发根因：

1. **Active-low 投币输入逻辑反转**：
   - 在 `d_cv1k.cpp:838` 中：`bool force_early = (DrvInputs[0] & 0x0c) != 0;`。
   - 街机投币开关为低电平有效（未按下时对应位为 1）。`DrvInputs[0]` 初始化为 `0xff`，未投币状态下 `(0xff & 0x0c) == 0x0c`（非零），导致 `force_early` 在**第 0 帧**即恒为真。
2. **非 fastboot 游戏在第 0 帧空转**：
   - `mmpork` 没有在 `cv1k_fastboot.h` 中配置 RAM 测试跳过补丁，`cv1k_boot_pending` 初始即为 `NULL`。
   - 因此在第 0 帧、甚至在调用 `Sh3NewFrame()` 运行第一条 SH3 指令之前，条件 `(!cv1k_boot_pending && (nCurrentFrame >= 30 || force_early))` 便满足，直接调用了 `Sh3DrcPrecompile` 并将 `s_cv1k_aot_done` 置为 `true`。
3. **冷启动内存分配未就绪**：
   - `Sh3Ppc::allocate()` 是在首次 DRC 块 dispatch 时按需延迟调用的（`sh3_drc_dispatch.h:152`）。在第 0 帧尚未执行任何指令时，`Sh3Ppc::blocks` 与 `Sh3Ppc::lookup` 均为 `NULL`。
   - `Sh3DrcPrecompile` 起始校验包含：
     ```cpp
     if (!sh3_drc_enabled || !Sh3Ppc::blocks || !Sh3Ppc::lookup || size < 2) return;
     ```
   - 因 `Sh3Ppc::blocks == NULL`，`Sh3DrcPrecompile` 在第 0 帧瞬间直接 `return`，未执行任何编译。
   - 但此时 `s_cv1k_aot_done` 已经被标记为 `true`，后续帧再也不会触发 AOT！
4. **结果**：
   - 整场游戏（10,025 帧）的 10,375 个基本块**全由运行时 JIT 随用随编生成**，AOT 预编译在 `mmpork` 上形同虚设！

---

## 6. 下一步演进方案（第二优先级准备）

根据第一优先级采集到的确凿事实，下一轮小范围修正聚焦于：
1. **确保预编译能安全触达内存分配**：
   - `Sh3DrcPrecompile` 中若 `!Sh3Ppc::blocks`，应主动调用 `Sh3Ppc::allocate()`，避免因尚未执行过 dispatch 而静默跳过。
2. **修正投币低电平触发判定**：
   - 投币按下为低电平（`!(DrvInputs[0] & 0x04) || !(DrvInputs[0] & 0x08)` 或 `(DrvInputs[0] & 0x0c) != 0x0c`）。未按投币时应等待解包帧数（例如 30 帧），而非第 0 帧立即触发。
3. **计数与预算解耦 & 队满 visited 修正**（按 Codex 4.1 与 4.2 规划）。
