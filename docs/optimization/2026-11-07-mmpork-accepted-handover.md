# Salvia / FBNeo mmpork Boss 对话卡顿优化交接

日期：2026-11-07。状态：**用户已验收，本轮优化结案；仍有卡顿，可接受。**
用户最终回执原文：**“虽然还有卡顿，但可接受，生成交接文档”**。

本记录取代原 `mmpork_boss_dialog_context_for_codex.md` 中的待办状态与推测方案。
不能将本次验收写成“完全消除卡顿”“稳定60 FPS”或“所有CV1000游戏满帧”。

## 1. 问题、存档与环境

Xbox 360 Jasper，Salvia / FBNeo CV1000，游戏 `mmpork`（Muchi Muchi Pork!）。
第一关 Boss 死亡后出现漫画对话气泡与大量飞散金币，原场景明显卡顿。
此次使用用户最新保存的 Boss 对话存档，保存帧号12913，原文件21933835字节。
主机路径：`/Usb0/Salvia-360/data/states/FinalBurn Neo/mmpork.state`。
本轮使用相同存档、无输入、180帧固定测试；SH3时钟102400000，render cores=2，
DIP=`00,07,00,00`，bpp=4。原始ROM、存档与日志只保留在仓库外证据目录。

自动测试必须经通用FBNeo前端读取 `salvia_benchmark.txt`；用户实际成功入口是
`default.xex`（消息中曾写作defualt.xex）。独立 `mmpork.xex` 的自启动分支会跳过
该配置。在这台主机上测试结束的 `exit(0)` 实际会关机，不能承诺自动返回Aurora；
用户重启并进入Aurora后取日志。以 `done frames=180` 确认完成。

## 2. 证据支持的归因

慢帧的CPU/I/O约1.8ms、compiled_blocks=0，draw_sync可达120–123ms。
主要耗时来自CPU离屏staging处理。原交接对“大量snapshot、队列反复flush”的
猜测不能作为既定根因：source-page、region、source-dependency中途flush均为零；
snapshot计数是累计值，不能当作每44帧新增工作量。已有copy_rows本身已使用VMX，
无需因看到复制就假设它未向量化。此次没有修改JIT/AOT、GPU批次上限或render cores。

抓取的首个有效Boss绘制任务包含7861条普通离屏命令、1276928像素。
其中大量命令宽2像素、高84/94/103等，目的X每条只增加1像素，彼此重叠，
源坐标也会变化；旧横向融合不接纳。逐列遍历时相邻访问相隔32768字节，
并重复写入随后立即被覆盖的第二个像素。

## 3. 最终保留的改动

功能提交：`84989393396ce84b38417d8eaf187fff4bc65282`。

收集4至128条连续的、中性着色、不透明、不翻转、宽2像素的离屏命令，要求
目的Y/高度相同、目的X逐条加1、无裁剪/越界，且所有源的包围框与合并目的框分离。
将合格组改为逐行执行，每条写最终保留的第一个像素，最后一条再写第二个像素。
首个任务可覆盖48组、3501条命令、639942像素。

保留原GPU生产者/消费者屏障、每条命令的页版本递增与客机像素延迟计费；
仍按 `命令数×2×高度` 计费，不按减少后的实际写入数计费。
128-source-page批次上限、颜色公式、GPU shader与SH3代码有效性检查均维持既有行为。
诊断版只增加暂停时累计 `staging_column_rows` 计数，不增加逐指令计时。

主要文件：
- `libretro/FBNeo/src/burn/devices/epic12_staging_columns.h`：收集与逐行复制内核。
- `libretro/FBNeo/src/burn/devices/epic12_gpu.h`：staging_stream接入与依赖/计费处理。
- `libretro/FBNeo/src/burn/devices/epic12_gpu_xbox.h`：暂停诊断计数。
- `libretro/FBNeo/tests/epic12_staging_columns/` 与 `tests/epic12_gpu/test.cpp`：内核与集成验证。

## 4. 实机对照与边界

两轮均完成180帧，100个worker jobs，25319953次dispatch，1973次timer。
工作量完全对齐：364387条staging-ident、1457条staging-replay、17716条onscreen，
4278个快照/13993098像素，99个已计时GPU批次/42231479像素。
首轮优化实际命中1691组、136234条列命令、23253902个计费像素。

| 指标 | 原版诊断镜像 | 保留的逐行列复制 |
| --- | ---: | ---: |
| Worker busy，100个任务 | 5177.217ms | 2647.162ms |
| GPU flush，99个批次 | 492.001ms | 508.644ms |
| 超过100ms的帧 | 20 | 1 |
| 全帧峰值，首次绘制初始化 | 348.630ms | 279.279ms |
| 两个随机采样中的峰值 | 122.658ms | 45.232ms |

Worker busy下降48.87%；剩余超过100ms帧在首次绘制，编译块数仍为零。
只有两个随机核心采样，不能据此计算全程平均FPS，也不能排除其他较短卡顿。
用户随后正常游玩并确认“虽然还有卡顿，但可接受”。这是本轮验收标准与最终状态。

## 5. 正确性验证

- 新内核512个生成用例在本机、ASan/UBSan和PPC执行下通过，包含保护区、别名、
  裁剪、标志/着色、容量与短组拒绝。
- GPU集成对照原绘制函数，核对像素顺序、消费命令数、准确页版本和客机延迟。
- 完整PPC GPU回归通过，见 `gpu-final-ppc.log`。
- mmpork和先前ddpsdoj两个不同场景存档各重放180帧，逐帧视频/音频哈希与完整最终
  状态均匹配原基线。ddpsdoj两个存档各自与同场景基线比较，不能互相当A/B。

| 重放 | 最终状态SHA-256 |
| --- | --- |
| mmpork | `7fa1476b7f6cd6579645501bd59d2d027025d51994a3ad20a6750d8f63c7b1db` |
| ddpsdoj slot0 | `d69b58404e12c4df694ffc026c36b0ee9078dcbc085372983825ec2e6bcde403` |
| ddpsdoj slot1 | `b1d70f125ee5414bde0d4ec67143094c3a38a476ea2faf4609663b3afaeae444` |

主机/QEMU用于确定性与PPC执行验证，不代表Xenon cache或真实帧时间；
本次没有宣称重新运行整个SH3指令差分全集，也不以这些有限存档证明所有游戏正确。

## 6. 已否决的后续尝试

`528486775224f5fdc107761bf7a63ea7fa746812` 为对齐且不重叠的32像素离屏行复用VMX复制。
候选SHA-256：`be44e1856c2e64b0cdf6cf9686dd071dd41c88ae7c83a9d2a241c6d44a39aa9b`。
正确性通过，实机新路径命中173517次/10444736像素，工作量相同，但worker busy仅
2647.162→2612.659ms，单轮差异1.30%，不足以认定稳定收益，未予保留。
这不是正确性失败，也不是已测得变慢。

已执行 `git revert --no-edit 528486775224f5fdc107761bf7a63ea7fa746812`，生成
`99512531`，并恢复三份源码头文件镜像、仓库/运行目录与主机的正收益XEX，FTP回读
SHA确认983c镜像。不能仅做Git回退而保留候选二进制。
完整记录：[32像素尝试否决记录](2026-11-07-mmpork-staging-vector32-rejected.md)。

## 7. 当前已验收镜像与运行状态

主机 `/Usb0/Salvia-360/fbneo.xex`：逐行列复制诊断版，用户验收的实际镜像。
- 源码功能提交：`84989393`，诊断标志 `SALVIA_FBNEO_DIAGNOSTICS=1`。
- build tag：`mmpork-column-rows 84989393 20261107-2028 diag`。
- 34832384字节，SHA-256：`983cbbaa1d264952f5ca437b082ee30932aba34b56467cd56287090756182c91`。
- 归档：`/home/humor/salvia-tests/xex-archive/fbneo-20261107-2032-mmpork-column-rows-diag.xex`。

原始对照诊断版SHA-256：`0d08a0e5a8616e04a07f190c3bf9ec25736bdfa8bb6eaa0db90e27d5da1d4618`。
本地A/B目录 `/home/humor/src/Salvia/Distro360/mmpork-dialog-ab/` 恢复为这一对：
`fbneo-mmpork-dialog-base-diag.xex`（原始）与 `fbneo-mmpork-dialog-cand-diag.xex`（已验收）。
已验收的ddpsdoj独立专版未在本轮覆盖。

自动测试已关闭，`salvia_benchmark.txt` 恢复原始空内容，完成标记已删除。
正常游玩需要手动加载存档；不要继续运行旧的 `collect-vector32.py` 或重新武装测试。

## 8. 证据、源码镜像与后续规则

工作仓库：`/home/humor/src/Salvia`。
构建源码镜像：`/mnt/e/Baiduyundownload/salvia-toolchain/.work/Salvia-main`。
原始证据目录：`/home/humor/salvia-tests/mmpork-boss-dialog-20261107/`。
重点：`candidate-evidence.json`、`final-host-comparison.json`、`column-admission.json`、
`baseline-console.log`、`candidate-console.log`、`vector32-evidence.json`、
`vector32-console.log`、`benchmark-control.json`。

已验收功能保留在84989393；99512531回退后续试验；1ea9238c记录否决与恢复。
文档提交不应误认为额外性能改动。没有自动push。

本轮到此结案。若以后继续优化，重新取得用户需求、独立量覆盖与危险计数，一次实机
比较一个变量；不要把已否决的32像素路径作为默认待办，也不要重新扩大GPU批次上限。
header变更必须确认epic12.cpp重编译，源码回退必须同步实际部署镜像。
release构建和身份另列在下节；实机计时和用户验收均来自上述诊断版。


## 9. 验收后release归档

按仓库AGENTS.md在用户验收后编译一次release：核心与前端均完成构建，
`epic12.cpp` 在核心日志第34行确认重编译。

- 构建源码HEAD：`1ea9238c623014f8644337cf8933e2201280a998`；功能内容为保留84989393、已回退52848677。
- 标志：`SALVIA_FBNEO_DIAGNOSTICS=0`，tag：`mmpork-row-columns-accepted`。
- 成品：`/home/humor/salvia-tests/xex-archive/fbneo-20261107-2116-mmpork-row-columns-accepted-release.xex`。
- 大小：34799616字节。
- SHA-256：`9bb0adae5aa042e2f111b574a0d227fd5b230e2f8180d23d88de7d9fbc16a1ac`。
- 链接PE SHA-256：`79243929ac7ab54ee8e8a9dd767711f363bf098d7b342aeed54a71ebb1dd07dc`，核对其不含暂停诊断column计数字符串。
- 元数据：`/home/humor/salvia-tests/xex-build/artifact-mmpork-row-columns-accepted.json`。
- 日志：证据目录 `accepted-release-build.log`，以及xex-build目录中的对应core/frontend日志。

实际命令：
```bash
rtk proxy bash /home/humor/salvia-tests/mmpork-boss-dialog-20261107/build-reviewed-xex.sh --baseline 5f685238 --tag mmpork-row-columns-accepted --flavor release --no-deploy
```

release仅归档，尚未在实机运行；不能把诊断版实测数字标为release实测。
主机与仓库Distro360/fbneo.xex仍是已验收诊断版983c；A/B目录仍是原始对照与已验收诊断版。
运行源码镜像Distro360/fbneo.xex及其链接PE已由本次构建更新为release输出，
不能据该构建目录推断主机当前版本。源码头文件镜像逐字节核对匹配。


## 10. 后续AOT建议的独立交接

应用户后续要求，预编译优化建议另见
[CV1000 AOT优化建议交接](2026-11-07-cv1k-aot-optimization-plan-handover.md)。
这是后续研究建议，尚未实施，不改变本次mmpork已验收状态。
外部副本：`cv1000_aot_optimization_handover.md`，与本交接外部副本位于同一目录。
