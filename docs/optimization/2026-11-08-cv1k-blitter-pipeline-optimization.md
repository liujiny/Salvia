# CV1000 优化：GPU Blitter 绘图管线渲染状态过滤与单页几何快速路径

日期：2026-11-08。基线提交：`60b17005`。
对应优化方向：**GPU Blitter 绘图管线优化（Direct3D 9 状态流精简、单页几何快径与列解卷）**。

---

## 1. 变更背景与归因分析

### 1.1 实机性能遥测数据揭示的 40 FPS 慢帧瓶颈
在 Xbox 360 Jasper（`192.168.2.232`）上完成的 13,092 帧实机游玩日志（`console_cv1000_gpu_r3.log`）中，稳态慢帧监控捕获了位于游戏第 12,933 至 12,968 帧（第一关 Boss 对话气泡与漫天金币密集场景）的 8 个严重掉帧（每帧耗时 47.5ms ~ 53.5ms，对应 18.6 ~ 21 FPS，体感均帧 40+ FPS）：
```
CV1000 GPU: core_drop rank=1 interval_frame=12953 prep=0.003 cpu_io=1.860 audio_tail_misc=0.733 draw_sync=50.989 total=53.585 compiled_blocks=0
CV1000 GPU: core_drop rank=2 interval_frame=12937 prep=0.002 cpu_io=1.825 audio_tail_misc=1.151 draw_sync=50.239 total=53.217 compiled_blocks=0
CV1000 GPU: core_drop rank=3 interval_frame=12968 prep=0.004 cpu_io=1.772 audio_tail_misc=1.177 draw_sync=48.429 total=51.383 compiled_blocks=0
CV1000 GPU: core_drop rank=4 interval_frame=12949 prep=0.003 cpu_io=1.911 audio_tail_misc=0.725 draw_sync=45.653 total=48.292 compiled_blocks=0
CV1000 GPU: worker_wait sampling=random-1-in-64 jobs=10329 waits=10329 samples=176 avg_ms=3.910 peak_ms=44.665
```
数据特征极其鲜明：
1. **CPU 模拟极其轻量**：`cpu_io` 仅 1.77ms ~ 1.91ms，新编译块 `compiled_blocks=0`。
2. **瓶颈 100% 集中在 `draw_sync`**：`draw_sync` 高达 44.5ms ~ 50.989ms（占全帧耗时的 **95.2%**）。
3. **渲染 Worker 线程耗时成为关键阻塞源**：`worker_wait` 峰值达到 44.665ms，主线程在 `epic12_wait_blitterthread()` 处长时间同步等待 Blitter Worker 线程完成。

### 1.2 绘制管线热点定位
1. **Direct3D 9 状态流冗余提交**：
   在 `epic12_gpu_xbox.h` 的 `epic12_xenos_run` 绘制循环中，批次被划分为众多命令组（Group）。对每个组，即使混合模式与前一组完全相同，均无条件反复调用 `d->SetRenderState(D3DRS_ALPHABLENDENABLE, ...)`、`d->SetRenderState(D3DRS_SRCBLEND, ...)`、`d->SetRenderState(D3DRS_DESTBLEND, ...)` 及 `d->SetRenderState(D3DRS_BLENDFACTOR, ...)`。XTL Direct3D 9 的 `SetRenderState` 每次调用都会向 Ring Buffer 写入命令包，导致大量无效开销。
2. **PixelShader 初态与专用着色器不一致**：
   在进入绘制循环前，`activePs` 被固定初始化为通用着色器 `epic12_xenos_ps`。由于稳态下专用着色器（`epic12_xenos_specialized`）已开启，第一组绘制必然产生一次多余的 `SetPixelShader(epic12_xenos_fast_ps)` 状态切换。
3. **单页 Sprite 顶点几何循环开销**：
   `epic12_gpu_batch.h` 中的 `epic12_gpu_vertices` 原先使用双重循环遍历跨页切片。对于超过 95% 的绝大多数 Sprite，其尺寸不跨越 128x128 源页边界。双重嵌套循环、切片计算与多次乘除法纯属冗余开销。
4. **离屏交织列复制流水线气泡**：
   `epic12_staging_columns.h` 中的 `epic12_staging_draw_columns` 原为逐像素标量循环 `dst[i] = src[columns.source[i]]`。在 Xenon PPC 顺序双发射架构下，紧邻的单像素依赖导致 load-use 阻塞气泡。

---

## 2. 修改方案与实现细节

### 2.1 Direct3D 混合渲染状态冗余过滤与初态对齐 ([`epic12_gpu_xbox.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_gpu_xbox.h))
1. **初态对齐**：根据批次开头设置的默认渲染状态，将跟踪变量初始化为：
   - `activePs` 直接预设为专用快速着色器 `epic12_xenos_fast_ps`（当 `epic12_xenos_specialized` 生效时）。
   - `cur_alpha_blend = FALSE`。
   - `cur_src_blend = D3DBLEND_ONE`。
   - `cur_dest_blend = D3DBLEND_ONE`。
   - `cur_blend_factor = 0xffffffff`。
2. **状态变迁精细过滤**：
   在循环遍历各 Group 时，仅在目标状态与当前硬件状态不同时才调用 `SetRenderState`：
   - 不透明 / 无混合 / Feedback 模式：若 `cur_alpha_blend != FALSE` 则关闭混合。
   - 乘法混合模式：仅在对应 `ALPHABLENDENABLE`、`SRCBLEND`、`DESTBLEND` 改变时调用。
   - 加法饱和模式（`da_norm == 31`）：仅在对应状态改变时调用。
   - 因子混合模式：仅在混合因子（BlendFactor）或混合通道实际改变时更新。
3. **暂停诊断计数**：
   在 `SALVIA_FBNEO_DIAGNOSTICS` 下新增 `epic12_xenos_state_redundant` 与 `epic12_xenos_state_changes` 累计计数器，在暂停日志的 `submission` 行精准输出过滤效率。

### 2.2 单页 Sprite 顶点几何生成直接快速路径 ([`epic12_gpu_batch.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_gpu_batch.h))
在 `epic12_gpu_vertices` 中引入源页单页判定：
```cpp
const int spx0 = c.sx >> 7, spx1 = (c.sx + c.w - 1) >> 7;
const int spy0 = c.sy >> 7, spy1 = (c.sy + c.h - 1) >> 7;
if (spx0 == spx1 && spy0 == spy1) {
    int slot = slots[(spy0 << 6) | spx0];
    int u0 = (slot & 15) * 128 + (c.sx & 127), u1 = u0 + c.w;
    int v0 = (slot >> 4) * 128 + (c.sy & 127), v1 = v0 + c.h;
    int dx = c.x - bounds.min_x;
    int dy = c.y - bounds.min_y;
    Epic12GpuVertex v[3] = {
        {(UINT16)dx, (UINT16)dy, (UINT16)(c.flipx ? u1 : u0), (UINT16)(c.flipy ? v1 : v0), tint, mode},
        {(UINT16)(dx + c.w), (UINT16)dy, (UINT16)(c.flipx ? u0 : u1), (UINT16)(c.flipy ? v1 : v0), tint, mode},
        {(UINT16)dx, (UINT16)(dy + c.h), (UINT16)(c.flipx ? u1 : u0), (UINT16)(c.flipy ? v0 : v1), tint, mode}};
    out[0] = v[0]; out[1] = v[1]; out[2] = v[2];
    return 3;
}
```
消除双重循环结构、页跨度分解和多次坐标重新解算，对于不跨页几何实现 3 顶点直接赋值。

### 2.3 4 路流水化离屏交织列复制解卷 ([`epic12_staging_columns.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_staging_columns.h))
在 `epic12_staging_draw_columns` 中对每行像素赋值进行 4 路解卷：
```cpp
unsigned i = 0;
for (; i + 3 < columns.count; i += 4) {
    uint32_t p0 = src[columns.source[i + 0]];
    uint32_t p1 = src[columns.source[i + 1]];
    uint32_t p2 = src[columns.source[i + 2]];
    uint32_t p3 = src[columns.source[i + 3]];
    dst[i + 0] = p0;
    dst[i + 1] = p1;
    dst[i + 2] = p2;
    dst[i + 3] = p3;
}
for (; i < columns.count; ++i) {
    dst[i] = src[columns.source[i]];
}
dst[columns.count] = src[columns.source[columns.count - 1] + 1];
```
使 PPC 乱序流水线能同时发起 4 条独立的加载请求，随后批量存储，消除相邻依赖气泡。

---

## 3. 测试与验证闭环

### 3.1 三级验证结果
1. **Tier 1 单组件测试**：
   - `python3 libretro/FBNeo/tests/epic12_staging_columns/run.py`：**100% PASS**（512 组随机列/行像素与边界测试，容量/对齐一致）。
   - `python3 libretro/FBNeo/tests/epic12_staging_columns/run.py --toolchain-root /home/humor/salvia-tests/toolchains/ppc/root`：**100% PASS**（PPC QEMU 环境一致）。
2. **Tier 2 主机分发与 AOT 测试**：
   - `test_aot`：**100% PASS**（生命周期、阶段遍历、R0 影子记账）。
   - `run_inline.py`：**100% PASS**（8 套套件，12,000 条随机 Trace $\times$ 12 Slices）。
3. **Tier 3 PPC QEMU 全量指令与 GPU 测试**：
   - `tests/epic12_gpu/run.py`：**100% PASS**（23 个全量子套件通过，包含全部 14,400 条命令、6.29M 着色器用例及 0 WAR 刷新）。
   - `tests/sh3_ppc/run.py`：**100% PASS**（全部 851,662 个用例通过）。

---

## 4. 交付约束遵循
- **128 源页上限**：严格维持 `epic12_gpu_batch_page_capacity() = 128`，不改动批次容量上限。
- **单提交原则**：本轮所有改动聚焦于 GPU Blitter 绘图管线状态与几何生成流。
- **构建环境**：按规范镜像至 `../Salvia` 运行目录进行 XDK 诊断镜像编译（`SALVIA_FBNEO_DIAGNOSTICS 1`）。
