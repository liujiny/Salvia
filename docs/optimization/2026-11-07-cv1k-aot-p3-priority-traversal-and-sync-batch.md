# Salvia CV1000 / SH3 AOT 控制流优先遍历、批量代码同步与硬件计时修正（第三优先级）

日期：2026-11-07。状态：**功能实现与全套宿主验证，准备实机构建。**
基线 SHA：`85d87897c92b2383bb78031d2be11833d7b87834`。
依据文档：`cv1000_aot_optimization_handover.md`（Codex 第三优先级建议）与 `docs/optimization/2026-11-07-cv1k-aot-p2-trigger-and-budget-fix.md`（第二优先级实机遥测数据）。

## 1. 动机与第二优先级实机事实

在第二优先级实机运行（10,669 帧 `mmpork.xex`）中，遥测捕获到决定性数据：
1. **AOT 块执行命中率极低（2.6%）**：
   - 启动预编译了 15,593 个块，但在整场 1 万多帧游玩中，实际被执行的仅有 **406 块**。
   - 游戏中依然发生了 **9,669 次** JIT 补编。
   - 消耗了 17.3 MB 的 PPC 代码缓存，却几乎全是游戏中未访问的“冷块”。
2. **根因定位：队列被静态种子堵死，控制流分支完全无法展开**：
   - 原实现先扫描 2MB 内存，将 **20,217 个启发式种子**（7,308 个函数序言 + 12,905 个指针）一股脑全部推入 FIFO 队列。
   - 当出队编译入口向量（`0x0C000000`）并发现条件分支与子程序目标（深度 1）时，这些真实分支被推到了队列末尾（排在 14,883 个种子之后）。
   - 在 BFS 出队 8,590 个种子后，循环预算（16,384）瞬间耗尽，预编译提前退出！
   - **结果：100% 编译的都是排在前面的静态种子（绝大多数是数据表、贴图与音效的伪代码），真正的游戏入口控制流分支从未被出队编译（深度 1 从未触及）！**
3. **`compile()` 内部冗余 `sync_code`**：
   - 每次编译单块均调用 `sync_code`，在 Xbox 360 上伴随 `__dcbst`、`icbi`、两次 `__sync()` 强总线屏障和 `isync` 指令。
   - 两次 AOT 共执行了 **15,595 次** 昂贵的全套缓存失效，而末尾已有全范围批量同步。
4. **硬件定时器优化器消除**：
   - MSVC PPC 优化器（`/Ox`）将无副作用的 `__mftb()` 判定为纯函数并实施 CSE 消除，导致 `t1 - t0 == 0`（`time_ms=0.000`）。

## 2. 变更内容

### 2.1 控制流优先的分阶段种子补充机制 (`sh3_drc_ppc.h`)
- 严格遵循 Codex 第三优先级规范：“只改变编译优先顺序：实际热点入口优先，再按现有方式补足预算。暂不取消既有扫描，也不扩大 arena、队列与批次。”
- **阶段 1：首推核心入口向量**：
  - 队列中只初始化放入 `start_pc`、异常向量（`+0x100`、`+0x400`、`+0x600`）及头部 Reset 向量。
- **阶段 2：全速展开控制流图**：
  - 只要队列非空，BFS 全速出队、编译并追踪真实分支（BF, BT, BRA, BSR, JSR 等）。
  - 深度 1、2、3... 紧随其后被优先遍历，所有从游戏入口可达的代码均被优先编译进缓存。
- **阶段 3：队列真正排空后，按序提取未访问序言种子**：
  - 当且仅当从主入口可达的控制流子图完全遍历完毕（队列自然排空），才依次在内存中搜寻下一个尚未被访问的函数序言（`0x4f22` STS.L 等），推入队列并立即深入该函数的调用树。
- **阶段 4：序言耗尽后，按序提取未访问指针种子**：
  - 当所有函数序言及子树均遍历完毕且预算未耗尽时，才继续提取未访问的 32 位代码指针。
- **预算与资源界限完全不变**：保持 16,384 循环上限，保持 65,536 队列上限，零额外内存分配。

### 2.2 AOT 期间批量代码同步 (`sh3_drc_ppc.h`)
- 将 `s_in_aot_precompile` 提升为无条件静态变量（诊断与 Release 均可用）。
- 在 `Sh3Ppc::compile()` 中：
  ```cpp
  if(count) {
   if(!s_in_aot_precompile) sync_code(code+used,c.out);
   used=(unsigned)(c.out-code+3)&~3u;
   ...
  }
  ```
- AOT 编译过程中跳过逐块 `sync_code`，在 AOT 退出前统一执行一次大跨度 `Sh3Ppc::sync_code(code + used_before, code + used)`。
- 将 15,595 次强硬件总线屏障直降为 **1 次批量同步**。

### 2.3 硬件定时器内存屏障保护 (`sh3_aot_profile.h` & `sh3_drc_ppc.h`)
- 在 Xbox 360 `sh3_aot_timer_ticks()` 内外引入 `_ReadWriteBarrier()`：
  ```cpp
  _ReadWriteBarrier();
  UINT64 val = (UINT64)__mftb();
  _ReadWriteBarrier();
  return val;
  ```
- 将 `t0`、`t1` 声明为 `volatile UINT64`，彻底阻止优化器跨代码块 reorder 或 CSE 合并。

## 3. 验证结果

1. **AOT 遥测与分阶段优先遍历单元测试**：
   - 增加 Test 8：验证主向量 -> 深度 1 分支 -> 深度 2 分支 -> 队列排空 -> 提取序言种子 -> 提取指针种子的严格执行顺序。
   - `g++ -O2 -Ilibretro/FBNeo/src libretro/FBNeo/tests/sh3_aot_profile/test.cpp -o /tmp/test_aot && /tmp/test_aot`：**PASS**。
2. **宿主 Dispatch 回归套件**：
   - `python3 libretro/FBNeo/tests/sh3_dispatch/run_inline.py --output /tmp/salvia-inline-tests`：**8/8 全部 PASS**。
3. **PPC DRC 指令差分回归**：
   - `python3 libretro/FBNeo/tests/sh3_ppc/run.py --toolchain-root /home/humor/salvia-tests/toolchains/ppc/root`：**848,846 个测试用例全部 PASS**（含 `EDGE AOT precompilation / BFS control flow traversal PASS`）。

## 4. 构建产物与哈希

- 构建命令：
  ```bash
  bash /home/humor/salvia-tests/xex-build/build-xex.sh --baseline f61421f6 --tag aot-p3-opt --flavor diag --force
  ```
- 产物 SHA-256：
  - `Distro360/fbneo.xex`：`7128e701c56a827771235252aefcf1c85fa049d17ea99a203168a82f280f6a1e`
  - `Distro360/mmpork.xex`：`a8923d09f5628d6524739b61687ddedd77ab4286435236ac53bcdf5796cc33da`
  - `Distro360/ddpsdoj.xex`：`4d0915ef522a94530c0a8da3c7573bf9586cf8dcedb8ca769071136af895d5bf`
- 归档路径：`/home/humor/salvia-tests/xex-archive/`。

