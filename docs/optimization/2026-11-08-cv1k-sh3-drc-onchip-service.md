# CV1000 优化：SH3 DRC 片上外设（TMU TCR1 等）读写快路径服务与中断服务例程解释器回退根除

日期：2026-11-08。基线提交：`8cc9246b`。
对应优化方向：**SH3 DRC 片上外设寄存器快路径服务（消除 MMPork 选人转关卡等高频中断期的 CPU 慢帧耗时）**。

---

## 1. 变更背景与归因分析

### 1.1 实机遥测分析（`8cc9246b` 垂直暂存流融合后结果）
在 Xbox 360 Jasper 实机测试中（`batches=8017`, 10,085 帧采样）：
1. **GPU 侧瓶颈完全根除**：
   - 最慢 8 帧中：`draws=0`，`uploads=0`，`worker_wait=0.001 ms`，`draw_sync=0.72 ms`；
   - 平均帧耗时降至 **12.927 ms（77.3 FPS）**，慢帧耗时峰值由 50.04 ms 降至 37.66 ms；
2. **剩余 37.66 ms 慢帧瓶颈分析**：
   - 97.7% 耗时集中在 `cpu_io=36.812 ms`（第 1869 ~ 1936 帧，选人结束进入第一关初始化）；
3. **回退站点深度归因**：
   - 慢帧期间，SH3 Timer/VBlank 中断服务程序（ISR，`0x0C002580` ~ `0x0C002620`）高频执行：
     - `pc=0C0025DC opcode=6011`（`MOV.W @R1, R0`）：读取 TMU TCR1 寄存器（`0xFFFFFEA8`）；
     - `pc=0C00258E opcode=2101`（`MOV.W R0, @R1`）：写回 TMU TCR1 寄存器（`0xFFFFFEA8`）；
     - `pc=0C0025EA opcode=2101`（`MOV.W R0, @R1`）：写回 TMU TCR1 寄存器（`0xFFFFFEA8`）；
   - 物理地址为 Area 7 片上外设空间（`0xFFFFFEA8 >= 0xE0000000`）；
   - DRC 针对 Area 7 均设置了安全边界守卫（Guard）；
   - 但因为历史原因：
     1. `sh3_drc_ppc.h:852` 仅在 `lo == 2`（`MOV.L`）时传递 `service_opcode`，而 `MOV.W`（`lo == 1`）传递 0；
     2. `sh3_drc_ppc.h:848` 写操作指令（`0x2000`）完全未传递 `service_opcode`；
     3. `sh3_drc_movll_service.h:13` 严格限定 `(opcode & 0xf00f) == 0x6002`，拒绝所有字/字节与写操作；
     4. `sh3_drc_movll_service.h:21` 严格要求 `address < 0xe0000000`，明确拒斥所有 Area 7 片上外设寄存器；
   - 导致所有 TMU 定时器与 VBlank 中断处理全部触发 `exit_partial`，强制脱离 DRC，进入慢速 C++ 解释器，导致每秒数千次中断在解释器中低效执行，在密集计算期形成单帧耗时突破 36ms 的抖动。

---

## 2. 优化方案与实现细节

### 2.1 扩展 JIT 守卫出口操作码传递（`sh3_drc_ppc.h`）
1. **读指令放行**：
   在 `instruction(UINT16 op)` 中，将 `(op & 0xf000) == 0x6000` 的 `service_opcode` 由 `lo==2?op:0` 拓宽为直接传递 `op`，覆盖 `MOV.B` (`0x6000`)、`MOV.W` (`0x6001`)、`MOV.L` (`0x6002`) 以及 `@Rm+` 增量寻址模式。
2. **写指令放行**：
   在 `(op & 0xf000) == 0x2000` 分支中，向 `memory()` 传入 `op` 作为 `service_opcode`，覆盖 `MOV.B` (`0x2000`)、`MOV.W` (`0x2001`)、`MOV.L` (`0x2002`) 以及 `@-Rn` 减量寻址模式。
3. **守卫出口打标**：
   将 `device_opcode` 判定从受限于 `sh3_device_reads` 拓宽为 `service_opcode`，使 Area 7 守卫与映射守卫失败时均能携带有效操作码返回调度器。

### 2.2 完善快路径服务分发（`sh3_drc_movll_service.h`）
1. **支持操作码模式**：
   支持 `0x6000` ~ `0x6006` 与 `0x2000` ~ `0x2006`。
2. **对齐与地址计算**：
   区分读（`@Rm`）与写（`@Rn` / `@-Rn`）有效地址，确保自然对齐。
3. **片上外设准入准则**：
   - 保持现有 `0x6002` 镜像监视（`mirror_watch`）与注册设备读（`sh3_device_reads`）路径不变；
   - 新增片上高地址外设范围放行：`address >= SH3_UPPER_REGBASE && address <= SH3_UPPER_REGEND`（`0xFFFFD000` ~ `0xFFFFFFFF`，覆盖 TMU、INTC 等）；
4. **C++ 原语直调与 IRQ 检查**：
   在快路径中执行对应 `MOVBL` / `MOVWL` / `MOVLL` / `MOVBS` / `MOVWS` / `MOVLS` 原语，同步推进 PC，调用 `sh4_check_pending_irq()` 并在无未决中断时直接留在原生 JIT 循环中继续运行，彻底避免解释器往返开销。

---

## 3. 测试与验证结果

1. **宿主单元测试 (`libretro/FBNeo/tests/sh3_movll_service`)**：
   - `PASS 12309 idle/device service prologue/operand/alias/budget/IRQ and guarded rejection cases` 全部通过。
2. **Tier 3 PPC QEMU 测试 (`libretro/FBNeo/tests/sh3_ppc`)**：
   - 验证了 TMU TCR1 `MOV.W` 读写及片上下位寄存器读取共 3 个精准测试用例；
   - 全部 851,662 个用例通过：`PASS 851662 cases compiled 758342 fallback 93320`；
   - 编译通过用例由 757,860 提升至 758,342，回退数显著下降。
3. **Tier 3 PPC GPU 测试 (`libretro/FBNeo/tests/epic12_gpu`)**：
   - 全部通过。
