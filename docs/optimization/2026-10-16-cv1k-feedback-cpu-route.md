# CV1000: Eliminate Visual Mask and GPU Feedback Resolves via CPU Blitter Fallback

Baseline `0c37e58c`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

During Cave CV1000 gameplay (*Muchi Muchi Pork!*, *Ibara*), a persistent dark mask / contrasting upper-screen shading appeared during gameplay and vanished when paused.

### Root Cause Analysis

1. **Feedback Blending in GPU Pipeline**:
   - In *Muchi Muchi Pork!*, 240 horizontal 1-pixel scanlines (`w=351, h=1`) with `blend=1, dmode=0, da=16, sa=20` are drawn consecutively across the screen for background raster effects and dialogs.
   - When routed to the GPU, each line forced an EDRAM Resolve (`D3DRESOLVE_RENDERTARGET0`) to copy the render target back into texture memory so it could be sampled as a destination texture.
   - In real console logs (`Distro360/cv1000-gpu.log`), this caused **524,674 feedback resolves** across 4,457 batches (~120 resolves per frame).
   - In addition to choking GPU bandwidth and stalling the worker thread (`submit = 12.85 ms / frame`, `worker_wait = 6.70 ms / frame`), the feedback texture sampling and bit29 masking on Xenos produced incorrect destination alpha states, causing the dark horizontal mask across the upper screen.

## What

1. **Route Feedback Blending to CPU Blitter in `libretro/FBNeo/src/burn/devices/epic12_gpu.h`**:
   - In `epic12_gpu_submit`:
     ```cpp
     if (blend && (dmode != 0 || (d_alpha != 0 && d_alpha != 31))) {
         return false;
     }
     ```
   - Normal sprites and additive blends (`dmode==0, da==31`, bullets, explosions, glow) remain 100% on the GPU.
   - Only destination-dependent feedback blends are returned to the CPU blitter, which computes exact integer RGB565 / RGB555 alpha arithmetic without GPU texture readback.

2. **Clamped Resolve Bounds in `libretro/FBNeo/src/burn/devices/epic12_gpu_xbox.h`**:
   - Ensured `epic12_xenos_resolve` coordinates are strictly clamped within the $512 \times 512$ render target bounds:
     ```cpp
     int x1 = x & ~7; if (x1 < 0) x1 = 0; if (x1 > 512) x1 = 512;
     int y1 = y & ~7; if (y1 < 0) y1 = 0; if (y1 > 512) y1 = 512;
     int x2 = (x + w + 7) & ~7; if (x2 < 0) x2 = 0; if (x2 > 512) x2 = 512;
     int y2 = (y + h + 7) & ~7; if (y2 < 0) y2 = 0; if (y2 > 512) y2 = 512;
     if (x2 <= x1 || y2 <= y1) return;
     ```

## Verification & Results

1. **Visual Confirmation**:
   - User visual inspection of screenshots (`mask_focused_comparison.png`, `mask_comparison_side_by_side.png`) confirmed: the dark mask is completely eliminated ("消除了").

2. **GPU Performance Metrics (120 frames `mmpork.state`, Xenia Canary)**:
   - `feedback_resolves`: **6,391 -> 0** (100% eliminated).
   - `readback_wait`: **32.0 ms -> 1.27 ms** (96% reduction in GPU stalls).
   - `peak_total`: **45.9 ms -> 13.8 ms** (70% reduction).
   - GPU sprite pixel throughput maintained: over 31.3 million pixels rendered across 120 frames.

## Revert

`git revert <sha>` to restore original GPU routing.
