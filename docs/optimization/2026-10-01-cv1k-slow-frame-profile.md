# CV1000：保留合并上传，定位剩余慢帧

## 基线与结论

- Baseline SHA：`2c1902bc3470b14b0e15733756c435564057c3fb`。
- 基线 XEX SHA256：`cd15ac6fe0930ee48a65dec1f1d3d755a5095bf2974dff5a32d79f755daaa773`。
- 本版标识：`cv1k-slow-frame-profile-20261001-r1`。
- 修改前工作树干净，相关源码与 Windows sibling 一致；原始日志、基线源码和 XEX 已保存在仓库外。

用户实机反馈：**明显改善，但仍偶尔低于 40 帧**。新日志确认运行合并上传版，`fused atlas/alpha self-test passed`，`page_upload=fused-alpha`，GPU fallback、async fallback 和 timeout 均为零。因此保留这一优化。

| 正常游戏窗口 | 前一份 GBR 日志 | 合并上传日志 |
| --- | ---: | ---: |
| 帧数 / 时间样本 | 2,874 / 44 | 13,960 / 214 |
| CPU / 设备处理均值 | 19.532 ms | 15.279 ms |
| draw_sync 均值 | 5.176 ms | 1.362 ms |
| 核心总时间均值 | 24.864 ms | 16.778 ms |
| 核心采样峰值 | 47.782 ms | 48.318 ms |
| batch upload 均值 | 3.339 ms | 0.997 ms |
| worker wait 均值 / 峰值 | 4.531 / 25.556 ms | 0.373 / 12.810 ms |

这些是两段不同长度、不同场景分布的日志，不能将差值全部归因于优化或换算成 FPS 提升。会话平均上传页数/批也从约 9.63 降到 3.32，工作量变化会影响上传均值。启动窗口无有效样本，排除在游戏评估之外。GPU 字段可能累计，核心字段按暂停窗口统计；没有相加不同窗口或重叠计时。

本次 68 个独立工作量统计帧有 66,236 interpreter steps、2,408 no-entry、28,555 budget、19,405 partial。约 84% 的 fallback site 未收录，局部排名不代表完整热点。核心峰值仍约 48 ms，但旧日志未保存该峰值样本的 CPU / draw 分解，无法据此选择下一处性能改动。

音频记录之间有 12,522 callbacks、165 次 underrun，dropped samples=0。存档 slot 0 加载成功，151,786,536 字节。tempo 字段不作为恒定游戏 FPS 测量。

## 本轮修改：慢帧诊断

复用现有约 1/64 帧采样的五个计时点，增加两个暂停时输出的记录：

- `core_slow_frame_ms`：核心总时间严格超过 25 ms 的有效采样帧数、各阶段均值、总均值，以及各阶段为最大耗时的样本数。相等时计入首个最大阶段。
- `core_sampled_peak_ms`：本窗口最慢有效样本的窗口内帧号、prep / cpu_io / audio / draw_sync 和总时间。所有阶段来自同一个样本，不组合互不相关的阶段最大值。

25 ms 阈值由初始化时查询的计时频率计算。每帧不增加 QueryPerformanceCounter；只在已有有效采样结束后更新少量整数统计。工作量与计时采样仍不重叠。暂停后清空当前窗口统计，游戏初始化时同时重置；计时频率读取失败则关闭新统计。日志仍使用原文件和暂停输出方式，不在游玩中写文件。

CPU/IO 包含设备处理、命令复制和部分声音工作；draw_sync 包含同步与绘图，不能直接称为纯 GPU 时间。25 ms 核心阈值不包含前端显示/限速，也不代表捕获了所有低于 40 FPS 的显示帧；随机采样可能漏过短暂尖峰。

这是诊断提交，**没有新增性能收益的实机结论**。SH3、GPU 合并上传、shader、反馈顺序、模拟周期和音频算法均未修改。

## 验证

使用 g++ `-std=gnu++98 -O2 -fsanitize=address,undefined -fno-omit-frame-pointer` 编译并运行：

- `libretro/FBNeo/tests/review_profile/test.cpp`：通过。覆盖严格阈值边界、CPU/绘图主导的混合样本、同帧峰值分解、较短帧中更高 CPU 分量不污染峰值、无效时间戳排除、窗口重置、禁用统计和 64 位计时。
- `libretro/FBNeo/tests/sh3_work_profile/test.cpp`：通过。一百万帧采样保持计时/工作量零重叠；计数和窗口重置通过。

源代码检查确认：新统计仅在 `cv1k_review.record()` 接受有效时间戳后执行；新增写入只涉及独立诊断对象，且发生在最后一个既有计时点之后。游戏输入、执行/绘图循环和存档扫描没有改动。本轮未重跑未修改的 SH3 指令和游戏画面回放，也不将这些主机统计测试视为 Xbox 帧率测量。

## Xbox 编译与文件

```text
rtk proxy /mnt/c/Windows/System32/cmd.exe /c "E:\Baiduyundownload\salvia-toolchain\.work\cv1k-slow-frame-profile-20261001\build_fbneo_incremental.cmd E:\Baiduyundownload\salvia-toolchain"
```

返回 0，FBNeo、SDL 与必要前端链接全部通过。镜像前后及编译后相关文件 SHA256 一致。最终链接 EXE 含新版标识、两条新日志文本以及既有合并上传日志字段；XEX2 文件头有效。

- 最终 XEX：34611200 字节。
- SHA256：`2681fb56de6d126f098621406f1f1b8375265cc2613e786bfa8d54946dca5f14`。
- 路径：`E:\Baiduyundownload\salvia-toolchain\.work\Salvia\Distro360\fbneo.xex`。

## 回退与下一次日志

本报告、实现和测试形成独立提交。外部 `final-artifact.json` 和运行目录 `FBNeo-CV1000-SLOW-FRAME-PROFILE.json` 记录完整提交和 `git revert <sha>`。

回退本提交后，镜像相关文件并重新编译，或恢复已保存的合并上传版 `baseline-fbneo.xex` 并核对上文 SHA256。Git revert 本身不会更新 sibling 或当前 XEX。

外部证据：`E:\Baiduyundownload\salvia-toolchain\.work\cv1k-slow-frame-profile-20261001`。未提交 ROM、存档、原始日志、SDK 或编译产物。

替换 XEX 并重启内核，在出现偶发掉帧的战斗场景游玩后暂停，上传原位置的日志。根据慢帧分组与同帧峰值，决定后续优先处理 CPU/设备还是绘图同步。
