# Salvia CV1000 / SH3 AOT 触发、分配就绪与预算解耦修正（第二优先级）

日期：2026-11-07。状态：**功能实现与全套宿主验证通过，准备实机验证。**
基线 SHA：`23a50dd1152dcf0be649c07ec7624cb5a909fc7f`。
依据文档：`docs/optimization/2026-11-07-cv1k-aot-optimization-plan-handover.md`（Codex 第二优先级建议）与 `docs/optimization/2026-11-07-cv1k-aot-telemetry-baseline.md`（第一优先级采集事实）。

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

