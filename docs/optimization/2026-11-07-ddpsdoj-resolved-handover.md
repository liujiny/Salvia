# Salvia / FBNeo CV1000 ddpsdoj 问题解决交接

状态：**已解决，用户已确认。**
日期：2026-11-07。
用户最终回执原文：**“已解决。生成解决交接文档”**。

本文为本次问题的最终交接，替代原 `ddpsdoj_context_for_codex.md` 中的未解决结论和推测方案。原文作为历史材料保留。

## 1. 本次问题与复现场景

项目：Xbox 360 Jasper 上的 Salvia / FBNeo，游戏 `ddpsdoj`（怒首领蜂最大往生，CV1000 Type D）。

用户报告两个现象：

- 选完飞机后，飞机刚进入画面的瞬间停住约 2–3 秒；用户明确补充，**画面静止时音乐继续播放**。
- 游戏中爆炸特效附近出现画面卡住或白屏。

用户提供的两个存档代表**不同场景**，不能将 slot0 与 slot1 互相当作 A/B 对照：

| 场景 | 主机存档名 | 存档中的帧号 | 正确比较方法 |
| --- | --- | ---: | --- |
| slot0 | `ddpsdoj.state` | 22802 | 同一个 slot0，GPU 路径与 CPU 绘制参考对照 |
| slot1 | `ddpsdoj.state1` | 7106 | 同一个 slot1，GPU 路径与 CPU 绘制参考对照 |
| 选机后刚出场 | 冷启动、投币、选机 | 由输入过程决定 | 单独验证，不能假定两个存档就是出场现场 |

主机存档目录：`/Usb0/Salvia-360/data/states/FinalBurn Neo/`。
两个原始存档未被覆盖或修改。已取回副本在证据目录的 `console-states/`。

## 2. 对原交接判断的纠正

原交接将开场问题主要归因于“预编译太早、RAM 尚未解包、运行时出现单帧上万块 JIT 编译”。本轮证据不支持将该推测作为已证实根因：

1. 基线开机重放在第 46 帧的前四个 RAM MiB 已找到 `144 / 2291 / 16859 / 681` 个函数序言，接近游戏中存档；不能说当时只有最小引导程序。
2. `(DrvInputs[0] & 0x0c) != 0` 把低电平投币输入的含义读反，重放中 AOT 已在第 11 帧触发，不能只按 `nCurrentFrame >= 30` 推断实际时机。
3. 旧日志第 46 帧的 `85.843 ms` 属于 `cpu_io`；AOT 位于该阶段开始之前，因此该值不是该帧 AOT 耗时的证据。
4. 旧版只随机采样每 64 帧中的一帧，采样峰值不能排除漏掉的长停顿；累计生成 25474 个块也不等于同一帧生成这些块。
5. 旧版 `blend=0` 拒绝数、零 GPU 渲染失败，不能证明 CPU 离屏合成与 GPU 缓存、快照之间一致。

本轮没有提高 AOT 上限、扩大队列、增加预编译轮次，也没有改变 SH3 指令语义或放松动态代码有效性检查。AOT 的重复计数、满队列标记等问题仍属于独立维护事项，不能据此重新认定本次根因。

## 3. 实际修复的三类渲染错误

### 3.1 CPU 回放丢失源混合模式

GPU 路径新增接纳源模式 1（平方）和 2（目标调制）后，`epic12_gpu_replay` 仍主要按目标模式选取旧的源模式 0 绘制函数。

结果：小批次回放、GPU 渲染失败回放，以及复杂离屏合成，会执行错误的颜色公式。独立对照原始绘制函数复现了源/目标模式 `(1,0)、(1,3)、(1,7)、(2,3)、(2,7)` 五组错误。

修复：对已接纳的源模式 1/2，调用对应的原始源/目标模式绘制函数，保留翻转、透明、着色和 alpha 参数；保留模式 0 的既有快速路径。

### 3.2 混合离屏写入绕过缓存与快照一致性处理

`epic12_gpu_submit` 的混合离屏分支直接在 CPU 上回放，漏掉了纹理图集失效、离屏页版本更新及必要的 GPU/CPU 依赖处理。

结果：CPU 已写入新合成画面，GPU 却读取旧纹理页，或者复用更新前的快照。在两个存档中，第四个重放帧均可复现差异；涉及的源包含 `(7872,32)` 开始的 `320×240` 整帧离屏画面。

| 场景 | 首次差异帧 | 像素 | 正确值 | 旧 GPU 模型值 |
| --- | ---: | --- | --- | --- |
| slot0 | 22805 | (44,32) | `20b8d8b0` | `20d8f8b8` |
| slot1 | 7109 | (32,32) | `209898b8` | `209898b0` |

修复：读取 CPU 源前完成可能的 GPU 生产者；写入前复用既有的页依赖处理并使纹理页失效；CPU 写入更新离屏页版本，防止同一批次内复用旧快照。没有依赖的批次继续保留。

### 3.3 非混合直接复制、流式复制漏掉读依赖

完成上一项后，slot0 的 180 帧已一致；slot1 前 168 帧一致，最后 12 帧从 7274 开始仍有差异。逐 GPU 批次像素对照通过，说明还存在批次外的 CPU 复制问题。

直接离屏复制和流式合并复制原来只检查“将覆盖 GPU 正在使用的目标”，没有检查“CPU 将读取 GPU 尚未完成的源”。因此会将旧屏幕内容复制到离屏缓冲，再正确地绘制出这份错误内容。

修复：两个入口都检查 CPU 源矩形与待完成 GPU 目标范围的重叠；有依赖时先完成 GPU。流式复制在刷新 GPU 前，先发布此前累计的 CPU 脏页。重复源合并检查原始源宽度，连续源合并检查整个源跨度。

这些修复采用保守矩形/页判断，未引入逐 GPU 命令的精确依赖扫描，**128 源页批次限制保持不变**。

## 4. 提交与主要代码位置

仓库：`/home/humor/src/Salvia`。
本轮开始时为干净的 `main`，基线 `f9b2738a3cf1f4aa2b5959444888b9e2c95f360d`。
最终功能源码：`c511cd159101a34560f0cff99361b4698563485e`。
提交均在本地，未推送远端。

| 提交 | 作用 |
| --- | --- |
| `f61421f6` | 保存稀有长帧现场及当帧编译数，暂停时输出 |
| `dbd3a4df` | 修复平方/调制源混合模式的 CPU 回放 |
| `3a3c08aa` | 修复混合离屏写入的缓存、快照和依赖一致性；同步模型容量 |
| `c511cd15` | 修复直接、流式 CPU 离屏读取的 GPU 生产者依赖 |

主要文件：

- `libretro/FBNeo/src/burn/devices/epic12_gpu.h`：回放、离屏合成、直接/流式复制和页失效处理。
- `libretro/FBNeo/src/burn/devices/cv1k_review_profile.h`、`libretro/FBNeo/src/burn/drv/cave/d_cv1k.cpp`、`src/salvia.cpp`：核心和前端长帧记录。
- `libretro/FBNeo/src/cpu/sh4/sh3_drc_ppc.h`、`sh4.cpp`、`sh4_intf.h`：只增加编译计数读取接口。
- `libretro/FBNeo/tests/epic12_gpu/`：原始函数对照、缓存/快照/读依赖测试、模型容量修正。
- `libretro/FBNeo/tests/review_profile/test.cpp`：长帧阈值、固定容量、阶段一致性与时钟异常测试。

对应的四篇过程记录在仓库 `docs/optimization/2026-11-07-ddpsdoj-*.md`。本文件记录最终结论，过程文档中“待实机验证”的表述是当时状态。

## 5. 验证结果与证据边界

| 验证 | 结果 |
| --- | --- |
| SH3 PPC 指令差分 | 848846 例通过 |
| SH3 dispatch | 8 组主机套件，优化构建及 ASan/UBSan 均通过 |
| GPU 原始绘制函数对照 | 73728 组源模式用例通过 |
| GPU 完整套件 | 原生主机及 QEMU PPC 均通过 |
| 新增依赖与容量测试 | 缓存失效、快照版本、无依赖批次保留、混合/直接/流式读取、4096 命令容量均通过 |
| 内存检查 | ASan/UBSan 通过 |
| slot0 | 同存档、无输入，180 帧画面/声音逐帧一致，完整状态逐字节一致 |
| slot1 | 同存档、无输入，180 帧画面/声音逐帧一致，完整状态逐字节一致 |
| 冷启动 | 固定输入重放 1800 帧，画面/声音逐帧及完整状态一致 |
| Xbox 360 实机 | 用户反馈“已解决”；以用户验收回执记录 |

两个存档各自的完整主机状态大小为 `151788648` 字节。主机转换保留相同扫描区序列，去掉 gzip 解压数据前的 4 字节大端帧号和尾部空 RCHV 附加数据；原始文件保留。

测试模型原来的裁剪数组只有 1024 条命令，而生产路径允许 4096 条。已将模型容量与生产数组对齐；旧模型大批次溢出后的异常计数不能作为性能结论。临时批次对照器复制完整 4352 行（含快照区），并恢复 blitter delay，不安装到实机版本中。

用户确认属于实机验收证据。**本轮没有在用户确认后再次取回实机耗时日志，也没有量化 FPS 或宣称所有 CV1000 游戏满帧。**

## 6. 已验收的构建与部署身份

当前为诊断版，`SALVIA_FBNEO_DIAGNOSTICS=1`；标签 `ddpsdoj-staging-read-fix`，功能源码 `c511cd15`。本次交接没有重新编译或切换 release，验收对象就是下列诊断版。

实际 Windows 构建源码：
`/mnt/e/Baiduyundownload/salvia-toolchain/.work/Salvia-main`。

最终构建使用证据目录内 `build-reviewed-xex.sh`，来自既有构建脚本的任务副本，只构建 FBNeo 通用入口与 ddpsdoj 专用入口，跳过无关的 mmpork；保留源码校验、诊断开关、构建标识、归档及哈希检查。头文件修改后已核实 `epic12.cpp`、`d_cv1k.cpp`、`sh4.cpp` 实际重新编译。

部署位置：`192.168.2.232:/Usb0/Salvia-360/`。两文件均已通过 FTP 回读 SHA-256 校验。

| 文件 | 大小（字节） | SHA-256 |
| --- | ---: | --- |
| `fbneo.xex` | 34832384 | `0d08a0e5a8616e04a07f190c3bf9ec25736bdfa8bb6eaa0db90e27d5da1d4618` |
| `ddpsdoj.xex` | 34832384 | `2f110d601f1ee7c5ed1a6efe8985190ec44858d753af297819e2980c807190c9` |

最终归档：

- `/home/humor/salvia-tests/xex-archive/fbneo-20261107-1934-ddpsdoj-staging-read-fix-diag.xex`
- `/home/humor/salvia-tests/xex-archive/ddpsdoj-20261107-1936-ddpsdoj-staging-read-fix-diag.xex`

XEX 内不能靠直接搜明文字符串验证构建标识。已在实际链接产物 `Salvia.exe` 中核实最终标签、源码短 SHA、核心与前端 watchdog 字符串，并核对构建日志和最终 XEX 哈希。

## 7. 证据、构建与回退入口

证据根目录：`/home/humor/salvia-tests/ddpsdoj-takeover-20261107/`。

- `final-evidence.json`：源码、测试、图像哈希、部署回读及用户验收记录。
- `final-source-mirror.json`：最终仓库源码与实际构建树逐文件一致性记录。
- `console-before.log`：本轮开始前的实机日志；不要误当作修复后日志。
- `console-states/`：原始存档及重放转换副本。
- `slot0-read-fixed.*`、`slot1-read-fixed.*`、`cold-final.*`：最终重放结果。
- `slot0-cpu.*`、`slot1-cpu.*`、`cold-cpu.*`：对应 CPU 绘制参考。
- `gpu-read-final.log`、`gpu-read-barrier.log`、`gpu-sanitize-final.log`、`ppc-test.log`、`dispatch-run.log`：测试记录。
- `xex-read-fix-build.log`：最终构建记录。
- `/home/humor/salvia-tests/xex-build/artifact-ddpsdoj-staging-read-fix.json`：构建产物清单。

原版二进制备份在证据根目录 `remote-before/`：

| 原文件 | 基线 SHA-256 |
| --- | --- |
| `fbneo.xex` | `d76ffbd292440f4661e59b841eade9c4a1de1c8eb3d87e787196790028dc234b` |
| `ddpsdoj.xex` | `4f8409d9d1103015234ed4deb8f97b9db6fc685c28a0e70451810d660211fe83` |

本地独立 A/B 目录：`/home/humor/src/Salvia/Distro360/ddpsdoj-stall-ab/`，只有 `ddpsdoj-baseline.xex` 与 `ddpsdoj-candidate.xex` 两个镜像。

需要回退运行版本时，将已校验的原始 XEX 恢复到主机同名位置。只执行 Git revert 不会恢复 Windows 构建镜像或主机 XEX。源码回退应按依赖逆序建立独立 revert 提交，保留记录，禁止 reset/clean/改写已有历史。

## 8. 后续继续工作的约束

- 本次问题已验收解决，继续工作以 `c511cd15` 的功能源码和上述诊断版字节为基准。
- 若后续生成 release，作为独立交付记录编译配置与新哈希，不得混用本次诊断版身份。
- 发生相似现象时，先区分“执行长帧”和“GPU 读旧画面”，不要只凭音乐继续、采样峰值或累计 JIT 数下结论。
- 暂停时的 `core_stall_watch` / `core_stall`、`frontend_stall_watch` / `frontend_stall` 保留最慢八个超过 100 ms 的帧；统计只在暂停时写日志，未增加逐指令计时或游戏中实时文件写入。
- 保留 128 源页上限、代码字节/映射/扇区有效性验证及现有双缓冲语义。无新证据不要恢复已否决的 GPU 批次扩容或精确扫描实验。
- ROM、存档、SDK、原始运行日志和生成二进制保持在 Git 仓库外；每项独立改动连同验证记录单独提交，未经新授权不推送。
