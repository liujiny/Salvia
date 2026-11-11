> Codex接手更新（2026-11-11）：已取回用户两份最新slot0和日志，并用回读SHA纠正了首次实际运行仍是旧版的部署差异；随后真正部署6e1fb9e2再次实机确认仍会失音。真实MPEG解码对照与现场末帧完全匹配。宿主单变量复现发现，Xbox新增整段Bus Stall CPU扣周期使下一段音乐命令晚745帧（约12.42秒）；无扣周期的解释器与DRC都能及时衔接。当前正在请用户切换本游戏Timing Model到Unthrottled (102.4MHz No Stall)验证。详见 [Codex接手取证与对照](2026-11-11-ddpdfk-bgm-codex-takeover.md)。下文关于MPEG损坏、音序无限快进和保存执行ACB_WRITE的表述属于未证实/需纠正假设，不能继续当作根因。

# Salvia / FBNeo CV1000 大复活 (DDPDFK) BGM 背景音乐中断问题排查交接文档

- **状态**：未解决，交接给 Codex 进一步排查与定位。
- **日期**：2026-11-11
- **目标平台**：Xbox 360 Jasper（192.168.2.232:21, `xboxftp:xboxftp`）
- **目标游戏**：*DoDonPachi DaiFukkatsu* (`ddpdfk`)（怒首领蜂大复活，CV1000 Type D 硬件）
- **基线提交**：`6e1fb9e278dac042fed87eb18066c8ce241fed03`
- **对应用户反馈**：
  1. 实机游玩一段后出现**“有音效、无背景音乐 (BGM)”**；
  2. 单纯暂停呼出 Salvia 菜单返回：**无音乐**；
  3. 执行保存即时存档（Slot 0）返回：**音乐立刻恢复**；
  4. 保存时右上角空闲内存指示由 11M 降至 10M，返回后升至 11M；
  5. 用户明确指出：**“这个音乐问题以前没有，不知道是哪个提交造成的”**；
  6. 最新的 `6e1fb9e2` 修复（Blitter 忙标志清理与时钟防击穿）实机验证后，用户确认：**“仍然没有解决”**。

---

## 一、 现象与边界特征深度剖析

### 1. 声音表现与解耦机理（极关键事实）
在故障发生时：
- **普通音效 (SFX)**：自机射击、保险（Bomb）、敌机爆炸、道具拾取、角色语音等音效**完全正常播放**，声音清脆无卡顿。
- **背景音乐 (BGM)**：完全静音。

> [!IMPORTANT]
> **硬件架构启示**：
> 在 Cave CV1000 的 Yamaha YMZ770C ("AMMS-A") 音频子系统中，所有 16 个声音通道（Channel 0 为 BGM，Channel 4~15 为 SFX）最终都是在 `ymz770.cpp` 中的 `ymz770_stream_update()` 被混音到同一个立体声输出流 `pBurnSoundOut`，再经由 Salvia 前端送入 SDL 音频驱动输出。  
> **SFX 能正常发声，铁证了 Xbox 360 的硬件 DAC、SDL 音频回调、Salvia 的 `g_audioBuffer` 和 `AudioTempo` 环形缓冲器本身并没有崩溃、挂死或被系统级静音。**  
> 问题被严格锁定在 **YMZ770 内部 BGM 音轨（Channel 0 / Sequencer 0）的生成过程**，或者上层 **SH-3 游戏驱动的音序触发时机** 上。

### 2. 为什么单纯暂停无用，而保存进度能恢复？
- **单纯暂停（Pause Menu）**：
  - 玩家按下 Back 键进入 Salvia 菜单；
  - 前端只是暂停调用游戏主循环，但**不改变音频底层状态**，不触发 `StateAudioPause`，也不清理缓冲区。
  - 返回后，依然处于静音状态。
- **保存即时存档（Save State）**：
  - 调用 `src/io/statesram.h` 中的 `saveState()`；
  - 构造了 `StateAudioPause audioPause`：显式执行 `SDL_PauseAudio(1)`，并在析构时调用 `SDL_LockAudio(); gameMenu->g_audioBuffer.Clear(); SDL_UnlockAudio(); SDL_PauseAudio(0);`；
  - 调用 `BurnDrvScan(ACB_WRITE)` 对所有模块进行序列化扫描（包括 `epic12_scan` 和 `ymz770_scan`）；
  - `saveState()` 涉及 USB 写盘与 zlib 压缩，阻塞耗时数百毫秒；
  - **返回游戏瞬间，BGM 立即恢复播放**。
- **内存 10M -> 11M 的真相**：
  - 在 `src/salvia.cpp:1340` 的 `take_screenshot()` 中，保存即时存档前会临时 `new uint8_t[1280 * 720 * 4]`（约 3.7MB）用于生成存档缩略图；
  - 此时空闲内存减少，右上角 HUD 显示从 11M 降至 10M；
  - 存档写盘完成后，调用 `clearStateScreenshot()` 释放该 3.7MB 内存，显示恢复为 11M。
  - **结论：内存数值变化纯属正常的缩略图生命周期，不是导致音乐中断的内存泄漏。**

---

## 二、 历史排查记录与已证伪假说

在当前的排查中，先后验证并证伪了三个主要假说：

### 1. 假说一：YMZ770 序列器非循环 0x0F 斩断通道（已证伪）
- **提交**：`617ed654`（`fix(ymz770): preserve active channels across sequencer loop boundaries`）
- **推测**：DDPDFK 第一关 BGM（Sequence 22）时长 117.638 秒，末尾包含 `0x0F`（EOS）。FBNeo 官方代码无条件执行 `m_channels[ch].is_playing = false`，误以为是在循环边界杀死了通道。
- **改动**：将通道静音限制在 `else`（非循环）分支。
- **实测结果**：**失败**。游戏内依然会出现音乐中断。

### 2. 假说二：非循环 EOS 未重置 `sequence.delay`（已证伪）
- **提交**：`527b985b`（`fix(cv1k): reset ymz770 sequence delay on non-looping eos`）
- **推测**：非循环序列结束残留 `delay = -1`，导致后续重新触发音序时产生意外等待。
- **改动**：在非循环 EOS 分支显式置 `sequence.delay = 0`。
- **实测结果**：**失败**。游戏内依然会出现音乐中断。

### 3. 假说三：VBlank 硬件 Blitter 忙标志锁死主循环转场锁（已证伪）
- **提交**：`6e1fb9e2`（`fix(cv1k): clear epic12 blitter busy on frame boundaries and async completion`）
- **推测**：通过反编译玩家在无音乐现场保存的 `ddpdfk-slot0.raw`，发现 SH-3 PC 处于 `0x0c1d1344` 等待循环，返回地址在关卡转场主循环 `while (*(volatile uint32_t*)(0x0c3dc58c) != 0) wait_vblank(1);`。而 `*(0x0c3dc58c)` 必须由 VBlank 例程 `0x0c1d14e0` 读取 `0xb8000010` Bit 4（Blitter 空闲）后置零。推测是 `m_blitter_busy` 跨帧泄漏导致判定失败。
- **改动**：在工作线程结束、帧边界强制清理 `m_blitter_busy`，接入非阻塞轮询，并防止 `rem` 周期烧尽。
- **实测结果**：**失败**，用户实机测试后反馈“仍然没有解决”。
- **深度反思**：`ddpdfk-slot0.raw` 的快照点碰巧在转场等待阶段，诱导了排查方向。但在正常关卡中游玩时，关卡转场早已结束，游戏处于战斗中，主循环正常在计算子弹碰撞，此时发生的音乐中断显然与转场锁 `0x0c3dc58c` 无关。

---

## 三、 用户指出的“以前没有”——近期提交全景与嫌疑排查

用户明确表示“以前没有这个问题”，说明必然是最近几天的某次改动引入的 Regression。

### 关键时间线与高嫌疑提交列表（11月4日 ~ 11月8日）

| Commit | 日期 | 标题 / 变动内容 | 嫌疑度 | 潜在隐患分析 |
| --- | --- | --- | :---: | --- |
| `638f80dc` | 11-04 | `audio & drc: accelerated YMZ770 MPEG IDCT32 and hoisted idle RAM speedhack guard` | **极高** | **直接修改了 YMZ770 音频解码器**（重构了 IDCT32 矩阵计算与缓冲推进），且移动了 Speedhack 守卫！ |
| `26adabb1` | 11-08 | `fix(cv1k): widen idle loop pc range in speedhack to match delayed branch target` | **极高** | 将 Speedhack 判定放宽到 `+10`，导致 DDPDFK 的 `0x0c1d1346` 每次等待都大额燃烧周期，破坏了音频流的时钟步进！ |
| `779a9a16` | 11-04 | `cv1k: blitter busy spin loop fast-forwarding and branchless staging blend` | **高** | 引入 `Sh3GetRemainingCycles()`，改变了 SH3 的时间片推进节奏。 |
| `8d571ced` | 11-06 | `feat: implement ibara360 architecture mechanisms for CV1000` | **高** | 引入总线等待 `Sh3BurnCycles(cycles)` 与 78% 降频逻辑。 |
| `8cd2d004` | 11-07 | `fix(cv1k,drc): resolve ibara double throttling and ddpsdoj memory/freeze` | 中 | 恢复基准 100% 频率，调整 DRC 预编译大小。 |
| `574c78f6` | 11-08 | `feat(sh3,drc): service on-chip peripheral reads and writes in drc fastpath` | 中 | DRC 快路径直接拦截 TMU / RTC，可能影响基于定时器中断的声音驱动。 |
| `802dc2ad` / `fdfda1a1` | 11-08 | `smode 6 gpu acceleration and expand/revert drc arena` | 低 | 纯 GPU 混合与 DRC 内存池容量，不涉及音频与 CPU 逻辑。 |

---

## 四、 留给 Codex 的高优先级深挖方向与诊断建议

### 1. 重点深挖方向 A：YMZ770 MPEG 解码器解码失败导致通道永久休眠 (`is_playing = false`)
在 [`libretro/FBNeo/src/burn/snd/ymz770.cpp`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/snd/ymz770.cpp#L355-L368)：
```cpp
if (channel.is_playing)
{
    // next block
    int sample_rate, channel_count;
    if (!mpeg_decoder[ch]->decode_buffer(channel.pptr, m_rom_size*8, channel.output_data, channel.output_remaining, sample_rate, channel_count) || channel.output_remaining == 0)
    {
        channel.is_playing = !channel.last_block; // detect infinite retry loop
        channel.last_block = true;
        channel.output_remaining = 0;
        goto retry;
    }

    channel.last_block = channel.output_remaining < 1152;
    channel.output_ptr = 0;
}
```
并且在 `retry:` 处（第 332 行）：
```cpp
retry:
    if (channel.last_block)
    {
        if (channel.loop)
        {
            ... // 重新装载短语指针
        }
        else
        {
            channel.is_playing = false;  // <--- 致命点！
            channel.output_remaining = 0;
            mpeg_decoder[ch]->clear();
        }
    }
```
**分析**：
- BGM 的单次播放短语其 `channel.loop` 往往为 0（硬件循环由外部序列器 Sequencer 0 负责调度，通道本身不配置单个短语循环）。
- 如果在游玩过程中，MPEG 解码器遇到数据断流、解包不同步或 `decode_buffer` 偶发返回 false / `output_remaining == 0`，代码会进入 `goto retry;`。
- 此时 `channel.last_block` 为 true 且 `channel.loop` 为 0，**`channel.is_playing` 直接被置为 false 并执行 `mpeg_decoder[ch]->clear()`**！
- 一旦通道被置为 false，BGM 通道就彻底哑巴了，直到下次外部重新发送 Key-On。

### 2. 重点深挖方向 B：`Sh3BurnCycles` 与 `stream.update()` 导致的时钟撕裂与快进
在 [`libretro/FBNeo/src/burn/snd/stream.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/snd/stream.h#L205-L230)：
```cpp
INT32 SyncInternal(INT32 cycles)
{
    if (!buffered) return 0;
    return (INT32)((double)cycles * ((double)pTotalCyclesCB() / ((double)nCpuMHZ / (nBurnFPS / 100.0000))));
}
```
- `stream.update()` 依赖 `Sh3TotalCycles()` 计算当前音频流应该步进到的样本位置。
- 如果在某一个切片中，由于 Speedhack（`26adabb1`）或 Blitter Bus Contention（`8d571ced`）调用了 `Sh3BurnCycles`，`Sh3TotalCycles()` 会在一瞬间跳跃数万到数十万周期。
- 此时若游戏发出任意一次音效 MMIO 写入，会触发 `stream.update()`。
- `SyncInternal` 认为客机时钟已经走过了大量时间，**迫使 `ymz770_stream_update` 在一次宿主调用中连续执行上千次 `ymz770_sequencer()`**！
- 这种由于周期骤烧引起的“音序器瞬间快进”，极容易造成：
  1. 序列延时被瞬间倒计时耗尽；
  2. 短语解码超前于 SH-3 游戏逻辑的实时状态；
  3. 解码指针 `channel.pptr` 超越了数据边界，导致 `decode_buffer` 报错退出。

### 3. 重点深挖方向 C：为什么 `saveState()` 之后音乐会恢复？
请 Codex 仔细追踪执行 `saveState()` 时到底做了什么动作，使得 Channel 0 能够重新起死回生：
1. `StateAudioPause` 进行了 `gameMenu->g_audioBuffer.Clear()` 并重置了 SDL 音频设备状态；
2. `DrvScan(ACB_WRITE)` 扫描了 `mpeg_decoder[i]->scan()`；
3. 检查 `mpeg_audio::scan()`（位于 [`mpeg_audio.cpp`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/snd/mpeg_audio.cpp#L45-L75)）：
   ```cpp
   if (!s_cos_transposed_inited) {
       for (int j = 0; j < 32; j++) {
           for (int i = 0; i < 32; i++) {
               s_cos_transposed[j][i] = m_cos_cache[i][j];
           }
       }
       s_cos_transposed_inited = true;
   }
   ```
   是否 `s_cos_transposed` 或其它静态余弦缓冲在正常运行中被踩内存，而在 `scan` 时被重新刷新？
4. `saveState()` 导致的主循环数十帧暂停，是否让 SH-3 的某些中断标志或看门狗状态得到了恢复？

### 4. 最直接有效的方法：Git Bisect（二分查找）
因为用户明确反馈“以前没有这个问题”，最省时、最权威的定位手段是使用 Git Bisect：
- **Bad Commit**：`527b985b`（当前用户遇到问题的版本）
- **Good Commit**：推荐回退到 11月3日之前的基线，例如 `df5fc83e` 或 `b0fbba54` 之前。
- 编译并推送至 Xbox 360 测试，只要确认某一个老版本即使玩很长时间 BGM 也绝不断流，即可在 3~4 次二分内精准抓出罪魁祸首提交。

---

## 五、 工具链与实机验证操作指南

1. **工作区说明**：
   - 当前 Git 仓库：`/home/humor/src/Salvia`
   - Windows 运行时构建目录：`/mnt/e/Baiduyundownload/salvia-toolchain/.work/Salvia-main`
   - 修改代码后，必须同步镜像至运行时目录，或通过 `build-xex.sh` 脚本自动同步。
2. **快速编译 `fbneo.xex`**：
   ```bash
   /home/humor/salvia-tests/xex-build/build-xex.sh --tag <test-tag> --flavor diag --fbneo-only
   ```
   - 产物将自动部署至 `/home/humor/src/Salvia/Distro360/fbneo.xex` 并归档在 `/home/humor/salvia-tests/xex-archive/`。
3. **实机部署上传**：
   ```bash
   python3 /home/humor/salvia-tests/upload_to_xbox.py /home/humor/src/Salvia/Distro360/fbneo.xex
   ```
   或启动后台守护脚本自动监听 Aurora：
   ```bash
   python3 /home/humor/salvia-tests/upload_fbneo_daemon.py
   ```
4. **现有现场存档**：
   - 存档文件：`/tmp/ddpdfk-slot0.raw`（解压后的无音乐故障现场内存及寄存器 dump）。
