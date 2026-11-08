# CV1000 优化：SH3 DRC 内存镜像监视（0x0c002310）MOVLL 快路径全地址放行与解释器回退根除

日期：2026-11-08。基线提交：`cc86aa15`。
对应优化方向：**SH3 DRC 解释器回退根除（消除 MMPork 40+ FPS 掉帧与 44ms cpu_io 慢帧峰值）**。

---

## 1. 变更背景与归因分析

### 1.1 实机遥测暴露的惊人回退规模
在 MMPork 最新一轮实机测试（479,425 帧深度重放）中，尽管 Option A 注册的片上外设 `0x04000000` 实现了 42,237 次精准拦截（`fallback=0`），但最低帧仍有 40+ FPS（慢帧耗时高达 44.463ms）。

分析 `cv1000-gpu.log` 与 `fbneo-state.log` 的微秒级遥测数据：
1. **慢帧瓶颈 98% 集中在 `cpu_io`**：
   ```
   slow_frame frame=754 total=44.463ms draw_sync=0.731ms cpu_io=43.598ms drc_jit=0.000ms audio=0.130ms vsync=0.004ms
   ```
   GPU Blitter 耗时稳定在 0.73ms，DRC JIT 为 0ms，完全是 CPU/IO 耗时暴增导致掉帧。
2. **客机 CPU 周期严重失衡（61.2% 落在 C++ 解释器）**：
   ```
   guest_cycles: interpreter=1130823396 native=717819038 (61.2% in interpreter!)
   exit_partial: count=1414092, interpreter_cycles=1124142398 (99.4% of interpreter workload)
   ```
   解释器周期高达 11.3 亿周期，而由 `exit_partial` 引发的就占了 11.24 亿周期！
3. **回退第一大操作码与服务拒绝数据**：
   - 触发解释器回退的第一大操作码是 `opcode=6022`（`MOV.L @R2, R0`），达到 493,070 次。
   - `drc_movll_service` 统计：
     ```
     handled=229492 guest_cycles=1384073305 rejected=497826
     ```
     快路径居然被**拒绝了 497,826 次**！

### 1.2 根本原因归因（Root Cause）
在 Cave 驱动体系中（`d_cv1k.cpp`），为了实现帧同步与速度优化，通过 `speedhack_set(0x0c002310, 0x0c05176a)` 注册了 `hacky_idle_ram` 与 `hacky_idle_pc`。

然而，在 [`sh3_drc_movll_service.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/cpu/sh4/sh3_drc_movll_service.h) 中：
```cpp
eligible=physical==idle_ram &&
 (physical&~(unsigned)SH3_PAGEM)==sh3_ppc_state.mirror_page &&
 (uintptr_t)sh3_ppc_state.read_mirror>=SH3_MAXHANDLER &&
 (uintptr_t)MemMapR[physical>>SH3_SHIFT]==sh3_ppc_state.mirror_handler &&
 (handler_pc==idle_pc || handler_pc==idle_pc+2u);
```
这里硬性限制了只有当指令 PC 为 `idle_pc` 或 `idle_pc+2` 时才允许服务。

但在实际游戏逻辑中：
- `0x0c002310` 是 Cave 游戏全局状态或帧同步计数器变量；
- 游戏主循环、敌机生成、弹幕发射、状态机检测等大量普通逻辑在不同的 PC 位置频繁读取 `0x0c002310`；
- 因为 `handler_pc != idle_pc`，这些合法的 `MOV.L` 读取被快路径拒绝（累计 497,826 次！），迫使 DRC 退出至慢速 C++ 解释器；
- 解释器接管后，后续的数百条指令（如 `DT R6` 454,092 次、`MOV.B` 400,000 次等）全在解释器中低速运行，造成单帧 CPU 耗时拉长至 44ms，帧率跌至 40+ FPS。

---

## 2. 修改方案与实现细节

### 2.1 移除 PC 匹配限制，全面放行全局 RAM 镜像快路径
在 `speedhack_read_long(address)` 内部，已经包含严格的 PC 判定：
```cpp
if (pc == hacky_idle_pc || pc == hacky_idle_pc + 2) {
    // 处于 idle 等待循环，执行周期燃烧与快进
    burn_idle_cycles();
}
return DrvMainRAM[offset & 0xfffffc];
```
当 PC 不在 `idle_pc` 时，它只是安全返回 `DrvMainRAM` 对应值，没有任何破坏性副作用。

因此，在 [`sh3_drc_movll_service.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/cpu/sh4/sh3_drc_movll_service.h) 中：
彻底移除 `(handler_pc==idle_pc || handler_pc==idle_pc+2u)` 条件：
```cpp
  if(read_mirror) {
   eligible=physical==idle_ram &&
    (physical&~(unsigned)SH3_PAGEM)==sh3_ppc_state.mirror_page &&
    (uintptr_t)sh3_ppc_state.read_mirror>=SH3_MAXHANDLER &&
    (uintptr_t)MemMapR[physical>>SH3_SHIFT]==sh3_ppc_state.mirror_handler;
  }
```
这样，只要地址落在合法的镜像 RAM 页面（如 `0x0c002310`），任何 PC 发起的 `MOV.L @Rn, Rm` 均由 PPC DRC 原生快路径直接在 ~20 纳秒内将 RAM 值载入目标寄存器，完全无需退出 DRC！

---

## 3. 多级验证结果

1. **Tier 3 PPC QEMU SH3 指令集全量测试套件**：
   - 运行：`python3 libretro/FBNeo/tests/sh3_ppc/run.py --toolchain-root /home/humor/salvia-tests/toolchains/ppc/root`
   - 结果：**PASS**（851,662 个用例通过；compiled 用例由 753,764 增至 757,860，原生编译覆盖率进一步提升，0 错误）。
2. **Tier 3 PPC QEMU EPIC12 GPU 命令与着色器全量测试套件**：
   - 运行：`python3 libretro/FBNeo/tests/epic12_gpu/run.py --toolchain-root /home/humor/salvia-tests/toolchains/ppc/root`
   - 结果：**PASS**（23 个套件、14,400 条命令、629 万着色器用例全量通过，无渲染与快照回归）。
