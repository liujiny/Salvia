# CV1000 稳定版：日常无诊断日志版本与带日志版本保留

## 基线和范围

- Baseline SHA：`52e0ea98fa6e3a5e462a5608a5ce88657e64436d`。
- 实机验证稳定版 XEX SHA256：`41023f6504699240072e51d7cb7650e447f23daf32ae45a8ca56acd4e7cd6907`。
- 用户指令：保留稳定版，上传默认不记录诊断日志的版本，同时保留带日志侦测版；进一步优化等待用户指令。
- 修改前相关 Git / Windows sibling 源码一致。原源码、XEX、编译及校验记录保存在仓库外 `.work/cv1k-stable-no-diagnostics-20261001`。

本轮只分离诊断功能。GPU 启动自检、失败回退、绘图顺序与同步、缓存、SH3 模拟和声音/存档算法保留；不进行新的性能优化，也不声称关闭低频采样带来已测量的 FPS 收益。

## 默认无日志版

新增统一编译开关 `SALVIA_FBNEO_DIAGNOSTICS`，默认 `0`。默认版不创建或追加以下文件：

- `cv1000-gpu.log`
- `fbneo-audio.log`
- `fbneo-state.log`

同时关闭 CV1000 核心/前端的计时与工作量采样、GPU 批次采样、渲染线程等待计时；暂停时诊断格式化/音频快照/存档日志输出成为空操作。保留暂停时原有线程等待，以保持同步行为。普通前端错误提示不受此开关控制，已有日志文件不会被自动删除。

需要从新版源码构建诊断版本时，在 FBNeo 和 Salvia 前端两个项目的预处理定义中统一设置 `SALVIA_FBNEO_DIAGNOSTICS=1`。历史诊断源码和已实测 XEX 独立保留，恢复历史版不依赖此开关。

## 带日志稳定版保留

Git 标签：`fbneo-cv1000-stable-diagnostics-20261001`，指向上述 baseline SHA。它包含当时全部优化及实机验证文档。

已保存并核对原 XEX：

```text
E:\Baiduyundownload\salvia-toolchain\.work\Salvia\Distro360\versions\fbneo-cv1000-stable-diagnostics-20261001\fbneo.xex
```

其 SHA256 与上面的实机版完全一致。切换时把该文件复制到 Xbox 的游戏目录作为 `fbneo.xex`，重启内核即可。两个文件不同时运行。

## 验证

默认配置与 `SALVIA_FBNEO_DIAGNOSTICS=1` 配置分别编译并运行生产 `statesram.h` 的前端存档测试（g++ C++98，ASan/UBSan，严格警告）。两者均通过：151 MiB streaming、gzip CRC/截断拒绝、原文件原子保护、旧存档、worker 所有权与关闭。开启配置在外部测试目录生成 state 日志；关闭配置的 stateLog 编译为空操作。

命令：

```text
rtk proxy python3 libretro/FBNeo/tests/frontend_state/run.py --sanitize --output /home/humor/salvia-tests/cv1k-stable-no-diagnostics-20261001/frontend-state
```

开启配置使用同一测试源码及参数，增加 `-DSALVIA_FBNEO_DIAGNOSTICS=1`；测试配置、产物哈希与结果保存于外部证据。首次关闭配置编译暴露空函数未使用参数警告，已显式标记参数未使用并重新通过严格警告检查；最终前端在镜像该修正后重新编译。

GPU、SH3 和实际声音算法没有改动；没有重新运行不受本轮影响的全游戏回放，也没有新实机 FPS 结果。

## Xbox 编译与交付

FBNeo、SDL 及必要前端的 Xbox 编译返回 0。最终前端在未使用参数修正同步后再次编译返回 0。编译前后相关源码哈希与 Git / sibling 一致。

最终链接 EXE 中三份诊断日志文件名的 ASCII / UTF-16 字符串均不存在。开启诊断的主机测试二进制含 state 日志文件名，关闭配置不含，作为开关验证。XEX2 文件头有效，原诊断 XEX 哈希保持不变。XEX 打包后不能用直接字符串搜索验证日志内容，因此只在实际链接 EXE 上作该检查。这是编译产物校验，尚未新增 Xbox 实机启动测试。

- 无日志 XEX：34611200 字节。
- SHA256：`32d22edd9c55fcd3fba7c0b6fc8e6e5cad20ebfbb3f61df2ae4308f8d8cd15e2`。
- 保留诊断 XEX：34611200 字节，SHA256 为上述基线哈希。
- 两份 XEX 及其版本记录分别放入 Distro360 的 `versions` 子目录，日常无日志版另位于原默认路径。

默认无日志版仍安装在：

```text
E:\Baiduyundownload\salvia-toolchain\.work\Salvia\Distro360\fbneo.xex
```

默认源码将推送到 `liujiny/Salvia` 的 `main`，无日志稳定版另设标签 `fbneo-cv1000-stable-20261001`；带日志版使用前文独立标签。所有源码历史保留，每个逻辑修改仍为独立提交。仓库上传源码、测试及报告，不提交 XEX、ROM、SDK、原始日志或编译产物。

## 回退

外部 `final-artifact.json` 与 Distro360 的版本 JSON 记录本提交 SHA 和 `git revert <sha>`。回退后须同步 Windows sibling 源码并重编译；或直接恢复上面的诊断版 XEX，并核对基线 SHA256。Git revert 不会自行更新 sibling 或 XEX。

完成此次发布后，进一步优化等待用户明确指令。
