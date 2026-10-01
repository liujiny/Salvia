# CV1000 CPU 精灵绘制：按实际裁剪范围失效纹理页

## 基线和日志证据

- Baseline SHA：`fa780c82be9ee23115e85f7a43826145719711b9`。
- 基线 XEX SHA256：`2681fb56de6d126f098621406f1f1b8375265cc2613e786bfa8d54946dca5f14`。
- 新版标识：`cv1k-clipped-invalidation-20261001-r1`。
- 修改前工作树干净，相关源码与 Windows sibling 一致。原始三份日志、基线源码和 XEX 存于仓库外 `.work/cv1k-clipped-invalidation-20261001`。

日志为同一 DDPDFK 会话、102.4 MHz、2 render cores、DIP 00,07,00,00。GPU 自检通过，合并上传和异步显示启用，GPU fallback/async fallback/timeout 均为零。

| 暂停窗口 | 帧数/时间样本 | CPU/设备均值 | 绘图同步均值 | 核心均值 | 核心峰值 | >25 ms 样本 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 开头短窗口 | 201/3 | 30.612 | 0.819 | 31.648 | 78.674 | 1 |
| 第二窗口 | 1804/26 | 11.385 | 0.869 | 12.397 | 16.553 | 0 |
| 最后窗口 | 4967/68 | 18.275 | 3.021 | 21.447 | 37.572 | 17 |

单位 ms。开头窗口场景未确认，不能把 78.674 ms 当作持续战斗表现。最后窗口 17 个慢帧中 CPU/设备均值 21.251 ms、绘图同步 8.829 ms、核心总均值 30.238 ms，CPU/设备都是最大阶段。最慢有效样本为窗口第 1673 帧：CPU/设备 36.554 ms、绘图同步 0.891 ms、合计 37.572 ms。CPU/设备包含设备与部分声音工作；这些数值不能单独证明解释器占比，也不能换算成实测 FPS。

GPU 统计累计。最后窗口相对前一个快照的差值：15864 个实际 GPU 批次、141645 个上传页、47740 次 CPU draw 导致的非空批次 flush、36849 次小批次软件回放（跨 flush 原因统计，不能全部归于 CPU draw）。最后累计 worker wait 平均 1.449 ms、峰值 14.715 ms。`gpu_fallback=0` 仅表示 GPU 渲染失败回放为零，不能解释为没有软件绘制。存在 CPU 与绘图同步两类耗时，不能继续笼统判断 GPU 路径完全无关。

音频前三行计数并非全部单调（underrun 131→106→156），不得直接把全段当一个累计窗口相减。最后两条 callbacks 1526→5155、underrun 106→156、dropped=0；日志未标明完整重置语义，不把该差值当作每帧音频质量测量。

## 修改原因及边界

原先 `gfx_draw()` 在进入 CPU 绘制前，用未裁剪的目标矩形调用 `epic12_gpu_cpu_write()`。该函数同时服务 VRAM 上传；上传确实可能跨行，因此负坐标、越界矩形会清空整个源纹理页缓存。软件精灵绘制本身会按 `m_clip` 裁剪，这导致它将未写到的源页也丢弃，后续需要重新上传和重新生成 alpha 元数据。

新增专用 `epic12_gpu_cpu_draw()`：只将目标矩形与原软件裁剪框取交集，再调用原失效逻辑。空交集仍 flush 已排队命令。交集超出物理 VRAM 时仍按旧规则清空缓存。普通上传继续使用原函数，保留跨行保护。

这是矩形级缓存失效修正。原 CPU/GPU 同步和 flush 时机保留；未引入命令依赖扫描或跳过同步。所有 blend/透明/源读取/绘制函数、设备周期、SH3、source validation、shader、128 页批次上限和音频算法保持原逻辑。

日志支持减少上传开销这一方向，但没有直接记录被错误失效的页数，不能声称已证明本修改解决全部掉帧。实机 FPS 收益待验证。

## 验证与文件

### PowerPC/QEMU 回归

命令：

```text
rtk proxy python3 libretro/FBNeo/tests/epic12_gpu/run.py --toolchain-root /home/humor/salvia-tests/toolchains/ppc/root --output /home/humor/salvia-tests/cv1k-clipped-invalidation-20261001/unit
```

返回 0。新增测试独立枚举裁剪后目标像素，验证全部 2048 个物理页在 128/256 槽缓存下的精确保留/失效结果，覆盖负坐标、右下越界、空交集、页边界、大尺寸精灵。裁剪后仍越界与跨行上传继续触发全缓存重置。另有 2304 次真实 gfx_draw 调用覆盖全部 64 种混合模式、四种翻转及九组裁剪位置；完整 VRAM 与模拟 blitter delay 和软件参考完全一致。

既有 6291456 个 shader 分量用例、4096 个 plain 用例、14400 条随机命令的完整 VRAM/周期对比、分组/反馈/页上限/缓存一致性/alpha/排序测试全部通过。纹理数据由软件 GPU 模型处理，不能视为真实 Xenos 执行或性能测量。

### 固定输入游戏回放

私有脚本（ROM/状态/产物不入 Git）：

```text
rtk proxy python3 /home/humor/salvia-tests/cv1k-clipped-invalidation-20261001/game/build.py
rtk proxy python3 /home/humor/salvia-tests/cv1k-clipped-invalidation-20261001/game/run-candidate.py
```

DDPDFK、DDPSDOJ 各 600 帧，分别与上一轮相同代码基线和输入的已保存回放比较：每款 9 个画面/逐帧哈希/中途与最终存档产物 SHA256 完全一致，CPU、设备周期、GPU、缓存和 alpha 日志计数一致。DDPDFK 上传 2886 页、DDPSDOJ 上传 907 页，均未下降。这两段场景没有证明本轮优化的上传收益，作用是回归验证。每个文件哈希与结论存于外部 game-comparison.json。

### Xbox 编译

```text
rtk proxy /mnt/c/Windows/System32/cmd.exe /c "E:\Baiduyundownload\salvia-toolchain\.work\cv1k-clipped-invalidation-20261001\build_fbneo_incremental.cmd E:\Baiduyundownload\salvia-toolchain"
```

返回 0，FBNeo、SDL 及必要前端链接通过。编译前后源码镜像 SHA256 一致，链接 EXE 含新版本标识且无旧版本标识，产物 XEX2 文件头有效。

- XEX：`E:\Baiduyundownload\salvia-toolchain\.work\Salvia\Distro360\fbneo.xex`
- 大小：34611200 字节。
- SHA256：`41023f6504699240072e51d7cb7650e447f23daf32ae45a8ca56acd4e7cd6907`。
- 新版仍保留慢帧分组和同帧峰值诊断；请在原掉帧场景实机验证画面和帧率，然后暂停保存日志。

本轮没有实机 FPS 测量，不能保证达到 50 FPS。未新增其它内核构建，未改声音算法；未提交 ROM、日志、SDK 或编译产物。

## 回退

独立提交的完整 SHA 与 `git revert <sha>` 存入外部 `final-artifact.json` 及 Distro360 的同版 JSON。回退后须镜像相关文件并重新编译，或恢复基线 XEX 并核对以上哈希；Git revert 本身不会更新 Windows sibling/XEX。
