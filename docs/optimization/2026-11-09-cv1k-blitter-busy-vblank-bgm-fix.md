# CV1000 硬件 Blitter 忙状态与 VBlank 转场锁解挂：彻底修复大复活 (DDPDFK) 关卡/转场背景音乐 (BGM) 中断

日期：2026-11-09  
基线提交：`527b985bdb3966b8546308064d371000297746bc`  
对应问题：实机游玩《怒首领蜂大复活》(DDPDFK) 时，背景音乐 (BGM) 运行一段时间后出现中断（仅剩普通音效 SFX，无背景音乐）；单纯暂停并返回游戏无法恢复，只有执行保存进度（Save State）返回后音乐才立即恢复。保存时右上角内存显示从 11M 降至 10M，返回后回升至 11M。

---

## 一、 根本原因全链路定位与机理剖析

### 1. 现象取证与数据还原
- **存档现场取证 (`ddpdfk-slot0.raw`)**：
  - SH-3 程序计数器处于 `0x0c1d1344` 等待循环，返回地址 `0x0c1e3d6c`，持续循环等待 `*(0x0c3dc58c) == 0`。
  - 关卡调度例程必须跳出该循环，才能执行 `0x0c1dc8b4`（事件与音序调度器）向 YMZ770 发送 Key-On 开启 BGM 音序。
- **VBlank 中断例程与硬件 Blitter 检查 (`0x0c1d14e0`)**：
  ```asm
  0x0c1d14e0: MOV.L   @(0x10, R1), R0    ; 读取 0xb8000010 (epic12_blitter_read(0x10))
  0x0c1d14e2: TST     #0x10, R0          ; 检查 Bit 4 (Blitter 是否空闲)
  0x0c1d14e4: BF      0x0c1d14f0         ; Bit 4 为 1 (空闲) -> 进入正常 VBlank 渲染提交并置 *(0x0c3dc58c) = 0
  0x0c1d14e6: RTS                        ; Bit 4 为 0 (忙) -> 立即中止返回 0，保留 *(0x0c3dc58c) = 1
  0x0c1d14e8: MOV     #0, R0
  ```
  在故障状态下，`0xb8000010` 持续返回 `0x00000000`（Bit 4 为 0），导致 VBlank 中断例程判定 Blitter 忙而丢弃转场提交，CPU 被死锁在 `wait_vblank(1)` 中。

### 2. 为什么 Bit 4 为 0：四重饥饿与跨帧泄漏 Bug
1. **帧同步边界未清空 `m_blitter_busy`**：
   在帧末尾，`d_cv1k.cpp` 调用 `epic12_wait_blitterthread()` 和 `epic12_draw_screen()`，通过 `thready.notify_wait()` 等待渲染工作线程与 Xenos GPU 彻底完成绘制。此时硬件实际已经 100% 空闲，但此前没有任何代码将 `m_blitter_busy` 清零，依然完全依赖 `cave_blitter_delay` 计时器回调。
2. **总线等待 (`Sh3BurnCycles`) 与计时器推进的饿死效应**：
   `gfx_exec_write` 在触发 Blitter 时设置了 `sh4_set_cave_blitter_delay_timer(cycles)`，并在 Xbox 上执行了 `Sh3BurnCycles(cycles)`。CPU 周期被一次性大额扣除后，导致随后切片（Slice）目标周期为负，每个切片仅执行 1 周期，`sh4_run_timers` 无法正常推进 `cave_blitter_delay`。
3. **`epic12_blitter_read(0x10)` 吞噬切片余额**：
   历史代码在 Blitter 忙时执行 `Sh3BurnCycles(rem > 0 ? rem : m_burn_cycles)`，直接烧掉了当前切片的全部剩余预算 `rem`，导致一旦发生忙状态，整个切片直接被扼杀，雪上加霜。
4. **为何保存进度能恢复**：
   保存进度耗时数百毫秒，期间调用 `epic12_scan`（内部强行触发 `thready.notify_wait()`）、`StateAudioPause` 并重置了切片时钟，打破了周期负债死锁，使随后的 VBlank 例程得以读到 Bit 4 = 1，成功清除 `0x0c3dc58c`，进而调度 BGM 启动。
5. **内存指示从 10M 到 11M**：
   前端在发起保存时分配了 3.7MB 的截图缓冲区生成预览图（内存占用增加，显示 10M），保存完成后通过 `clearStateScreenshot()` 释放（内存回升至 11M），属于正常截图生命周期。

---

## 二、 修复方案

1. **工作线程状态即时探测 (`poll_complete` / `is_busy`)**：
   - 在 `BurnRenderWorker` 中增加非阻塞完成事件探测 `poll_complete()`（Windows: `WaitForSingleObject(complete, 0) == WAIT_OBJECT_0`；Pthreads: 检查 `done` 状态）。
   - 在 `epic12_thread` 中暴露 `is_busy()`，一旦工作线程绘制完成，立即重置 `pending = false` 并返回非忙。
2. **读操作即时解除忙状态**：
   - 在 `epic12_blitter_read(0x10)` 中，若 `m_blitter_busy` 为 1 但 `!thready.is_busy()`，立即解除忙状态并停止计时器，返回 `0x00000010`（空闲）。
   - 在忙状态下，将周期扣除恢复为温和的 `m_burn_cycles`（~1000 周期 / 10μs），严禁销毁整个切片预算 `rem`。
3. **帧边界与扫描强制归位**：
   - 在 `epic12_wait_blitterthread()`、`epic12_draw_screen()`、`epic12_scan()`、`epic12_reset()` 中，在 `thready.notify_wait()` 之后显式设置 `m_blitter_busy = 0;` 并调用 `sh4_clear_cave_blitter_delay_timer();`，确保帧边界与 VBlank 入口处 Blitter 绝对处于空闲态。
4. **零延时 Blit 防护**：
   - 在 `gfx_exec_write` 中，当计算所得 `cycles <= 0` 时，显式清零 `m_blitter_busy` 与计时器，防止零开销指令误置忙状态。

---

## 三、 验证结果

- **宿主单元测试 (`tests/epic12_blitter_busy/run.py`)**：
  - Test 1: 初始空闲读取返回 0x10。
  - Test 2: 渲染中读取返回 0x00 并消耗温和的 `m_burn_cycles`。
  - Test 3: 工作线程完成后读取自动恢复 0x10 并停止计时器。
  - Test 4: 零延时指令维持空闲。
  - Test 5 & 6: 帧同步边界强制清空忙状态与计时器，保证 VBlank 转场例程读取 0x10 成功。
  - **全部测试通过 (PASS)**。
- **全量回归测试**：
  - `ymz770_sequencer`: **PASS**
  - `epic12_staging_columns`: **PASS**
  - `audio_tempo`: **PASS**
  - `frontend_state`: **PASS**
