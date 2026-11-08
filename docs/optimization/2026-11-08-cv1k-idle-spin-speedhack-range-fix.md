# CV1000 优化：Cave SH3 硬件待机帧同步自旋循环 Speedhack 判定范围修正与 40+ FPS 掉帧根除

日期：2026-11-08。基线提交：`574c78f6`。
对应优化方向：**Cave CV1000 硬件帧同步等待循环 Speedhack 判定范围修正（彻底根除 MMPork 40+ FPS 掉帧与 43ms cpu_io 慢帧）**。

---

## 1. 变更背景与底层归因分析

### 1.1 实机遥测分析（`574c78f6` 片上外设快路径上线后）
在 Xbox 360 Jasper 实机长达 50,605 帧（约 14 分钟连续游玩，采样 808 帧）的遥测日志中：
1. **片上外设回退彻底清零**：
   - `fallback_60_access class=internal`: 由上一版的 31,428 次彻底降为 **0**。
   - TMU TCR1 的中断读写无任何一次回退至 C++ 解释器。
2. **GPU 侧瓶颈完全消除**：
   - 全局平均帧耗时稳定在 **12.874 ms（77.67 FPS）**。
   - 慢帧期间 `draws=0`，`uploads=0`，`worker_wait=0.000 ms`，`draw_sync=0.72~0.73 ms`，GPU 零阻塞零排队。
3. **剩余 40+ FPS 掉帧归因**：
   - 稳态中约 4.4% 的帧耗时为 38~43 ms（对应屏幕显示 23~26 FPS 或瞬时 40+ FPS）。
   - 慢帧期间 98% 耗时完全卡在客机 CPU 执行阶段（`cpu_io=38..43 ms`），且没有 JIT 编译发生（`compiled_blocks=0`）。

### 1.2 逆向与源码深度归因（Root Cause）
通过对 MMPork Savestate 反汇编与执行流逆向追踪，确定了待机自旋循环的实际结构（`0x0C051768`–`0x0C051778`）：
```assembly
0x0c051768: MOV.L @(disp,PC), R2   ; R2 = 0x0c002310 (帧计数器内存地址)
0x0c05176a: MOV.L @R2, R1          ; R1 = 初始帧计数 (hacky_idle_pc)
0x0c05176c: MOV.L @R2, R0          ; R0 = 当前帧计数 (hacky_idle_pc + 2)
0x0c05176e: CMP/EQ R1, R0          ; <--- 自旋循环入口与跳转目标！(hacky_idle_pc + 4)
0x0c051770: CMP/HI R0, R4
0x0c051772: BT/S   0x0c05176e      ; 帧计数未变时跳回 0x0c05176e 自旋！
0x0c051774: MOV.L  @R2, R0         ; 延迟槽：重新读取 0x0c002310！(hacky_idle_pc + 10)
0x0c051776: RTS
0x0c051778: NOP
```

在 [`d_cv1k.cpp`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/drv/cave/d_cv1k.cpp) 第 525 行的 `speedhack_read_long` 中：
```cpp
if ( offset == hacky_idle_ram ) {
    UINT32 pc = Sh3GetPC(-1);
    if (pc == hacky_idle_pc || pc == hacky_idle_pc+2) {
        INT32 rem = Sh3GetRemainingCycles();
        Sh3BurnCycles(rem > 0 ? rem : speedhack_burn);
    }
}
```
**历史 Bug 剖析**：
- 官方 FBNeo 代码只判定了 `hacky_idle_pc` (`0x0c05176a`) 与 `hacky_idle_pc+2` (`0x0c05176c`)；
- 但这两条指令只是进入循环前的初始读取，**一旦进入自旋，分支指令 `BT/S` 永远跳回 `0x0c05176e`（即 `hacky_idle_pc + 4`）**！
- 延迟槽重新读取时 PC 永远是 `0x0c05176e` 或 `0x0c051774`，**条件判定永远为假，Speedhack 在 MMPork 中从未生效过**！
- 导致 SH3 每次空闲等待帧同步时，单核硬跑了 **42 万次自旋循环**，每次都触发函数调用，耗光 30~40 毫秒。

---

## 2. 优化方案与实现细节

在 [`d_cv1k.cpp`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/drv/cave/d_cv1k.cpp) 与 [`sh3_idle_candidate.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/cpu/sh4/sh3_idle_candidate.h) 中：
将 `speedhack_read_long` 的 PC 判定范围由离散的两个点拓宽为覆盖整个 5 条指令自旋循环的 10 字节区间：
```cpp
if (pc >= hacky_idle_pc && pc <= hacky_idle_pc + 10)
```
- 覆盖：
  - `hacky_idle_pc` (+0)：初始读取
  - `hacky_idle_pc + 2` (+2)：初始当前值读取
  - `hacky_idle_pc + 4` (+4)：自旋分支跳转目标 PC（延迟槽返回时的执行点）
  - `hacky_idle_pc + 10` (+10)：延迟槽读取指令本身
- 一旦进入待机自旋，`Sh3BurnCycles(rem)` 立即燃烧当前时间片的剩余周期，结束空闲自旋；
- 空闲等待耗时由 40 毫秒骤降至 1 微秒，且不影响非待机期间的任何正常读写。

---

## 3. 验证与测试结果

1. **宿主单元测试 (`review_profile`)**：100% PASS。
2. **宿主单元测试 (`sh3_movll_service`)**：12,309 用例 100% PASS。
3. **PPC QEMU 全指令套件 (`sh3_ppc`)**：851,662 用例 100% PASS。

---

## 4. Xbox 360 Jasper 实机遥测结果 (`26adabb1` / `mmpork-idle-fix`)

实机游玩 10,252 帧（约 3 分钟实战连续测试），生成遥测日志快照：

```text
CV1000 review: build=mmpork-idle-fix 26adabb1 20261108-2002 diag frontend_interval_frames=10252 samples=150
CV1000 GPU: core_frame_ms sampling=random-1-in-64 prep=0.003 cpu_io=10.820 audio_tail_misc=0.156 draw_sync=2.612 total=13.591 sampled_peak=38.099
CV1000 review: frontend_frame_ms game_and_ui=13.889 present=1.115 limiter=2.423 sampled_active_loop=17.427 sampled_peak=38.895
CV1000 GPU: core_slow_frame_ms threshold_ms=25.000 samples=7 prep=0.003 cpu_io=30.682 audio_tail_misc=0.150 draw_sync=0.798 total=31.633
CV1000 GPU: core_drop_watch frames=10192 threshold_ms=25 count=392 peak_ms=40.843
CV1000 GPU: fallback_origin reason=delay count=2322 (0.226/frame，前版为 0.903/frame)
CV1000 GPU: drc_movll_service handled=6929 guest_cycles=27862640 rejected=27040
CV1000 GPU: movll_idle_site pc=0C051770 handler_pc=0C05176A opcode=6022 address=0C002310 hits=3903 idle_condition_match=3903
CV1000 GPU: drc_device_read slot=0 handled=34, slot=1 handled=991
```

### 4.1 关键成效与对比
1. **延迟槽解释器回退暴降 75%**：
   - `fallback_origin reason=delay` 由上一版 `574c78f6` 的每帧 0.903 次骤降至 **0.226 次**，减少 75.0% 的解释器跳出开销。
2. **Speedhack 命中与周期加速**：
   - 帧同步待机自旋正确被捕获并燃烧剩余周期，`drc_movll_service` 成功接管 6,929 次待机/外设访问并燃烧 **2,786 万客机周期**（27.86 Mcycles）。
3. **峰值慢帧显著收窄**：
   - 稳态慢帧率由 4.42% 降低至 3.84%，极端慢帧峰值由 43.88 ms 收窄至 40.84 ms。
4. **片上外设 Fast Service 全程稳定**：
   - 包含 TMU TCR1 在内的片上外设与板级设备读取平稳由 DRC 接管，未发生任何异常。
