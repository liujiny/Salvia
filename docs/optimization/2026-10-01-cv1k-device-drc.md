# CV1000：在 SH3 DRC 调度内处理绘图和 DMA 状态读取

## 基线与交付

- 基线提交：`f351ed9a80edd391c954ff20524cfe0f1fcb57b7`。
- 本次提交标题：`perf(fbneo): service CV1000 blitter and DMA polling in SH3 DRC`。
- 回退对象：本报告所在的独立优化提交；完整提交 SHA 和 `git revert` 命令记录在运行目录的 `FBNeo-CV1000-DEVICE-DRC.json`。
- 构建标识：`cv1k-device-drc-20261001-r1`。
- 交付：`E:\Baiduyundownload\salvia-toolchain\.work\Salvia\Distro360\fbneo.xex`。
- XEX 大小：34,611,200 字节。
- XEX SHA256：`b79e9e5a768a10d48316daf1d3e50c586bb8581c9d0cf635d7d30cc1fd45dc26`。
- 基线 XEX SHA256：`058c22ab7ce471f39ada2432c234746e90bf1e7e64dbd7c18c5f2265a0af55ae`。

## 新日志与定位

用户上传的 DDPDFK 会话标识为 `cv1k-idle-drc-20261001-r1`，累计 6,676 帧，94 个时间样本、27 个工作量统计帧。上一版 idle 服务执行 20,990 次，watched fallback 只剩 32 次，证明该路径正在使用。

这次窗口的 `cpu_io` 均值为 14.458 ms，核心总时间 16.911 ms；GPU 等待样本均值约 0.291 ms，GPU fallback 为 0，async present 正常。先前诊断窗口的 `cpu_io` 为 14.456 ms，但场景、长度和采样不同，不能用两者判定实际提升或退化，也不能换算成测得 FPS。日志中的 `guest_cycles` 是模拟周期，不是主机耗时。

剩余 `0x60` 读取中，handler 类为 26,996 次；`6012` 为 11,277 次，`6022` 为 14,571 次。通用 site 表有 65,498 个未收录事件，不能把已收录的站点当作全局热点排名。

为确认地址，另在私有 QEMU 回放 CPU 副本内统计 MOV.L handler 站点，不改交付代码、不增加实机逐指令计时或文件写入。DDPDFK 600 帧结果：

| 读取 PC | 指令 | 地址 / 物理地址 | 次数 | 实际含义 |
| --- | --- | --- | ---: | --- |
| `0C1D171E` | `6012` | `B8000010` / `18000010` | 204,595 | EPIC12 绘图忙状态 |
| `0C1D1504` | `6012` | `B8000010` / `18000010` | 593 | 同一绘图忙状态 |
| `0C3C0D10` | `6022` | `A400002C` / `0400002C` | 269,941 | SH3 DMA 通道 0 控制 / 完成状态 CHCR0 |

两种读取合计约 792 次 / 帧。DDPSDOJ 也有相同寄存器读取，DMA 读取 PC 为 `0C30C5F0`。这是回放中存在的热点，尚不能证明用户实机每次特定精灵掉帧都由它们造成。

## 修改

CV1000 驱动显式注册两个可服务的 MOV.L 设备地址。CPU 配置记录地址、handler 索引和注册时的 callback；它们不属于存档里的架构状态。初始化和退出清除注册；注册或撤销时清空 DRC 代码缓存。

普通 `MOV.L @Rm,Rn` 的 RAM 范围 / 通用映射保护失败时，可以返回带已校验 opcode 的服务标记。调度器只允许当前地址、映射索引和 callback 仍符合注册的读取走新路径。内部 P4 地址、未对齐地址、其它寄存器、替换 callback 或重新映射的数据均回退到原路径。合并的保护分支仅共享同一 PC、opcode 服务标记和未执行读取前的寄存器快照。

服务调用原有 `MOVLL -> RL -> WaitState -> ReadLong`，保持原来的 PC / delay-slot 前置处理、EA 更新、`Sh3BurnCycles`、读取结果、IRQ 检查和最后的基础周期扣除。绘图 busy 的烧周期数量、DMA 处理和 timer 更新均未改动。仍有 native block 返回，也仍校验每个后继代码块。

`hacky_idle_ram` / `hacky_idle_pc` 继续动态读取，没有固定某个游戏 PC。没有注册设备读取的其它驱动不启用新增服务标记。新日志只在既有低频统计帧计数、暂停时输出两行 `drc_device_read`，分别记录 handled / fallback / guest_cycles；普通模板不更新这些计数。

## 测试结果

### 主机合成测试

实际命令（Git 工作树根目录）：

```sh
rtk proxy python3 libretro/FBNeo/tests/sh3_dispatch/run_inline.py --output /mnt/e/Baiduyundownload/salvia-toolchain/.work/cpu-blitter-read-20261001/host-final
```

8 组测试在 optimized 与 ASan/UBSan 模式通过。服务测试合计 12,309 案例，覆盖全部 Rm/Rn 组合、地址别名、延迟槽、正 / 负预算、IRQ 顺序和契约拒绝。设备 fallback 总数在通用 site 表溢出时仍保持准确。首次测试遇到合成 fixture 缺少新元数据定义，补齐 fixture 后上述最终运行通过；该测试使用模型 RL / IRQ，不是实机。

### 真实 PPC 指令差分

```sh
rtk proxy python3 libretro/FBNeo/tests/sh3_ppc/run.py --toolchain-root /home/humor/salvia-tests/toolchains/ppc/root --output /home/humor/salvia-tests/cv1000-blitter-read/ppc
```

QEMU PPC 执行生产生成代码和真实 SH3 指令 helper，689,637 个差分案例通过，其中新增设备服务案例 16,396 个。覆盖直接 RAM 范围保护和通用 map 保护、全部操作数组合、四种别名、分支延迟槽、读取与烧周期、副作用次数、callback / mapping 变更、撤销注册；原 source validation 和生命周期测试也通过。中断顺序另由主机模型和下面的游戏回放核验。

### 游戏回放

私有 harness：

```sh
rtk proxy python3 /home/humor/salvia-tests/cv1000-blitter-read/game/build.py
rtk proxy python3 /home/humor/salvia-tests/cv1000-blitter-read/game/run.py
```

分别使用基线与候选 CPU / 驱动，DDPDFK 和 DDPSDOJ 各运行 600 帧，2 个 render cores，第 240 帧存档并读回。两款游戏各 9 个产物（逐帧音视频 hash、原始画面、途中存档、完整最终存档）逐字节相同。初次搭建基线 fixture 链接到候选驱动导致新 API 缺失，分开编译两版驱动后最终四次运行均返回 0。

| 游戏 | interpreter_steps 基线 → 候选 | partial 基线 → 候选 | 新增设备服务 |
| --- | ---: | ---: | ---: |
| DDPDFK | 1,197,973 → 723,254（减少 39.63%） | 710,235 → 235,516（减少 66.84%） | 474,719 |
| DDPSDOJ | 1,275,655 → 1,012,237（减少 20.65%） | 528,683 → 265,265（减少 49.83%） | 263,418 |

每款减少的 interpreter_steps 与 partial 恰好等于新增设备服务次数，原 idle 服务次数未变。DDPDFK 新路径处理 busy 205,075 次、DMA 269,644 次，剩余两种 fallback 为 113 / 297；DDPSDOJ 为 190,398 / 73,020 次，剩余 107 / 63。

这些是全部帧计数的私有回放结果，不是 Xbox 帧率。其 GPU 为软件模型，复用未修改的渲染、声音等对象；没有覆盖 Xenos shader、显示线程和实机暂停菜单。

### Windows XDK 构建及产物

```sh
rtk proxy /mnt/c/Windows/System32/cmd.exe /c "E:\Baiduyundownload\salvia-toolchain\.work\cpu-cache-ready-20260930-205053\git-baseline\tools\build_fbneo_checkpoint.cmd E:\Baiduyundownload\salvia-toolchain"
```

返回 0，FBNeo 核心、相关 SDL 库和 `Release_finalburn` 前端通过。没有编译其它模拟器内核。12 个修改的 FBNeo 源码 / 测试路径已镜像，Git 与运行副本 SHA256 相同。XEX 头为 `XEX2`，链接 EXE 包含新 build ID。

XDK disassembly 验证 CV1000 初始化实际注册 `18000010 / handler 0` 与 `0400002C / handler 7`。普通 MOV.L 服务 helper 为 520 字节，调用真实 MOVLL 和 pending IRQ，然后扣除基础周期；没有统计模板的计数写入。与基线 helper 相比增加了设备契约检查，净收益必须由实机验证，不能用解释器调用减少比例推断 FPS。

## 实机验收与限制

替换 Xbox 上的 `fbneo.xex`，重新启动内核。保持原时钟、Speed Hacks、滤镜与 render core 数，重点对照同一 DDPDFK 精灵 / 爆炸场景，并测 DDPSDOJ；暂停后上传既有日志。核对 build ID、两行 `drc_device_read`、`cpu_io`、partial 和 interpreter_steps。若要计量 FPS，请直接记录相同场景的实机显示值。

当前状态：逻辑、回放和构建通过；实机净收益待确认。没有保证达到 50 或 60 FPS，也没有宣称已经消除所有特定精灵掉帧。

## 回退与证据

完整提交号记录在 `Distro360\FBNeo-CV1000-DEVICE-DRC.json` 和外部 `final-artifact.json`。执行其 `git revert` 后，必须将还原的源码路径镜像到 `../Salvia` 并重编译；Git revert 不会更新现有 XEX。也可恢复外部 `baseline-fbneo.xex`，恢复后核对上文基线 SHA256。

外部证据：`E:\Baiduyundownload\salvia-toolchain\.work\cpu-blitter-read-20261001`，含基线源码 / 清单 / XEX、输入日志、私有诊断、镜像 hash、最终主机测试、游戏比较 JSON、XDK 构建日志、反汇编和候选产物。PPC 测试及私有游戏文件位于 `/home/humor/salvia-tests/cv1000-blitter-read/`。ROM、存档、日志、SDK 与编译产物均不提交。
