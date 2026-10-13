# CV1000: optimize epic12_gpu_submit with single-page fast-path and memoized new-page tracking

Baseline `894a9c09`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

From `Distro360/cv1000-gpu.log` (steady state, 497-frame window in `mmpork`):
- `submit=8106.849 ms (submit_calls=650554)`：平均每帧约 1,308 次精灵队列提交，占 Worker 线程总时间（13,613 ms）的 **59.5%**！
- 分析 [`epic12_gpu.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_gpu.h) 中的 `epic12_gpu_submit` 函数：
  1. 对每个提交的精灵命令，原实现分别执行两次 2D 循环（遍历 `py` 从 `c.sy/128` 到 `(c.sy+c.h-1)/128`，`px` 从 `c.sx/128` 到 `(c.sx+c.w-1)/128`）：
     - 第一次循环：计算该精灵所触及的未缓存源页面数量 `newPages`，以判断批次容量是否超限。
     - 第二次循环：在批次刷新判定（容量、数量、区域、数据依赖）后，再次遍历相同的 2D 区域，重新执行 `!epic12_gpu_source_pages[page]` 判断，并将页面标记为已加载 (`true`) 同时递增 `epic12_gpu_source_count`。
  2. 在真实的 2D 街机游戏（如 MMPork、Deathsmiles、Futari 等）中：
     - 绝大多数精灵（子弹、爆炸粒子、角色部件、UI 数字等）尺寸在 8×8 到 64×64 之间，完全落在单个 128×128 的显存页面内 (`py0 == py1 && px0 == px1`)。双重循环、`/ 128` 除法以及 `py * 64 + px` 的重复计算是纯粹的多余开销。
     - 在一个批次中绘制多个同图集精灵时，后继精灵触及的页面往往已被当前批次加载 (`newPages == 0`)。若此时没有触发显式刷新，原实现依然会重复跑第二轮 2D 循环并对已为 `true` 的页面进行重复比较。

## What

在 [`epic12_gpu.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_gpu.h) 的 `epic12_gpu_submit` 中进行以下优化：
1. **单页快速判定 (`py0 == py1 && px0 == px1`)**：
   - 使用位运算 `c.sy >> 7` 与 `(c.sy + c.h - 1) >> 7`（因 `sy >= 0`）快速求出页面跨度。
   - 对单页精灵跳过任何 2D 嵌套循环，单次查表即可完成 `newPages` 计数。
2. **多页循环行基址提升 (`py << 6`)**：
   - 对跨多页的大尺寸精灵，将 `row = py << 6` 移出内层 `px` 循环，消除每次迭代的乘法运算。
3. **记忆化新页列表 (`newPageList`) 与免循环更新**：
   - 在首次统计阶段将发现的未缓存页索引直接暂存在栈上固定小数组 `newPageList[32]`（数学证明 512×512 限制下单精灵最多跨 25 页，绝无溢出风险）。
   - 在无需刷新的绝大多数通常提交路径下：
     - 若 `newPages == 0`，完全跳过第二轮循环，直接跳过页面更新逻辑。
     - 若 `newPages > 0`，直接对暂存的 `newPages` 个页面进行标记并累加计数，彻底消除第二轮 2D 遍历与冗余条件分支。
   - 若触发了批次刷新（或首个命令）：单页精灵直接单次赋值，多页精灵才执行基址提升后的单次标记循环。

## Verification

所有单元测试套件在 Host (x86_64) 与 PPC Big-Endian (QEMU) 双端 100% PASS：
1. `libretro/FBNeo/tests/epic12_gpu/run.py` (PPC under QEMU):
   - `PASS 6291456 component cases on unified and fast/feedback shaders, including additive saturation`
   - `PASS 4096 plain shader cases independent of alpha and destination`
   - `PASS clipped CPU invalidation: all 2048 pages at 128/256 slots; conservative upload/invalid-clip reset; 2304 all-mode draws, full VRAM and delay identical`
   - `PASS 14400 commands, full VRAM and delay comparison; simulated GPU batches=1180 commands=7104`
   - `PASS 640 grouped commands, snapshot overlap/area barriers, RECTLIST atlas geometry and draw capacity`
   - `PASS 640 varying-style commands on all paths; draws uniform=640 attributes-unified=42 specialized=42`
   - `PASS restored batch cap: 260 source pages -> 3 batches with either 128/256-slot cache`
   - `PASS shader selection: fast=1096 feedback=515 unified=4567 groups`
   - `PASS cache 128 slots: 144 pages/3 passes uploads=432; CPU/GPU source writes and reset coherent`
   - `PASS cache 256 slots: 144 pages/3 passes uploads=144; CPU/GPU source writes and reset coherent`
   - `PASS reorder: eight-command boundary, edge/overlap barriers, full-field permutation, untouched input and in-place equivalence`
   - `PASS snapshot lookahead: exact groups, all preceding writes, area and 32-command boundaries`
   - `PASS alpha/snapshot/reorder integration 128 slots / 256 slots: PASS`
   - `PASS reorder model exercised: batches=296 moved_commands=594`
2. `libretro/FBNeo/tests/epic12_blit/run.py` (PPC under QEMU):
   - `PASS 65536 tint/alpha rounding cases and 1000000 packed colour sums`
   - `PASS 5000 image cases: tint, transparency, clipping, flips, source wrap, overlapping VRAM and delay`
3. `libretro/FBNeo/tests/sh3_dispatch/run_inline.py`:
   - `PASS 8 host suites in optimized and ASan/UBSan modes`

## Revert

`git revert <sha>` 恢复原 `epic12_gpu.h` 并删除本报告。
