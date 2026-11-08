# CV1000 优化：垂直暂存流融合、掉帧细分遥测与 AGENTS.md 规范调整

日期：2026-11-08。基线提交：`0f9f306e`。
优化目标：**MMPork 实机 40+ FPS 慢帧根因追踪与暂存流性能提升**。

---

## 1. 变更背景与规范调整

### 1.1 规范调整（AGENTS.md）
根据用户明确指示：
> “修改agents.md规范，一次可以改多个逻辑。这3个方案同时实施”

在 [`AGENTS.md`](file:///home/humor/src/Salvia/AGENTS.md) 中更新规范：
允许在用户明确指示下，将多个紧密相关的逻辑变更整合入单次独立可审阅和可回退的 Git 提交中。

### 1.2 现状与问题分析
在 MMPork 实机 479,425 帧深度重放日志分析中，Option A 外设快路径已消除所有片上外设未命中，`draw_sync` 耗时稳定在 0.72~0.73ms。但实机在 Boss 散弹等复杂场景仍偶现 40+ FPS（慢帧总耗时约 44ms，其中 `cpu_io` 占 43.5ms）。
为彻底厘清慢帧时是否存在 GPU 工作线程排队阻塞（Blitter Worker Wait），以及进一步降低暂存区高频零散 Sprite 的开销，本次同时实施以下两项方案。

---

## 2. 方案实现细节

### 2.1 方案二：暂存流垂直序列融合（Vertical Staging Stream Fusion）
在 [`epic12_gpu.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_gpu.h) 的 `epic12_gpu_staging_stream()` 中：
1. **自动侦测融合方向**：
   - 侦测水平融合（`fusion_dir = 1`）：`ny == y && ndimy == dimy && nx == x + total_dimx`。
   - 侦测垂直融合（`fusion_dir = 2`）：`nx == x && ndimx == dimx && ny == y + total_dimy`。
2. **垂直跨度合并与光栅化优化**：
   - 支持源连续垂直条带（`dimx * total_dimy`）与重复源垂直条带。
   - 合并 WAR 冲突检测范围（`dpy0 = y >> 7, dpy1 = (y + total_dimy - 1) >> 7`）。
   - 合并脏纹理矩形失效范围（`inv_min_y` 至 `inv_max_y`）。
   - 统一客机 Blit 延迟计算：`column_count ? (step_count * 2 * dimy) : (total_dimx * total_dimy)`。

### 2.2 方案三：慢帧/停顿精细化遥测（Drop & Stall Telemetry Instrumentation）
在 [`cv1k_review_profile.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/cv1k_review_profile.h)、[`epic12.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12.h)、[`epic12.cpp`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12.cpp) 与 [`d_cv1k.cpp`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/drv/cave/d_cv1k.cpp) 中：
1. **结构扩展**：
   `SalviaReviewStalls<N>::Event` 增加 `draws`、`uploads` 与 `worker_wait` 字段。
2. **高精度工作线程等待度量**：
   在 `DrvFrame()` 中包裹 `epic12_wait_blitterthread()`，记录精确的 `wait_start` 与 `wait_end`，计算主线程等待 Blitter 工作线程的纯等待耗时 `workerWait`。
3. **单帧 GPU 吞吐捕获**：
   通过 `epic12_gpu_get_frame_telemetry(&drawsThisFrame, &uploadsThisFrame)` 捕获每帧递增的 Xenos 命令数与纹理页面上传数。
4. **日志字符串输出增强**：
   `core_stall` 与 `core_drop` 遥测格式扩展：
   `... compiled_blocks=%u draws=%u uploads=%u worker_wait=%.3f`
   使实机日志直接揭示慢帧时究竟是 CPU 纯计算卡死、还是等待 GPU Blitter 工作线程耗尽。

---

## 3. 多级验证结果

1. **Host `review_profile` 单元测试**：
   - 运行：`g++ -O2 libretro/FBNeo/tests/review_profile/test.cpp -o /tmp/review_test && /tmp/review_test`
   - 结果：**100% PASS**（停顿捕获、慢帧阈值判定、事件排序均正常）。
2. **Host `epic12_staging_columns` 单元测试**：
   - 运行：`python3 libretro/FBNeo/tests/epic12_staging_columns/run.py`
   - 结果：**100% PASS**（512 组随机用例完全一致）。
3. **Tier 3 PPC QEMU `epic12_gpu` 全量测试套件**：
   - 运行：`python3 libretro/FBNeo/tests/epic12_gpu/run.py --toolchain-root /home/humor/salvia-tests/toolchains/ppc/root`
   - 结果：**100% PASS**（23 个套件全部通过，涵盖 73,728 个原始光栅化重放用例、629 万着色器用例、交叠暂存流、暂存与重排集成、128/256 槽缓存等）。
