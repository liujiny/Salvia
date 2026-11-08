# CV1000 优化：SH3 DRC 片上外设（0x04000000）设备读取快路径修复与动态 Handler 同步

日期：2026-11-08。基线提交：`c43aa7b2`。
对应优化方向：**方案 A：SH3 DRC 内存访问与片上外设快路径优化（消除 MMPork 瞬时 39ms cpu_io 慢帧）**。

---

## 1. 变更背景与归因分析

### 1.1 实机遥测暴露的解释器回退瓶颈
在 MMPork 实机遥测数据中（`mmpork-snap-opt-cv1000-gpu.log`），GPU Blitter 耗时已被压至 0.729ms，但残余的个别慢帧（第 733 帧及第 1803..1871 帧）中，`cpu_io` 仍偶现高达 39.8ms 的耗时峰值。
深入 SH3 工作负载统计与回退观测器（`sh3_fallback_observer.h`）分析发现：
1. **工作负载严重倾斜至 C++ 解释器**：
   `interpreter_guest_cycles = 43,000,931`，而 `native = 17,677,839`，超过 70% 的客机 CPU 周期落在慢速 C++ 解释器中。
2. **`exit_partial` 构成了退出的主要来源**：
   在单次会话中，`exit_partial` 高达 36,191 次。
3. **No.1 回退热点地址绝对集中**：
   ```
   fallback_site rank=1 pc=8C000604 opcode=6002 reason=partial class=handler delay=0 first_address=A4000000 count=1041
   drc_device_read slot=1 address=04000000 handler=0 handled=0 guest_cycles=0 fallback=1041
   drc_movll_service handled=3462 guest_cycles=23814082 rejected=16544
   ```
   每当敌机编队生成或场景状态切换时，`pc=8C000604`（`MOV.L @R0, R0`，`R0=0xA4000000`）频繁被调用以读取片上中断事件寄存器 `INTEVT2`（物理地址 `0x04000000`）。

### 1.2 根本原因归因（Root Cause）
1. **Handler ID 注册失配导致服务路径 100% 拒绝**：
   在 `sh4.cpp:965` 中，SH3 低区内部寄存器范围由框架映射为 Handler 7：
   ```cpp
   Sh3MapHandler(SH3_MAXHANDLER - 1, SH3_LOWER_REGBASE, SH3_LOWER_REGEND, MAP_READ | MAP_WRITE);
   ```
   因此，`MemMapR[0x04000000 >> 16]` 实际值为 `7`（`SH3_MAXHANDLER - 1`），对应的读取回调为 `Sh3LowerReadLong`。
   然而在 [`d_cv1k.cpp:761`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/drv/cave/d_cv1k.cpp#L761) 初始化时：
   ```cpp
   Sh3SetDrcDeviceRead(1, 0x04000000, 0); // 错误注册了 Handler 0
   ```
   在编译好的 PPC DRC 代码通过 Exit 2 返回请求服务时，[`sh3_drc_movll_service.h:34`](file:///home/humor/src/Salvia/libretro/FBNeo/src/cpu/sh4/sh3_drc_movll_service.h#L34) 执行了严格的合法性校验：
   ```cpp
   (uintptr_t)MemMapR[physical >> SH3_SHIFT] == read.handler && ReadLong[read.handler] == read.callback
   ```
   此时 `MemMapR[0x0400]` 是 7，而 `read.handler` 是 0，条件判定永久失败！
   导致 `sh3_drc_service_movll` 每次都返回 `false`（累计 `rejected=16,544`），DRC 块被迫以 `exit_partial` 退出并回退至慢速 C++ 解释器循环，引发了帧 CPU 耗时瞬时暴增至 39ms。

---

## 2. 修改方案与实现细节

### 2.1 修正 CV1000 驱动中的 Handler 索引 ([`d_cv1k.cpp`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/drv/cave/d_cv1k.cpp))
在 `DrvInit()` 中将 slot 1 的 Handler ID 修正为 7（对应 `SH3_MAXHANDLER - 1`）：
```cpp
	Sh3SetDrcDeviceRead(0, 0x18000010, 0);
	Sh3SetDrcDeviceRead(1, 0x04000000, 7);
```

### 2.2 在 DRC 注册层增加动态映射自愈与同步 ([`sh3_drc_ppc.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/cpu/sh4/sh3_drc_ppc.h))
在 `Sh3SetDrcDeviceRead` 内部，增加对传入物理地址对应 `MemMapR` 映射项的自动核对：
```cpp
void Sh3SetDrcDeviceRead(INT32 slot, UINT32 address, INT32 handler)
{
 if(slot<0 || slot>=2)return;
 Sh3DrcDeviceRead &read=sh3_device_reads[slot];
 read.address=0; read.handler=0; read.callback=NULL;
 if(handler>=0 && handler<SH3_MAXHANDLER && address<=AM && !(address&3)) {
  UINT8 *pr = MemMapR[(address&AM) >> SH3_SHIFT];
  if((uintptr_t)pr < SH3_MAXHANDLER) handler = (INT32)(uintptr_t)pr;
  read.address=address; read.handler=handler; read.callback=ReadLong[handler];
 }
 // Tagged exits depend on registration at compile time, not just execution.
 sh3_drc_reset();
}
```
当注册的设备地址已由系统或驱动映射为有效 Handler 时，自动与页映射表的实际 Handler 保持严格同步，彻底根除因硬编码 Handler 编号不一致导致的静默拒绝与解释器回退。

---

## 3. 多级验证闭环

1. **Tier 3 PPC QEMU SH3 指令集全量测试套件**：
   - 运行：`python3 libretro/FBNeo/tests/sh3_ppc/run.py --toolchain-root /home/humor/salvia-tests/toolchains/ppc/root`
   - 结果：**PASS**（全部 851,662 个用例全部通过，包括 `EDGE compiled device MOV.L / all operands / aliases / delay / callback and mapping revocation PASS cases=16396`）。
2. **Tier 3 PPC QEMU EPIC12 GPU 命令与着色器全量测试套件**：
   - 运行：`python3 libretro/FBNeo/tests/epic12_gpu/run.py --toolchain-root /home/humor/salvia-tests/toolchains/ppc/root`
   - 结果：**PASS**（全部 23 套子套件通过，覆盖全部 14,400 条命令、629 万着色器测试用例，快照缓存与 WAR 零回归）。
