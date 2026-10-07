# CV1000：Xenos 合成器初始化失败的可读诊断

Baseline SHA for this round: `e4ce0298c3f24dea2b0fbc686ea82e5d15e7e9b5`.

Companion documents:
[code arena size](./2026-10-07-cv1k-arena-size.md),
[block table size](./2026-10-07-cv1k-block-table-size.md),
[arena accounting](./2026-10-07-cv1k-arena-accounting.md).

## 现象

用户在实机载入游戏后，屏幕左下角出现：

```text
cv1000 gpu:cpu fallback - initialization failed
```

这条消息对应 `epic12_gpu_take_message()` 的 `case 3`，也就是
`epic12_xenos_create()` 返回 `false`：Xenos 合成器没有建立起来，本局
回退到 CPU 渲染。GPU 回退会同时解释上一轮报告的两件事——平均帧数下降、
以及内存压力下菜单截图路径崩溃风险上升——因此先定位它，而不是继续做性能
优化。

## 为什么现有信息不够

1. 横幅由前端 `Fonts::recortarAlTamanyo(text, overlay->w)` 按 overlay 宽度
   裁剪，`initialization failed (` 之后的括号内容（原来的自由内存数值）在实机
   上看不到。
2. `epic12_xenos_create()` 有 16 条 `return false` 路径，原来没有记录是
   哪一条触发，也没有记录 `HRESULT`，因此无法区分“内存不足”和“资源/着色器
   不被支持”。
3. `epic12_xenos_log()` 整体位于 `#if SALVIA_FBNEO_DIAGNOSTICS` 内，定版
   （`SALVIA_FBNEO_DIAGNOSTICS=0`）不写任何日志。仓库内最后一份
   `cv1000-gpu.log` 停留在 2026-10-01 17:35，之后没有任何实机 GPU 记录。

## 本轮排查到的历史事实

在修改代码之前先确认了回退是不是回归：

- 最后一次“GPU 可用”的实机证据是 `cv1k-clipped-invalidation-20261001-r1`
  （2026-10-01 17:35），其日志含 `pixel self-test passed; GPU compositor
  active (sprite attributes)`。
- 2026-10-01 的可用构建源自提交 `28e01aae`，其
  `epic12_gpu_xbox.h` 哈希为 `31d95a3f830acc3eb37f3006d5f783f2ed5a1fc811e3caa93c946f4a8f466312`；
  当前 HEAD 的同一文件哈希为 `1c589a6dc93102831d3fd270f0fffb2bc66e1f0187dd5963ee75bdc359a35671`。
- 两者差异仅为诊断宏开关、`page_upload=` 报告字段，以及贴图上传与透明掩码
  合并路径（`2c1902bc`）。这些都在 2026-10-01 当天于实机验证过，且都不在
  `epic12_xenos_create()` 的失败分支上。
- 2026-10-01 之后唯一改变静态内存布局的改动是 `b1b61901`（块表
  32768 -> 131072，堆上 10.5 MiB）与 `07ef7cea`（代码 arena 8 -> 32 MiB，
  `static UINT32 xbox_code[CACHE_BYTES/4]` 直接进 `.bss`）。arena 使 `.bss`
  从 53.4 MiB 增长到 77.4 MiB。

因此“Xenos 初始化失败是 10-01 之后的回归、且最可能的触发因素是 arena 多占的
24 MiB 静态内存”是当前主要假设，但**尚未有直接证据**，本轮只负责取回这个证据。

## 修改

只改 `libretro/FBNeo/src/burn/devices/epic12_gpu_xbox.h`：

1. `epic12_xenos_init_failed(const char *step, HRESULT hr)` 现在同时记录失败
   步骤和 `HRESULT`；`epic12_xenos_compile()`、
   `epic12_xenos_create_pixel_shader()`、`epic12_xenos_create_atlas()` 都把
   最后一次 `HRESULT` 写入 `epic12_xenos_last_hr`，因此 16 个失败点全部带码。
2. `epic12_xenos_create()` 入口记录一次空闲物理内存
   （`GlobalMemoryStatus().dwAvailPhys`），失败时再记一次，形成前后对比。
3. 本机启动后**第一次**失败时，向 `game:\\cv1000-gpu.log` 追加一行：

   ```text
   CV1000 GPU: INIT FAILED step=<资源名> hr=0x<HRESULT> free=<入口>MB-><失败>MB \
   tiled=<0/1> fast_transfer=<0/1> alpha_vector=<0/1> tile_alpha=<0/1> cache=<n> \
   specialized=<0/1> attributes=<0/1> (batches=<n>)
   ```

   该行每个启动最多写一次，只在 GPU 路径即将丢失时写，不属于游戏中的实时文件
   I/O，也不进入每批采样路径；定版的 `SALVIA_FBNEO_DIAGNOSTICS=0` 行为不变。
4. 横幅改为把最有用的信息放在最前面，避免被 overlay 裁剪吃掉：

   ```text
   CV1000 GPU: init@<资源名> free <入口>-><失败>MB hr=<HRESULT>
   ```

`hr=0x8007000E`（`E_OUTOFMEMORY`）配合偏低的前后空闲内存可以直接判定内存
不足；`D3DERR_INVALIDCALL` 一类则表示与内存无关的资源/参数问题。

## 验证

```text
cmd.exe /d /s /c "E:\Baiduyundownload\salvia-toolchain\.work\build-main-gpuinit.cmd"
```

FBNeo 360 静态库与 `Salvia.vcxproj` `Release_finalburn` 均编译通过
（`PASS`）。产物 `Salvia.exe` 中确认存在 `INIT FAILED step=`、
`CV1000 GPU: init@`、`hr=0x%08X` 三个字符串各一次。

- 探测 XEX：`Distro360/fbneo.xex`，34,619,392 字节，
  SHA256 `68f79e8e6da8cdb59789435e436bd0951783b9095d017e56c3792d7aa48f3af1`。
- 上一版（仅含步骤名、无 HRESULT 的探测）保留为
  `Distro360/fbneo-gpuinit-probe1.xex`，
  SHA256 `c41b04541f649359195b1083641e2d156a2c019d0e0c3fd2e80fa82332a07b8a`。
- 上一轮用户实测的 arena32+table128 构建保留在构建目录外
  （`fbneo-arena32m-table128k.xex`，
  SHA256 `d18da53c2b804249d8ce6d8983d7d977a82c6de39178a3ef14a423614ee10a13`）。

## 验证限制

只有 Xbox 360 链接成功这一级证据。本轮的结论必须由实机写入的
`game:\\cv1000-gpu.log` 决定，不声称任何帧率或修复效果。

## Status

已构建并打包，等待实机确认。拿到 `INIT FAILED` 行后再决定是缩小 arena
（宿主已验证 128K 表下 20 MiB 与 32 MiB 等价，可省 12 MiB）还是处理与内存
无关的资源失败点。
