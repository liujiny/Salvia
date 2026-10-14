# CV1000 GPU: 8-Bin Dynamic Pipeline to Absorb Multiple Buffer Cycles

Baseline `34f18ac2`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

In `mmpork`, Cave CV1000's game loop executes repetitive cycles of:
1. Composing sprites into off-screen staging buffers (Zones 1 and 2: $X \in [768..896]$ and $X \in [1280..1344]$).
2. Blitting the composed buffers onto the active display (Zone 0: $X \in [0..384]$).
3. Overwriting or updating the staging buffers for subsequent visual passes.

While 4 bins significantly reduced premature flushes, the pipeline depth of 4 was exhausted after just 2 alternations (write A $\to$ read A $\to$ write B $\to$ read B). When the game attempted to write back to off-screen buffer A, WAR hazards with the active reads forced `bin[0]` to retire prematurely, truncating batches while they still contained fewer than 8,192 pixels.

## What

In [`epic12_gpu.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_gpu.h) and [`epic12_gpu_xbox.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_gpu_xbox.h):
- Extended the dynamic batch queue pipeline to 8 slots (`EPIC12_GPU_BINS 8`).
- Maintained the exact Bernstein commutation checking across all active bins in the pipeline.
- Total BSS memory overhead is only ~480 KB (negligible on Xbox 360's 512 MB architecture).
- Retained the 128-source-page batch limit and 512x512 render target per batch.

## Actual Benchmark Results (300 frames `mmpork`, Xenia Canary)

| Metric | 2-Bin Disjoint | 4-Bin Dynamic Pipe | 8-Bin Dynamic Pipe | Improvement vs 4-Bin |
| :--- | :--- | :--- | :--- | :--- |
| **Small CPU Batches** | 28,240 | 28,160 | **27,911** | **-249 batches** |
| Sub-512px Replay Batches | 14,743 | 14,511 | **14,023** | **-488 batches (-3.4%)** |
| **Source Dependency Flushes** | 71 | 3,762 | **984** | **-2,778 flushes (-73.8%)** |
| **Draw Sync Wait Time (300f)** | 8.19 ms | 8.55 ms | **6.27 ms** | **-2.28 ms/frame (-26.7%)** |
| **GPU Busy Time (300f)** | 14,968 ms | 14,819 ms | **14,395 ms** | **-424 ms overall** |
| **Worker Busy Time (300f)** | 16,006 ms | 15,974 ms | **15,686 ms** | **-288 ms overall** |
| **Total Core Frame Time** | 74.57 ms | 76.29 ms | **73.74 ms** | **-2.55 ms/frame** |
| **Active Loop Time** | 75.29 ms | 77.15 ms | **74.51 ms** | **-2.64 ms/frame** |

### Bin Hits Distribution (8-Bin Pipeline)
- Bin 0: 41,420
- Bin 1: 39,987
- Bin 2: 86,974
- Bin 3: 3,362
- Bin 4: 3,145
- Bin 5: 2,852
- Bin 6: 2,679
- Bin 7: 121,063
- Total commands batched: **301,482** (all 8 bins actively utilized)

## Verification & Artifacts

- **QEMU PPC Regression Test**:
  - `python3 libretro/FBNeo/tests/epic12_gpu/run.py --toolchain-root /home/humor/salvia-tests/toolchains/ppc/root`
  - PASS: All 14,400 commands, full VRAM and delay comparison bit-identical to reference CPU blitter.
- **Xbox 360 Diagnostics XEX**:
  - Tag: `bin8-pipe` (`7d16c5d3`)
  - File: `xex-archive/fbneo-20261015-0640-bin8-pipe-diag.xex`
  - SHA256: `51cc04fa64608304a41c1d1afa7a2f8330e451b5285cc0e2da4716aea830df68`
