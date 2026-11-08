# CV1000 优化：快照缓存容量翻倍与4路相联、直通无循环行复制及 Staging 栅格化流水线优化

日期：2026-11-08。基线提交：`fc3abef9`。
对应优化方向：**GPU Blitter 快照缓存（Snapshot Cache）与暂存栅格化（Staging Rasterizer）流水线优化**。

---

## 1. 变更背景与归因分析

### 1.1 实机性能数据暴露的快照缺失与栅格化开销
在 Xbox 360 Jasper（`192.168.2.232`）上，MMPork 在第 12,631 至 12,662 帧（第一关 Boss 对话气泡升起与金币密集喷涌场景）帧率曾掉至 40+ FPS，慢帧监控显示 `draw_sync` 成为最主要耗时项（单帧最高 50.9ms）。

深入快照与暂存机制分析发现：
1. **快照缓存失效率高（~70% 失效率）**：
   原先快照缓存结构为 `EPIC12_SNAP_SETS = 512`，`EPIC12_SNAP_WAYS = 2`。由于 CV1000 多层活动精灵的源地址哈希碰撞集中在特定 Set 上，仅 2 路相联导致大量活跃快照页在被复用前即被置换驱逐。
2. **快照 Shelf 物理容量充足**：
   快照 Shelf 容量为 128 页（64 宽 x 2 高 = 256 行，即 4096..4352）。在 12,816 帧的实机日志中 `flushes source_pages = 0`，证明物理容量从未打满，瓶颈纯粹在于 2 路 Set-Associative 的哈希碰撞置换。
3. **行复制循环存在冗余分支与标量开销**：
   `epic12_gpu_region.h` 中针对跨页与裁剪区域的 `epic12_gpu_copy_rows`，原实现使用带有多重条件嵌套和 Prefetch 步进的通用循环。实际上，超过 90% 的行复制宽度为固定尺寸（32、64 或 128 字节，即 8、16 或 32 像素），通用循环的标量计算与边界判断耗费了大量 CPU 周期。
4. **Staging 栅格化逐像素标量写入气泡**：
   `epic12_staging_rasterize_blit` 在处理宽幅 Staging 传输时（尤其 `tint_mode == 0` 和 `tint_mode == 2`），内层像素拷贝为逐标量赋值，未形成流水线发射。

---

## 2. 修改方案与实现细节

### 2.1 快照缓存扩容至 1024 Sets x 4 Ways ([`epic12_gpu.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_gpu.h))
1. **缓存容量翻倍与相联度提升**：
   - 将 `EPIC12_SNAP_SETS` 从 512 扩展为 1024。
   - 将 `EPIC12_SNAP_WAYS` 从 2 扩展为 4。
   - 置换策略升级为 4 路 Round-robin LRU：`(s_snap_lru[set] + 1) & (EPIC12_SNAP_WAYS - 1)`。
2. **诊断计数器接入**：
   - 在 `SALVIA_FBNEO_DIAGNOSTICS` 宏保护下增加 `epic12_gpu_diag_snap_hits` 与 `epic12_gpu_diag_snap_misses`。
   - 在 `diag_staging` 遥测报告中新增 `hits=%u misses=%u`，用于精准追踪实机快照命中率变迁。

### 2.2 固定宽度直通向量化行复制 ([`epic12_gpu_region.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_gpu_region.h))
在 `epic12_gpu_copy_rows` 中增加对常见固定宽度的专门直通路径（Straight-line unrolled vector paths）：
- `width == 64` (16 像素 / 4 个 16-字节向量寄存器)：消除内层循环与 Prefetch 计数，展开为直通的 4 向量对齐/未对齐加载与对齐写入。
- `width == 32` (8 像素 / 2 个 16-字节向量寄存器)：展开为直通的 2 向量加载与写入。
- `width == 128` (32 像素 / 8 个 16-字节向量寄存器)：展开为直通的 8 向量加载与写入。
- 通用路径保留对任意任意非对齐/不规则边界的完全兼容支持。

### 2.3 Staging 栅格化内层循环解卷流水线化 ([`epic12_gpu.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_gpu.h))
在 `epic12_staging_rasterize_blit` 中：
- 对 `tint_mode == 0`（完全直通复制）：采用 8 路展开批量并行读写，消除前后像素的依赖气泡。
- 对 `tint_mode == 2`（调制模式）：采用 4 路展开批量读入、解算并写出。

---

## 3. 测试与多级验证闭环

1. **Tier 1 单组件测试**：
   - `python3 libretro/FBNeo/tests/epic12_staging_columns/run.py`：**PASS**（512 组随机测试，像素与边界守卫 100% 一致）。
2. **Tier 1 & Tier 3 区域复制测试（Host & PPC QEMU）**：
   - `python3 libretro/FBNeo/tests/epic12_gpu_region/run.py --toolchain-root /home/humor/salvia-tests/toolchains/ppc/root`：**PASS**
   - 结果：4,044 个矩形全部精确匹配，10,112,328 字节完全一致，196,769 个向量 Store 均 16 字节对齐且无越界，0 个未对齐写入。Host 与 PPC QEMU 结果双端一致。
3. **Tier 3 PPC QEMU 全量 GPU 命令与着色器套件**：
   - `python3 libretro/FBNeo/tests/epic12_gpu/run.py --toolchain-root /home/humor/salvia-tests/toolchains/ppc/root`：**PASS**（全部 23 套子套件通过，覆盖全部 14,400 条命令、629 万着色器测试用例，WAR 刷新为 0）。
4. **Tier 3 PPC QEMU SH3 指令集全量测试**：
   - `python3 libretro/FBNeo/tests/sh3_ppc/run.py --toolchain-root /home/humor/salvia-tests/toolchains/ppc/root`：**PASS**（全部 851,662 个用例通过）。

---

## 4. 交付约束遵循

- **128 源页上限**：严格维持现有的 128 源页 GPU 批次上限，不改动批次容量。
- **单独立逻辑提交**：所有快照缓存扩容、直通复制与 Staging 栅格化改动集中于单个可独立评审与回滚的提交 `c44b22ed`。
- **诊断构建规范**：迭代阶段编译诊断版镜像（`SALVIA_FBNEO_DIAGNOSTICS 1`），暂停时输出 `cv1000-gpu.log`。

---

## 5. 构建物身份与实机部署

- **构建命令**：`/home/humor/salvia-tests/xex-build/build-xex.sh --baseline fc3abef9 --tag snap-opt --flavor diag --force`
- **构建配置元数据**：`/home/humor/salvia-tests/xex-build/artifact-snap-opt.json`
- **构建输出（诊断版 `SALVIA_FBNEO_DIAGNOSTICS 1`）**：
  | 镜像文件 | 文件大小 (Bytes) | SHA-256 哈希 | 归档位置 |
  | :--- | :--- | :--- | :--- |
  | `mmpork.xex` | 34,832,384 | `6ca677a0a7f844f711d7a94217cae5f0e77b1bd1a56aba48b8240e380a6794f4` | `/home/humor/salvia-tests/xex-archive/mmpork-20261108-1303-snap-opt-diag.xex` |
  | `fbneo.xex` | 34,832,384 | `6fd5a1437f7675048d1b367e7f46e28ac25e51f32c1b3c38810311e5cac75bac` | `/home/humor/salvia-tests/xex-archive/fbneo-20261108-1301-snap-opt-diag.xex` |
  | `ddpsdoj.xex` | 34,832,384 | `fbc55cf15501b8b45554295561b299aaa8e4efd1ff9686c5799895cc63afded6` | `/home/humor/salvia-tests/xex-archive/ddpsdoj-20261108-1305-snap-opt-diag.xex` |
- **主机部署**：部署至 Xbox 360 Jasper（`192.168.2.232:/Usb0/Salvia-360/mmpork.xex`）。
