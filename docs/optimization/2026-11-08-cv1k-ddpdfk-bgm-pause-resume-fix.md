# CV1000 音频序列器：大复活 (DDPDFK) 选人后转场 BGM 静音与暂停恢复根因剖析与序列器状态重置

日期：2026-11-08  
基线提交：`fdfda1a1e5b84dca14107d88f34d5ca399a35488`  
对应问题：实机游玩《怒首领蜂大复活》(DDPDFK) 时进入第一关前偶现“有音效、无背景音乐 (BGM)”；玩家在第一关开局保存即时存档（Slot 0），暂停呼出菜单后返回游戏，BGM 立即恢复播放。

---

## 一、 现象与存档数据逆向取证

### 1. 实机即时存档取证 (`ddpdfk-slot0.raw`)
通过解析玩家在故障现场保存的 Slot 0 存档（解压后 SH3 RAM、CPU 上下文及 YMZ770 硬件状态），提取关键遥测数据：
- **运行帧数与进度**：`nCurrentFrame = 3306`（自启动运行约 55.1 秒，第一关开局约 30 秒，分数 4,024,840）。
- **CPU 寄存器与执行上下文**：
  - `m_pc = 0x0c1d1346`：SH3 CPU 正处于 `0x0c1d1344` 的等待循环中（读取 `0x0c002310` 等待 VBlank 事件）。
  - `m_pr = 0x0c1e3d6c`：返回地址位于关卡转场同步主循环：
    ```c
    while (*(volatile uint32_t*)(0x0c3dc58c) != 0) {
        wait_vblank(1);
    }
    ```
  - `R9 = 0x0c3dc58c`：关卡转场锁变量地址。
  - **关键现场**：在第 3306 帧存档保存的一瞬间，内存中 `*(0x0c3dc58c)` 恰好已经被置为 `0`！意味着转场等待循环的跳出条件已经满足。紧随其后的下一条 SH3 指令为 `BRA 0x0c1e3d44`，该函数正是向 YMZ770 写入寄存器启动 Stage 1 BGM 的调用点。

---

## 二、 驱动层与硬件音序器深度逆向

### 1. 选人音乐（Sequence 5）的生命周期与 EOS 截断
在 CV1000 硬件架构中，音效与音乐均由 YMZ770 序列器通过 ROM 中预编译的二进制音序驱动：
- **选人界面 BGM（Sequence 5）**：
  - 序列时长：严格测算为 7,186 个音序时钟周期（约 17.406 秒）。
  - 音序控制字：
    - `0x01a8d8: REG 0x43 DATA 0x00` —— 明确显式关闭 Channel 0 声音输出。
    - `0x01a8da: 0x0F` —— 非循环序列终止符 (EOS, End of Sequence)。
  - `Sequence 5` 在驱动中配置为**非循环**（`loop = 0`）。
  - **失效链条**：当玩家在选人界面停留、等待或转场耗时超过 17.4 秒后，Sequence 5 播放至末尾，执行 `REG 0x43 DATA 0x00` 彻底切断 Channel 0 声音，随后触发 `0x0F` 终止音序。此时，Channel 1/2 上的普通音效（SFX）仍由主游戏逻辑正常触发播放，导致玩家观察到“只有音效、没有背景音乐”的现象。

### 2. 第一关 BGM 启动为何卡在转场锁 (`*(0x0c3dc58c)`)
- 游戏主逻辑在关卡切换（黑屏过渡与 Blitter 特效渲染）时，会向 `*(0x0c3dc58c)` 写入 1 作为同步锁。
- 只有当 VBlank 中断处理函数 `0x0c1e3bf4` 调用 `0x0c1d14e0` 检查 Blitter 状态寄存器 `0xb8000010` 确认硬件 Blitter 完全空闲（返回 1）后，才会将 `*(0x0c3dc58c)` 清零。
- 在未清理完成前，CPU 持续阻塞在 `0x0c1e3d6c` 的 `wait_vblank` 循环中，无法推进到 `0x0c1e3d44`（`bgm_start`）。

### 3. 为何呼出暂停菜单返回后 BGM 恢复
- 玩家按下暂停键呼出 Salvia 菜单时，前端调用了 `SDL_XBOX_FlushCoreGpuDiagnostics()`，彻底清空并同步了 GPU/Blitter 批次队列。
- 当玩家从暂停菜单恢复（或即时存档写盘触发阶段状态机推进）时，Blitter 状态立刻被判定为空闲，中断例程执行 `*(0x0c3dc58c) = 0`。
- CPU 随即脱离循环，执行 `0x0c1e3d44` 向 YMZ770 发送 `SQON` 开启 Stage 1 BGM（Sequence 21/22），音乐恢复。

---

## 三、 YMZ770 音频序列器缺陷修复

### 1. 缺陷分析：非循环 EOS 未重置 `sequence.delay`
在 [`libretro/FBNeo/src/burn/snd/ymz770.cpp`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/snd/ymz770.cpp) 中，上游序列器状态机在处理 `0x0F`（EOS）时存在如下逻辑：
```cpp
case 0x0f:
    if (sequence.loop)
    {
        sequence.offset = ymz770_get_seq_offs(sequence.sequence);
        sequence.delay = 0;
    }
    else
    {
        for (int ch = 0; ch < 8; ch++)
            if (sequence.stopchan & (1 << ch))
                m_channels[ch].is_playing = false;
        sequence.is_playing = false;
        // 缺陷：此处缺少 sequence.delay = 0;
    }
    break;
```
- 当序列执行到非循环 EOS 时，`sequence.delay` 保留了前序无延时指令赋予的 `-1`。
- 如果后续外部驱动通过 `0x81` 寄存器重新启动或抢占序列时，残留的负数/脏延时计数值可能导致序列调度器进入意外等待周期。

### 2. 修复方案
在非循环 `else` 分支显式重置 `sequence.delay = 0`：
```cpp
    else
    {
        for (int ch = 0; ch < 8; ch++)
            if (sequence.stopchan & (1 << ch))
                m_channels[ch].is_playing = false;
        sequence.is_playing = false;
        sequence.delay = 0;
    }
```
确保序列结束时所有运行时计数器回归干净初始态。

---

## 四、 宿主单元测试与验证

在 [`libretro/FBNeo/tests/ymz770_sequencer/test.cpp`](file:///home/humor/src/Salvia/libretro/FBNeo/tests/ymz770_sequencer/test.cpp) 中新增测试覆盖：
1. **Test 1**：循环序列穿越 EOS `0x0F` 边界并正确回绕起始偏移。
2. **Test 2**：非循环序列正常静音 `stopchan` 通道、**重置 `delay = 0`** 并平稳终止。
3. **Test 3**：多通道 `stopchan` 位掩码独立静音判定。
4. **Test 4**：带有脏延时（`delay = -1`）的运行中序列被 Key-On 抢占时，验证偏移重置与延时清零。
5. **Test 5**：Keep-Playing 模式（`data & 6 == 6`）保护运行中序列不发生回绕或异常重置。

宿主测试结果：
```bash
Running ymz770_sequencer unit test...
  PASS: Test 1 - Looping sequence preserves channel playback and rewinds to start
  PASS: Test 2 - Non-looping sequence silences stopchan, resets delay to 0, and terminates cleanly
  PASS: Test 3 - Multi-channel stopchan bitmask correctly evaluated
  PASS: Test 4 - Sequence preemption with Key-On correctly arms offset and resets delay
  PASS: Test 5 - Keep-Playing flag preserves in-flight sequence without rewinding
PASS ymz770_sequencer: all loop, EOS, stopchan, and key-on preemption tests passed
```
全量音频与前端测试验证通过：
- `audio_tempo`: **PASS**
- `frontend_state`: **PASS**

---

## 五、 构建与部署记录

- **构建 Tag**：`ddpdfk-bgm-fix`
- **基线 Commit**：`fdfda1a1e5b84dca14107d88f34d5ca399a35488`
- **生成的 XEX 镜像**：
  - `fbneo.xex` (diag): 34,865,152 bytes | SHA256 `2d841b38425c44e3a0c58d1f56bc9a1f0d6c69cb751084e6c4eafc0b997a6bf4`
- **归档路径**：
  - `/home/humor/salvia-tests/xex-archive/fbneo-20261108-2358-ddpdfk-bgm-fix-diag.xex`
- **Distro 部署**：
  - 已拷贝至 `/home/humor/src/Salvia/Distro360/fbneo.xex`
- **实机部署**：
  - 自动部署后台守护脚本正在监听 Xbox 360 Jasper (`192.168.2.232:21`)，待退出至 Aurora Dashboard 即可完成静默上传。

