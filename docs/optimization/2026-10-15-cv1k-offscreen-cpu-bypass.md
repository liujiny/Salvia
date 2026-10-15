# CV1000 GPU: Bypass GPU Pipeline for Off-Screen Staging Buffers

Baseline `03e4280e` (restored to `77c7f81b` / `c1fc9771`). Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

In `mmpork` (and Cave CV1000 games in general), the blitter frequently uses off-screen VRAM (Zones 1 & 2: $X \in [768..818]$ and $[1280..1335]$) as temporary staging scratchpads to assemble multi-part sprites with 1–13 tiny draw commands ($<8,192$ px) before immediately blitting them onto the visible display ($X < 320$).

In the previous multi-bin GPU pipeline:
1. **512-px Span Overflow**: When an off-screen staging draw ($X \ge 768$) was submitted, it could not merge with the active on-screen bin ($X \in [0..320]$) because the bounding box exceeded the 512-pixel hardware render target span limit.
2. **Ping-Pong Bin Allocation**: The scheduler allocated a separate bin for the off-screen staging draw.
3. **Severe False RAW Hazards**: 2–3 sprites later, the game blitted the composite sprite from the staging buffer ($SX \ge 768$) to the visible screen ($DX \in [0..320]$). Because the source read overlapped the uncommitted write in the off-screen bin, the Bernstein checker flagged a RAW hazard (`c_raw = 66,350`).
4. **Catastrophic Fragmentation & Pipeline Thrashing**:
   - The RAW hazard forced the queue to retire Bin 0, triggering **28,165 `region` flushes** to CPU software replay (`small_cpu = 28,575`).
   - More crucially, it chopped the visible on-screen sprite workload into 7–8 fragmented bins per frame.
   - At frame end, each fragmented on-screen bin was flushed as an independent GPU batch.
   - Across 300 frames, **2,973 GPU batches** were executed ($\approx 9.91$ batches/frame).
   - Because each GPU batch incurred a fixed $\approx 4.28$ ms D3D `LockRect` (`readback_wait`) latency, the worker thread was saturated with **42.85 ms / frame** of GPU stall, and the main emulation thread was forced to stall at `draw_sync` ($1.83$ ms/frame).

## What

In [`epic12_gpu.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_gpu.h):
- Added an immediate off-screen filter in `epic12_gpu_submit`:
  - When a draw destination is completely off-screen ($X \ge 512$):
    - Check if the draw reads from any active GPU bin's bounding box (`epic12_gpu_intersects`). If so, flush that bin first (ensuring full RAW correctness).
    - Immediately execute the draw directly into VRAM (`m_bitmaps`) using the CPU software blitter (`epic12_gpu_replay`).
    - Mark the corresponding texture page dirty in the GPU cache (`epic12_gpu_invalidate(dest)`).
    - Return `true` without adding the command to any GPU bin.
- **Rationale**:
  - The CPU blitter executes off-screen staging draws in microseconds ($\approx 0.005$ ms).
  - Off-screen draws never enter the GPU queue, eliminating 512-pixel span overflows.
  - Active GPU bins now contain **exclusively visible on-screen draws** ($X \in [0..320]$).
  - When the subsequent on-screen sprite reads from the staging buffer ($SX \ge 768$), its source coordinates never overlap any write in the active GPU bins. **RAW hazards collapse to ZERO**.
  - Without RAW hazards or span overflows, all visible on-screen draws coalesce into **a single continuous GPU batch** per frame.

## Actual Benchmark Results (300 frames `mmpork`, Xenia Canary)

| Metric | Fast-Tail Baseline (`c1fc9771`) | Offscreen-CPU (`c0bea854`) | Delta / Improvement |
| :--- | :--- | :--- | :--- |
| **GPU Batches (300f)** | 2,973 | **299** | **-2,674 batches (-90.0%)** |
| **GPU Batches / Frame** | 9.91 / frame | **1.00 / frame** | **Optimal 1 batch per frame** |
| **Worker Busy Time (300f)**| 14,032.8 ms | **8,287.7 ms** | **-5,745.1 ms (-40.9%)** |
| **Worker Busy / Frame** | 46.78 ms / frame | **27.63 ms / frame** | **-19.15 ms / frame** |
| **GPU Busy Time (300f)** | 12,854.5 ms | **6,465.0 ms** | **-6,389.5 ms (-49.7%)** |
| **Draw Sync Wait Time** | 1.829 ms | **0.141 ms** | **-1.688 ms / frame (-92.3%)** |
| **`region` Flushes** | 28,165 | **0** | **-28,165 (-100.0%)** |
| **`source_dependency` Flushes**| 984 | **0** | **-984 (-100.0%)** |
| **`end` List Flushes** | 2,120 | **265** | **-1,855 (-87.5%)** |
| **`small_cpu` Batches** | 28,575 | **0** | **-28,575 (-100.0%)** |
| **`c_raw` Conflicts** | 66,350 | **0** | **-66,350 (-100.0%)** |
| **`c_dest` Conflicts** | 3,437 | **0** | **-3,437 (-100.0%)** |
| **Bin 0 Utilization** | 7,340 hits (2.4%) | **158,204 hits (100.0%)**| **All draws unified in Bin 0** |
| **Total Core Frame Time** | 69.56 ms | **66.18 ms** | **-3.38 ms / frame (-4.9%)** |
| **Active Loop Time** | 70.40 ms | **66.57 ms** | **-3.83 ms / frame (-5.4%)** |

## Verification & Artifacts

- **Xbox 360 Diagnostics XEX**:
  - Tag: `offscreen-cpu` (`c0bea854`)
  - File: `xex-archive/fbneo-20261015-0801-offscreen-cpu-diag.xex`
  - SHA256: `7a0ec7a450a5eeff16a56d63c632533c9f652b4f50ad547eb8cc3d9db3b7559d`
- **QEMU PPC Regression Test**:
  - `python3 libretro/FBNeo/tests/epic12_gpu/run.py --toolchain-root /home/humor/salvia-tests/toolchains/ppc/root`
  - PASS: All 14,400 commands, full VRAM and delay comparison bit-identical to reference CPU blitter.
