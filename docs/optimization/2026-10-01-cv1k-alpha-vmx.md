# CV1000：向量化贴图上传前的透明掩码生成

## 基线与交付

- 基线：`0bc558720c2080ab7ba81511bdf21bb6e8717823`。
- 本次独立提交标题：`perf(fbneo): vectorize CV1000 atlas alpha masks on Xenon`。
- 回退对象：本报告所在提交；完整 SHA 和 `git revert` 命令记录在 `Distro360\FBNeo-CV1000-ALPHA-VMX.json`。
- 新构建标识：`cv1k-alpha-vmx-20261001-r1`。
- 内核：`E:\Baiduyundownload\salvia-toolchain\.work\Salvia\Distro360\fbneo.xex`。
- XEX：34,611,200 字节。
- XEX SHA256：`ef4564fa5dffb77585e2f8d94a45f7844997fe24bc3b6444f7b749e6e7d54868`。
- 基线 XEX SHA256：`b79e9e5a768a10d48316daf1d3e50c586bb8581c9d0cf635d7d30cc1fd45dc26`。

## 用户反馈与新日志

用户反馈：不射击时帧率高，持续射击并命中敌机时降到二十多帧。最新日志为 DDPDFK、`cv1k-device-drc-20261001-r1`，整段 5,131 帧；71 个核心时间样本、20 个工作量统计帧、76 个 worker 等待样本，102.4 MHz 模拟时钟，2 个 render cores，DIP `00,07,00,00`。

| 项目 | 当前记录 |
| --- | ---: |
| 核心 CPU / IO 均值 | 16.367 ms |
| draw_sync 均值 | 4.337 ms |
| 核心总时间均值 / 采样峰值 | 20.885 / 78.167 ms |
| worker 等待均值 / 峰值 | 6.608 / 37.181 ms |
| batch upload 均值，含 CPU 准备 | 2.950 ms |
| GPU readback_wait 均值 | 0.290 ms |
| async-after-present source_upload 均值 | 2.569 ms |
| 贴图页上传 / 字节数，累计 | 98,952 / 6,484,918,272 |
| GPU fallback | 0 |

上一版的寄存器服务正在生效：busy 处理 9,914 次、fallback 4 次；DMA 处理 10,656 次、fallback 10 次。解释器总步数 45,861、partial 15,091，解释器 guest_cycles 59,062；这些是模拟工作量，不是主机耗时。

绘图线程的等待和上传准备值得继续优化。每次 atlas 页上传后，原实现还逐像素读取整页并构造透明掩码。该会话累计上传页对应约 16.21 亿个像素透明位检查，是本次优化的明确工作量依据。实际 CPU 检查字节数没有额外计时，不能将全部 upload 时间归给它。

该日志没有分别标注“不射击”和“持续命中”的时间窗口，也没有单帧尖峰的 CPU / renderer 分解。因此不能断言已经确定某个爆炸效果的唯一原因。与先前日志的场景和采样不同，不进行 FPS A/B 推算，也不相加核心时间、worker 等待或 batch 均值。

音频日志有 148 个 underrun callbacks、最低 tempo 0.280；本次不改音频算法。射击时更大的模拟和绘图工作量仍可能同时影响节奏。

## 修改原因与实现

为 aligned cached source 生成掩码时，使用 Xenon 向量指令一次处理 32 个像素：

1. 读取 8 个 16 字节向量。
2. 从大端像素的高字节提取 bit 29。
3. 对每组八个像素进行字节旋转及 OR 归并。
4. 合成原格式的 32 位掩码，bit 31 仍对应最左边像素。

只读取普通 cached source，写入普通 CPU metadata，不读取 GPU WC memory。贴图数据仍完整上传，原 bit-29 定义、每八行的 OR summary、精确裁剪、查询缓存失效、透明与非透明命令语义保持一致。未对齐 source 保留标量生成路径。

整页 build 复用向量常量，并在构建前失效查询缓存；单行 build 仍先失效缓存。没有更改 SH3、handler、模拟周期、GPU shader、反馈依赖、批次上限、atlas 容量或渲染线程数。GPU batch 上限继续为 128 页，atlas 为 256 slots。

启动时在私有 cached memory 上检查单像素、全透明、全不透明、随机像素、四种 word-store lane 和写入保护。仅通过后开启向量路径；失败自动使用原标量路径。自检在 renderer 初始化时执行，不在游戏帧循环内执行。没有新增逐指令计时或游戏期间日志写入。

日志新增：`VMX alpha mask self-test passed`，以及既有 pipeline 行的 `alpha_masks=vmx-bitpack` / `scalar`。

## 测试

### 主机及真实 PPC 向量测试

```sh
rtk proxy python3 libretro/FBNeo/tests/epic12_gpu_alpha_vmx/run.py --toolchain-root /home/humor/salvia-tests/toolchains/ppc/root --output /mnt/e/Baiduyundownload/salvia-toolchain/.work/gpu-alpha-vmx-20261001/tests
rtk proxy python3 libretro/FBNeo/tests/epic12_gpu_alpha/run.py --sanitize --output /mnt/e/Baiduyundownload/salvia-toolchain/.work/gpu-alpha-vmx-20261001/scalar-tests
```

全部返回 0。主机 ASan/UBSan 使用数值大端 intrinsic 模型；QEMU `g4` 执行真实 AltiVec 指令。各 48,661 个 row / page 案例通过：所有 bit 位置及无关位、随机输入、输出 word 对齐、guard page、8192-word VRAM pitch、未对齐标量回退、group summary、缓存失效，以及自检失败后的回退。每行向量读取恰好 512 字节，无源数据越界。

向量版和原标量版的完整 alpha / crop 测试均通过，包含 12,000 个精确 bbox 比较、跨页、翻转、VRAM 边界、空批次、缺失 slot、缓存碰撞和逐像素结果一致。

这些测试没有运行 Xbox VMX128 扩展或 Xenos 驱动；其指令形式另由 XDK 构建和启动自检覆盖。测试打印的 host / QEMU 时间不代表 Xbox 性能。

### 游戏回放

私有 harness：

```sh
rtk proxy python3 /home/humor/salvia-tests/cv1000-alpha-vmx/game/build.py
rtk proxy python3 /home/humor/salvia-tests/cv1000-alpha-vmx/game/run.py
```

DDPDFK 与 DDPSDOJ 各 600 帧、2 个 render cores，包含投币、开始、周期射击、左右移动和第 240 帧存读档。基线使用保存的标量 alpha header；候选 renderer 使用生产向量 helper 和真实 PPC AltiVec。两边复用相同 CPU、驱动及其它未修改对象，GPU 为软件模型。

两款游戏各 9 个产物（逐帧音视频 hash、原始画面、中途及最终存档）逐字节相同。CPU 周期、绘图命令、GPU model 像素、上传页、透明裁剪与查询计数也一致；DDPDFK 构建 47,284,224 像素元数据，DDPSDOJ 14,860,288 像素。

第一次 fixture 未指定支持向量指令的 QEMU CPU，候选遇到 SIGILL；停止该轮并保存日志，改为 `-cpu g4` 后最终四次运行均通过。这是 fixture CPU 配置问题，不是 Xbox 实机测试结果。

回放未覆盖用户所指的同一敌机命中场景或 Xenos 驱动、GPU 着色器、SDL 显示；不能据此宣称实际 FPS 提升或所有掉帧已解决。

### Windows XDK 与反汇编

```sh
rtk proxy /mnt/c/Windows/System32/cmd.exe /c "E:\Baiduyundownload\salvia-toolchain\.work\cpu-cache-ready-20260930-205053\git-baseline\tools\build_fbneo_checkpoint.cmd E:\Baiduyundownload\salvia-toolchain"
```

源码镜像并核对 SHA256 后构建，返回 0。FBNeo 与相关库 / `Release_finalburn` 前端通过，没有编译其它模拟器内核。XEX 头为 `XEX2`，链接 EXE 含新 build ID 和自检日志文本。

XDK 生成的 520 字节向量行方法包含 `lvx128`、`vperm128`、`vrlb`、`vsldoi128` 和 `stvewx128`，没有逐像素 helper 调用。基线内循环每 32 像素执行 184 条标量指令，候选循环为 42 条指令。此统计排除了 row / page 初始化和 group 更新，不同指令成本也不同；不能作为测得加速比例或 FPS。

## 实机验收与回退

替换 Xbox 上的 `fbneo.xex` 并重新启动内核，核对日志 build ID 和 `alpha_masks=vmx-bitpack`。保持模拟时钟、滤镜与核心数不变，建议分别在“不射击”和“持续命中同一敌机”时暂停，形成两个独立日志窗口，同时记录实机帧率。对照 source_upload、worker_wait、cpu_io 与 draw_sync，继续区分游戏逻辑和绘图开销。

当前：正确性与构建通过，实机净收益待确认；不能保证已达到 50 / 60 FPS。CPU / IO 的剩余开销和复杂场景峰值仍需继续看实机数据。

回退此独立提交后必须镜像源码到 `../Salvia` 并重新编译；Git revert 不更新现有 XEX。或恢复外部 `baseline-fbneo.xex` 并核对上文基线 SHA256。完整回退命令在运行目录的 artifact JSON。

外部证据：`E:\Baiduyundownload\salvia-toolchain\.work\gpu-alpha-vmx-20261001`，包括基线源码 / XEX、用户原始日志、测试输出、游戏比较 JSON、镜像 hash、构建日志、两版反汇编和候选 XEX。私有回放位于 `/home/humor/salvia-tests/cv1000-alpha-vmx/`。ROM、存档、原始日志、SDK 与编译产物均不提交。
