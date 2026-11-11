# ddpdfk BGM 中断：Codex 现场复核与总线等待对照

日期：2026-11-11。接手HEAD `be75a8a4`，功能基线 `6e1fb9e2`。工作区接手时干净。
状态：**宿主对照已复现控制命令延误；已请用户实机切换No Stall验证，尚未验收。**
本记录修正原交接中的若干推测；本轮当前没有改动生产模拟器源码。

## 1. 实机版本与两份原始现场

证据根目录 `/home/humor/salvia-tests/ddpdfk-bgm-20261111/`，ROM和存档仅在该外部目录。

初次取回：slot0保存帧3914，压缩18505821字节，SHA
`5838716cce6665fea9883c8f6a47a31fb8e7b50fdf4fb64cf0921b53fbfa497a`。
主机fbneo.xex回读SHA `2d841b38425c44e3a0c58d1f56bc9a1f0d6c69cb751084e6c4eafc0b997a6bf4`，
对应11月8日 `ddpdfk-bgm-fix fdfda1a1`。不是原交接声称正在测试的6e1fb9e2。
当时未发现upload_fbneo_daemon在运行，不能把“守护上传已安排”视为已成功部署。

已部署归档 `fbneo-20261109-2127-blitter-busy-fix-diag.xex`，并FTP回读确认SHA
`d081f068625925bc81a6467c1b4c87a43862fd1f798bd33fe4958f4faff7c996`。
用户重新开局后反馈“背景音乐仍然消失”，再次取回日志确认运行tag
`blitter-busy-fix 6e1fb9e2 20261109-2124 diag`，本轮才真正排除该镜像能单独解决问题。

第二份现场保留在 `busy-fix-console/`，slot0帧3134，压缩17307800字节，SHA
`132fe8179ba28540a407d622ce2ea29eb5e617b86f17a9cfc4f5f8c9dfd67f1b`。
两份现场都备份，不能再用/tmp/ddpdfk-slot0.raw旧文件替代用户这次提供的新现场。

## 2. 最新现场的实际声音状态

从原始gzip解压，去掉前4字节前端帧号和末尾空RCHV trailer，得到native scan数据。
以16份连续MPEG cosine表的首行32个big-endian double 1.0定位decoder区域，间距29488字节；
结合生产ymz_channel(8232字节)与sequence(20字节)布局解析。字段和值互相吻合。

两份现场共同状态：
- 通道0 phrase=2，is_playing=0，last_block=1，remaining=0，output_ptr=864。
- 通道0 bit pointer=12815497，PCM buffer峰值25378，音量不是零。
- 序列0 sequence=5，loop=0，is_playing=0，delay=0，offset=108764(0x1a8dc)。
- 序列结束位置前4字节是 `43 00 0f 00`，明确的通道停止与EOS。
- 音效通道1、2、4等仍在播放，不能沿用“BGM固定0、SFX固定4..15”的硬编码划分。
- PC=0x0c1d1346，PR=0x0c1e3d6c，RAM 0x0c3dc58c=1。

有音效不能逻辑上完全排除混音后处理，但此现场确有芯片侧通道停止的独立证据。
仅凭保存瞬间flag=1也不能认定死锁。

## 3. MPEG独立完整解码对照

读取用户已有u23+u24，按DrvLoadRoms执行16位字节交换，不能直接拼接后就解读音序表。
使用真实生产mpeg_audio.cpp从phrase2起点解码至合法短末帧，与638f80dc之前的MPEG实现比较。
只对Burn接口提供最小测试替身，实际解码函数没有复制重写。

两版都得到：
```
PCM_HASH=1e79443d79f519e0
EOS phrase=2 block=241 samples=278496 last_samples=864 rate=16000 ch=1 pos=12815497 seconds=17.406000 peak=25378
```
结束位置、末帧长度、峰值与两份slot0完全一致。支持“当前短语正常结束，后续音乐命令延误”，
不支持把当前现场直接归为MPEG损坏、断流或IDCT数值错误。
这只验证了这段短语，不能证明所有MPEG输入均等价。

## 4. 排除“等待位置必然死锁”

由最新3134帧坏存档继续重放180帧，解释器与DRC逐帧视频/音频hash完全一致，BGM仍停止。
状态布局由真实BurnAreaScan逐区读取日志校准；两版最终CPU序列化字段存在差异，未声称全STATE相同。

随后用真实解释器执行路径打印3帧的关键PC：
- 每帧执行0x0c1d14e0；Blitter读取为0x10。
- 返回到0x0c1e3c2a时R0=1。
- 每帧回到主循环0x0c1e3d40，并进入调度函数0x0c1dc8b4。

因此这条snapshot中的等待路径本身是正常每帧同步点，不能再据此认定关卡主循环锁死。
该重放未启用Xbox专用bus-cycle扣除，不能据其推断所有Xbox路径都健康。
反汇编必须先去掉前端4字节帧头；直接对整个raw文件按RAM地址偏移会使PC标签错4字节。
原交接将0x0c1d14e0的开头直接写成blitter MMIO读也不准确，实际先检查内存状态。

## 5. 真正复现故障的单变量对照

建立生产代码PPC游戏重放，实际YMZ寄存器写记录与BGM状态只在宿主记录，未加到主机热路径。
输入：启动600帧后每300帧投币/开始，射击与横向移动；ROM、DIP、48kHz输出与其它代码一致。
单个render core，关闭异步worker带来的宿主完成时机差异。

两条No Stall基线各4500帧：解释器、DRC都发出相同BGM序号转换：
```
frame1231 -> sequence0
frame1297 -> sequence1
frame1891 -> sequence5
frame2934 -> sequence6
```
两种CPU的整段视频/音频hash有差异(首个差异1677帧)，不能声称全程严格等价；
这里比较的是明确的音乐控制命令链和曲目衔接。

第三条对照仅在宿主打开epic12.cpp原来仅_XBOX启用的这一段：
```
if (dips && !(dips[1] & 0x08)) {
    Sh3BurnCycles(cycles);
}
```
`cycles`取自整段m_blit_delay_ns，而不是单独测量CPU对主RAM总线的占用。
生产源码保持原样，复制到外部replay-bus中用测试宏使该分支在PPC宿主执行。

该对照得到：
```
frame1231 -> sequence0
frame1325 -> sequence1
frame1890 -> sequence5
frame2934 -> sequence5 EOS，停止
frame3679 -> 才发出sequence6
```
与No Stall第2934帧的切换相比，切换命令晚745帧，约12.42秒。
停止时的sequence、phrase、last_block、末样本数与bit pointer全部与用户现场一致。
这提供了总线周期扣除会造成BGM控制命令延误的直接可复现证据，不是根据采样FPS猜测。

数据：`bus-stall-comparison.json`；日志 `replay/playing-jit0.log`、`playing-jit1.log`、
`replay-bus/playing-bus.log`。三条4500帧重放均已结束并生成STATE，完整状态见比较JSON；不以QEMU耗时推断实机性能。

## 6. 机制判断与纠正原交接推测

YMZ流以本帧音频样本推进，游戏发出下一段音乐命令依赖SH3逻辑及时运行。
新模型按整段blitter持续时间烧掉CPU周期，改变了CPU与音频序列之间的进度关系。
原曲自然结束时，CPU尚未发出后续Key-On，因此只有音效而没有BGM。
该整段扣周期分支由 `8d571ced8d7452b5f6929d5e26a03f4f1c386b7b` 于11月6日加入。
当前已用同一生产代码、单独打开此分支复现，优先针对该分支做兼容性修复，不整批回退AOT/GPU/IDCT。
具体TMU/IRQ调度中哪一部分导致745帧延误仍可继续细拆；不能据当前证据宣称已证明完整硬件总线模型。

- Stream::UpdateStream会把position夹到framelen，并只补position-nPosition；
  不能说Sh3BurnCycles一次跳跃就必然使音序器无限快进数千样本。
- 保存执行ACB_READ；ACB_WRITE是读档，不是保存。保存不调用读档AOT/DRC重置分支。
- 构造器已将s_cos_transposed_inited设为true，正常save scan不会重新生成cosine表。
- m_blitter_busy强清不能补回已经延误的音乐命令，不能再作为主解决方案。
- 比较要保留音序0x80/0x81控制写；只看decode false或EOS不足以区分正常曲终与错误停止。

## 7. 已安排的最小实机验证

现有DIP已有可逆控制，无需改代码或重新构建：
`Timing Model (ibara360)` -> `Unthrottled (102.4MHz No Stall)`。
它只置DrvDips[1]的0x08位，保持DriverClock=102400000、DRC、GPU加速及Blitter自身计时。
日志应从DIP 00,07,00,00变为00,0f,00,00。

已请用户只保存ddpdfk本游戏选项、重新开局，不先读取旧故障存档。
等待音乐跨原失音时刻的反馈，再取build-tagged日志核对DIP生效。
不能把保存/读档后的偶然恢复当成已修复，也不能先宣称验收完成。

FTP后续对大ROM目录LIST耗时较长，已停止该次大列表。不要重试全量列几千文件。
取存档与日志使用已知短目录。读日志时区分启动session与累加历史。
当前主机仍是已回读的6e1fb9e2诊断镜像；最初原版2d841b…完整字节仍在外部归档。
