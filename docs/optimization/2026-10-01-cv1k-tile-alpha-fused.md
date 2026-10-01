# CV1000：合并贴图上传与透明掩码生成

## 基线与本轮日志

- Baseline SHA：`c875ed60c8dc6b88a5c55d951008a06788d3de22`。
- 基线 XEX SHA256：`2ea8dfe308b7c362febe69ca6aa47dbe65bb706bc6b843ae5e2d239527ac9906`。
- 新版标识：`cv1k-tile-alpha-fused-20261001-r1`。
- 修改前工作树干净；相关源码与 Windows sibling 一致。原始日志、源码和基线 XEX 已保存到仓库外。

新日志确认运行 GBR 版本，游戏 DDPDFK，模拟时钟 102.4 MHz，render cores=2，DIP `00,07,00,00`。正常游戏窗口 2,874 帧，有 44 个时间样本、12 个独立工作量统计帧。启动窗口只有一个时间样本且 GPU 尚未初始化，排除在正常游戏分析之外。

| 项目 | 日志结果 |
| --- | ---: |
| CPU / 设备处理均值 | 19.532 ms |
| draw_sync 均值 | 5.176 ms |
| 核心总时间均值 / 采样峰值 | 24.864 / 47.782 ms |
| 前端 active loop 均值 / 采样峰值 | 26.180 / 48.573 ms |
| 解释器步数 / no-entry / budget / partial | 14,709 / 425 / 6,946 / 4,717 |
| 上传页 / 上传字节，GPU 会话累计 | 89,083 / 5,838,143,488 |
| 每批 upload 时间均值，含锁和准备 | 3.339 ms |
| worker 等待均值 / 峰值 | 4.531 / 25.556 ms |
| GPU fallback / async fallback / timeout | 0 / 0 / 0 |

原有 GPU 自检通过。no-entry 已较少，继续补普通指令的潜在收益下降；本轮选择有明确重复读取的贴图上传路径。CPU/IO 仍是主要时间，上传并非已证实的唯一瓶颈。场景和输入没有受控匹配，不能据此判定上一轮帧率提高或退化；不将时间倒数当 FPS，不叠加 worker wait 和 draw_sync，也不累加累计 GPU 快照。fallback site 有 10,972 次未收录，已收录排名并非完整热点排名。

音频两次记录之间有 3,344 callbacks、22 次 underrun，dropped samples=0。状态日志记录 slot 0 的 151,786,536 字节存档加载成功。

## 修改

原路径先读取整页 128×128×4 字节源像素，将其排列成 Xenos tiled texture；随后再次读取整页，提取 bit 29 生成透明掩码。新路径复用首次读取的向量，同时完成贴图写入和掩码生成。

每个 32×32 tile 按连续目的地址处理两行：读取并交错写入两个 16 像素区段，再从相同寄存器合并两个 16 位 alpha 掩码。每八行的 OR 摘要在向量寄存器中累积。保留原掩码位顺序、像素全部 32 位、查询缓存失效、裁剪和翻转语义。

每页源像素向量读取由 8,192 次降到 4,096 次，减少 **64 KiB 源数据读取**；源码另有五次显式向量常量加载；编译器还可能装载局部常量，测试中的 intrinsic 次数不等于机器指令总数。GPU 上传仍为完整 **64 KiB/页**。按本次已记录上传页数，若全部采用合并路径，可避免约 5.84 GB 的重复源读取；这不是实测带宽或帧率收益。

只对对齐的 cached source 使用合并路径。GPU WC 内存只作对齐、顺序写入，禁止读取。源地址/行距不满足条件或自检失败时，返回后使用原有两遍路径。一次性的私有内存自检比较 SDK tiled 地址、像素、mask/group 和写入边界；现有 GPU 像素自检继续验证真实上传与采样。现有日志增加 `fused atlas/alpha self-test passed` 和 `page_upload=fused-alpha` / `separate`。

本轮不改 SH3 执行、模拟周期、handler、shader 混合算法、反馈顺序或批次容量。batch page limit=128，atlas 仍为 256 slots。没有添加逐帧文件写入或逐像素计时。

## 验证

```text
rtk proxy python3 libretro/FBNeo/tests/epic12_gpu_tile_alpha/run.py --toolchain-root /home/humor/salvia-tests/toolchains/ppc/root --output /mnt/e/Baiduyundownload/salvia-toolchain/.work/gpu-tile-alpha-fused-20261001/tests
rtk proxy python3 libretro/FBNeo/tests/epic12_gpu_alpha_vmx/run.py --toolchain-root /home/humor/salvia-tests/toolchains/ppc/root --output /mnt/e/Baiduyundownload/salvia-toolchain/.work/gpu-tile-alpha-fused-20261001/alpha-regression
rtk proxy python3 /home/humor/salvia-tests/cv1000-tile-alpha-fused/game/build.py
rtk proxy python3 /home/humor/salvia-tests/cv1000-tile-alpha-fused/game/run.py
rtk proxy python3 /home/humor/salvia-tests/cv1000-tile-alpha-fused/game/run-candidate.py
```

主机 ASan/UBSan 与真实 PPC AltiVec 测试各通过 1,152 页、18,874,368 像素，覆盖全部 atlas slots、8192/132/128 行距、全透明/全不透明/单像素/随机 alpha、独立地址公式、掩码与八行摘要、bbox 缓存失效、源不变、边界、顺序写入和禁止 atlas 向量读取。每页精确为 4,096 次源向量加载及 4,096 次向量写入。未对齐和关闭路径在任何写入前返回；注入掩码写入错误时，自检能关闭新路径。

共享 alpha helper 的 48,661 个 row/page 案例及 12,000 个 bbox 对照测试在主机与 PPC 均通过，涵盖源边界、所有透明位、缓存、跨页、翻转和逐像素裁剪。

DDPDFK、DDPSDOJ 各 600 帧，包含投币、开始、周期射击/移动，以及第 240 帧存读档。四次运行返回 0；每款游戏的九个画面/音频 hash、周期原始帧和中途/最终存档逐字节相同。CPU 工作量、模拟周期、设备服务、GPU 命令、alpha/cache 统计也完全相同。

私有回放使用当前 GBR 版本 CPU。基线采用原上传/alpha 模型；候选真正执行合并 kernel，将生成的 tiled 输出解回纹理供软件 shader 使用，同时使用新 alpha metadata。DDPDFK 上传 2,886 页，DDPSDOJ 上传 907 页。回放未运行 Xenos 驱动，输入也未精确复现用户所指的某个敌机弹着场景，不能作为实机 FPS 测量。

## Xbox 构建与代码检查

```text
rtk proxy /mnt/c/Windows/System32/cmd.exe /c "E:\Baiduyundownload\salvia-toolchain\.work\gpu-tile-alpha-fused-20261001\build_fbneo_incremental.cmd E:\Baiduyundownload\salvia-toolchain"
```

最终返回 0：FBNeo、SDL 和必要前端链接全部通过，仅构建 FBNeo 及相关文件。编译前镜像相关文件并核对 SHA256，编译后再次确认 Git 源码与 sibling 的哈希一致。链接 EXE 包含新 build ID、融合自检文本；XEX2 文件头有效。

XDK 机器码每处理 64 个源像素，有 16 条源 `lvx128` 和 16 条目的 `stvx128`，同一批向量经 `vperm128` / `vrlb` / OR 生成透明掩码，循环内没有逐像素 helper 调用。局部向量常量仍有加载，不能把源码 intrinsic 计数当作全部访存次数。

第一次编译发现向量三元表达式产生临时栈写入及读取，改为显式 if 赋值后，这一处摘要状态保存在 `vr53`。最终 block 占用 760 字节（含四字节对齐），此前为 784 字节；这只证明生成代码减少，不是帧率测量。改写后重新通过融合测试，并重跑两款游戏候选回放，所有产物和计数仍与基线一致。原/最终机器码保存在证据目录。

- 最终 XEX：**34611200 字节**。
- SHA256：`cd15ac6fe0930ee48a65dec1f1d3d755a5095bf2974dff5a32d79f755daaa773`。
- 文件：`E:\Baiduyundownload\salvia-toolchain\.work\Salvia\Distro360\fbneo.xex`。

## 证据与回退

外部证据目录：`E:\Baiduyundownload\salvia-toolchain\.work\gpu-tile-alpha-fused-20261001`。ROM、存档、原始日志、SDK、XEX 和编译产物不提交。

本报告与实现、测试属于一个独立提交。提交后，外部 `final-artifact.json` 和运行目录 `FBNeo-CV1000-TILE-ALPHA-FUSED.json` 记录完整 SHA 与 `git revert <sha>`。回退后须镜像相关源码并重新编译，或恢复本目录 `baseline-fbneo.xex`，核对上文基线 SHA256；只执行 Git revert 不会改变运行目录的 XEX。

## 实机验收

替换内核并重新启动，核对 build ID、融合自检通过和 `page_upload=fused-alpha`。在相同关卡、时钟、核心数与滤镜下测试持续射击命中敌机的场景，前后暂停生成独立窗口。关注 source_upload、worker_wait、cpu_io、draw_sync 和实际帧率。主机/PPC 测试及回放不能代替 Xbox/Xenos 实测，尚不能宣称达到 50 FPS。
