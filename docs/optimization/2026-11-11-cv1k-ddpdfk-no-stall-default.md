# CV1000: DDPDFK 默认内核选项切换为 Unthrottled (102.4MHz No Stall)

日期：2026-11-11
基线 Commit：`9429c0e0026b0b13fd6a6fa1514540788faed721`
变更文件：`libretro/FBNeo/src/burn/drv/cave/d_cv1k.cpp`

## 1. 背景与变更原因

在之前的复盘与现场对照中（详见 [2026-11-11-ddpdfk-bgm-codex-takeover.md](2026-11-11-ddpdfk-bgm-codex-takeover.md)）：
- 提交 `8d571ced` 引入了 CV1000 的 Bus Stall 模型：在 `epic12.cpp` 的 `gfx_exec_write` 中调用 `Sh3BurnCycles(cycles)`，模拟 Blitter 占用 SDRAM 期间 SH-3 CPU 的等待周期。
- 在《怒首领蜂大复活》（`ddpdfk`、`ddpdfk10`、`dfkbl`）中，密集绘图期间 Bus Stall 导致 CPU 被频繁扣减大量周期。
- YMZ770 音频流依据输出音频采样推进，而负责在 Sequence 5 自然终结（`43 00 0F 00` 停止通道 0 并 EOS）后下发下一曲 Key-On 命令（Sequence 6 / Sequence 22）的 SH-3 游戏逻辑因此被严重推迟了 745 帧（约 12.42 秒）。在此期间，BGM 停歇只剩音效。
- 用户在实机（Xbox 360 Jasper）上将内核 DIP 选项切换至：
  `Timing Model (ibara360)` → `Unthrottled (102.4MHz No Stall)`
  （即 `DrvDips[1] |= 0x08`），跳过 `Sh3BurnCycles(cycles)`，实机测试反馈“音乐持续正常”。

为了使玩家无需手动在菜单中单独修改并保存该游戏选项，将 `ddpdfk`、`ddpdfk10`、`dfkbl` 的默认 DIP 配置设置为 `Unthrottled (102.4MHz No Stall)`。

## 2. 变更内容

在 `libretro/FBNeo/src/burn/drv/cave/d_cv1k.cpp` 中：
1. 新增专门面向大复活系列的 DIP 列表 `Cv1kDfkDIPList`，将 Dip B 的初始重置掩码值由 `0x07`（Bit 3 清零，对应 Arcade PCB Fit Bus Stall）改为 `0x0f`（Bit 3 置位，对应 Unthrottled 102.4MHz No Stall）：
   ```cpp
   static struct BurnDIPInfo Cv1kDfkDIPList[]=
   {
       DIP_OFFSET(0x18)
       {0x00, 0xff, 0xff, 0x00, NULL },
       {0x01, 0xff, 0xff, 0x0f, NULL },
       {0x02, 0xff, 0xff, 0x00, NULL },
       {0x03, 0xff, 0xff, 0x00, NULL },
       ...
   };
   ```
2. 导出 `Cv1kDfkDIPInfo` 并封装启动 RAM 测试跳过支持的 `Cv1kDfkBootDIPInfo`：
   ```cpp
   STDDIPINFOEXT(Cv1kDfk, Cv1kDfk, Default)
   static INT32 Cv1kDfkBootDIPInfo(struct BurnDIPInfo* pdi, UINT32 i);
   ```
3. 将 `ddpdfk`、`ddpdfk10`、`dfkbl` 三个驱动的 DIP 回调指向 `Cv1kDfkBootDIPInfo`。
4. 其它 CV1000 游戏（如 `ibara`, `futari`, `espgal2`, `deathsml`, `ddpsdoj` 等）保持既有的 `Cv1kDIPList` / `Cv1ksDIPList` 不受任何影响，遵循“避免把该兼容性选择无意应用到其它游戏”的既定约束。

## 3. 构建与部署验证

- 编译目标：`fbneo.xex`（诊断分支，`SALVIA_FBNEO_DIAGNOSTICS 1`，`--fbneo-only`）。
- 部署目标：Xbox 360 Jasper (`192.168.2.232:/Usb0/Salvia-360/fbneo.xex`)。
